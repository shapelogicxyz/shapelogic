// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo's window.bt: the engine client over the browser engine
// (wasm-transport.ts), served to the renderer's proxy as Electron's main
// process serves a window (main/engine-ipc.ts). A call that would start a
// search is refused before it reaches the engine: a solve, an analysis or
// an explorer would run on the page's one thread for as long as it took.

import type { BridgeMessage, BridgeRequest, BtBridge } from '../engine/bridge.ts';
import type { EngineClient } from '../engine/client.ts';
import type { EngineStatus } from '../engine/client-types.ts';
import { serveEngine } from '../main/engine-ipc.ts';

/** What a refused search says (shown after what was being done). */
export const SEARCH_REFUSED =
  'searches run in the app, not in this demo (Download ShapeLogic to solve)';

const SEARCHES =
  /^(solve\.(start|prepare|continue)|solution\.analyse|explore\.(openAssembly|openMovements|analyse))$/;

/** The engine in the page as the preload's bridge. */
export class EngineBridge implements BtBridge {
  private readonly handlers = new Set<(message: BridgeMessage) => void>();
  private readonly server: ReturnType<typeof serveEngine>;
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

  send(request: BridgeRequest): void {
    this.server.receive(request);
  }

  onMessage(handler: (message: BridgeMessage) => void): () => void {
    this.handlers.add(handler);
    return () => {
      this.handlers.delete(handler);
    };
  }
}
