// SPDX-License-Identifier: GPL-3.0-or-later
//
// A session with the engine recorded byte for byte: the lines the client
// wrote, the engine's stdout chunks as they came, its stderr and its end.
// The framing tests replay the stdout in chunks of any size; the fake
// engine (web/fake-engine/) answers a client with it.

import type { ExitInfo, Transport } from './transport.ts';

/** One thing that happened, `t` milliseconds after the recording began. */
export type RecordingEntry =
  /** A line the client wrote to stdin (without its newline). */
  | { t: number; send: string }
  /** The client closed stdin. */
  | { t: number; close: true }
  /** A chunk of stdout, base64. */
  | { t: number; recv: string }
  /** A chunk of stderr, as text. */
  | { t: number; stderr: string }
  /** The engine ended. */
  | { t: number; exit: ExitInfo };

/** A recorded session (a JSON file under web/fake-engine/recordings/). */
export interface Recording {
  format: 'bt-engine-recording';
  version: 1;
  /** The engine's `--version`. */
  engine: string;
  /** What the session does. */
  note: string;
  entries: RecordingEntry[];
}

/** Base64 of bytes (btoa works on strings of bytes, so in slices). */
export function toBase64(bytes: Uint8Array): string {
  let text = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    text += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(text);
}

export function fromBase64(text: string): Uint8Array {
  const s = atob(text);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) {
    out[i] = s.charCodeAt(i);
  }
  return out;
}

/** The engine's whole stdout in a recording, in one piece. */
export function recordedStdout(recording: Recording): Uint8Array {
  const chunks = recording.entries.flatMap((e) => ('recv' in e ? [fromBase64(e.recv)] : []));
  const out = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
  let at = 0;
  for (const c of chunks) {
    out.set(c, at);
    at += c.length;
  }
  return out;
}

/** A transport that records everything that passes through it. */
export class RecordingTransport implements Transport {
  private readonly inner: Transport;
  private readonly clock: () => number;
  private readonly start: number;
  private readonly entries: RecordingEntry[] = [];
  private readonly decoder = new TextDecoder();

  constructor(inner: Transport, clock: () => number = Date.now) {
    this.inner = inner;
    this.clock = clock;
    this.start = clock();
  }

  /** The session so far. */
  recording(engine: string, note: string): Recording {
    return { format: 'bt-engine-recording', version: 1, engine, note, entries: [...this.entries] };
  }

  private now(): number {
    return Math.round(this.clock() - this.start);
  }

  write(data: Uint8Array): void {
    const text = this.decoder.decode(data);
    for (const line of text.split('\n').slice(0, -1)) {
      this.entries.push({ t: this.now(), send: line });
    }
    this.inner.write(data);
  }

  onData(handler: (chunk: Uint8Array) => void): void {
    this.inner.onData((chunk) => {
      this.entries.push({ t: this.now(), recv: toBase64(chunk) });
      handler(chunk);
    });
  }

  onStderr(handler: (chunk: Uint8Array) => void): void {
    this.inner.onStderr?.((chunk) => {
      this.entries.push({ t: this.now(), stderr: this.decoder.decode(chunk) });
      handler(chunk);
    });
  }

  onExit(handler: (info: ExitInfo) => void): void {
    this.inner.onExit((info) => {
      this.entries.push({ t: this.now(), exit: info });
      handler(info);
    });
  }

  kill(): void {
    this.inner.kill();
  }

  close(): void {
    this.entries.push({ t: this.now(), close: true });
    if (this.inner.close !== undefined) {
      this.inner.close();
    } else {
      this.inner.kill();
    }
  }
}
