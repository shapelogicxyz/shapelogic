// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine client (decision 0012): typed calls over a Transport, binary
// frames as typed-array views, typed events, engine.hello first.
//
// Requests only, never notifications: the order of the replies is what the
// front end relies on. Requests are pipelined in the order they are made
// (a bounded number at the engine at once), and the engine answers them in
// that order, so a reply for a later call means the earlier ones' replies
// were lost. A reply is settled before the events behind it are dispatched
// (dispatch.ts). Calls can be coalesced by a key (CallOptions).

import type { Methods } from './api.generated.ts';
import type {
  CallArgs,
  CallOptions,
  EngineApi,
  EventName,
  EventParams,
  MethodName,
  Params,
  Reply,
} from './client-types.ts';
import { defaultDefer, OrderedInbox } from './dispatch.ts';
import type { Defer } from './dispatch.ts';
import { CLIENT_ERROR_CODE, EngineError } from './errors.ts';
import type { ClientErrc } from './errors.ts';
import { attachFrame, FrameError } from './frames.ts';
import { FrameParser, isReply, StderrRing } from './framing.ts';
import type { Inbound, ParserOptions, ProblemReason } from './framing.ts';
import type { ExitInfo, Transport } from './transport.ts';

/** The protocol this client speaks (engine.hello). */
export const PROTOCOL = 1;

/** The engine's limit on a request line (decision 0012); a longer call is refused here. */
export const ENGINE_MAX_LINE_BYTES = 64 * 1024 * 1024;

export interface ClientOptions {
  /** Calls at the engine at once; more wait in the client (default 32). */
  maxInFlight?: number;
  /** The longest request line sent (default the engine's 64 MiB); longer calls fail as the engine would fail them. */
  maxLineBytes?: number;
  /** The stdout parser's limits. */
  parser?: ParserOptions;
  /** How the client yields between a reply and the events behind it (dispatch.ts). */
  defer?: Defer;
}

/** A protocol problem, a misbehaving handler, or a reply that fits no call. */
export interface ClientProblem {
  reason:
    ProblemReason | 'unexpected-reply' | 'lost-reply' | 'bad-reply' | 'bad-frame-reply' | 'handler';
  detail: string;
}

type Hello = Methods['engine.hello']['result'];

interface Call {
  id: number;
  method: string;
  /** The request's line, until it is sent. */
  bytes: Uint8Array | null;
  key: string | undefined;
  resolve: (value: unknown) => void;
  reject: (error: EngineError) => void;
}

interface Slot {
  /** The key's call queued or at the engine. */
  current: Call;
  /** The newest call made while `current` is at the engine. */
  waiting: Call | null;
}

type Item = Inbound | { kind: 'exit'; info: ExitInfo };

const encoder = new TextEncoder();

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function clientError(errc: ClientErrc, message: string, method?: string): EngineError {
  return new EngineError(CLIENT_ERROR_CODE, errc, message, method);
}

/** One engine, over a transport. */
export class EngineClient implements EngineApi {
  private readonly transport: Transport;
  private readonly maxInFlight: number;
  private readonly maxLineBytes: number;
  private readonly parser: FrameParser;
  private readonly inbox: OrderedInbox<Item>;
  private readonly stderr = new StderrRing();

  private state: 'new' | 'greeting' | 'ready' | 'closed' = 'new';
  private nextId = 1;
  private readonly queue: Call[] = [];
  private readonly inFlight = new Map<number, Call>();
  private readonly slots = new Map<string, Slot>();
  private readonly handlers = new Map<string, Set<(params: never) => void>>();
  private readonly anyHandlers = new Set<(event: string, params: unknown) => void>();
  private readonly problemHandlers = new Set<(p: ClientProblem) => void>();
  private readonly exitHandlers = new Set<(info: ExitInfo) => void>();
  /** Why the client stopped talking to the engine before it ended (a refused hello, a lost stream). */
  private refusal: EngineError | null = null;
  private exitInfo: ExitInfo | null = null;
  private hello_: Hello | null = null;

  constructor(transport: Transport, options: ClientOptions = {}) {
    this.transport = transport;
    this.maxInFlight = Math.max(1, options.maxInFlight ?? 32);
    this.maxLineBytes = options.maxLineBytes ?? ENGINE_MAX_LINE_BYTES;
    this.inbox = new OrderedInbox<Item>(
      (item) => this.deliver(item),
      options.defer ?? defaultDefer,
    );
    this.parser = new FrameParser((item) => {
      this.inbox.push(item);
    }, options.parser);
    transport.onData((chunk) => {
      this.parser.push(chunk);
    });
    transport.onStderr?.((chunk) => {
      this.stderr.push(chunk);
    });
    transport.onExit((info) => {
      this.parser.end();
      this.inbox.push({ kind: 'exit', info });
    });
  }

  /** A client over `transport` that has greeted the engine. */
  static async connect(transport: Transport, options?: ClientOptions): Promise<EngineClient> {
    const client = new EngineClient(transport, options);
    await client.hello();
    return client;
  }

  /** engine.hello's answer, once the engine has agreed to the protocol. */
  get engine(): Hello | null {
    return this.hello_;
  }

  /** Whether the engine has ended (or the client was refused). */
  get closed(): boolean {
    return this.state === 'closed';
  }

  /** How the engine ended, once it has. */
  get exit(): ExitInfo | null {
    return this.exitInfo;
  }

  /** The last lines the engine wrote on stderr. */
  stderrTail(lines = 20): string {
    return this.stderr.tail(lines);
  }

  /**
   * Greets the engine: engine.hello with this client's protocol. Calls made
   * before it succeeds wait; an engine that refuses the protocol, or
   * answers with another, fails them and is ended.
   */
  async hello(): Promise<Hello> {
    if (this.state === 'closed') {
      // the engine ended (or was refused) before it could be greeted
      throw this.refused('engine.hello');
    }
    if (this.state !== 'new') {
      throw clientError('protocol', 'hello() is called once, first');
    }
    this.state = 'greeting';
    const reply = new Promise<unknown>((resolve, reject) => {
      const call = this.makeCall(
        'engine.hello',
        { protocol: PROTOCOL },
        undefined,
        resolve,
        reject,
      );
      if (call !== null) {
        this.send(call);
      }
    });
    let answer: unknown;
    try {
      answer = await reply;
    } catch (e) {
      this.refuse(
        e instanceof EngineError ? e : clientError('protocol', String(e), 'engine.hello'),
      );
      throw this.refusal ?? e;
    }
    // the engine is checked, not trusted: another protocol (or another program) is refused
    if (
      !isRecord(answer) ||
      answer.protocol !== PROTOCOL ||
      answer.name !== 'shapelogic_engine' ||
      typeof answer.version !== 'string'
    ) {
      const e = new EngineError(
        CLIENT_ERROR_CODE,
        'unsupported',
        `the engine answered ${JSON.stringify(answer).slice(0, 120)}; this client speaks protocol ${PROTOCOL}`,
        'engine.hello',
      );
      this.refuse(e);
      throw e;
    }
    const hello = answer as unknown as Hello;
    // the engine may have ended while it answered
    if (!this.closed) {
      this.state = 'ready';
      this.hello_ = hello;
      this.pump();
    }
    return hello;
  }

  call<M extends MethodName>(method: M, ...args: CallArgs<M>): Promise<Reply<M>> {
    const [params, options] = args as [Params<M> | undefined, CallOptions | undefined];
    return new Promise<Reply<M>>((resolve, reject) => {
      const call = this.makeCall(
        method,
        params,
        options?.coalesce,
        resolve as (v: unknown) => void,
        reject,
      );
      if (call !== null) {
        this.enqueue(call);
      }
    });
  }

  on<E extends EventName>(event: E, handler: (params: EventParams<E>) => void): () => void {
    let set = this.handlers.get(event);
    if (set === undefined) {
      set = new Set();
      this.handlers.set(event, set);
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

  /** Protocol problems and handlers that threw (default: logged to the console). */
  onProblem(handler: (p: ClientProblem) => void): () => void {
    this.problemHandlers.add(handler);
    return () => {
      this.problemHandlers.delete(handler);
    };
  }

  /** The engine ended (after every call has been failed as disconnected). */
  onExit(handler: (info: ExitInfo) => void): () => void {
    this.exitHandlers.add(handler);
    return () => {
      this.exitHandlers.delete(handler);
    };
  }

  /** Closes the engine's stdin (it pauses its solves and ends); kills it when the transport cannot. */
  close(): void {
    if (this.transport.close !== undefined) {
      this.transport.close();
    } else {
      this.transport.kill();
    }
  }

  /** Ends the engine at once. */
  kill(): void {
    this.transport.kill();
  }

  // ---- sending ---------------------------------------------------------------------------

  private makeCall(
    method: string,
    params: unknown,
    key: string | undefined,
    resolve: (v: unknown) => void,
    reject: (e: EngineError) => void,
  ): Call | null {
    if (this.state === 'closed') {
      reject(this.refusal !== null ? this.refused(method) : this.disconnected(method));
      return null;
    }
    const id = this.nextId++;
    const message =
      params === undefined
        ? { jsonrpc: '2.0', id, method }
        : { jsonrpc: '2.0', id, method, params };
    const bytes = encoder.encode(`${JSON.stringify(message)}\n`);
    if (bytes.length - 1 > this.maxLineBytes) {
      // what the engine would answer, without sending it the bytes
      reject(
        new EngineError(
          -32600,
          'invalid_request',
          `the line is too long (${bytes.length - 1} bytes)`,
          method,
        ),
      );
      return null;
    }
    return { id, method, bytes, key, resolve, reject };
  }

  private enqueue(call: Call): void {
    if (call.key !== undefined) {
      const slot = this.slots.get(call.key);
      if (slot === undefined) {
        this.slots.set(call.key, { current: call, waiting: null });
      } else if (slot.current.bytes !== null) {
        // the key's call has not been sent: this one takes its place in the queue
        const at = this.queue.indexOf(slot.current);
        if (at >= 0) {
          this.queue[at] = call;
        } else {
          this.queue.push(call);
        }
        this.supersede(slot.current);
        slot.current = call;
        return;
      } else {
        if (slot.waiting !== null) {
          this.supersede(slot.waiting);
        }
        slot.waiting = call;
        return;
      }
    }
    this.queue.push(call);
    this.pump();
  }

  private supersede(call: Call): void {
    call.bytes = null;
    call.reject(clientError('superseded', 'a newer call of its key took its place', call.method));
  }

  private pump(): void {
    while (
      this.state === 'ready' &&
      this.queue.length > 0 &&
      this.inFlight.size < this.maxInFlight
    ) {
      const call = this.queue.shift();
      if (call !== undefined) {
        this.send(call);
      }
    }
  }

  private send(call: Call): void {
    const bytes = call.bytes;
    call.bytes = null;
    this.inFlight.set(call.id, call);
    if (bytes !== null) {
      this.transport.write(bytes);
    }
  }

  // ---- receiving -------------------------------------------------------------------------

  private deliver(item: Item): 'reply' | 'other' {
    switch (item.kind) {
      case 'exit':
        this.ended(item.info);
        return 'other';
      case 'problem':
        this.problem(item.reason, item.detail);
        if (item.fatal) {
          // the stream cannot be followed: no reply can be read from here on,
          // so the calls fail now and the engine goes (a host restarts it)
          this.refuse(
            clientError('protocol', `the engine's output cannot be followed: ${item.detail}`),
          );
        }
        return 'other';
      case 'message': {
        const m = item.message;
        if (isReply(m)) {
          this.reply(m, item.frames);
          return 'reply';
        }
        if (typeof m.method === 'string' && !('id' in m)) {
          this.dispatch(m.method, m.params);
        } else {
          this.problem(
            'not-message',
            `neither a reply nor an event: ${JSON.stringify(m).slice(0, 120)}`,
          );
        }
        return 'other';
      }
    }
  }

  private reply(m: Record<string, unknown>, frames: ReadonlyMap<number, Uint8Array>): void {
    const id = m.id;
    let call: Call | undefined;
    if (id === null) {
      // the engine could not read a request line (too long, too deep): it
      // answers in order, so the line was the oldest call's
      call = this.inFlight.values().next().value;
    } else if (typeof id === 'number') {
      call = this.inFlight.get(id);
    }
    if (call === undefined) {
      this.problem('unexpected-reply', `a reply to ${JSON.stringify(id)}, which no call waits for`);
      return;
    }
    for (const [earlier, c] of this.inFlight) {
      if (earlier === call.id) {
        break;
      }
      this.inFlight.delete(earlier);
      this.problem('lost-reply', `${c.method} (id ${earlier}) got no reply before id ${call.id}'s`);
      c.reject(clientError('protocol', 'its reply never came', c.method));
      this.settled(c);
    }
    this.inFlight.delete(call.id);
    this.settle(call, m, frames);
    this.settled(call);
  }

  private settle(
    call: Call,
    m: Record<string, unknown>,
    frames: ReadonlyMap<number, Uint8Array>,
  ): void {
    const error = m.error;
    if (isRecord(error)) {
      const data = error.data;
      const errc = isRecord(data) ? data.errc : undefined;
      if (
        typeof error.code !== 'number' ||
        typeof error.message !== 'string' ||
        typeof errc !== 'string'
      ) {
        this.problem('bad-reply', `a malformed error reply: ${JSON.stringify(m).slice(0, 120)}`);
        call.reject(clientError('protocol', 'a malformed error reply', call.method));
      } else {
        call.reject(
          new EngineError(error.code, errc as EngineError['errc'], error.message, call.method),
        );
      }
      this.unclaimed(call, frames, []);
      return;
    }
    if (!('result' in m)) {
      this.problem(
        'bad-reply',
        `a reply without result or error: ${JSON.stringify(m).slice(0, 120)}`,
      );
      call.reject(clientError('protocol', 'a reply without result or error', call.method));
      this.unclaimed(call, frames, []);
      return;
    }
    try {
      const { value, used } = attachFrame(call.method, m.result, frames);
      this.unclaimed(call, frames, used);
      call.resolve(value);
    } catch (e) {
      const message = e instanceof FrameError ? e.message : String(e);
      this.problem('bad-frame-reply', message);
      this.unclaimed(call, frames, []);
      call.reject(clientError('protocol', message, call.method));
    }
  }

  private unclaimed(call: Call, frames: ReadonlyMap<number, Uint8Array>, used: number[]): void {
    const left = [...frames.keys()].filter((n) => !used.includes(n));
    if (left.length > 0) {
      this.problem(
        'frame-unclaimed',
        `frame ${left.join(', ')} came before ${call.method}'s reply, which does not name it`,
      );
    }
  }

  // A call is through: its coalescing slot moves on, the queue too.
  private settled(call: Call): void {
    if (call.key !== undefined) {
      const slot = this.slots.get(call.key);
      if (slot?.current === call) {
        if (slot.waiting !== null) {
          slot.current = slot.waiting;
          slot.waiting = null;
          this.queue.push(slot.current);
        } else {
          this.slots.delete(call.key);
        }
      }
    }
    this.pump();
  }

  private dispatch(event: string, params: unknown): void {
    for (const h of [...(this.handlers.get(event) ?? [])]) {
      try {
        (h as (p: unknown) => void)(params);
      } catch (e) {
        this.problem('handler', `a ${event} handler threw: ${String(e)}`);
      }
    }
    for (const h of [...this.anyHandlers]) {
      try {
        h(event, params);
      } catch (e) {
        this.problem('handler', `an event handler threw on ${event}: ${String(e)}`);
      }
    }
  }

  private problem(reason: ClientProblem['reason'], detail: string): void {
    const p: ClientProblem = { reason, detail };
    if (this.problemHandlers.size === 0) {
      console.error(`shapelogic_engine client: ${reason}: ${detail}`);
      return;
    }
    for (const h of [...this.problemHandlers]) {
      try {
        h(p);
      } catch (e) {
        console.error(`shapelogic_engine client: a problem handler threw: ${String(e)}`);
      }
    }
  }

  // ---- the end ---------------------------------------------------------------------------

  private disconnected(method: string): EngineError {
    const info = this.exitInfo;
    const how =
      info === null
        ? 'the engine is not running'
        : `the engine ended (${info.signal !== null ? `signal ${info.signal}` : `status ${String(info.code)}`}${info.error !== undefined ? `: ${info.error}` : ''})`;
    const tail = this.stderr.tail(5);
    return clientError(
      'disconnected',
      tail === '' ? how : `${how}; its last words: ${tail}`,
      method,
    );
  }

  // Fails every call that has not been answered.
  private failAll(error: (method: string) => EngineError): void {
    const calls = [...this.inFlight.values(), ...this.queue];
    for (const slot of this.slots.values()) {
      if (slot.waiting !== null) {
        calls.push(slot.waiting);
      }
    }
    this.inFlight.clear();
    this.queue.length = 0;
    this.slots.clear();
    for (const c of calls) {
      c.bytes = null;
      c.reject(error(c.method));
    }
  }

  private refuse(e: EngineError): void {
    if (this.state === 'closed') {
      return;
    }
    this.refusal = e;
    this.state = 'closed';
    this.failAll((method) => this.refused(method));
    this.transport.kill();
  }

  // The refusal, as the failure of a call of `method`.
  private refused(method: string): EngineError {
    const r = this.refusal;
    return r === null
      ? this.disconnected(method)
      : new EngineError(r.code, r.errc, r.detail, method);
  }

  private ended(info: ExitInfo): void {
    if (this.exitInfo !== null) {
      return;
    }
    this.exitInfo = info;
    this.state = 'closed';
    this.failAll((method) => this.refused(method));
    for (const h of [...this.exitHandlers]) {
      try {
        h(info);
      } catch (e) {
        this.problem('handler', `an exit handler threw: ${String(e)}`);
      }
    }
  }
}
