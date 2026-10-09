// SPDX-License-Identifier: GPL-3.0-or-later
//
// The errors a call to the engine ends with: the engine's own replies
// (decision 0012: a code and data.errc) and the few the client raises.

import type { Errc } from './api.generated.ts';

/**
 * What the client itself reports (code 0, no engine reply behind it):
 * - `disconnected`: the engine ended or is not running; the call's outcome
 *   is unknown (it may have run).
 * - `protocol`: the engine broke the protocol (a reply lost or malformed, a
 *   frame missing); the call's outcome is unknown.
 * - `superseded`: a newer call with the same coalescing key took this one's
 *   place before it was sent; it never reached the engine.
 */
export type ClientErrc = 'disconnected' | 'protocol' | 'superseded';

/** The code of the errors the client raises itself. */
export const CLIENT_ERROR_CODE = 0;

/** A call that failed: `errc` says why, `code` is the JSON-RPC code (0: the client's). */
export class EngineError extends Error {
  readonly code: number;
  readonly errc: Errc | ClientErrc;
  /** The method called, when the error ended a call. */
  readonly method: string | undefined;
  /** The engine's (or the client's) message, without the method in front. */
  readonly detail: string;

  constructor(code: number, errc: Errc | ClientErrc, detail: string, method?: string) {
    super(method === undefined ? detail : `${method}: ${detail}`);
    this.name = 'EngineError';
    this.code = code;
    this.errc = errc;
    this.method = method;
    this.detail = detail;
  }
}

/** Whether `e` is an EngineError (with that errc, when one is given). */
export function isEngineError(e: unknown, errc?: Errc | ClientErrc): e is EngineError {
  return e instanceof EngineError && (errc === undefined || e.errc === errc);
}

/** Whether a call was replaced by a newer one of its coalescing key. */
export function isSuperseded(e: unknown): boolean {
  return isEngineError(e, 'superseded');
}
