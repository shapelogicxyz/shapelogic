// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser engine's module as Emscripten generates it beside this file
// (shapelogic_engine.js and .wasm, from engine/wasm_main.cpp by the wasm
// preset, copied here by tools/build-demo.ts; they are not committed).

/** The engine's exports (engine/wasm_main.cpp) and the memory the bytes cross in. */
export interface EngineModule {
  _sl_start(): void;
  _sl_write(data: number, length: number): void;
  _sl_poll(): void;
  _sl_take(): void;
  _sl_taken_data(): number;
  _sl_taken_size(): number;
  _sl_log_take(): void;
  _sl_log_data(): number;
  _sl_log_size(): number;
  _malloc(bytes: number): number;
  _free(pointer: number): void;
  /** The whole memory; a new array after the memory grew, so read afresh each time. */
  readonly HEAPU8: Uint8Array;
}

/** Loads the module (its .wasm from beside the script). */
export default function createEngine(options?: {
  locateFile?: (file: string) => string;
}): Promise<EngineModule>;
