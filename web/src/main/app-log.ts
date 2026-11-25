// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's log file (View ▸ Log File Manager…): a line for each thing that
// helps to understand afterwards what happened, written by the main
// process for every window. The engines tell most of it (their `log`
// setting, engine/app_log.h): a file opened or saved, or not and why; a
// solve started (its options), paused, continued, aborted and how its runs
// ended; what the sliding searches told (a saved search carried on and how
// deep, one that could not be and why, a stop at the disk budget); every
// call that failed. Their lines come on stderr (`shapelogic_engine: LEVEL:
// Event: text`) and are split here (`EngineLines`); a line the engine
// wrote without a level (an assertion, a crash) is an error, its event
// `Engine Error`; an engine's last words without their newline are taken
// when it ends (teeStderr). The main process adds its own: the start, an
// engine that stopped, its protocol problems (logEngineProblems), a page
// that crashed or hung (renderer-watch.ts), settings that could not be
// stored.
//
// Each line is `YYYY-MM-DD HH:MM:SS.mmm LEVEL [source] Event: text`, local
// time, on one line: the event what happened in a few capitalised words
// (`File Opened`, `Solve Paused`), which the manager's Pretty view shows in
// a column of its own. The file is `shapelogic.log` in the app's data folder; once
// it would pass 20 MB (100 MB while Debug logging is on, Settings ▸
// General: the engines' DEBUG lines then too) it is renamed
// `shapelogic.log.1` and compressed to `shapelogic.log.1.gz` (replacing the
// one before, so there is at most one) while a new file is begun. Turning
// Debug logging off brings the log back to its size: the compressed file
// goes, the newest 100 lines stay in the file, and the 20 MB before them
// are compressed in its place; the rest is deleted. The limits are not
// strict: a line written while a file is compressed goes to the new one. The manager reads the current file newest
// first, a page at a time, filtered by level and text; Export writes all
// of it (the compressed file's lines first), or only its newest 100 lines,
// as plain text, and Clear
// deletes both files. Free of Electron (the unit tests use a temporary
// folder).

import {
  appendFileSync,
  closeSync,
  existsSync,
  mkdirSync,
  openSync,
  readSync,
  renameSync,
  rmSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import { createReadStream, createWriteStream } from 'node:fs';
import { statfsSync } from 'node:fs';
import { readFile, rename, rm } from 'node:fs/promises';
import { arch, cpus, freemem, release, totalmem, type } from 'node:os';
import { dirname, join } from 'node:path';
import { pipeline } from 'node:stream/promises';
import { promisify } from 'node:util';
import { createGzip, gunzip } from 'node:zlib';

import type {
  LogEntry,
  LogExportSizes,
  LogLevel,
  LogPage,
  LogQuery,
} from '../engine/app-bridge.ts';
import type { ClientProblem } from '../engine/client.ts';
import { setOnce } from '../engine/transport.ts';
import type { Transport } from '../engine/transport.ts';
import { writeFileSafely } from './export-file.ts';
import { replaceFileSync } from './replace-file.ts';

/** The size at which the log is compressed and a new one begun: 20 MB. */
export const LOG_MAX_BYTES = 20_000_000;
/** The same while Debug logging is on: 100 MB. */
export const LOG_DEBUG_MAX_BYTES = 100_000_000;
/** The lines the file keeps when Debug logging is turned off. */
export const LOG_KEEP_LINES = 100;
/** The bytes before them compressed then (the rest deleted): 20 MB. */
export const LOG_KEEP_ARCHIVE_BYTES = 20_000_000;
/** The file's name in its folder. */
export const LOG_NAME = 'shapelogic.log';
/** The lines a page of the manager holds. */
export const LOG_PAGE_LINES = 100;
/** The newest lines Export… offers on their own (Last 100 lines). */
export const LOG_EXPORT_TAIL = 100;

const gunzipAsync = promisify(gunzip);

const pad = (n: number, w = 2): string => String(n).padStart(w, '0');

/** A time as the log writes it: `2026-11-16 14:39:55.123`, local. */
export function logTime(d: Date): string {
  return (
    `${String(d.getFullYear())}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ` +
    `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`
  );
}

/** One line of the log: its time, level, where it came from, the event, and the text on one line. */
export function formatLogLine(
  time: Date,
  level: LogLevel,
  source: string,
  event: string,
  text: string,
): string {
  const flat = text.replace(/[\r\n]+/g, ' ').trimEnd();
  return `${logTime(time)} ${level.padEnd(5)} [${source}] ${event}${flat === '' ? '' : `: ${flat}`}\n`;
}

/** An event's name as the log writes it: capitalised words, letters, digits and spaces. */
export const EVENT_NAME = /^[A-Z][A-Za-z0-9]*(?: [A-Za-z0-9]+)*$/;

const LINE = /^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} (DEBUG|INFO|WARN|ERROR) /;

/** The level of a line of the log file (INFO for one it cannot read). */
export function levelOf(line: string): LogLevel {
  const m = LINE.exec(line);
  return (m?.[1] as LogLevel | undefined) ?? 'INFO';
}

const ENGINE = /^shapelogic_engine: (DEBUG|INFO|WARN|ERROR): (.*)$/;

/** A line of the engine's, as the log takes it. */
export interface EngineLine {
  level: LogLevel;
  /** What happened (`Solve Started`); `Engine` for a line that names none. */
  event: string;
  text: string;
}

/**
 * A line the engine wrote on stderr, as the log takes it: its level, event
 * and text; a line without a level is an error, `Engine Error` (it says
 * what went wrong: an assertion, a crash); none for an empty line.
 */
export function parseEngineLine(line: string): EngineLine | null {
  const t = line.trimEnd();
  if (t === '') {
    return null;
  }
  const m = ENGINE.exec(t);
  if (m === null) {
    return {
      level: 'ERROR',
      event: 'Engine Error',
      text: t.startsWith('shapelogic_engine: ') ? t.slice(19) : t,
    };
  }
  const rest = m[2] ?? '';
  const colon = rest.indexOf(': ');
  const event = colon < 0 ? rest : rest.slice(0, colon);
  return EVENT_NAME.test(event)
    ? { level: m[1] as LogLevel, event, text: colon < 0 ? '' : rest.slice(colon + 2) }
    : { level: m[1] as LogLevel, event: 'Engine', text: rest };
}

/** Splits the engine's stderr chunks into lines (a line may span chunks). */
export class EngineLines {
  private carry = '';
  private readonly decoder = new TextDecoder('utf-8');
  private readonly onLine: (line: string) => void;

  constructor(onLine: (line: string) => void) {
    this.onLine = onLine;
  }

  push(chunk: Uint8Array): void {
    const text = this.carry + this.decoder.decode(chunk, { stream: true });
    const lines = text.split('\n');
    this.carry = lines.pop() ?? '';
    for (const l of lines) {
      this.onLine(l);
    }
  }

  /** The last line, when the engine ended without its newline. */
  end(): void {
    if (this.carry !== '') {
      this.onLine(this.carry);
      this.carry = '';
    }
  }
}

/**
 * The transport with its stderr also handed to `onChunk` (the log), before
 * the client's handler (which keeps the tail for an engine that dies), and
 * `onEnd` called when the engine ended, before the client hears it (the
 * log's last line, EngineLines.end: a crash's words without a newline).
 */
export function teeStderr(
  t: Transport,
  onChunk: (chunk: Uint8Array) => void,
  onEnd: () => void = () => undefined,
): Transport {
  let handler: ((chunk: Uint8Array) => void) | null = null;
  t.onStderr?.((chunk) => {
    onChunk(chunk);
    handler?.(chunk);
  });
  return {
    write: (data) => {
      t.write(data);
    },
    onData: (h) => {
      t.onData(h);
    },
    onStderr: (h) => {
      handler = setOnce(handler, h, 'onStderr');
    },
    onExit: (h) => {
      t.onExit((info) => {
        onEnd();
        h(info);
      });
    },
    kill: () => {
      t.kill();
    },
    close: () => {
      t.close?.();
    },
  };
}

/** What logEngineProblems needs of the log. */
type ProblemLog = Pick<AppLog, 'warn'>;

/**
 * Logs each protocol problem of a host's engines at warn level, with its
 * kind and detail (a bad frame, which ends the engine; a lost reply; a line
 * too long; a handler that threw): without it they reached only the
 * console, which a packaged app does not show (cleanup item 7.2).
 */
export function logEngineProblems(
  host: { onProblem(handler: (p: ClientProblem) => void): () => void },
  log: ProblemLog,
  source: string,
): () => void {
  return host.onProblem((p) => {
    log.warn(source, 'Engine Problem', `${p.reason}: ${p.detail}`);
  });
}

/** The current file's lines, read once and then only what was added. */
interface LineCache {
  bytes: number;
  lines: string[];
  /** A line cut short at the end of what was read. */
  partial: string;
}

export interface AppLogOptions {
  /** The size at which the file is compressed and a new one begun (default 20 MB). */
  maxBytes?: number;
  /** The same while Debug logging is on (default 100 MB). */
  debugMaxBytes?: number;
  /** The bytes kept, compressed, when Debug logging is turned off (default 20 MB). */
  keepArchiveBytes?: number;
  /** Debug logging is on from the start (the stored setting). */
  debug?: boolean;
  /** The clock (the tests set it). */
  now?: () => Date;
}

/** The app's log file, its compressed predecessor, and their manager's reads. */
export class AppLog {
  /** The file being written. */
  readonly file: string;
  /** The file before it, compressed (at most one). */
  readonly archive: string;
  private readonly rotated: string;
  private readonly maxBytes: number;
  private readonly debugMaxBytes: number;
  private readonly keepArchiveBytes: number;
  private debug: boolean;
  private readonly now: () => Date;
  private bytes: number;
  private cache: LineCache | null = null;
  /** The compression under way, when there is one. */
  private compressing: Promise<void> | null = null;
  /** Writing failed (the folder gone, the disk full): the error, kept to tell once. */
  private failure: string | null = null;

  constructor(dir: string, options: AppLogOptions = {}) {
    this.file = join(dir, LOG_NAME);
    this.rotated = `${this.file}.1`;
    this.archive = `${this.rotated}.gz`;
    this.maxBytes = options.maxBytes ?? LOG_MAX_BYTES;
    this.debugMaxBytes = options.debugMaxBytes ?? LOG_DEBUG_MAX_BYTES;
    this.keepArchiveBytes = options.keepArchiveBytes ?? LOG_KEEP_ARCHIVE_BYTES;
    this.debug = options.debug ?? false;
    this.now = options.now ?? (() => new Date());
    try {
      mkdirSync(dir, { recursive: true });
    } catch {
      // writes fail and say so
    }
    this.bytes = sizeOf(this.file);
    // a compression a quit cut short
    if (existsSync(this.rotated)) {
      this.compressing = this.compress();
    }
  }

  /** Waits for the compressions under way (the tests, Export, Debug logging turned off). */
  async settled(): Promise<void> {
    while (this.compressing !== null) {
      await this.compressing;
    }
  }

  /** Debug logging is on: the file grows to the larger size. */
  get debugging(): boolean {
    return this.debug;
  }

  /** The size past which the file is compressed and a new one begun. */
  get limit(): number {
    return this.debug ? this.debugMaxBytes : this.maxBytes;
  }

  /**
   * Debug logging on or off. Off brings the log back to its size: the
   * compressed file goes, the newest LOG_KEEP_LINES lines stay in the
   * file, and the keepArchiveBytes before them are compressed in its place.
   * The larger size holds until then, so no line written meanwhile starts
   * a compression of the whole file.
   */
  async setDebug(on: boolean): Promise<void> {
    if (on || !this.debug) {
      this.debug = on;
      return;
    }
    await this.settled();
    try {
      this.trim();
    } catch (e) {
      this.failure = e instanceof Error ? e.message : String(e);
    }
    this.debug = false;
  }

  /** Keeps the newest lines and the bytes before them (setDebug's off). */
  private trim(): void {
    rmSync(this.archive, { force: true });
    rmSync(this.rotated, { force: true });
    const size = sizeOf(this.file);
    if (size === 0) {
      return;
    }
    // what is read: the bytes kept and room for the lines (a few hundred bytes each)
    const len = Math.min(size, this.keepArchiveBytes + 4_000_000);
    const start = size - len;
    const buf = Buffer.alloc(len);
    const fd = openSync(this.file, 'r');
    try {
      readSync(fd, buf, 0, len, start);
    } finally {
      closeSync(fd);
    }
    // where the newest lines begin: after the newline before them
    let lines = 0;
    let keepFrom = 0;
    for (let i = buf.length - 2; i >= 0; i--) {
      if (buf[i] === 10 && ++lines === LOG_KEEP_LINES) {
        keepFrom = i + 1;
        break;
      }
    }
    // the bytes before them, from the start of a line
    let from = Math.max(0, keepFrom - this.keepArchiveBytes);
    if (start + from > 0 && buf[from - 1] !== 10) {
      const nl = buf.indexOf(10, from);
      from = nl < 0 || nl >= keepFrom ? keepFrom : nl + 1;
    }
    const kept = buf.subarray(keepFrom);
    // synchronously: a line appended between the read and the rename would be lost
    replaceFileSync(this.file, kept);
    this.bytes = kept.length;
    this.cache = null;
    if (from < keepFrom) {
      writeFileSync(this.rotated, buf.subarray(from, keepFrom));
      this.compressing = this.compress();
    }
  }

  /** Why writing failed, when it did (the log goes on trying). */
  get writeError(): string | null {
    return this.failure;
  }

  info(source: string, event: string, text: string): void {
    this.write('INFO', source, event, text);
  }

  warn(source: string, event: string, text: string): void {
    this.write('WARN', source, event, text);
  }

  error(source: string, event: string, text: string): void {
    this.write('ERROR', source, event, text);
  }

  /**
   * Appends a line, `event` what happened in a few capitalised words
   * (EVENT_NAME); once the file would pass the limit, it is compressed and
   * a new one begun.
   */
  write(level: LogLevel, source: string, event: string, text: string): void {
    const line = formatLogLine(this.now(), level, source, event, text);
    const size = Buffer.byteLength(line);
    if (this.bytes > 0 && this.bytes + size > this.limit) {
      this.rotate();
    }
    try {
      appendFileSync(this.file, line);
      this.bytes += size;
      this.failure = null;
    } catch (e) {
      this.failure = e instanceof Error ? e.message : String(e);
    }
  }

  /** Renames the file (a new one is begun at once) and compresses it after. */
  private rotate(): void {
    if (this.compressing !== null) {
      // the one before is still being compressed: this one waits behind it
      return;
    }
    try {
      renameSync(this.file, this.rotated);
    } catch {
      return;
    }
    this.bytes = 0;
    this.cache = null;
    this.compressing = this.compress();
  }

  private async compress(): Promise<void> {
    const tmp = `${this.archive}.tmp`;
    try {
      await pipeline(createReadStream(this.rotated), createGzip(), createWriteStream(tmp));
      // the compressed file before goes: there is only ever one
      await rename(tmp, this.archive);
      await rm(this.rotated, { force: true });
    } catch {
      await rm(tmp, { force: true }).catch(() => undefined);
    } finally {
      this.compressing = null;
    }
  }

  /** The current file's lines, oldest first. */
  private lines(): string[] {
    const now = sizeOf(this.file);
    if (this.cache === null || now < this.cache.bytes) {
      this.cache = { bytes: 0, lines: [], partial: '' };
    }
    const c = this.cache;
    if (now > c.bytes) {
      const fd = openSync(this.file, 'r');
      try {
        const buf = Buffer.alloc(now - c.bytes);
        const got = readSync(fd, buf, 0, buf.length, c.bytes);
        const text = c.partial + buf.subarray(0, got).toString('utf8');
        const parts = text.split('\n');
        c.partial = parts.pop() ?? '';
        for (const p of parts) {
          if (p !== '') {
            c.lines.push(p);
          }
        }
        c.bytes += got;
      } finally {
        closeSync(fd);
      }
    }
    return c.lines;
  }

  /**
   * A page of the current file, newest first: the lines that match (the
   * errors and warnings only, and with the text, any case), from `offset` matches in.
   */
  page(query: LogQuery): LogPage {
    let all: string[];
    try {
      all = existsSync(this.file) ? this.lines() : [];
    } catch (e) {
      return {
        entries: [],
        offset: 0,
        matches: 0,
        lines: 0,
        more: false,
        bytes: 0,
        archived: existsSync(this.archive),
        file: this.file,
        debug: this.debug,
        limit: this.limit,
        error: e instanceof Error ? e.message : String(e),
      };
    }
    const needle = query.search.trim().toLowerCase();
    const limit = Math.max(1, Math.min(query.limit ?? LOG_PAGE_LINES, 1000));
    const offset = Math.max(0, query.offset);
    const entries: LogEntry[] = [];
    let matches = 0;
    for (let i = all.length - 1; i >= 0; i--) {
      const line = all[i] ?? '';
      const level = levelOf(line);
      if (query.errorsOnly && level !== 'ERROR' && level !== 'WARN') {
        continue;
      }
      if (needle !== '' && !line.toLowerCase().includes(needle)) {
        continue;
      }
      if (matches >= offset && entries.length < limit) {
        entries.push({ line, level });
      }
      matches++;
    }
    return {
      entries,
      offset,
      matches,
      lines: all.length,
      more: offset + entries.length < matches,
      bytes: sizeOf(this.file),
      archived: existsSync(this.archive) || existsSync(this.rotated),
      file: this.file,
      debug: this.debug,
      limit: this.limit,
      error: this.failure,
    };
  }

  /** Deletes the log and its compressed predecessor. */
  async clear(): Promise<void> {
    await this.settled();
    await rm(this.file, { force: true });
    await rm(this.archive, { force: true });
    await rm(this.rotated, { force: true });
    this.bytes = 0;
    this.cache = null;
  }

  /** All of the log as plain text, oldest first: the compressed file's lines, then the current file's. */
  async text(): Promise<Buffer> {
    await this.settled();
    const parts: Buffer[] = [];
    if (existsSync(this.archive)) {
      parts.push(await gunzipAsync(await readFile(this.archive)));
    }
    if (existsSync(this.file)) {
      parts.push(await readFile(this.file));
    }
    return Buffer.concat(parts);
  }

  /**
   * What Export… offers (Last 100 lines, All lines): the lines and bytes of
   * all of the log, the compressed file's too, and of its newest `tail` lines.
   */
  async exportSizes(tail = LOG_EXPORT_TAIL): Promise<LogExportSizes> {
    const all = await this.text();
    const last = tailOf(all, tail);
    return {
      lines: lineCount(all),
      bytes: all.length,
      tailLines: lineCount(last),
      tailBytes: last.length,
    };
  }

  /**
   * Writes the log to `path` as plain text (export-file.ts's safe write):
   * all of it, or only its newest `lines` lines.
   */
  async exportTo(path: string, lines?: number): Promise<void> {
    const all = await this.text();
    await writeFileSafely(path, lines === undefined ? all : tailOf(all, lines));
  }
}

/** The lines of a text: its newlines, and one more for a last line without one. */
export function lineCount(text: Buffer): number {
  let n = 0;
  for (let i = text.indexOf(10); i >= 0; i = text.indexOf(10, i + 1)) {
    n++;
  }
  return text.length > 0 && text[text.length - 1] !== 10 ? n + 1 : n;
}

/** The newest `lines` lines of a text (all of it when it has no more). */
export function tailOf(text: Buffer, lines: number): Buffer {
  if (lines <= 0) {
    return text.subarray(text.length);
  }
  // the newline that ends the last line is not one between lines
  let at = text.length > 0 && text[text.length - 1] === 10 ? text.length - 1 : text.length;
  for (let n = 0; n < lines; n++) {
    const nl = at > 0 ? text.lastIndexOf(10, at - 1) : -1;
    if (nl < 0) {
      return text;
    }
    at = nl;
  }
  return text.subarray(at + 1);
}

/**
 * The machine, for the log when Debug logging is turned on: the system, its
 * cores and memory, and the space free where each folder is (the nearest
 * folder above one that is not made yet).
 */
export function machineText(folders: readonly string[]): string {
  const gb = (n: number): string => `${(n / 1e9).toFixed(1)} GB`;
  const disks = folders
    .filter((f) => f !== '')
    .map((f) => {
      let at = f;
      for (;;) {
        try {
          const s = statfsSync(at);
          return `${gb(s.bavail * s.bsize)} free for ${f}`;
        } catch {
          const up = dirname(at);
          if (up === at) {
            return `no free space known for ${f}`;
          }
          at = up;
        }
      }
    });
  const c = cpus();
  return (
    `${type()} ${release()} ${arch()}, ${String(c.length)} cores (${c[0]?.model ?? 'unknown'}), ` +
    `${gb(totalmem())} memory (${gb(freemem())} free)` +
    (disks.length > 0 ? `; ${disks.join(', ')}` : '')
  );
}

function sizeOf(path: string): number {
  try {
    return statSync(path).size;
  } catch {
    return 0;
  }
}
