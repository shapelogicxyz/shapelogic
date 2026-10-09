// SPDX-License-Identifier: GPL-3.0-or-later
//
// The byte pipe the client speaks over: a child process in Node
// (node-transport.ts), a recording (fake-engine/), later a WebSocket.

/** How the engine ended. */
export interface ExitInfo {
  /** The exit status, or null when a signal ended it. */
  code: number | null;
  /** The signal that ended it, or null. */
  signal: string | null;
  /** Why it could not be started or talked to, when that is how it ended. */
  error?: string;
}

/**
 * One bt-engine's stdio. The client attaches its handlers right after the
 * transport is made, before any data can arrive (the engine never speaks
 * first). `onExit` is called once, after the last stdout chunk.
 */
export interface Transport {
  /** Sends bytes to the engine's stdin; dropped once it has ended. */
  write(data: Uint8Array): void;
  /** The engine's stdout, chunk by chunk, in order. */
  onData(handler: (chunk: Uint8Array) => void): void;
  /** The engine's stderr (its logs), when the transport has one. */
  onStderr?(handler: (chunk: Uint8Array) => void): void;
  /** The engine ended and its stdout is through. */
  onExit(handler: (info: ExitInfo) => void): void;
  /** Ends the engine at once. */
  kill(): void;
  /** Closes the engine's stdin: it pauses its solves and ends by itself. */
  close?(): void;
}
