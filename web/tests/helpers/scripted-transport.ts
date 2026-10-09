// SPDX-License-Identifier: GPL-3.0-or-later
//
// An in-memory transport whose engine side the test plays by hand: it sees
// the lines the client wrote and sends whatever bytes it likes.

import type { ExitInfo, Transport } from '../../src/engine/transport.ts';

const encoder = new TextEncoder();

export class ScriptedTransport implements Transport {
  /** The request lines written, parsed. */
  readonly sent: Record<string, unknown>[] = [];
  killed = false;
  closedStdin = false;
  private dataHandler: ((chunk: Uint8Array) => void) | null = null;
  private stderrHandler: ((chunk: Uint8Array) => void) | null = null;
  private exitHandler: ((info: ExitInfo) => void) | null = null;
  private exited = false;

  write(data: Uint8Array): void {
    const text = new TextDecoder().decode(data);
    for (const line of text.split('\n').slice(0, -1)) {
      this.sent.push(JSON.parse(line) as Record<string, unknown>);
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
    this.killed = true;
    this.exit({ code: null, signal: 'SIGKILL' });
  }
  close(): void {
    this.closedStdin = true;
    this.exit({ code: 0, signal: null });
  }

  /** The engine writes bytes (or text) on stdout. */
  emit(data: Uint8Array | string): void {
    this.dataHandler?.(typeof data === 'string' ? encoder.encode(data) : data);
  }
  /** One JSON message as a line. */
  line(message: unknown): void {
    this.emit(`${JSON.stringify(message)}\n`);
  }
  /** A successful reply to the request with this id. */
  reply(id: number | null, result: unknown): void {
    this.line({ jsonrpc: '2.0', id, result });
  }
  /** An error reply. */
  error(id: number | null, code: number, errc: string, message: string): void {
    this.line({ jsonrpc: '2.0', id, error: { code, message, data: { errc } } });
  }
  event(method: string, params: unknown): void {
    this.line({ jsonrpc: '2.0', method, params });
  }
  /** A frame: its announcement and bytes. */
  frame(n: number, bytes: Uint8Array): void {
    const head = encoder.encode(
      `${JSON.stringify({ jsonrpc: '2.0', method: 'frame', params: { frame: n, bytes: bytes.length } })}\n`,
    );
    const all = new Uint8Array(head.length + bytes.length);
    all.set(head);
    all.set(bytes, head.length);
    this.emit(all);
  }
  stderr(text: string): void {
    this.stderrHandler?.(encoder.encode(text));
  }
  exit(info: ExitInfo): void {
    if (!this.exited) {
      this.exited = true;
      this.exitHandler?.(info);
    }
  }

  /** The last request written. */
  last(): Record<string, unknown> {
    const m = this.sent[this.sent.length - 1];
    if (m === undefined) {
      throw new Error('nothing was sent');
    }
    return m;
  }
  /** Answers engine.hello (the client's first request). */
  greet(protocol = 1): void {
    const hello = this.sent[0];
    if (hello?.method !== 'engine.hello') {
      throw new Error(`the first request is ${JSON.stringify(hello)}`);
    }
    this.reply(hello.id as number, { protocol, name: 'bt-engine', version: '0.1.0' });
  }
}

/** Lets pending promise reactions and deferred deliveries run. */
export async function settle(rounds = 3): Promise<void> {
  for (let i = 0; i < rounds; i++) {
    await new Promise<void>((resolve) => setImmediate(resolve));
  }
}
