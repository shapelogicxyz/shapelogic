// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser engine (engine/wasm_main.cpp) as a Transport: each line the
// client writes is handed to the engine, whose output is taken after it
// and delivered as stdout would be (a task later, in order); a timer polls
// the engine for its events, as the stdio engine's run loop does between
// lines. Logs are the console's.

import type { ExitInfo, Transport } from '../engine/transport.ts';
import type { EngineModule } from './engine/shapelogic_engine.js';

const NEWLINE = 0x0a;
const POLL_MS = 250;

/** The engine in this page's WebAssembly module. */
export class WasmTransport implements Transport {
  private readonly m: EngineModule;
  private pending: Uint8Array = new Uint8Array(0);
  private readonly outbox: Uint8Array[] = [];
  private draining = false;
  private dataHandler: ((chunk: Uint8Array) => void) | null = null;
  private stderrHandler: ((chunk: Uint8Array) => void) | null = null;
  private exitHandler: ((info: ExitInfo) => void) | null = null;
  private timer: ReturnType<typeof setInterval> | null = null;
  private ended = false;

  constructor(m: EngineModule) {
    this.m = m;
    m._bt_start();
    this.timer = setInterval(() => {
      this.m._bt_poll();
      this.take();
    }, POLL_MS);
  }

  write(data: Uint8Array): void {
    if (this.ended) {
      return;
    }
    const all = new Uint8Array(this.pending.length + data.length);
    all.set(this.pending);
    all.set(data, this.pending.length);
    let from = 0;
    for (let i = 0; i < all.length; i++) {
      if (all[i] === NEWLINE) {
        this.line(all.subarray(from, i));
        from = i + 1;
      }
    }
    this.pending = all.slice(from);
    this.take();
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
    this.end({ code: 0, signal: null });
  }

  private line(bytes: Uint8Array): void {
    const m = this.m;
    const p = m._malloc(Math.max(1, bytes.length));
    try {
      m.HEAPU8.set(bytes, p);
      m._bt_write(p, bytes.length);
    } finally {
      m._free(p);
    }
  }

  // the engine's output and logs since the last take, delivered a task later
  private take(): void {
    const m = this.m;
    m._bt_take();
    const n = m._bt_taken_size();
    if (n > 0) {
      this.outbox.push(m.HEAPU8.slice(m._bt_taken_data(), m._bt_taken_data() + n));
    }
    m._bt_log_take();
    const l = m._bt_log_size();
    if (l > 0) {
      const log = m.HEAPU8.slice(m._bt_log_data(), m._bt_log_data() + l);
      this.stderrHandler?.(log);
      console.warn(`shapelogic_engine: ${new TextDecoder().decode(log)}`);
    }
    if (this.outbox.length > 0 && !this.draining) {
      this.draining = true;
      queueMicrotask(() => {
        this.draining = false;
        while (this.outbox.length > 0 && !this.ended) {
          const chunk = this.outbox.shift();
          if (chunk !== undefined) {
            this.dataHandler?.(chunk);
          }
        }
      });
    }
  }

  private end(info: ExitInfo): void {
    if (this.ended) {
      return;
    }
    this.ended = true;
    if (this.timer !== null) {
      clearInterval(this.timer);
      this.timer = null;
    }
    this.outbox.length = 0;
    this.exitHandler?.(info);
  }
}
