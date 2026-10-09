// SPDX-License-Identifier: GPL-3.0-or-later
//
// bt-engine as a child process of Node (Electron's main process, the
// tests): its stdin, stdout and stderr as a Transport.

import { execFile, spawn } from 'node:child_process';
import type { ChildProcessWithoutNullStreams } from 'node:child_process';

import type { ExitInfo, Transport } from './transport.ts';

export interface NodeTransportOptions {
  /** The engine's working directory (relative paths in requests resolve against it). */
  cwd?: string;
  /** Refuse an engine whose `--version` is not this. */
  expectVersion?: string;
}

/** What `bt-engine --version` says: "bt-engine 0.1.0" gives "0.1.0". */
export function engineVersion(enginePath: string): Promise<string> {
  return new Promise((resolve, reject) => {
    execFile(enginePath, ['--version'], { timeout: 10_000 }, (error, stdout) => {
      if (error !== null) {
        reject(new Error(`${enginePath} --version: ${error.message}`));
        return;
      }
      const m = /^bt-engine (\S+)\s*$/.exec(stdout);
      if (m?.[1] === undefined) {
        reject(
          new Error(
            `${enginePath} --version said ${JSON.stringify(stdout.slice(0, 80))}, not "bt-engine <version>"`,
          ),
        );
        return;
      }
      resolve(m[1]);
    });
  });
}

/** One bt-engine process. */
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

  /** Starts the engine at once (start() checks its version first). */
  constructor(enginePath: string, options: NodeTransportOptions = {}, version?: string) {
    this.version = version;
    this.child = spawn(enginePath, [], {
      cwd: options.cwd,
      stdio: ['pipe', 'pipe', 'pipe'],
      windowsHide: true,
    });
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
      // a process that never started emits no 'close' on some platforms
      if (this.child.pid === undefined) {
        this.finish({ code: null, signal: null, error: e.message });
      }
    });
    // after 'exit' and after stdout and stderr are through
    this.child.on('close', (code, signal) => {
      this.finish({
        code,
        signal,
        ...(this.spawnError !== undefined ? { error: this.spawnError } : {}),
      });
    });
  }

  /** Starts an engine after checking that `--version` answers (and, if asked, with that version). */
  static async start(
    enginePath: string,
    options: NodeTransportOptions = {},
  ): Promise<NodeTransport> {
    const version = await engineVersion(enginePath);
    if (options.expectVersion !== undefined && version !== options.expectVersion) {
      throw new Error(
        `${enginePath} is version ${version}; this front end needs ${options.expectVersion}`,
      );
    }
    return new NodeTransport(enginePath, options, version);
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

  private finish(info: ExitInfo): void {
    if (this.exitInfo !== null) {
      return;
    }
    this.exitInfo = info;
    this.exitHandler?.(info);
  }
}
