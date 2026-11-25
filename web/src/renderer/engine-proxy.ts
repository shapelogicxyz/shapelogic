// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine as the renderer sees it (decision 0013): the client's
// interface over the preload bridge (window.bt). The renderer never sees
// Node; frames come as ArrayBuffers and get the client's own views
// (frames.ts), never parsed by hand. Replies settle before the events
// behind them are dispatched, as in the client (dispatch.ts).

import type { BridgeMessage, BtBridge } from '../engine/bridge.ts';
import type {
  CallArgs,
  CallOptions,
  EngineApi,
  EngineStatus,
  EventName,
  EventParams,
  MethodName,
  Params,
  Reply,
} from '../engine/client-types.ts';
import { OrderedInbox } from '../engine/dispatch.ts';
import type { Defer } from '../engine/dispatch.ts';
import { CLIENT_ERROR_CODE, EngineError } from '../engine/errors.ts';
import { attachFrame, FrameError } from '../engine/frames.ts';
import { EventHandlers, Listeners } from '../engine/handlers.ts';

interface Pending {
  method: string;
  resolve: (value: unknown) => void;
  reject: (error: EngineError) => void;
}

/** A handler that threw: the console says so (the page has no log of its own). */
function threw(detail: string): void {
  console.error(`bt engine proxy: ${detail}`);
}

/** The engine host of this window, over the bridge. */
export class EngineProxy implements EngineApi {
  private readonly bridge: BtBridge;
  private readonly inbox: OrderedInbox<BridgeMessage>;
  private readonly off: () => void;
  private nextId = 1;
  private readonly pending = new Map<number, Pending>();
  private readonly handlers = new EventHandlers(threw);
  private readonly statusHandlers = new Listeners<EngineStatus>('a status handler', threw);
  private status_: EngineStatus | null = null;

  constructor(bridge: BtBridge, defer?: Defer) {
    this.bridge = bridge;
    this.inbox = new OrderedInbox<BridgeMessage>((m) => this.deliver(m), defer);
    this.off = bridge.onMessage((m) => {
      this.inbox.push(m);
    });
    // the host told the window its status before this proxy listened (the
    // engine starts while the page loads): ask for it
    bridge.send({ kind: 'status' });
  }

  /** The engine's status as the host last told it. */
  get status(): EngineStatus | null {
    return this.status_;
  }

  call<M extends MethodName>(method: M, ...args: CallArgs<M>): Promise<Reply<M>> {
    const [params, options] = args as [Params<M> | undefined, CallOptions | undefined];
    const id = this.nextId++;
    return new Promise<Reply<M>>((resolve, reject) => {
      this.pending.set(id, { method, resolve: resolve as (v: unknown) => void, reject });
      this.bridge.send({
        kind: 'call',
        id,
        method,
        ...(params === undefined ? {} : { params }),
        ...(options?.coalesce === undefined ? {} : { coalesce: options.coalesce }),
      });
    });
  }

  on<E extends EventName>(event: E, handler: (params: EventParams<E>) => void): () => void {
    return this.handlers.on(event, handler);
  }

  onEvent(handler: (event: string, params: unknown) => void): () => void {
    return this.handlers.onEvent(handler);
  }

  /** The engine started, ended, was restarted (a new document: reload), or could not be kept running. */
  onStatus(handler: (status: EngineStatus) => void): () => void {
    return this.statusHandlers.on(handler);
  }

  /** Stops listening; calls still waiting fail as disconnected. */
  dispose(): void {
    this.off();
    for (const [id, p] of this.pending) {
      this.pending.delete(id);
      p.reject(
        new EngineError(CLIENT_ERROR_CODE, 'disconnected', 'the proxy was closed', p.method),
      );
    }
  }

  private deliver(m: BridgeMessage): 'reply' | 'other' {
    switch (m.kind) {
      case 'reply':
      case 'error': {
        const p = this.pending.get(m.id);
        if (p === undefined) {
          return 'other';
        }
        this.pending.delete(m.id);
        if (m.kind === 'error') {
          p.reject(new EngineError(m.code, m.errc as EngineError['errc'], m.message, p.method));
          return 'reply';
        }
        try {
          const frames = new Map<number, Uint8Array>();
          if (m.frame !== undefined) {
            frames.set(m.frame.desc.frame, new Uint8Array(m.frame.data));
          }
          p.resolve(attachFrame(p.method, m.result, frames).value);
        } catch (e) {
          p.reject(
            new EngineError(
              CLIENT_ERROR_CODE,
              'protocol',
              e instanceof FrameError ? e.message : String(e),
              p.method,
            ),
          );
        }
        return 'reply';
      }
      case 'event':
        this.handlers.emit(m.event, m.params);
        return 'other';
      case 'status':
        this.status_ = m.status;
        this.statusHandlers.emit(m.status);
        return 'other';
    }
  }
}
