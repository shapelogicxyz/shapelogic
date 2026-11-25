// SPDX-License-Identifier: GPL-3.0-or-later
//
// shapelogic_engine as a child process of Node (Electron's main process, the
// tests): its stdin, stdout and stderr as a Transport. A launch that fails
// keeps every detail the system gave (spawn-failure.ts): Node throws some
// spawn errors at once (Windows' `UNKNOWN`, `EPERM`) and reports others
// later (`ENOENT`, `EACCES`), so both are caught.

import { execFile, spawn } from 'node:child_process';
import type { ChildProcessWithoutNullStreams, ExecFileException } from 'node:child_process';

import { EngineStartError, systemFields } from './spawn-failure.ts';
import type { SpawnFailure } from './spawn-failure.ts';
import type { ExitInfo, Transport } from './transport.ts';

/** How long `--version` may take to answer. */
export const VERSION_TIMEOUT_MS = 10_000;

/** The SpawnFailure of a `--version` run's error. */
function versionFailure(
  enginePath: string,
  error: ExecFileException | Error,
  t0: number,
  stderr = '',
): SpawnFailure {
  const fields = systemFields(error);
  const e = error as ExecFileException;
  const afterMs = Date.now() - t0;
  if (e.killed === true && afterMs >= VERSION_TIMEOUT_MS - 50) {
    return { which: 'version', code: 'TIMEOUT', message: error.message, path: enginePath, afterMs };
  }
  // execFile's code is the exit status when the program ran and failed
  const exited = typeof (e as { code?: unknown }).code === 'number';
  return {
    which: 'version',
    code: exited ? 'EXITED' : (fields.code ?? 'UNKNOWN'),
    message: error.message,
    ...(fields.errno !== undefined ? { errno: fields.errno } : {}),
    ...(fields.syscall !== undefined ? { syscall: fields.syscall } : {}),
    path: fields.path ?? enginePath,
    afterMs,
    ...(exited ? { exitCode: (e as { code: number }).code, signal: e.signal ?? null } : {}),
    ...(stderr !== '' ? { stderr: stderr.slice(-2000) } : {}),
  };
}

export interface NodeTransportOptions {
  /** The engine's working directory (relative paths in requests resolve against it). */
  cwd?: string;
  /** Refuse an engine whose `--version` is not this. */
  expectVersion?: string;
}

/**
 * What `shapelogic_engine --version` says: "shapelogic_engine 0.1.0" gives
 * "0.1.0". Fails with an EngineStartError (which: 'version').
 */
export function engineVersion(enginePath: string, t0 = Date.now()): Promise<string> {
  return new Promise((resolve, reject) => {
    const fail = (error: Error, stderr = ''): void => {
      reject(
        new EngineStartError(
          `${enginePath} --version: ${error.message}`,
          versionFailure(enginePath, error, t0, stderr),
        ),
      );
    };
    try {
      execFile(
        enginePath,
        ['--version'],
        { timeout: VERSION_TIMEOUT_MS },
        (error, stdout, stderr) => {
          if (error !== null) {
            fail(error, stderr);
            return;
          }
          const m = /^shapelogic_engine (\S+)\s*$/.exec(stdout);
          if (m?.[1] === undefined) {
            const message = `${enginePath} --version said ${JSON.stringify(stdout.slice(0, 80))}, not "shapelogic_engine <version>"`;
            reject(
              new EngineStartError(message, {
                which: 'version',
                code: 'VERSION',
                message,
                path: enginePath,
                afterMs: Date.now() - t0,
              }),
            );
            return;
          }
          resolve(m[1]);
        },
      );
    } catch (e) {
      // spawn errors Node throws at once (Windows: UNKNOWN, EPERM)
      fail(e instanceof Error ? e : new Error(String(e)));
    }
  });
}

/** One shapelogic_engine process. */
export class NodeTransport implements Transport {
  /** The engine's version, as `--version` said (start() asks it). */
  readonly version: string | undefined;
  private readonly child: ChildProcessWithoutNullStreams;
  private dataHandler: ((chunk: Uint8Array) => void) | null = null;
  private stderrHandler: ((chunk: Uint8Array) => void) | null = null;
  private exitHandler: ((info: ExitInfo) => void) | null = null;
  // what came before a handler was attached
  private early: Uint8Array[] = [];
  private earlyStderr: Uint8Array[] = [];
  private exitInfo: ExitInfo | null = null;
  private spawnError: string | undefined;
  private spawnFields: ExitInfo['system'];

  /**
   * Starts the engine at once (start() checks its version first). A spawn
   * error the system gives at once throws an EngineStartError (which:
   * 'session'); one it gives later ends the transport with its fields in
   * ExitInfo.system.
   */
  constructor(
    enginePath: string,
    options: NodeTransportOptions = {},
    version?: string,
    t0 = Date.now(),
  ) {
    this.version = version;
    try {
      this.child = spawn(enginePath, [], {
        cwd: options.cwd,
        stdio: ['pipe', 'pipe', 'pipe'],
        windowsHide: true,
      });
    } catch (e) {
      const error = e instanceof Error ? e : new Error(String(e));
      const fields = systemFields(error);
      throw new EngineStartError(`${enginePath}: ${error.message}`, {
        which: 'session',
        code: fields.code ?? 'UNKNOWN',
        message: error.message,
        ...(fields.errno !== undefined ? { errno: fields.errno } : {}),
        ...(fields.syscall !== undefined ? { syscall: fields.syscall } : {}),
        path: fields.path ?? enginePath,
        afterMs: Date.now() - t0,
      });
    }
    this.child.stdout.on('data', (chunk: Buffer) => {
      if (this.dataHandler !== null) {
        this.dataHandler(chunk);
      } else {
        this.early.push(chunk);
      }
    });
    this.child.stderr.on('data', (chunk: Buffer) => {
      if (this.stderrHandler !== null) {
        this.stderrHandler(chunk);
      } else {
        this.earlyStderr.push(chunk);
      }
    });
    // a write after the engine ended fails here; its end is told by 'close'
    this.child.stdin.on('error', () => undefined);
    this.child.on('error', (e) => {
      this.spawnError = e.message;
      const fields = systemFields(e);
      this.spawnFields = Object.keys(fields).length > 0 ? fields : undefined;
      // a process that never started emits no 'close' on some platforms
      if (this.child.pid === undefined) {
        this.finish(this.withSpawnError({ code: null, signal: null }));
      }
    });
    // after 'exit' and after stdout and stderr are through
    this.child.on('close', (code, signal) => {
      this.finish(this.withSpawnError({ code, signal }));
    });
  }

  /** Starts an engine after checking that `--version` answers (and, if asked, with that version). */
  static async start(
    enginePath: string,
    options: NodeTransportOptions = {},
  ): Promise<NodeTransport> {
    const t0 = Date.now();
    const version = await engineVersion(enginePath, t0);
    if (options.expectVersion !== undefined && version !== options.expectVersion) {
      const message = `${enginePath} is version ${version}; this front end needs ${options.expectVersion}`;
      throw new EngineStartError(message, {
        which: 'version',
        code: 'VERSION',
        message,
        path: enginePath,
        afterMs: Date.now() - t0,
      });
    }
    return new NodeTransport(enginePath, options, version, t0);
  }

  /** The process id, while it runs. */
  get pid(): number | undefined {
    return this.child.pid;
  }

  write(data: Uint8Array): void {
    if (this.exitInfo === null && this.child.stdin.writable) {
      this.child.stdin.write(data);
    }
  }

  onData(handler: (chunk: Uint8Array) => void): void {
    this.dataHandler = handler;
    for (const c of this.early.splice(0)) {
      handler(c);
    }
  }

  onStderr(handler: (chunk: Uint8Array) => void): void {
    this.stderrHandler = handler;
    for (const c of this.earlyStderr.splice(0)) {
      handler(c);
    }
  }

  onExit(handler: (info: ExitInfo) => void): void {
    this.exitHandler = handler;
    if (this.exitInfo !== null) {
      handler(this.exitInfo);
    }
  }

  kill(): void {
    if (this.exitInfo === null) {
      this.child.kill('SIGKILL');
    }
  }

  close(): void {
    this.child.stdin.end();
  }

  private withSpawnError(info: ExitInfo): ExitInfo {
    return {
      ...info,
      ...(this.spawnError !== undefined ? { error: this.spawnError } : {}),
      ...(this.spawnFields !== undefined ? { system: this.spawnFields } : {}),
    };
  }

  private finish(info: ExitInfo): void {
    if (this.exitInfo !== null) {
      return;
    }
    this.exitInfo = info;
    this.exitHandler?.(info);
  }
}
