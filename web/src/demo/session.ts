// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo's recorded session (site/public/demo/session.json): what the
// real engine answered to each call the app made while tools/record-demo.ts
// drove it through the demo puzzle. The demo page (replay-bridge.ts)
// answers the same calls with the same replies, so the site needs no
// engine. Plain types and helpers, shared by the recorder (Node) and the
// page.

import type { Frame } from '../engine/api.generated.ts';
import type { BridgeMessage } from '../engine/bridge.ts';
import type { EngineStatus } from '../engine/client-types.ts';

/** A message as the recorder hands it to the page while recording (a frame's bytes as base64). */
export type WireMessage =
  | Exclude<BridgeMessage, { kind: 'reply' }>
  | { kind: 'reply'; id: number; result: unknown; frame?: { desc: Frame; data: string } };

/**
 * A reply as recorded: a bridge reply or error without its id; a frame's
 * bytes are the session's `frames[data]` (replies with the same bytes, such
 * as a shape's mesh and its piece's in the playback, share them).
 */
export type SessionReply =
  | { kind: 'reply'; result: unknown; frame?: { desc: Frame; data: number } }
  | { kind: 'error'; code: number; errc: string; message: string };

/** An event the engine sent. */
export interface SessionEvent {
  event: string;
  params: unknown;
}

/** One call: its method and parameters, the reply, and the events that came after the reply. */
export interface SessionCall {
  method: string;
  params?: unknown;
  reply: SessionReply;
  events: SessionEvent[];
}

/** The file. */
export interface DemoSession {
  format: 'shapelogic-demo-session';
  version: 1;
  /** The engine's status when the page connected. */
  status: EngineStatus;
  /** The puzzle the demo opens (the path the engine was given). */
  puzzle: string;
  /** Events before the first reply. */
  events: SessionEvent[];
  /** Distinct calls with their replies, in the order they were first made. */
  calls: SessionCall[];
  /** The frames' bytes, base64. */
  frames: string[];
}

/** A canonical text of a value (object keys sorted): equal values give equal keys. */
export function canonical(v: unknown): string {
  if (Array.isArray(v)) {
    return `[${v.map(canonical).join(',')}]`;
  }
  if (typeof v === 'object' && v !== null) {
    const o = v as Record<string, unknown>;
    return `{${Object.keys(o)
      .filter((k) => o[k] !== undefined)
      .sort()
      .map((k) => `${JSON.stringify(k)}:${canonical(o[k])}`)
      .join(',')}}`;
  }
  // undefined (in an array) has no JSON
  return v === undefined ? 'null' : JSON.stringify(v);
}

/** The key of a call: its method and parameters. */
export function callKey(method: string, params: unknown): string {
  return `${method} ${canonical(params ?? null)}`;
}
