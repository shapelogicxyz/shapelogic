// SPDX-License-Identifier: GPL-3.0-or-later
//
// The host's side of the bridge to a window's renderer (decision 0013):
// calls from the renderer go to the engine host; replies, events and the
// engine's status go back on one channel, in the order the client
// delivers them (a reply before the events behind it). A call the main
// process does not admit (from stage 8: one naming a file the window was
// not handed, path-grants.ts) is refused here, never sent. Free of
// Electron: main/index.ts connects it to ipcMain and webContents.

import { ERROR_CODES, FRAME_PATHS, METHOD_NAMES } from '../engine/api.generated.ts';
import { isBridgeCall, isStatusRequest } from '../engine/bridge.ts';
import type { BridgeMessage } from '../engine/bridge.ts';
import type { CallOptions, EngineStatus } from '../engine/client-types.ts';
import { CLIENT_ERROR_CODE, isEngineError } from '../engine/errors.ts';
import type { FrameView } from '../engine/frames.ts';
import type { EngineHost } from './engine-host.ts';

type Host = Pick<EngineHost, 'call' | 'onEvent' | 'onStatus' | 'status'>;
type UntypedCall = (method: string, params?: unknown, options?: CallOptions) => Promise<unknown>;

const methods: ReadonlySet<string> = new Set(METHOD_NAMES);

/** A call the renderer made (its id) and how it ended (errc null: it succeeded). */
export interface CallOutcome {
  id: number;
  method: string;
  errc: string | null;
}

/**
 * Serves a host to one renderer; `post` sends a message to it, `log` (when
 * given) hears how each call ended, `admit` (when given) says why a call
 * may not go to the engine (it fails as `invalid_argument` with those
 * words), or null.
 */
export function serveEngine(
  host: Host,
  post: (message: BridgeMessage) => void,
  log?: (outcome: CallOutcome) => void,
  admit?: (method: string, params: unknown) => string | null,
): { receive(message: unknown): void; dispose(): void } {
  let disposed = false;
  const send = (message: BridgeMessage): void => {
    if (!disposed) {
      post(message);
    }
  };
  const offEvent = host.onEvent((event, params) => {
    send({ kind: 'event', event, params });
  });
  const offStatus = host.onStatus((status: EngineStatus) => {
    // after the errors of the calls the change failed: the host rejects them
    // first, and their rejections are delivered a microtask later
    queueMicrotask(() => {
      send({ kind: 'status', status });
    });
  });
  send({ kind: 'status', status: host.status });

  return {
    receive(message: unknown): void {
      if (isStatusRequest(message)) {
        send({ kind: 'status', status: host.status });
        return;
      }
      if (!isBridgeCall(message)) {
        return;
      }
      const { id, method } = message;
      if (!methods.has(method)) {
        send({
          kind: 'error',
          id,
          code: -32601,
          errc: 'method_not_found',
          message: `no method "${method}"`,
        });
        return;
      }
      const refused = admit?.(method, message.params) ?? null;
      if (refused !== null) {
        log?.({ id, method, errc: 'invalid_argument' });
        send({
          kind: 'error',
          id,
          code: ERROR_CODES.invalid_argument[0],
          errc: 'invalid_argument',
          message: refused,
        });
        return;
      }
      const options: CallOptions =
        message.coalesce === undefined ? {} : { coalesce: message.coalesce };
      (host.call.bind(host) as UntypedCall)(method, message.params, options).then(
        (value) => {
          log?.({ id, method, errc: null });
          const path = (FRAME_PATHS as Partial<Record<string, string>>)[method];
          if (path === undefined) {
            send({ kind: 'reply', id, result: value });
            return;
          }
          const result = value as Record<string, unknown>;
          const view = result[path] as FrameView;
          const desc = { frame: view.frame, bytes: view.bytes, arrays: view.layout };
          send({
            kind: 'reply',
            id,
            result: { ...result, [path]: desc },
            frame: { desc, data: view.buffer },
          });
        },
        (e: unknown) => {
          log?.({ id, method, errc: isEngineError(e) ? e.errc : 'protocol' });
          if (isEngineError(e)) {
            send({ kind: 'error', id, code: e.code, errc: e.errc, message: e.detail });
          } else {
            send({
              kind: 'error',
              id,
              code: CLIENT_ERROR_CODE,
              errc: 'protocol',
              message: String(e),
            });
          }
        },
      );
    },
    dispose(): void {
      disposed = true;
      offEvent();
      offStatus();
    },
  };
}
