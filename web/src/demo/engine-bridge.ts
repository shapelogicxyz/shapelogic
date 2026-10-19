// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo's window.bt: the engine client over the browser engine
// (wasm-transport.ts), served to the renderer's proxy as Electron's main
// process serves a window (main/engine-ipc.ts). A call that would start a
// search is refused before it reaches the engine: a solve, an analysis or
// an explorer would run on the page's one thread for as long as it took.
// solve.start alone may be taken by the demo (`solveHook`): the call is
// answered as the engine would, and the events of a run are played to the
// renderer while the demo puts the saved solution back (main.ts).

import type { SolveProgress, SolveState } from '../engine/api.generated.ts';
import type { BridgeCall, BridgeMessage, BridgeRequest, BtBridge } from '../engine/bridge.ts';
import type { EngineClient } from '../engine/client.ts';
import type { EngineStatus } from '../engine/client-types.ts';
import { serveEngine } from '../main/engine-ipc.ts';

/** What a refused search says (shown after what was being done). */
export const SEARCH_REFUSED =
  'searches run in the app, not in this demo (Download ShapeLogic to solve)';

const SEARCHES =
  /^(solve\.(start|prepare|continue)|solution\.analyse|explore\.(openAssembly|openMovements|analyse))$/;

/** The events a played run sends to the renderer, as the engine's would come. */
export interface RunEvents {
  state(problem: string, state: SolveState): void;
  progress(problem: string, state: SolveState, progress: SolveProgress): void;
  solutionsAdded(problem: string, added: { assemblies: number; solutions: number }): void;
}

/**
 * Takes a solve.start of a problem: true when the demo plays the run
 * (the call is then answered with `revision`), false to refuse it.
 */
export type SolveHook = (problem: string, events: RunEvents) => boolean;

/** The engine in the page as the preload's bridge. */
export class EngineBridge implements BtBridge {
  private readonly handlers = new Set<(message: BridgeMessage) => void>();
  private readonly server: ReturnType<typeof serveEngine>;
  private solutionsVersion = 1_000;
  /** The demo's Solve, when it has one. */
  solveHook: SolveHook | null = null;
  /** The document's revision now (a played run's reply says it). */
  revisionOf: () => number = () => 0;
  /** A played run is under way (until its state ends it). */
  private playing = false;
  // messages reach the renderer a task later, in order, as they do over
  // IPC: the app subscribes after it has asked for the status
  private readonly outbox: BridgeMessage[] = [];
  private draining = false;

  constructor(client: EngineClient, version: string) {
    const status: EngineStatus = { state: 'running', version, restarts: 0 };
    this.server = serveEngine(
      {
        status,
        call: client.call.bind(client),
        onEvent: client.onEvent.bind(client),
        onStatus: () => () => undefined,
      },
      (message) => {
        this.post(message);
      },
      undefined,
      (method) => (SEARCHES.test(method) ? SEARCH_REFUSED : null),
    );
  }

  send(request: BridgeRequest): void {
    if (request.kind === 'call' && request.method === 'solve.start' && this.played(request)) {
      return;
    }
    // a played run cannot be paused or cleared: it is over in a moment
    if (
      request.kind === 'call' &&
      this.playing &&
      (request.method === 'solve.pause' || request.method === 'solve.abort')
    ) {
      this.post({
        kind: 'error',
        id: request.id,
        code: -32602,
        errc: 'invalid_argument',
        message: 'the demo plays its run to the end (a moment)',
      });
      return;
    }
    this.server.receive(request);
  }

  onMessage(handler: (message: BridgeMessage) => void): () => void {
    this.handlers.add(handler);
    return () => {
      this.handlers.delete(handler);
    };
  }

  // a solve.start the demo plays: answered here, its events posted by the hook
  private played(call: BridgeCall): boolean {
    const problem = (call.params as { problem?: unknown } | undefined)?.problem;
    if (this.solveHook === null || typeof problem !== 'string') {
      return false;
    }
    const events: RunEvents = {
      state: (p, state) => {
        this.playing = state === 'preparing' || state === 'assembling' || state === 'draining';
        this.post({ kind: 'event', event: 'solve.state', params: { problem: p, state } });
      },
      progress: (p, state, progress) => {
        this.post({
          kind: 'event',
          event: 'solve.progress',
          params: { problem: p, state, progress },
        });
      },
      solutionsAdded: (p, added) => {
        this.post({
          kind: 'event',
          event: 'solve.solutionsAdded',
          params: {
            problem: p,
            saved: added.solutions,
            solutionsVersion: this.solutionsVersion++,
            ...added,
          },
        });
      },
    };
    if (!this.solveHook(problem, events)) {
      return false;
    }
    this.playing = true;
    this.post({ kind: 'reply', id: call.id, result: { revision: this.revisionOf() } });
    return true;
  }

  private post(message: BridgeMessage): void {
    this.outbox.push(message);
    if (this.draining) {
      return;
    }
    this.draining = true;
    queueMicrotask(() => {
      this.draining = false;
      while (this.outbox.length > 0) {
        const m = this.outbox.shift();
        if (m !== undefined) {
          for (const h of [...this.handlers]) {
            h(m);
          }
        }
      }
    });
  }
}
