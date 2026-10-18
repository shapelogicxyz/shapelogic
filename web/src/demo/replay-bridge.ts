// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo's engine: a window.bt that answers each call with the reply the
// real engine gave to the same call in the recorded session (session.ts),
// then the events that followed it. A call made more than once in the
// recording gets its replies in their order, the last one from then on.
// A call the recording does not have fails as `superseded`, which the UI
// takes as nothing to show; when it would have changed the puzzle the
// page is told (`onRefused`) and says that this is a demo. Nothing is
// kept: a reload starts from the recording again.

import type { BridgeMessage, BridgeRequest, BtBridge } from '../engine/bridge.ts';
import { CLIENT_ERROR_CODE } from '../engine/errors.ts';
import { fromBase64 } from '../engine/recorder.ts';
import { callKey } from './session.ts';
import type { DemoSession, SessionCall, SessionEvent } from './session.ts';

// methods that only read (or end a view): a miss is not a refused change
const READS =
  /\.(get|list|page|info|table|mesh|geometry|neighbour|poses|track|status|history|kind|fit|preview|taskPreview|placements|node|board|tray|rodSetLayout|stlParameters|stlMesh|analyseStatus|close|hello)$|^settings\.set$/;

/** The recorded engine as the preload's bridge. */
export class ReplayBridge implements BtBridge {
  private readonly session: DemoSession;
  private readonly frames: Uint8Array[];
  private readonly onRefused: (method: string) => void;
  private readonly calls = new Map<string, SessionCall[]>();
  private readonly used = new Map<string, number>();
  private readonly handlers = new Set<(message: BridgeMessage) => void>();
  private started = false;
  /** Calls the recording did not have (for the console: the recorder's tour is missing them). */
  readonly misses: string[] = [];

  /** `onRefused` hears of each call that would have changed the puzzle. */
  constructor(session: DemoSession, onRefused: (method: string) => void = () => undefined) {
    this.session = session;
    this.frames = session.frames.map(fromBase64);
    this.onRefused = onRefused;
    for (const c of session.calls) {
      const key = callKey(c.method, c.params);
      const list = this.calls.get(key);
      if (list === undefined) {
        this.calls.set(key, [c]);
      } else {
        list.push(c);
      }
    }
  }

  send(request: BridgeRequest): void {
    if (request.kind === 'status') {
      const first = !this.started;
      this.started = true;
      this.post([
        { kind: 'status', status: this.session.status },
        ...(first ? this.events(this.session.events) : []),
      ]);
      return;
    }
    const key = callKey(request.method, request.params);
    const list = this.calls.get(key);
    if (list === undefined) {
      this.misses.push(key);
      console.info(`demo: not in the recording: ${key.slice(0, 300)}`);
      this.post([
        {
          kind: 'error',
          id: request.id,
          code: CLIENT_ERROR_CODE,
          errc: 'superseded',
          message: 'not in the demo',
        },
      ]);
      if (!READS.test(request.method)) {
        const method = request.method;
        setTimeout(() => {
          this.onRefused(method);
        }, 0);
      }
      return;
    }
    const n = this.used.get(key) ?? 0;
    this.used.set(key, n + 1);
    const call = list[Math.min(n, list.length - 1)];
    if (call === undefined) {
      return;
    }
    const r = call.reply;
    const reply: BridgeMessage =
      r.kind === 'error'
        ? { kind: 'error', id: request.id, code: r.code, errc: r.errc, message: r.message }
        : {
            kind: 'reply',
            id: request.id,
            result: r.result,
            ...(r.frame === undefined
              ? {}
              : {
                  frame: {
                    desc: r.frame.desc,
                    data: (this.frames[r.frame.data] ?? new Uint8Array(0)).slice().buffer,
                  },
                }),
          };
    this.post([reply, ...this.events(call.events)]);
  }

  onMessage(handler: (message: BridgeMessage) => void): () => void {
    this.handlers.add(handler);
    return () => {
      this.handlers.delete(handler);
    };
  }

  private events(events: SessionEvent[]): BridgeMessage[] {
    return events.map((e) => ({ kind: 'event', event: e.event, params: e.params }));
  }

  // a task later, as the host's messages come
  private post(messages: BridgeMessage[]): void {
    setTimeout(() => {
      for (const m of messages) {
        for (const h of [...this.handlers]) {
          h(m);
        }
      }
    }, 0);
  }
}
