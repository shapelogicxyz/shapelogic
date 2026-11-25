// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a failed engine start most likely means, in plain words (cleanup
// item 2.2): a table keyed by the system's error code, and one of the
// Windows exit codes of an engine that started and ended before its hello.
// The log line of a failed start (main) and the window's dialog (2.3, the
// renderer) use these. Platform-free.

import type { SpawnFailure } from './spawn-failure.ts';

/** The Windows status codes an engine may end with before its hello (NTSTATUS, unsigned). */
export const WINDOWS_EXIT_CODES: ReadonlyMap<number, string> = new Map([
  [0xc0000135, 'a library the engine needs is missing'],
  [0xc000007b, 'the engine file is damaged, or built for another processor'],
  [0xc0000142, 'a library the engine needs failed to start'],
  [0xc0000005, 'the engine crashed'],
  [0xc000001d, 'the engine used an instruction this processor lacks'],
  [0xc0000409, 'the engine stopped itself on a broken check'],
  [0xc0000022, 'Windows denied the engine access to something it needs'],
  [0xc0000428, "Windows could not verify the engine's signature (code integrity)"],
]);

/** An exit status as decimal and hex, and what it means on Windows when the table knows it. */
export function exitCodeText(code: number, platform: string): string {
  // Node gives Windows' NTSTATUS as an unsigned 32-bit number; a negative
  // one is the same status read as signed
  const unsigned = code < 0 ? code >>> 0 : code;
  const hex = `0x${unsigned.toString(16).toUpperCase().padStart(8, '0')}`;
  const meaning = platform === 'win32' ? WINDOWS_EXIT_CODES.get(unsigned) : undefined;
  return `exit code ${String(code)} (${hex})${meaning !== undefined ? `: ${meaning}` : ''}`;
}

/** The likely cause of a failed start, in plain words; null when the table has none. */
export function likelyCause(f: SpawnFailure, platform: string): string | null {
  const windows = platform === 'win32';
  switch (f.code) {
    case 'UNKNOWN':
      return windows
        ? 'Windows refused to start the engine. This is usually security software (an antivirus, ' +
            "Smart App Control or a company's application control) blocking a program that is not " +
            'signed, or a damaged engine file.'
        : 'the system refused to start the engine without saying why; the engine file may be damaged.';
    case 'ENOENT':
      return "the engine's file is missing: security software may have quarantined it, or the install is incomplete.";
    case 'NO_ENGINE':
      return 'the app has no engine file: security software may have quarantined it, or the install is incomplete.';
    case 'EACCES':
    case 'EPERM':
      return windows
        ? "Windows denied running the engine: security software or the file's permissions."
        : "the system denied running the engine: the file's permissions, or security software.";
    case 'EBUSY':
      return 'the engine file is locked, often by an antivirus scanning it; try again.';
    case 'TIMEOUT':
      return 'the engine started but did not answer within 10 seconds; security software may be holding it.';
    case 'VERSION':
      return 'another version of the engine is installed; reinstall ShapeLogic.';
    case 'EXITED': {
      if (f.exitCode === undefined || f.exitCode === null) {
        return f.signal !== undefined && f.signal !== null
          ? `the engine was ended by ${f.signal} before it answered.`
          : null;
      }
      const unsigned = f.exitCode < 0 ? f.exitCode >>> 0 : f.exitCode;
      const meaning = windows ? WINDOWS_EXIT_CODES.get(unsigned) : undefined;
      return meaning !== undefined ? `${meaning}.` : null;
    }
    default:
      return null;
  }
}

/**
 * The log's text of a failed start, e.g. "spawn UNKNOWN (errno -4094,
 * syscall spawn, the version check) at C:\…\shapelogic_engine.exe, 120 ms
 * after start. Likely cause: …".
 */
export function failureText(reason: string, f: SpawnFailure | undefined, platform: string): string {
  if (f === undefined) {
    return reason;
  }
  const details = [
    ...(f.errno !== undefined ? [`errno ${String(f.errno)}`] : []),
    ...(f.syscall !== undefined ? [`syscall ${f.syscall}`] : []),
    ...(f.exitCode !== undefined && f.exitCode !== null
      ? [exitCodeText(f.exitCode, platform)]
      : []),
    ...(f.signal !== undefined && f.signal !== null ? [`signal ${f.signal}`] : []),
    f.which === 'version' ? 'the version check' : 'the session',
  ];
  const cause = likelyCause(f, platform);
  return (
    `${f.message} (${details.join(', ')})` +
    (f.path !== undefined ? ` at ${f.path}` : '') +
    `, ${String(f.afterMs)} ms after start` +
    (reason !== f.message && !reason.includes(f.message) ? `; ${reason}` : '') +
    (cause !== null ? `. Likely cause: ${cause}` : '')
  );
}

/** The engine's last stderr text of a failure, on one line, or null. */
export function stderrLine(f: SpawnFailure | undefined): string | null {
  const text = f?.stderr?.trim();
  return text === undefined || text === '' ? null : text.replace(/\s*\n\s*/g, ' | ');
}
