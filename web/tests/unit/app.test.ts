// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage 6's parts without a window: the main process's store (written
// whole by rename, read back keeping what is well formed, the recent
// files), the rescue copy (its record, a record that does not match the
// copy, a removal), the menus' template (the platforms' layouts, what is
// greyed, the keys shown and not registered, every item's command one the
// renderer takes), the bridge's checks, the stored settings read back, and
// the convert dialog's fields.

import {
  chmodSync,
  existsSync,
  lstatSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  rmSync,
  statSync,
  symlinkSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import type { MenuItemConstructorOptions } from 'electron';
import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  isAppCommand,
  isExportKind,
  isFileKind,
  isMenuState,
  isRescueInfo,
} from '../../src/engine/app-bridge.ts';
import type { AppCommand, MenuState } from '../../src/engine/app-bridge.ts';
import { ExportGrants, writeFileSafely } from '../../src/main/export-file.ts';
import { commandItems, menuTemplate, NO_STATE, recentLabel } from '../../src/main/menu.ts';
import { RescueStore } from '../../src/main/rescue.ts';
import { RECENT_MAX, StateStore } from '../../src/main/state-store.ts';
import { DEFAULTS, validPrefs } from '../../src/renderer/shell/prefs.ts';
import { toShapesParams } from '../../src/renderer/solver/to-shapes-form.ts';

const dirs: string[] = [];
function dir(): string {
  const d = mkdtempSync(join(tmpdir(), 'bt-unit-app-'));
  dirs.push(d);
  return d;
}
afterEach(() => {
  vi.useRealTimers();
  for (const d of dirs.splice(0)) {
    rmSync(d, { recursive: true, force: true });
  }
});

describe('the store of settings, layouts and recent files', () => {
  it('writes a moment after a change, whole, and reads it back', () => {
    vi.useFakeTimers();
    const d = dir();
    const s = new StateStore(d, 100);
    s.set('prefs', { theme: 'Dark', undo: 100 });
    s.set('layout', { tab: 'solver' });
    s.setBounds({ x: 1, y: 2, width: 900, height: 700 });
    s.addRecent('/a.xmpuzzle');
    expect(existsSync(join(d, 'state.json'))).toBe(false);
    vi.advanceTimersByTime(100);
    expect(existsSync(join(d, 'state.json.tmp'))).toBe(false);
    const back = new StateStore(d);
    expect(back.prefs).toEqual({ theme: 'Dark', undo: 100 });
    expect(back.layout).toEqual({ tab: 'solver' });
    expect(back.bounds).toEqual({ x: 1, y: 2, width: 900, height: 700 });
    expect(back.recent).toEqual(['/a.xmpuzzle']);
  });

  it('keeps the recent files newest first, once each, at most ten', () => {
    const d = dir();
    const s = new StateStore(d);
    const told = vi.fn();
    s.onRecent(told);
    for (let i = 0; i < 12; i++) {
      s.addRecent(`/p${String(i)}.xmpuzzle`);
    }
    s.addRecent('/p5.xmpuzzle');
    expect(s.recent).toHaveLength(RECENT_MAX);
    expect(s.recent[0]).toBe('/p5.xmpuzzle');
    expect(s.recent.filter((p) => p === '/p5.xmpuzzle')).toHaveLength(1);
    expect(s.recent).not.toContain('/p0.xmpuzzle');
    s.removeRecent('/p5.xmpuzzle');
    expect(s.recent).not.toContain('/p5.xmpuzzle');
    s.clearRecent();
    expect(s.recent).toEqual([]);
    expect(told).toHaveBeenCalledTimes(15);
    s.flush();
    expect(new StateStore(d).recent).toEqual([]);
  });

  it('reads a file that is not JSON, or not ours, as an empty store', () => {
    const d = dir();
    writeFileSync(join(d, 'state.json'), '{not json');
    expect(new StateStore(d).prefs).toBeUndefined();
    writeFileSync(
      join(d, 'state.json'),
      JSON.stringify({ prefs: [1], recent: ['/ok', 3, ''], bounds: { x: 'a' }, layout: {} }),
    );
    const s = new StateStore(d);
    expect(s.prefs).toBeUndefined();
    expect(s.recent).toEqual(['/ok']);
    expect(s.bounds).toBeUndefined();
    expect(s.layout).toEqual({});
  });
});

describe('the rescue copy', () => {
  const info = { path: '/puzzles/a.xmpuzzle', name: 'a.xmpuzzle', revision: 7, time: 1000 };
  const data = Buffer.from('<puzzle/>').toString('base64');

  it('is written as the file’s bytes with its record, read back, and removed', () => {
    const d = dir();
    const r = new RescueStore(d);
    expect(r.read()).toBeNull();
    r.write(data, info);
    expect(readFileSync(join(d, '__rescue.xmpuzzle'), 'utf8')).toBe('<puzzle/>');
    expect(r.read()).toEqual({ data, info });
    r.clear();
    expect(r.read()).toBeNull();
    expect(existsSync(join(d, '__rescue.json'))).toBe(false);
  });

  it('offers a copy whose record does not match it as a puzzle without a file', () => {
    const d = dir();
    const r = new RescueStore(d);
    r.write(data, info);
    // the app died between the record and the copy of a later write
    writeFileSync(join(d, '__rescue.xmpuzzle'), '<puzzle version="2"/>');
    const got = r.read();
    expect(got?.info.path).toBeNull();
    expect(Buffer.from(got?.data ?? '', 'base64').toString()).toBe('<puzzle version="2"/>');
    // no record at all
    rmSync(join(d, '__rescue.json'));
    expect(r.read()?.info.path).toBeNull();
  });
});

describe('the menus', () => {
  const state: MenuState = {
    ...NO_STATE,
    document: true,
    dirty: true,
    path: '/p/a.xmpuzzle',
    undo: 'Rename shape',
    redo: null,
    solutions: true,
    keys: {
      new: 'CmdOrCtrl+N',
      save: 'CmdOrCtrl+S',
      undo: 'CmdOrCtrl+Z',
      keys: 'F1',
      settings: 'CmdOrCtrl+,',
    },
  };
  const build = (
    s: MenuState,
    platform: string,
    recent: string[] = [],
  ): { template: MenuItemConstructorOptions[]; sent: AppCommand[] } => {
    const sent: AppCommand[] = [];
    const template = menuTemplate(s, {
      platform,
      name: 'BurrTools Alpha',
      recent,
      dev: false,
      send: (c) => sent.push(c),
      openHelp: () => undefined,
      clearRecent: () => undefined,
    });
    return { template, sent };
  };

  it('has the original’s menus on each platform: About and Settings in the app menu on macOS', () => {
    const mac = build(state, 'darwin').template.map((m) => m.id);
    expect(mac).toEqual(['app', 'file', 'edit', 'view', 'help']);
    const win = build(state, 'win32').template.map((m) => m.id);
    expect(win).toEqual(['file', 'edit', 'view', 'help']);
    const winItems = commandItems(build(state, 'win32').template);
    expect(winItems.has('about')).toBe(true);
    expect(winItems.has('settings')).toBe(true);
  });

  it('greys what this version cannot do, and what the state does not allow', () => {
    const none = (): ReturnType<typeof commandItems> =>
      commandItems(build(NO_STATE, 'darwin').template);
    const items = commandItems(build(state, 'darwin').template);
    for (const id of [
      'import.ps3d',
      'import.scad',
      'export.scad',
      'export.stl',
      'export.glb',
      'convertGrid',
      'anaglyph',
      'anaglyph.swapped',
      'style.stl',
      'redo',
      'exportPaused',
    ]) {
      expect(items.get(id)?.enabled, id).toBe(false);
    }
    for (const id of [
      'new',
      'open',
      'save',
      'saveAs',
      'undo',
      'notes',
      'toShapes',
      'close',
      'export.image',
      'export.vector',
    ]) {
      expect(items.get(id)?.enabled, id).toBe(true);
    }
    // the STL needs a shape with cells, the animation a solution with its moves shown
    const exports = commandItems(
      build({ ...state, shapes: true, animation: true }, 'darwin').template,
    );
    expect(exports.get('export.stl')?.enabled).toBe(true);
    expect(exports.get('export.glb')?.enabled).toBe(true);
    expect(none().get('export.image')?.enabled).toBe(false);
    expect(items.get('undo')?.label).toBe('Undo Rename shape');
    // nothing open: only New, Open and the like
    expect(none().get('save')?.enabled).toBe(false);
    expect(none().get('new')?.enabled).toBe(true);
    // solving: no conversion; a paused solve can be exported
    const solving = commandItems(
      build({ ...state, solving: true, paused: true }, 'darwin').template,
    );
    expect(solving.get('toShapes')?.enabled).toBe(false);
    expect(solving.get('exportPaused')?.enabled).toBe(false);
    const paused = commandItems(build({ ...state, paused: true }, 'darwin').template);
    expect(paused.get('exportPaused')?.enabled).toBe(true);
  });

  it('shows the table’s keys without registering them', () => {
    const items = commandItems(build(state, 'darwin').template);
    expect(items.get('new')).toMatchObject({
      accelerator: 'CmdOrCtrl+N',
      registerAccelerator: false,
    });
    expect(items.get('keys')).toMatchObject({ accelerator: 'F1', registerAccelerator: false });
    for (const item of items.values()) {
      if (item.accelerator !== undefined) {
        expect(item.registerAccelerator, item.id).toBe(false);
      }
    }
  });

  it('sends only commands the renderer takes', () => {
    const { template, sent } = build(state, 'win32', ['/x/b.xmpuzzle']);
    for (const item of commandItems(template).values()) {
      (item.click as (() => void) | undefined)?.();
    }
    expect(sent.length).toBeGreaterThan(15);
    for (const c of sent) {
      expect(isAppCommand(c), JSON.stringify(c)).toBe(true);
    }
    expect(sent).toContainEqual({ cmd: 'openPath', path: '/x/b.xmpuzzle' });
    expect(sent).toContainEqual({ cmd: 'style', style: 'flat' });
    expect(sent).toContainEqual({ cmd: 'colourView', on: true });
  });

  it('lists the recent files by name and folder, or says there are none', () => {
    expect(recentLabel('/Users/me/puzzles/a.xmpuzzle')).toBe('a.xmpuzzle — /Users/me/puzzles');
    expect(recentLabel('C:\\p\\a.xmpuzzle')).toBe('a.xmpuzzle — C:\\p');
    const none = commandItems(build(state, 'darwin').template);
    expect(none.get('recent.none')?.enabled).toBe(false);
    expect(none.get('recent.clear')?.enabled).toBe(false);
  });
});

describe('the bridge’s checks', () => {
  it('takes the commands and states it knows, nothing else', () => {
    expect(isAppCommand({ cmd: 'save' })).toBe(true);
    expect(isAppCommand({ cmd: 'openPath', path: '' })).toBe(false);
    expect(isAppCommand({ cmd: 'style', style: 'stl' })).toBe(false);
    expect(isAppCommand({ cmd: 'rm' })).toBe(false);
    expect(isAppCommand(null)).toBe(false);
    expect(isMenuState(NO_STATE)).toBe(true);
    expect(isMenuState({ ...NO_STATE, undo: 3 })).toBe(false);
    expect(isMenuState({ ...NO_STATE, keys: { new: 1 } })).toBe(false);
    expect(isMenuState({ ...NO_STATE, animation: 1 })).toBe(false);
    expect(isAppCommand({ cmd: 'exportGlb' })).toBe(true);
    expect(isFileKind('glb')).toBe(true);
    expect(isFileKind('exe')).toBe(false);
    expect(isExportKind('puzzle')).toBe(false);
    expect(isRescueInfo({ path: null, name: 'x', revision: 1, time: 2 })).toBe(true);
    expect(isRescueInfo({ path: '', name: 'x', revision: 1, time: 2 })).toBe(false);
  });
});

describe('the stored settings read back', () => {
  it('keeps what is well formed and drops the rest', () => {
    expect(
      validPrefs({
        theme: 'Dark',
        density: 'Huge',
        undo: 100,
        threads: -1,
        tips: false,
        cube: 'yes',
        rot: 'Arc-ball',
        gone: 1,
      }),
    ).toEqual({ theme: 'Dark', undo: 100, tips: false, rot: 'Arc-ball' });
    expect(validPrefs({ ...DEFAULTS })).toEqual(DEFAULTS);
  });
});

describe('Convert Assemblies to Pieces: the fields as the call', () => {
  const fields = {
    problem: 'p1',
    addTo: 'nothing' as const,
    target: 'p2',
    min: '0',
    max: '1',
    minCells: '0',
    maxCells: '1000000',
    drops: { dropDisconnected: true, dropNonMillable: true, dropIdentical: true },
  };

  it('asks for the original’s filters (millable and notchable on bricks only)', () => {
    expect(toShapesParams(fields, false)).toEqual({
      problem: 'p1',
      addTo: 'nothing',
      min: 0,
      max: 1,
      minCells: 0,
      maxCells: 1000000,
      dropDisconnected: true,
      dropMirror: false,
      dropSymmetric: false,
      dropIdentical: true,
    });
    expect(toShapesParams({ ...fields, addTo: 'problem' }, true)).toMatchObject({
      dropNonMillable: true,
      dropNonNotchable: false,
      target: 'p2',
    });
  });

  it('refuses a range that is not one', () => {
    expect(typeof toShapesParams({ ...fields, min: '3', max: '2' }, true)).toBe('string');
    expect(typeof toShapesParams({ ...fields, max: 'x' }, true)).toBe('string');
    expect(typeof toShapesParams({ ...fields, max: '70000' }, true)).toBe('string');
    expect(typeof toShapesParams({ ...fields, minCells: '-1' }, true)).toBe('string');
  });
});

describe('an export’s file, written as the core writes a puzzle', () => {
  let dir = '';
  afterEach(() => {
    if (dir !== '') {
      chmodSync(dir, 0o755);
      rmSync(dir, { recursive: true, force: true });
      dir = '';
    }
  });
  const fresh = (): string => {
    dir = mkdtempSync(join(tmpdir(), 'bt-export-'));
    return dir;
  };

  it('writes a new file and replaces an old one whole, leaving no temporary file', async () => {
    const d = fresh();
    const f = join(d, 'a.png');
    await writeFileSafely(f, new Uint8Array([1, 2, 3]));
    expect([...readFileSync(f)]).toEqual([1, 2, 3]);
    await writeFileSafely(f, new Uint8Array([4]));
    expect([...readFileSync(f)]).toEqual([4]);
    expect(readdirSync(d)).toEqual(['a.png']);
  });

  it('writes the file a link names and keeps the link', async () => {
    const d = fresh();
    mkdirSync(join(d, 'real'));
    const target = join(d, 'real', 'b.svg');
    writeFileSync(target, 'old');
    const link = join(d, 'b.svg');
    symlinkSync(join('real', 'b.svg'), link);
    await writeFileSafely(link, new TextEncoder().encode('new'));
    expect(lstatSync(link).isSymbolicLink()).toBe(true);
    expect(readFileSync(target, 'utf8')).toBe('new');
    expect(readdirSync(join(d, 'real'))).toEqual(['b.svg']);
  });

  it('refuses a read-only file and leaves it, and keeps a file’s permissions', async () => {
    const d = fresh();
    const locked = join(d, 'c.stl');
    writeFileSync(locked, 'old');
    chmodSync(locked, 0o444);
    await expect(writeFileSafely(locked, new Uint8Array([1]))).rejects.toThrow(
      'c.stl is read-only',
    );
    expect(readFileSync(locked, 'utf8')).toBe('old');
    const own = join(d, 'd.glb');
    writeFileSync(own, 'old');
    chmodSync(own, 0o600);
    await writeFileSafely(own, new Uint8Array([1]));
    expect(statSync(own).mode & 0o777).toBe(0o600);
  });

  it('fails without touching the old file when the temporary file cannot be made', async () => {
    const d = fresh();
    const f = join(d, 'e.png');
    writeFileSync(f, 'old');
    mkdirSync(`${f}.tmp`);
    await expect(writeFileSafely(f, new Uint8Array([1]))).rejects.toThrow('could not write e.png');
    expect(readFileSync(f, 'utf8')).toBe('old');
  });

  it('never writes through a link planted at the temporary file’s name, and replaces a stale one', async () => {
    const d = fresh();
    const victim = join(d, 'victim.txt');
    writeFileSync(victim, 'untouched');
    const f = join(d, 'f.png');
    symlinkSync('victim.txt', `${f}.tmp`);
    await writeFileSafely(f, new TextEncoder().encode('PNG'));
    expect(readFileSync(victim, 'utf8')).toBe('untouched');
    expect(lstatSync(f).isSymbolicLink()).toBe(false);
    expect(readFileSync(f, 'utf8')).toBe('PNG');
    writeFileSync(`${f}.tmp`, 'stale');
    await writeFileSafely(f, new TextEncoder().encode('PNG2'));
    expect(readFileSync(f, 'utf8')).toBe('PNG2');
    expect(readdirSync(d).sort()).toEqual(['f.png', 'victim.txt']);
  });

  it('lets a window write only a path its dialog gave for an export, once, with the kind’s extension', () => {
    const ext = { image: '.png', vector: '.svg', stl: '.stl', glb: '.glb' } as const;
    const grants = new ExportGrants<object>((k) => ext[k]);
    const a = {};
    const b = {};
    grants.grant(a, '/x/view.png', 'image');
    expect(grants.take(b, '/x/view.png')).toBeNull();
    expect(grants.take(a, '/x/other.png')).toBeNull();
    expect(grants.take(a, '/x/view.png')).toBe('image');
    expect(grants.take(a, '/x/view.png')).toBeNull();
    grants.grant(a, '/x/puzzle.xmpuzzle', 'glb');
    expect(grants.take(a, '/x/puzzle.xmpuzzle')).toBeNull();
  });
});
