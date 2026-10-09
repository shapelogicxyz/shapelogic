// SPDX-License-Identifier: GPL-3.0-or-later
//
// The typed surface of the engine for the rest of the front end: what the
// client (client.ts) and the renderer's proxy of it (engine-proxy.ts) both
// offer. Calls are typed by the generated Methods, events by Events.

import type { Events, Frame, FrameMethod, Methods } from './api.generated.ts';
import type { FrameView } from './frames.ts';

export type MethodName = keyof Methods;
/** The events a front end subscribes to (`frame` is the client's own business). */
export type EventName = Exclude<keyof Events, 'frame'>;
export type Params<M extends MethodName> = Methods[M]['params'];
export type EventParams<E extends EventName> = Events[E]['params'];

type WithFrameView<R> = R extends { frame: Frame } ? Omit<R, 'frame'> & { frame: FrameView } : R;

/** What a call resolves to: the result, with a frame's description replaced by its view. */
export type Reply<M extends MethodName> = M extends FrameMethod
  ? WithFrameView<Methods[M]['result']>
  : Methods[M]['result'];

export interface CallOptions {
  /**
   * Coalesces the call with others of the same key: at most one of them is
   * at the engine at a time; a newer one waits, and an even newer one takes
   * the waiting one's place (which then fails as `superseded`, unsent). For
   * reads whose newest answer is what counts (a shape's mesh during a
   * drag); such a call may be sent after calls made later.
   */
  coalesce?: string;
}

/** The arguments after the method: the params may be left out when none is required. */
export type CallArgs<M extends MethodName> =
  Record<string, never> extends Params<M>
    ? [params?: Params<M>, options?: CallOptions]
    : [params: Params<M>, options?: CallOptions];

/** What the engine host tells about its engine. */
export type EngineStatus =
  | { state: 'running'; version: string; restarts: number }
  | { state: 'exited'; code: number | null; signal: string | null; stderr: string }
  | { state: 'failed'; reason: string };

/** The engine as the front end uses it: the client in Node, the proxy in the renderer. */
export interface EngineApi {
  /** Calls a method; resolves with its reply, or fails with an EngineError. */
  call<M extends MethodName>(method: M, ...args: CallArgs<M>): Promise<Reply<M>>;
  /** Subscribes to an event; returns the unsubscribe. */
  on<E extends EventName>(event: E, handler: (params: EventParams<E>) => void): () => void;
  /** Subscribes to every event (those the schema does not know too); returns the unsubscribe. */
  onEvent(handler: (event: string, params: unknown) => void): () => void;
}
