// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fake shapelogic_engine for fast tests: it answers a client with a recorded
// session (src/engine/recorder.ts), the same stdout bytes in the same
// chunks, each run of output after the request that came before it in the
// recording. The client must make the recorded requests in the recorded
// order (the same session code does); a request that differs is answered
// with an error and noted in `mismatches`. Timing is optional: by default
// every chunk comes in a task of its own, at once.

import { fromBase64, toBase64 } from '../src/engine/recorder.ts';
import type { Recording, RecordingEntry } from '../src/engine/recorder.ts';
import type { ExitInfo, Transport } from '../src/engine/transport.ts';

export interface FakeEngineOptions {
  /** Keep the recorded delays between a request and what followed it (default false). */
  timing?: boolean;
  /** With timing, how much faster than recorded (default 1). */
  speed?: number;
}

type Output = Extract<RecordingEntry, { recv: string } | { stderr: string } | { exit: ExitInfo }>;

function jsonEqual(a: unknown, b: unknown): boolean {
  if (a === b) {
    return true;
  }
  if (typeof a !== 'object' || typeof b !== 'object' || a === null || b === null) {
    return false;
  }
  if (Array.isArray(a) !== Array.isArray(b)) {
    return false;
  }
  const ka = Object.keys(a);
  const kb = Object.keys(b);
  return (
    ka.length === kb.length &&
    ka.every((k) => jsonEqual((a as Record<string, unknown>)[k], (b as Record<string, unknown>)[k]))
  );
}

function parse(line: string): unknown {
  try {
    return JSON.parse(line);
  } catch {
    return line;
  }
}

/** A recorded engine as a Transport. */
export class FakeEngine implements Transport {
  /** Requests that were not the recorded ones (a test expects none). */
  readonly mismatches: string[] = [];
  private readonly entries: RecordingEntry[];
  private readonly timing: boolean;
  private readonly speed: number;
  private cursor = 0;
  private partial = '';
  private readonly decoder = new TextDecoder();
  private dataHandler: ((chunk: Uint8Array) => void) | null = null;
  private stderrHandler: ((chunk: Uint8Array) => void) | null = null;
  private exitHandler: ((info: ExitInfo) => void) | null = null;
  // output waiting to go, in order, each with the time it is due
  private readonly outbox: { entry: Output; due: number }[] = [];
  private timer: ReturnType<typeof setTimeout> | null = null;
  private exited = false;

  constructor(recording: Recording, options: FakeEngineOptions = {}) {
    this.entries = recording.entries;
    this.timing = options.timing ?? false;
    this.speed = options.speed ?? 1;
    // whatever the engine said before the first request
    this.release(0, Date.now());
  }

  write(data: Uint8Array): void {
    if (this.exited) {
      return;
    }
    const text = this.partial + this.decoder.decode(data);
    const lines = text.split('\n');
    this.partial = lines.pop() ?? '';
    for (const line of lines) {
      this.request(line);
    }
  }

  onData(handler: (chunk: Uint8Array) => void): void {
    this.dataHandler = handler;
  }

  onStderr(handler: (chunk: Uint8Array) => void): void {
    this.stderrHandler = handler;
  }

  onExit(handler: (info: ExitInfo) => void): void {
    this.exitHandler = handler;
  }

  kill(): void {
    this.end({ code: null, signal: 'SIGKILL' });
  }

  close(): void {
    const next = this.entries[this.cursor];
    if (next !== undefined && 'close' in next) {
      this.cursor++;
      this.release(next.t, Date.now());
    } else {
      this.end({ code: 0, signal: null });
    }
  }

  /** Whether every recorded request has been made. */
  get through(): boolean {
    return !this.entries.slice(this.cursor).some((e) => 'send' in e || 'close' in e);
  }

  private request(line: string): void {
    const next = this.entries[this.cursor];
    if (next !== undefined && 'send' in next && jsonEqual(parse(next.send), parse(line))) {
      this.cursor++;
      this.release(next.t, Date.now());
      return;
    }
    const expected = next !== undefined && 'send' in next ? next.send : 'the end of the recording';
    this.mismatches.push(`expected ${expected.slice(0, 200)}, got ${line.slice(0, 200)}`);
    const id = (parse(line) as { id?: unknown } | null)?.id ?? null;
    const reply = {
      jsonrpc: '2.0',
      id,
      error: {
        code: -32603,
        message: `fake engine: expected ${expected.slice(0, 200)}`,
        data: { errc: 'internal' },
      },
    };
    const last = this.outbox[this.outbox.length - 1]?.due ?? Date.now();
    const bytes = new TextEncoder().encode(`${JSON.stringify(reply)}\n`);
    this.outbox.push({ entry: { t: 0, recv: toBase64(bytes) }, due: last });
    this.schedule();
  }

  // Queues the output that follows the entry at time `t` (up to the next request).
  private release(t: number, now: number): void {
    let last = this.outbox[this.outbox.length - 1]?.due ?? now;
    while (this.cursor < this.entries.length) {
      const e = this.entries[this.cursor];
      if (e === undefined || 'send' in e || 'close' in e) {
        break;
      }
      this.cursor++;
      const due = this.timing ? Math.max(last, now + (e.t - t) / this.speed) : last;
      last = due;
      this.outbox.push({ entry: e, due });
    }
    this.schedule();
  }

  private schedule(): void {
    if (this.timer !== null || this.exited) {
      return;
    }
    const next = this.outbox[0];
    if (next === undefined) {
      return;
    }
    this.timer = setTimeout(
      () => {
        this.timer = null;
        this.emitOne();
      },
      Math.max(0, next.due - Date.now()),
    );
  }

  private emitOne(): void {
    const next = this.outbox.shift();
    if (next === undefined || this.exited) {
      return;
    }
    const e = next.entry;
    if ('recv' in e) {
      this.dataHandler?.(fromBase64(e.recv));
    } else if ('stderr' in e) {
      this.stderrHandler?.(new TextEncoder().encode(e.stderr));
    } else {
      this.end(e.exit);
    }
    this.schedule();
  }

  private end(info: ExitInfo): void {
    if (this.exited) {
      return;
    }
    this.exited = true;
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
    }
    this.outbox.length = 0;
    this.exitHandler?.(info);
  }
}
