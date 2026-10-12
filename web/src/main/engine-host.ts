// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine host: owns one bt-engine (one per window, decision 0013) and
// starts it again when it dies. The window is told through the status
// (the renderer then offers the rescue copy: renderer/shell/rescue.ts); calls made while there is no
// engine fail as `disconnected` at once, never queued for the next engine,
// whose document is a new one.

import { EngineClient } from '../engine/client.ts';
import type { ClientOptions, ClientProblem } from '../engine/client.ts';
import type {
  CallArgs,
  EngineApi,
  EngineStatus,
  EventName,
  EventParams,
  MethodName,
  Reply,
} from '../engine/client-types.ts';
import { CLIENT_ERROR_CODE, EngineError } from '../engine/errors.ts';
import type { ExitInfo, Transport } from '../engine/transport.ts';

export interface EngineHostOptions {
  /** Starts an engine (NodeTransport.start in the app). */
  start: () => Transport | Promise<Transport>;
  client?: ClientOptions;
  /** An engine that dies sooner than this after its start counts as a quick death (default 10 s). */
  quickDeathMs?: number;
  /** Quick deaths in a row (failed starts among them) at which the host gives up (default 5). */
  maxQuickDeaths?: number;
  /** The wait before restart n (1, 2, ...); default none for the first, then 250 ms doubling to 5 s. */
  restartDelayMs?: (attempt: number) => number;
  /**
   * What the host tells each engine after its hello and before the window
   * may call it (the app's folders: settings.set {panexFolder}); a failure
   * fails the start.
   */
  setup?: (client: EngineClient) => Promise<void>;
}

const defaultDelay = (attempt: number): number =>
  attempt <= 1 ? 0 : Math.min(5000, 250 * 2 ** (attempt - 2));

/** One engine for one window, restarted when it dies. */
export class EngineHost implements EngineApi {
  private readonly options: EngineHostOptions;
  private client_: EngineClient | null = null;
  private status_: EngineStatus = { state: 'failed', reason: 'not started' };
  private restarts = 0;
  private quickDeaths = 0;
  private startedAt = 0;
  private stopping = false;
  private restartTimer: ReturnType<typeof setTimeout> | null = null;
  /** The launch under way, while one is (start() joins it, stop() waits for it). */
  private launching: Promise<void> | null = null;
  private readonly statusHandlers = new Set<(s: EngineStatus) => void>();
  private readonly eventHandlers = new Map<string, Set<(params: never) => void>>();
  private readonly anyHandlers = new Set<(event: string, params: unknown) => void>();
  private readonly problemHandlers = new Set<(p: ClientProblem) => void>();

  constructor(options: EngineHostOptions) {
    this.options = options;
  }

  /** The engine's client while one runs. */
  get client(): EngineClient | null {
    return this.client_;
  }

  get status(): EngineStatus {
    return this.status_;
  }

  /**
   * Starts the first engine; fails when it cannot be started or refuses the
   * protocol. A second call while the first is under way joins it.
   */
  async start(): Promise<void> {
    if (this.client_ !== null) {
      return;
    }
    if (this.launching !== null) {
      await this.launching;
      return;
    }
    this.stopping = false;
    try {
      await this.launch();
    } catch (e) {
      if (this.stopped()) {
        // stop() came first and ended it: nothing to report
        return;
      }
      this.setStatus({ state: 'failed', reason: e instanceof Error ? e.message : String(e) });
      throw e;
    }
  }

  /**
   * Ends the engine (its solves pause) and starts no other; kills it after
   * `graceMs`. An engine being started when this is called is ended too:
   * one that has not said hello at once, one still being spawned as soon
   * as it exists.
   */
  async stop(graceMs = 10_000): Promise<void> {
    this.stopping = true;
    if (this.restartTimer !== null) {
      clearTimeout(this.restartTimer);
      this.restartTimer = null;
    }
    if (this.launching !== null) {
      const greeting = this.client_;
      if (greeting !== null && greeting.engine === null) {
        greeting.kill();
      }
      await this.launching.catch(() => undefined);
    }
    const client = this.client_;
    if (client === null || client.closed) {
      return;
    }
    await new Promise<void>((resolve) => {
      const timer = setTimeout(() => {
        client.kill();
      }, graceMs);
      client.onExit(() => {
        clearTimeout(timer);
        resolve();
      });
      client.close();
    });
  }

  call<M extends MethodName>(method: M, ...args: CallArgs<M>): Promise<Reply<M>> {
    const client = this.client_;
    // not before the engine has said hello: a call made during a restart
    // would reach the new engine with the old document's ids
    if (client === null || client.closed || this.status_.state !== 'running') {
      return Promise.reject(
        new EngineError(
          CLIENT_ERROR_CODE,
          'disconnected',
          `the engine is ${this.status_.state === 'failed' ? 'not running' : 'restarting'}`,
          method,
        ),
      );
    }
    return client.call(method, ...args);
  }

  on<E extends EventName>(event: E, handler: (params: EventParams<E>) => void): () => void {
    let set = this.eventHandlers.get(event);
    if (set === undefined) {
      set = new Set();
      this.eventHandlers.set(event, set);
    }
    const h = handler as (params: never) => void;
    set.add(h);
    return () => {
      set.delete(h);
    };
  }

  onEvent(handler: (event: string, params: unknown) => void): () => void {
    this.anyHandlers.add(handler);
    return () => {
      this.anyHandlers.delete(handler);
    };
  }

  /** The engine started, ended, or could not be kept running. */
  onStatus(handler: (status: EngineStatus) => void): () => void {
    this.statusHandlers.add(handler);
    return () => {
      this.statusHandlers.delete(handler);
    };
  }

  /** Protocol problems of every engine the host runs, and handlers that threw. */
  onProblem(handler: (p: ClientProblem) => void): () => void {
    this.problemHandlers.add(handler);
    return () => {
      this.problemHandlers.delete(handler);
    };
  }

  // Read through a call: the checker takes `stopping` for unchanged across
  // the awaits of start() and launch(), where stop() sets it.
  private stopped(): boolean {
    return this.stopping;
  }

  private report(p: ClientProblem): void {
    if (this.problemHandlers.size === 0) {
      console.error(`bt-engine: ${p.reason}: ${p.detail}`);
    }
    for (const h of [...this.problemHandlers]) {
      h(p);
    }
  }

  // A handler that throws must not stop the host (a restart hangs on it).
  private setStatus(status: EngineStatus): void {
    this.status_ = status;
    for (const h of [...this.statusHandlers]) {
      try {
        h(status);
      } catch (e) {
        this.report({ reason: 'handler', detail: `a status handler threw: ${String(e)}` });
      }
    }
  }

  private launch(): Promise<void> {
    const p = this.doLaunch();
    this.launching = p;
    return p.finally(() => {
      if (this.launching === p) {
        this.launching = null;
      }
    });
  }

  private async doLaunch(): Promise<void> {
    this.startedAt = Date.now();
    const transport = await this.options.start();
    if (this.stopped()) {
      transport.kill();
      throw new Error('the host was stopped while its engine started');
    }
    const client = new EngineClient(transport, this.options.client);
    client.onEvent((event, params) => {
      for (const h of [...(this.eventHandlers.get(event) ?? [])]) {
        (h as (p: unknown) => void)(params);
      }
      for (const h of [...this.anyHandlers]) {
        h(event, params);
      }
    });
    client.onProblem((p) => {
      this.report(p);
    });
    client.onExit((info) => {
      this.died(client, info);
    });
    this.client_ = client;
    let hello;
    try {
      hello = await client.hello();
    } catch (e) {
      client.kill();
      if (this.client_ === client) {
        this.client_ = null;
      }
      throw e;
    }
    if (this.stopped()) {
      // stop() waits for this launch, then ends the engine as it would a running one
      throw new Error('the host was stopped while its engine started');
    }
    if (this.options.setup !== undefined) {
      try {
        await this.options.setup(client);
      } catch (e) {
        client.kill();
        if (this.client_ === client) {
          this.client_ = null;
        }
        throw e;
      }
      if (this.stopped()) {
        throw new Error('the host was stopped while its engine started');
      }
    }
    this.setStatus({ state: 'running', version: hello.version, restarts: this.restarts });
  }

  private died(client: EngineClient, info: ExitInfo): void {
    if (this.client_ !== client) {
      return;
    }
    this.client_ = null;
    if (client.engine === null) {
      // it never agreed to the protocol: launch() fails and its caller reports
      return;
    }
    this.setStatus({
      state: 'exited',
      code: info.code,
      signal: info.signal,
      stderr: client.stderrTail(),
    });
    if (this.stopping) {
      return;
    }
    this.quickDeaths =
      Date.now() - this.startedAt < (this.options.quickDeathMs ?? 10_000)
        ? this.quickDeaths + 1
        : 1;
    this.scheduleRestart();
  }

  private scheduleRestart(): void {
    if (this.quickDeaths >= (this.options.maxQuickDeaths ?? 5)) {
      this.setStatus({
        state: 'failed',
        reason: `the engine died ${this.quickDeaths} times in a row soon after starting`,
      });
      return;
    }
    const delay = (this.options.restartDelayMs ?? defaultDelay)(this.quickDeaths);
    this.restartTimer = setTimeout(() => {
      this.restartTimer = null;
      if (this.stopping) {
        return;
      }
      this.restarts++;
      this.launch().catch((e: unknown) => {
        if (this.stopping) {
          return;
        }
        // could not start, or refused the protocol: a quick death too
        this.quickDeaths++;
        this.setStatus({ state: 'exited', code: null, signal: null, stderr: String(e) });
        this.scheduleRestart();
      });
    }, delay);
  }
}
