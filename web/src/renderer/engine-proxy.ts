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

interface Pending {
  method: string;
  resolve: (value: unknown) => void;
  reject: (error: EngineError) => void;
}

/** The engine host of this window, over the bridge. */
export class EngineProxy implements EngineApi {
  private readonly bridge: BtBridge;
  private readonly inbox: OrderedInbox<BridgeMessage>;
  private readonly off: () => void;
  private nextId = 1;
  private readonly pending = new Map<number, Pending>();
  private readonly handlers = new Map<string, Set<(params: never) => void>>();
  private readonly anyHandlers = new Set<(event: string, params: unknown) => void>();
  private readonly statusHandlers = new Set<(status: EngineStatus) => void>();
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

  /** The engine started, ended, was restarted (a new document: reload), or could not be kept running. */
  onStatus(handler: (status: EngineStatus) => void): () => void {
    this.statusHandlers.add(handler);
    return () => {
      this.statusHandlers.delete(handler);
    };
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
        for (const h of [...(this.handlers.get(m.event) ?? [])]) {
          this.guard(() => {
            (h as (p: unknown) => void)(m.params);
          });
        }
        for (const h of [...this.anyHandlers]) {
          this.guard(() => {
            h(m.event, m.params);
          });
        }
        return 'other';
      case 'status':
        this.status_ = m.status;
        for (const h of [...this.statusHandlers]) {
          this.guard(() => {
            h(m.status);
          });
        }
        return 'other';
    }
  }

  private guard(f: () => void): void {
    try {
      f();
    } catch (e) {
      console.error('bt engine proxy: a handler threw', e);
    }
  }
}
