// SPDX-License-Identifier: GPL-3.0-or-later
//
// Why an engine could not be started, with every detail the system gave
// (cleanup item 2.2): Node's spawn errors carry a code (`UNKNOWN`, `ENOENT`,
// `EACCES`, `EPERM`, `EBUSY`), an errno (`UNKNOWN` is -4094 on Windows, where
// security software refused CreateProcess), the system call and the path; an
// engine that started and ended before its hello leaves an exit code. The
// start is made twice (`--version`, then the session), so the record says
// which of the two failed. Platform-free: it crosses to the renderer in the
// engine's failed status (client-types.ts), and main's start-diagnosis.ts
// turns it into words.

/** Why an engine could not be started. */
export interface SpawnFailure {
  /** Which launch failed: the `--version` check, or the session's engine. */
  which: 'version' | 'session';
  /**
   * The system's error code (`UNKNOWN`, `ENOENT`, `EACCES`, `EPERM`, `EBUSY`,
   * ...), `TIMEOUT` for a version check that did not answer, `VERSION` for an
   * engine of another version, `NO_ENGINE` for an app without one, `EXITED`
   * for an engine that ended before its hello.
   */
  code: string;
  /** The error's text as Node gave it. */
  message: string;
  /** The errno, when the system gave one. */
  errno?: number;
  /** The system call that failed (`spawn`). */
  syscall?: string;
  /** The engine's path. */
  path?: string;
  /** Milliseconds from the start of the launch to the failure. */
  afterMs: number;
  /** An engine that ended before its hello: its exit status (Windows: unsigned 32-bit). */
  exitCode?: number | null;
  /** The signal that ended it, if one did. */
  signal?: string | null;
  /** Its last words on stderr. */
  stderr?: string;
}

/** An error that carries a SpawnFailure (NodeTransport.start throws them). */
export class EngineStartError extends Error {
  readonly failure: SpawnFailure;

  constructor(message: string, failure: SpawnFailure) {
    super(message);
    this.name = 'EngineStartError';
    this.failure = failure;
  }
}

/** The SpawnFailure an error carries, if it is an EngineStartError (or looks like one across a realm). */
export function failureOf(e: unknown): SpawnFailure | undefined {
  if (e instanceof EngineStartError) {
    return e.failure;
  }
  if (typeof e === 'object' && e !== null && 'failure' in e) {
    const f = e.failure;
    if (typeof f === 'object' && f !== null && 'code' in f && 'which' in f) {
      return f as SpawnFailure;
    }
  }
  return undefined;
}

/** The system fields of a Node error (`code`, `errno`, `syscall`, `path`), those it has. */
export function systemFields(
  e: unknown,
): Pick<SpawnFailure, 'errno' | 'syscall' | 'path'> & { code?: string } {
  if (typeof e !== 'object' || e === null) {
    return {};
  }
  const r = e as Record<string, unknown>;
  return {
    ...(typeof r.code === 'string' ? { code: r.code } : {}),
    ...(typeof r.errno === 'number' ? { errno: r.errno } : {}),
    ...(typeof r.syscall === 'string' ? { syscall: r.syscall } : {}),
    ...(typeof r.path === 'string' ? { path: r.path } : {}),
  };
}
