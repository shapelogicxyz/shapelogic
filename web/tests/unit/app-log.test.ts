// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's log file (src/main/app-log.ts): its lines (time, level,
// source, one line each), the engine's stderr split into lines and given
// their level, the file compressed once it would pass its size with only
// one compressed file kept, the manager's pages newest first with the
// errors-only and search filters, Export of both files as plain text (or
// of the newest lines alone) with the sizes it offers, and Clear, in a
// temporary directory.

import {
  existsSync,
  lstatSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  rmSync,
  symlinkSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { gunzipSync } from 'node:zlib';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import {
  AppLog,
  EngineLines,
  formatLogLine,
  levelOf,
  lineCount,
  logTime,
  parseEngineLine,
  tailOf,
  teeStderr,
} from '../../src/main/app-log.ts';
import type { Transport } from '../../src/engine/transport.ts';

let dir: string;
const at = new Date(2026, 10, 16, 14, 39, 55, 7);

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), 'bt-applog-'));
});

afterEach(() => {
  rmSync(dir, { recursive: true, force: true });
});

const query = { offset: 0, errorsOnly: false, search: '' };

describe('the log lines', () => {
  it('have the local time, the level, the source, the event and one line of text', () => {
    expect(logTime(at)).toBe('2026-11-16 14:39:55.007');
    expect(formatLogLine(at, 'WARN', 'window 2', 'Disk Full', 'disk\nfull\r\n')).toBe(
      '2026-11-16 14:39:55.007 WARN  [window 2] Disk Full: disk full\n',
    );
    expect(formatLogLine(at, 'INFO', 'app', 'Debug Logging Off', '')).toBe(
      '2026-11-16 14:39:55.007 INFO  [app] Debug Logging Off\n',
    );
    expect(levelOf('2026-11-16 14:39:55.007 ERROR [app] x')).toBe('ERROR');
    expect(levelOf('2026-11-16 14:39:55.007 WARN  [app] x')).toBe('WARN');
    expect(levelOf('2026-11-16 14:39:55.007 DEBUG [app] x')).toBe('DEBUG');
    expect(levelOf('not a line of the log')).toBe('INFO');
  });

  it('take the engine level and event, and a line without a level as an error', () => {
    expect(parseEngineLine('shapelogic_engine: INFO: File Opened: "a: b.xmpuzzle"')).toEqual({
      level: 'INFO',
      event: 'File Opened',
      text: '"a: b.xmpuzzle"',
    });
    expect(
      parseEngineLine('shapelogic_engine: ERROR: File Open Failed: x (io_error): y\n'),
    ).toEqual({
      level: 'ERROR',
      event: 'File Open Failed',
      text: 'x (io_error): y',
    });
    expect(parseEngineLine('shapelogic_engine: INFO: Solve Paused')).toEqual({
      level: 'INFO',
      event: 'Solve Paused',
      text: '',
    });
    // a line whose start is not an event's name
    expect(parseEngineLine('shapelogic_engine: DEBUG: the start end is 4 moves deep')).toEqual({
      level: 'DEBUG',
      event: 'Engine',
      text: 'the start end is 4 moves deep',
    });
    expect(parseEngineLine('libc++abi: terminating')).toEqual({
      level: 'ERROR',
      event: 'Engine Error',
      text: 'libc++abi: terminating',
    });
    expect(parseEngineLine('shapelogic_engine: assertion failed')).toEqual({
      level: 'ERROR',
      event: 'Engine Error',
      text: 'assertion failed',
    });
    expect(parseEngineLine('  ')).toBeNull();
  });

  it('are split from the engine stderr chunks, a line across two chunks too', () => {
    const got: string[] = [];
    const lines = new EngineLines((l) => got.push(l));
    const enc = new TextEncoder();
    lines.push(enc.encode('one\ntw'));
    lines.push(enc.encode('o\nthr'));
    expect(got).toEqual(['one', 'two']);
    lines.end();
    expect(got).toEqual(['one', 'two', 'thr']);
  });

  it('come from a transport whose stderr the client hears as well', () => {
    let stderr: ((c: Uint8Array) => void) | null = null;
    const t: Transport = {
      write: () => undefined,
      onData: () => undefined,
      onStderr: (h) => {
        stderr = h;
      },
      onExit: () => undefined,
      kill: () => undefined,
    };
    const logged: Uint8Array[] = [];
    const client: Uint8Array[] = [];
    const tee = teeStderr(t, (c) => logged.push(c));
    tee.onStderr?.((c) => client.push(c));
    const chunk = new Uint8Array([65, 10]);
    (stderr as ((c: Uint8Array) => void) | null)?.(chunk);
    expect(logged).toEqual([chunk]);
    expect(client).toEqual([chunk]);
  });
});

describe('the log file', () => {
  it('is written a line at a time and read newest first', () => {
    const log = new AppLog(dir, { now: () => at });
    log.info('app', 'App Started', 'ShapeLogic');
    log.error('window 1', 'File Open Failed', '"a.xmpuzzle"');
    log.warn('window 1', 'Search Note', 'the disk budget');
    expect(readFileSync(log.file, 'utf8').split('\n')).toHaveLength(4);
    const page = log.page(query);
    expect(page.entries.map((e) => e.level)).toEqual(['WARN', 'ERROR', 'INFO']);
    expect(page.entries[2]?.line).toBe(
      '2026-11-16 14:39:55.007 INFO  [app] App Started: ShapeLogic',
    );
    expect(page).toMatchObject({ matches: 3, lines: 3, more: false, archived: false, error: null });
    expect(page.file).toBe(log.file);
    expect(page.bytes).toBe(readFileSync(log.file).length);
  });

  it('gives pages of a hundred lines, then the next, by the lines shown', () => {
    const log = new AppLog(dir);
    for (let i = 0; i < 250; i++) {
      log.info('app', 'Test Line', `line ${String(i)}`);
    }
    const first = log.page(query);
    expect(first.entries).toHaveLength(100);
    expect(first.entries[0]?.line).toMatch(/line 249$/);
    expect(first.more).toBe(true);
    const second = log.page({ ...query, offset: 100 });
    expect(second.entries[0]?.line).toMatch(/line 149$/);
    const last = log.page({ ...query, offset: 200 });
    expect(last.entries).toHaveLength(50);
    expect(last.entries.at(-1)?.line).toMatch(/line 0$/);
    expect(last.more).toBe(false);
    // what is written since is read without reading the file again
    log.info('app', 'Test Line', 'line 250');
    expect(log.page(query).entries[0]?.line).toMatch(/line 250$/);
  });

  it('keeps the errors and warnings only, and the lines with the text, any case', () => {
    const log = new AppLog(dir);
    log.info('window 1', 'File Opened', '"Heart.xmpuzzle"');
    log.error(
      'window 1',
      'Search Note',
      'The saved search could not be carried on: 000004.keys is missing',
    );
    log.info('window 1', 'File Saved', '"Heart.xmpuzzle"');
    log.warn('window 1', 'Search Note', 'The search reached its disk budget: the search is saved');
    log.write('DEBUG', 'window 1', 'Search Note', 'The start end is 84 moves deep');
    log.error('window 2', 'File Open Failed', '"gone.xmpuzzle"');
    expect(log.page({ ...query, errorsOnly: true }).entries.map((e) => e.level)).toEqual([
      'ERROR',
      'WARN',
      'ERROR',
    ]);
    const heart = log.page({ ...query, search: 'heart' });
    expect(heart.entries).toHaveLength(2);
    expect(heart).toMatchObject({ matches: 2, lines: 6 });
    expect(log.page({ ...query, search: 'KEYS', errorsOnly: true }).entries).toHaveLength(1);
    expect(log.page({ ...query, search: 'nothing like it' }).entries).toHaveLength(0);
  });

  it('is compressed once it would pass its size, one compressed file kept', async () => {
    const log = new AppLog(dir, { maxBytes: 2000 });
    const line = 'x'.repeat(150);
    const write = async (i: number): Promise<void> => {
      log.info('app', 'Test Line', `line ${String(i)} ${line}`);
      // as the app writes: a line now and then, each compression through
      // before the next is due
      await log.settled();
      expect(readFileSync(log.file).length).toBeLessThanOrEqual(2000);
    };
    for (let i = 0; i < 12; i++) {
      await write(i);
    }
    expect(existsSync(log.archive)).toBe(true);
    expect(gunzipSync(readFileSync(log.archive)).toString('utf8')).toContain('line 0 ');
    // the new file has what came after
    expect(readFileSync(log.file, 'utf8')).not.toContain('line 0 ');
    expect(readFileSync(log.file, 'utf8')).toContain('line 11 ');
    for (let i = 12; i < 40; i++) {
      await write(i);
    }
    // a later compression replaces the one before: there is only one
    const archived = gunzipSync(readFileSync(log.archive)).toString('utf8');
    expect(archived).not.toContain('line 0 ');
    // it ends with the line before the current file's first
    const firstNow = Number(/line (\d+) /.exec(readFileSync(log.file, 'utf8'))?.[1]);
    expect(firstNow).toBeGreaterThan(20);
    expect(archived).toContain(`line ${String(firstNow - 1)} `);
    expect(readFileSync(log.file, 'utf8')).toContain('line 39 ');
    expect(readdirSync(dir).sort()).toEqual(['shapelogic.log', 'shapelogic.log.1.gz']);
    expect(log.page(query).archived).toBe(true);
  });

  it('loses no line written while the file before is compressed', async () => {
    const log = new AppLog(dir, { maxBytes: 2000 });
    for (let i = 0; i < 30; i++) {
      log.info('app', 'Test Line', `line ${String(i)} ${'x'.repeat(150)}`);
    }
    await log.settled();
    const all = await log.text();
    expect(all.toString('utf8').match(/line \d+ /g)).toHaveLength(30);
  });

  it('grows to the larger size while Debug logging is on', async () => {
    const log = new AppLog(dir, { maxBytes: 2000, debugMaxBytes: 8000, debug: true });
    expect(log.page(query)).toMatchObject({ debug: true, limit: 8000 });
    for (let i = 0; i < 30; i++) {
      log.info('app', 'Test Line', `line ${String(i)} ${'x'.repeat(150)}`);
      await log.settled();
    }
    // 30 lines of about 200 bytes: past 2000, not past 8000
    expect(existsSync(log.archive)).toBe(false);
    expect(readFileSync(log.file).length).toBeGreaterThan(2000);
    await log.setDebug(false);
    expect(log.page(query)).toMatchObject({ debug: false, limit: 2000 });
  });

  it('keeps the newest lines and the bytes before them when Debug logging is turned off', async () => {
    const log = new AppLog(dir, {
      maxBytes: 20_000,
      debugMaxBytes: 1_000_000,
      keepArchiveBytes: 10_000,
    });
    // an older compressed log, from before
    for (let i = 0; i < 200; i++) {
      log.info('app', 'Test Line', `old ${String(i)} ${'o'.repeat(120)}`);
      await log.settled();
    }
    expect(existsSync(log.archive)).toBe(true);
    await log.setDebug(true);
    for (let i = 0; i < 600; i++) {
      log.write('DEBUG', 'window 1', 'Test Line', `line ${String(i)} ${'d'.repeat(60)}`);
    }
    await log.setDebug(false);
    await log.settled();
    // the file: the newest 100 lines, whole
    const kept = readFileSync(log.file, 'utf8').trimEnd().split('\n');
    expect(kept).toHaveLength(100);
    expect(kept[0]).toMatch(/ line 500 /);
    expect(kept.at(-1)).toMatch(/ line 599 /);
    // the compressed file: whole lines, the 10 kB before them and no more,
    // and none of the older compressed log
    const archived = gunzipSync(readFileSync(log.archive)).toString('utf8');
    expect(archived.length).toBeLessThanOrEqual(10_000);
    expect(archived.length).toBeGreaterThan(9_000);
    expect(archived.startsWith('20')).toBe(true);
    expect(archived.endsWith(' line 499 ' + 'd'.repeat(60) + '\n')).toBe(true);
    expect(archived).not.toContain(' old ');
    expect(readdirSync(dir).sort()).toEqual(['shapelogic.log', 'shapelogic.log.1.gz']);
    // and it is written on at the smaller size
    log.info('app', 'Test Line', 'after');
    expect(log.page(query).entries[0]?.line).toMatch(/after$/);
    expect(log.page(query).limit).toBe(20_000);
  });

  it('keeps a short log whole when Debug logging is turned off', async () => {
    const log = new AppLog(dir, { debug: true });
    for (let i = 0; i < 40; i++) {
      log.write('DEBUG', 'app', 'Test Line', `line ${String(i)}`);
    }
    await log.setDebug(false);
    await log.settled();
    expect(readFileSync(log.file, 'utf8').trimEnd().split('\n')).toHaveLength(40);
    expect(existsSync(log.archive)).toBe(false);
  });

  it('finishes a compression a quit cut short', async () => {
    writeFileSync(join(dir, 'shapelogic.log.1'), 'left over\n');
    const log = new AppLog(dir);
    await log.settled();
    expect(gunzipSync(readFileSync(log.archive)).toString('utf8')).toBe('left over\n');
    expect(existsSync(join(dir, 'shapelogic.log.1'))).toBe(false);
  });

  it('compresses into a temporary file of its own, never through a link at a name', async () => {
    // an older version compressed into `<archive>.tmp` and followed a link left there
    const victim = join(dir, 'victim');
    writeFileSync(victim, 'mine\n');
    symlinkSync(victim, join(dir, 'shapelogic.log.1.gz.tmp'));
    // what a quit in the middle of a compression leaves
    writeFileSync(join(dir, 'shapelogic.log.1.gz.123.0123456789ab.tmp'), 'half');
    writeFileSync(join(dir, 'shapelogic.log.1'), 'left over\n');
    const log = new AppLog(dir);
    await log.settled();
    expect(readFileSync(victim, 'utf8')).toBe('mine\n');
    expect(lstatSync(log.archive).isFile()).toBe(true);
    expect(gunzipSync(readFileSync(log.archive)).toString('utf8')).toBe('left over\n');
    expect(readdirSync(dir).filter((n) => n.endsWith('.tmp'))).toEqual([]);
  });

  it('exports both files as plain text, the older first, and clears both', async () => {
    const log = new AppLog(dir, { maxBytes: 600 });
    for (let i = 0; i < 12; i++) {
      log.info('app', 'Test Line', `line ${String(i)} ${'y'.repeat(60)}`);
    }
    await log.settled();
    const out = join(dir, 'export.txt');
    await log.exportTo(out);
    const text = readFileSync(out, 'utf8');
    const numbers = [...text.matchAll(/line (\d+)/g)].map((m) => Number(m[1]));
    expect(numbers).toEqual(numbers.slice().sort((a, b) => a - b));
    expect(numbers.at(-1)).toBe(11);
    expect(numbers.length).toBeGreaterThan(1);
    await log.clear();
    expect(existsSync(log.file)).toBe(false);
    expect(existsSync(log.archive)).toBe(false);
    expect(log.page(query)).toMatchObject({ entries: [], lines: 0, archived: false });
    // and it is written again
    log.info('app', 'Test Line', 'after');
    expect(log.page(query).entries).toHaveLength(1);
  });
});

describe('Export of the newest lines', () => {
  it('counts the lines and cuts the newest from a text, a last line without its newline too', () => {
    const b = (t: string): Buffer => Buffer.from(t, 'utf8');
    expect(lineCount(b(''))).toBe(0);
    expect(lineCount(b('a\n'))).toBe(1);
    expect(lineCount(b('a\nb'))).toBe(2);
    expect(lineCount(b('a\nb\nc\n'))).toBe(3);
    expect(tailOf(b('a\nb\nc\n'), 2).toString()).toBe('b\nc\n');
    expect(tailOf(b('a\nb\nc'), 2).toString()).toBe('b\nc');
    expect(tailOf(b('a\nb\nc\n'), 3).toString()).toBe('a\nb\nc\n');
    expect(tailOf(b('a\nb\nc\n'), 10).toString()).toBe('a\nb\nc\n');
    expect(tailOf(b('a\nb\nc\n'), 0).toString()).toBe('');
    expect(tailOf(b(''), 5).toString()).toBe('');
  });

  it('offers all of the log, the compressed file too, and its last lines, with their sizes', async () => {
    const log = new AppLog(dir, { maxBytes: 3000 });
    for (let i = 0; i < 150; i++) {
      log.info('app', 'Test Line', `line ${String(i)} ${'z'.repeat(20)}`);
    }
    await log.settled();
    expect(existsSync(log.archive)).toBe(true);
    const all = await log.text();
    const sizes = await log.exportSizes();
    expect(sizes).toEqual({
      lines: 150,
      bytes: all.length,
      tailLines: 100,
      tailBytes: tailOf(all, 100).length,
    });
    const out = join(dir, 'last.txt');
    await log.exportTo(out, 100);
    const text = readFileSync(out, 'utf8');
    const numbers = [...text.matchAll(/line (\d+)/g)].map((m) => Number(m[1]));
    expect(numbers).toEqual(Array.from({ length: 100 }, (_, i) => i + 50));
    expect(Buffer.byteLength(text)).toBe(sizes.tailBytes);
  });

  it('offers the whole of a short log as its last lines', async () => {
    const log = new AppLog(dir);
    log.info('app', 'Test Line', 'one');
    log.info('app', 'Test Line', 'two');
    const sizes = await log.exportSizes();
    expect(sizes.lines).toBe(2);
    expect(sizes.tailLines).toBe(2);
    expect(sizes.tailBytes).toBe(sizes.bytes);
  });
});
