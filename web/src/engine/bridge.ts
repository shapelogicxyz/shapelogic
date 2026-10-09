// SPDX-License-Identifier: GPL-3.0-or-later
//
// What crosses between the engine host (Electron's main process) and the
// renderer's proxy (decision 0013): calls one way; replies, events and the
// engine's status the other, on one ordered channel, so the renderer sees
// decision 0012's order. A reply's frame crosses as its description plus
// an ArrayBuffer; the proxy makes the same views the client makes.

import type { Frame } from './api.generated.ts';
import type { EngineStatus } from './client-types.ts';

/** From the renderer: a call. */
export interface BridgeCall {
  kind: 'call';
  id: number;
  method: string;
  params?: unknown;
  coalesce?: string;
}

/**
 * From the renderer: a call, or a request for the engine's status (the proxy
 * sends it when it connects: a status posted before the renderer listened,
 * such as `running` while the page loaded, was lost).
 */
export type BridgeRequest = BridgeCall | { kind: 'status' };

/** To the renderer. */
export type BridgeMessage =
  | { kind: 'reply'; id: number; result: unknown; frame?: { desc: Frame; data: ArrayBuffer } }
  | { kind: 'error'; id: number; code: number; errc: string; message: string }
  | { kind: 'event'; event: string; params: unknown }
  | { kind: 'status'; status: EngineStatus };

/** What the preload script exposes as `window.bt`. */
export interface BtBridge {
  send(request: BridgeRequest): void;
  /** Every message from the host, in order; returns the unsubscribe. */
  onMessage(handler: (message: BridgeMessage) => void): () => void;
}

/** Whether a message from the renderer is a well-formed call (the host checks before calling). */
export function isBridgeCall(v: unknown): v is BridgeCall {
  if (typeof v !== 'object' || v === null) {
    return false;
  }
  const c = v as Record<string, unknown>;
  return (
    c.kind === 'call' &&
    typeof c.id === 'number' &&
    Number.isSafeInteger(c.id) &&
    typeof c.method === 'string' &&
    (c.coalesce === undefined || typeof c.coalesce === 'string')
  );
}

/** Whether a message from the renderer asks for the engine's status. */
export function isStatusRequest(v: unknown): v is { kind: 'status' } {
  return typeof v === 'object' && v !== null && (v as Record<string, unknown>).kind === 'status';
}
