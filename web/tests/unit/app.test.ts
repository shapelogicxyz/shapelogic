// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage 6's parts without a window: the main process's store (written
// whole by rename, read back keeping what is well formed, the recent
// files), the rescue copy (its record, a record that does not match the
// copy, a removal), the menus' template (the platforms' layouts, what is
// grayed, the keys shown and not registered, every item's command one the
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

import { MESH_STYLE_VALUES } from '../../src/engine/api.generated.ts';
import {
  EXPORT_KINDS,
  isAppCommand,
  isExportKind,
  isFileKind,
  isMenuState,
  isCacheLink,
  isRescueInfo,
  OPEN_KINDS,
  THEMES,
} from '../../src/engine/app-bridge.ts';
import type { AppCommand, MenuState } from '../../src/engine/app-bridge.ts';
import {
  ExportGrants,
  FolderGrants,
  isPieceFileName,
  writeFileSafely,
} from '../../src/main/export-file.ts';
import { commandItems, menuTemplate, NO_STATE, recentLabel } from '../../src/main/menu.ts';
import { PathOwners } from '../../src/main/path-owners.ts';
import { isTempOf, replaceFile, replaceFileSync, tempName } from '../../src/main/replace-file.ts';
import { RescueSlots, RescueStore, rescueSlot } from '../../src/main/rescue.ts';
import { RECENT_MAX, StateStore } from '../../src/main/state-store.ts';
import { LIMITS } from '../../src/engine/api.generated.ts';
import {
  clampPref,
  DEFAULTS,
  ENGINE_PREFS,
  engineSettingsOf,
  NEW_SIZE_MAX,
  PREFS,
  prefsOfEngine,
  SIDE_MAX,
  SIDE_MIN,
  validPref,
  validPrefs,
} from '../../src/renderer/shell/prefs.ts';
import type { PrefKey } from '../../src/renderer/shell/prefs.ts';
import { DEFAULT_OPTIONS } from '../../src/renderer/solver/settings.ts';
import { BOXES, optKey, SETTING_BOXES } from '../../src/renderer/solver/boxes.ts';
import { toShapesParams } from '../../src/renderer/solver/to-shapes-form.ts';

const dirs: string[] = [];
function dir(): string {
  const d = mkdtempSync(join(tmpdir(), 'sl-unit-app-'));
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
    const s = new StateStore(d, { delayMs: 100 });
    s.set('prefs', { theme: 'Dark', undo: 100 });
    s.set('layout', { tab: 'solver' });
    s.setBounds({ x: 1, y: 2, width: 900, height: 700 });
    s.addRecent('/a.xmpuzzle');
    s.setLastDir('/puzzles');
    s.setSkipVersion('v1.4');
    s.setCacheRoot('/big');
    expect(existsSync(join(d, 'state.json'))).toBe(false);
    vi.advanceTimersByTime(100);
    // no temporary file left beside it (replace-file.ts)
    expect(readdirSync(d)).toEqual(['state.json']);
    const back = new StateStore(d);
    expect(back.prefs).toEqual({ theme: 'Dark', undo: 100 });
    expect(back.layout).toEqual({ tab: 'solver' });
    expect(back.bounds).toEqual({ x: 1, y: 2, width: 900, height: 700 });
    expect(back.recent).toEqual(['/a.xmpuzzle']);
    expect(back.lastDir).toBe('/puzzles');
    expect(back.skipVersion).toBe('v1.4');
    expect(back.cacheRoot).toBe('/big');
    // back to the default cache folders: none stored
    back.setCacheRoot(undefined);
    back.flush();
    expect(new StateStore(d).cacheRoot).toBeUndefined();
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

  it('tells a write that failed to its onError (the app log), not only the console (cleanup item 7.2)', () => {
    const d = dir();
    // a file where its folder would be: the folder cannot be made
    const blocked = join(d, 'blocked');
    writeFileSync(blocked, '');
    const told: string[] = [];
    const s = new StateStore(blocked, { delayMs: 0, onError: (m) => told.push(m) });
    s.setLastDir('/puzzles');
    s.flush();
    expect(told).toHaveLength(1);
    expect(told[0]).toContain(`could not store the settings in ${join(blocked, 'state.json')}`);
  });

  it('reads a file that is not JSON, or not ours, as an empty store', () => {
    const d = dir();
    writeFileSync(join(d, 'state.json'), '{not json');
    expect(new StateStore(d).prefs).toBeUndefined();
    writeFileSync(
      join(d, 'state.json'),
      JSON.stringify({
        prefs: [1],
        recent: ['/ok', 3, ''],
        bounds: { x: 'a' },
        layout: {},
        lastDir: 7,
        updates: { skip: 3 },
      }),
    );
    const s = new StateStore(d);
    expect(s.prefs).toBeUndefined();
    expect(s.recent).toEqual(['/ok']);
    expect(s.bounds).toBeUndefined();
    expect(s.layout).toEqual({});
    expect(s.lastDir).toBeUndefined();
    expect(s.skipVersion).toBeUndefined();
  });
});

describe('the rescue copy', () => {
  const info = { path: '/puzzles/a.xmpuzzle', name: 'a.xmpuzzle', revision: 7, time: 1000 };
  const data = '<puzzle/>';
  // the window's engine (document.saveRescue): writes the file it is given,
  // answers the revision the copy holds
  const saved: string[] = [];
  const save =
    (text: string, revision = info.revision) =>
    (file: string): Promise<number> => {
      saved.push(file);
      writeFileSync(file, text);
      return Promise.resolve(revision);
    };

  it('is written by the engine with its record, read back as its file, and removed', async () => {
    const d = dir();
    const r = new RescueStore(d);
    expect(await r.read()).toBeNull();
    await r.write(save(data), info);
    expect(saved.at(-1)).toBe(join(d, '__rescue.xmpuzzle'));
    expect(readFileSync(join(d, '__rescue.xmpuzzle'), 'utf8')).toBe('<puzzle/>');
    expect(await r.read()).toEqual({ file: join(d, '__rescue.xmpuzzle'), info });
    // the record holds the revision the engine wrote
    await r.write(save(data, 9), info);
    expect((await r.read())?.info.revision).toBe(9);
    // no temporary file left beside them (replace-file.ts)
    expect(readdirSync(d).sort()).toEqual(['__rescue.json', '__rescue.xmpuzzle']);
    await r.clear();
    expect(await r.read()).toBeNull();
    expect(existsSync(join(d, '__rescue.json'))).toBe(false);
  });

  it('makes its folder before the engine writes there, and fails as the engine’s write fails', async () => {
    const d = join(dir(), 'not', 'yet');
    const r = new RescueStore(d);
    await r.write(save(data), info);
    expect((await r.read())?.info).toEqual(info);
    const refused = new Error('io_error');
    await expect(r.write(() => Promise.reject(refused), info)).rejects.toBe(refused);
    // the copy before stays, with its record
    expect((await r.read())?.info).toEqual(info);
  });

  it('offers a copy whose record does not match it as a puzzle without a file', async () => {
    const d = dir();
    const r = new RescueStore(d);
    await r.write(save(data), info);
    // the app died between the copy of a later write and its record
    writeFileSync(join(d, '__rescue.xmpuzzle'), '<puzzle version="2"/>');
    const got = await r.read();
    expect(got?.info.path).toBeNull();
    expect(got?.file).toBe(join(d, '__rescue.xmpuzzle'));
    // no record at all
    rmSync(join(d, '__rescue.json'));
    expect((await r.read())?.info.path).toBeNull();
  });

  it('names its slots’ files, slot 0 as the earlier versions’ one copy', () => {
    expect(rescueSlot('__rescue.xmpuzzle')).toBe(0);
    expect(rescueSlot('__rescue.json.tmp')).toBe(0);
    // replace-file.ts's temporary files
    expect(rescueSlot('__rescue.xmpuzzle.4242.0123456789ab.tmp')).toBe(0);
    expect(rescueSlot('__rescue-3.json.17.abcdef012345.tmp')).toBe(3);
    expect(rescueSlot('__rescue.json.4242.0123456789ab')).toBeNull();
    expect(rescueSlot('__rescue-3.xmpuzzle')).toBe(3);
    expect(rescueSlot('__rescue-3.json')).toBe(3);
    expect(rescueSlot('__rescue-0.json')).toBeNull();
    expect(rescueSlot('__rescue-x.json')).toBeNull();
    expect(rescueSlot('state.json')).toBeNull();
    const d = dir();
    expect(new RescueStore(d, 2).file).toBe(join(d, '__rescue-2.xmpuzzle'));
  });

  it('gives each window a slot of its own, a copy left on disk to one window only', async () => {
    const d = dir();
    // a session that died left copies in slots 0 and 2
    await new RescueStore(d, 0).write(save(data), info);
    await new RescueStore(d, 2).write(save(data), { ...info, name: 'c.xmpuzzle' });
    const slots = new RescueSlots<string>(d);
    expect(slots.of('a').slot).toBe(0);
    expect(slots.of('a').slot).toBe(0);
    // the next window takes the other copy left, not an empty slot before it
    expect(slots.of('b').slot).toBe(2);
    expect((await slots.of('b').read())?.info.name).toBe('c.xmpuzzle');
    // no copy left: the lowest free slot
    expect(slots.of('c').slot).toBe(1);
    expect(slots.all().map((r) => r.slot)).toEqual([0, 1, 2]);
    // a window that closes frees its slot; a copy it left goes to the next window
    await slots.of('b').write(save(data), info);
    slots.release('b');
    expect(slots.of('d').slot).toBe(2);
    expect(await slots.of('d').read()).not.toBeNull();
    slots.release('c');
    expect(slots.all().map((r) => r.slot)).toEqual([0, 2]);
  });

  it('runs its writes and clears one after the other: a clear is never undone by a write before it', async () => {
    const d = dir();
    const r = new RescueStore(d);
    const big = 'x'.repeat(4_000_000);
    const writes = [r.write(save(big), info), r.write(save(data, 8), info)];
    const cleared = r.clear();
    await Promise.all([...writes, cleared]);
    expect(await r.read()).toBeNull();
    expect(readdirSync(d)).toEqual([]);
    await r.write(save(data, 9), info);
    expect((await r.read())?.info.revision).toBe(9);
  });
});

describe('the files the windows hold', () => {
  const key = (p: string): string => p.toLowerCase();
  const make = (): { owners: PathOwners<string>; told: [string, boolean][] } => {
    const told: [string, boolean][] = [];
    const owners = new PathOwners<string>((w, ro) => told.push([w, ro]), key);
    return { owners, told };
  };
  const save = (path?: string): [string, unknown] => [
    'document.save',
    path === undefined ? {} : { path },
  ];

  it('lets the first window own a file and holds it read-only in another', () => {
    const { owners, told } = make();
    owners.hold('a', '/p/A.xmpuzzle');
    owners.hold('b', '/p/a.xmpuzzle');
    expect(owners.readOnly('a')).toBe(false);
    expect(owners.readOnly('b')).toBe(true);
    expect(told).toEqual([['b', true]]);
    expect(owners.refusal('a', ...save())).toBeNull();
    expect(owners.refusal('b', ...save())).toMatch(/open in another window/);
    expect(owners.refusal('b', ...save('/p/a.xmpuzzle'))).toMatch(/open in another window/);
    // Save As elsewhere: allowed, and the window owns its new file
    expect(owners.refusal('b', ...save('/p/b.xmpuzzle'))).toBeNull();
    owners.hold('b', '/p/b.xmpuzzle');
    expect(owners.readOnly('b')).toBe(false);
    expect(told).toEqual([
      ['b', true],
      ['b', false],
    ]);
    // nor may a window save over a file another owns
    expect(owners.refusal('a', ...save('/p/b.xmpuzzle'))).toMatch(/open in another window/);
    // other calls are not looked at
    expect(owners.refusal('b', 'document.open', { path: '/p/a.xmpuzzle' })).toBeNull();
  });

  it('keeps a read-only window so when the owner goes, until it opens the file again', () => {
    const { owners } = make();
    owners.hold('a', '/p/a.xmpuzzle');
    owners.hold('b', '/p/a.xmpuzzle');
    owners.release('a');
    expect(owners.owner('/p/a.xmpuzzle')).toBeUndefined();
    // its next menu state names the same file: still read-only (its copy may be older)
    owners.hold('b', '/p/a.xmpuzzle');
    expect(owners.readOnly('b')).toBe(true);
    expect(owners.refusal('b', ...save())).not.toBeNull();
    // another window that opens it now owns it; b's copy may be saved over
    owners.hold('c', '/p/a.xmpuzzle');
    expect(owners.readOnly('c')).toBe(false);
    expect(owners.refusal('c', ...save())).toBeNull();
    owners.release('c');
    // b opens the file again: it owns it
    owners.claim('b', '/p/a.xmpuzzle');
    expect(owners.readOnly('b')).toBe(false);
    expect(owners.refusal('b', ...save())).toBeNull();
  });

  it('lets a file go when the document has another, or none', () => {
    const { owners, told } = make();
    owners.hold('a', '/p/a.xmpuzzle');
    owners.hold('b', '/p/a.xmpuzzle');
    // b's New: no file, not read-only
    owners.hold('b', null);
    expect(owners.readOnly('b')).toBe(false);
    expect(told.at(-1)).toEqual(['b', false]);
    // a opens another file: the first is free for b
    owners.hold('a', '/p/c.xmpuzzle');
    owners.claim('b', '/p/a.xmpuzzle');
    expect(owners.readOnly('b')).toBe(false);
    expect(owners.owner('/p/a.xmpuzzle')).toBe('b');
    expect(owners.owner('/p/c.xmpuzzle')).toBe('a');
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
  const focused: number[] = [];
  const build = (
    s: MenuState,
    platform: string,
    recent: string[] = [],
  ): { template: MenuItemConstructorOptions[]; sent: AppCommand[] } => {
    const sent: AppCommand[] = [];
    const template = menuTemplate(s, {
      platform,
      name: 'ShapeLogic',
      recent,
      dev: false,
      send: (c) => sent.push(c),
      openHelp: () => undefined,
      clearRecent: () => undefined,
      windows: [
        { id: 1, title: 'a.xmpuzzle — ShapeLogic', focused: true },
        { id: 2, title: 'b.xmpuzzle — ShapeLogic', focused: false },
      ],
      focusWindow: (id) => {
        focused.push(id);
      },
      copyDiagnostics: () => {
        diagnostics.push(platform);
      },
    });
    return { template, sent };
  };
  const diagnostics: string[] = [];

  it('has Help ▸ Copy Diagnostics on every platform, with or without a document (cleanup item 2.4)', () => {
    for (const platform of ['darwin', 'win32', 'linux']) {
      const items = commandItems(build(NO_STATE, platform).template);
      const copy = items.get('copyDiagnostics');
      expect(copy?.label).toBe('Copy Diagnostics');
      expect(copy?.enabled).not.toBe(false);
      (copy?.click as () => void)();
    }
    // the main process makes the report: no command goes to a page that may not work
    expect(diagnostics).toEqual(['darwin', 'win32', 'linux']);
  });

  it('has the original’s menus on each platform: About and Settings in the app menu on macOS', () => {
    const mac = build(state, 'darwin').template.map((m) => m.id);
    expect(mac).toEqual(['app', 'file', 'edit', 'view', 'window', 'help']);
    const win = build(state, 'win32').template.map((m) => m.id);
    expect(win).toEqual(['file', 'edit', 'view', 'window', 'help']);
    const winItems = commandItems(build(state, 'win32').template);
    expect(winItems.has('about')).toBe(true);
    expect(winItems.has('settings')).toBe(true);
    // the command line tool's dialog: the app menu on macOS, Help elsewhere
    expect(winItems.get('cliTool')?.label).toBe('Command Line Tool…');
    const macItems = commandItems(build(state, 'darwin').template);
    expect(macItems.get('cliTool')?.label).toBe('Install Command Line Tool…');
    const sends = build(state, 'darwin');
    (commandItems(sends.template).get('cliTool')?.click as () => void)();
    expect(sends.sent).toContainEqual({ cmd: 'cliTool' });
    expect(isAppCommand({ cmd: 'cliTool' })).toBe(true);
    // Check for Updates…: after About in the app menu on macOS, in Help elsewhere
    expect(winItems.get('checkUpdates')?.label).toBe('Check for Updates…');
    expect(macItems.get('checkUpdates')?.label).toBe('Check for Updates…');
    (commandItems(sends.template).get('checkUpdates')?.click as () => void)();
    expect(sends.sent).toContainEqual({ cmd: 'checkUpdates' });
    expect(isAppCommand({ cmd: 'checkUpdates' })).toBe(true);
  });

  it('has File ▸ New Window and a Window menu listing the windows (macOS’s own there)', () => {
    const win = build(state, 'win32');
    const items = commandItems(win.template);
    (items.get('newWindow')?.click as () => void)();
    expect(win.sent).toContainEqual({ cmd: 'newWindow' });
    expect(isAppCommand({ cmd: 'newWindow' })).toBe(true);
    const listed = win.template.find((m) => m.id === 'window')
      ?.submenu as MenuItemConstructorOptions[];
    const named = listed.filter((i) => i.id?.startsWith('window.') === true);
    expect(named.map((i) => [i.label, i.checked])).toEqual([
      ['a.xmpuzzle — ShapeLogic', true],
      ['b.xmpuzzle — ShapeLogic', false],
    ]);
    (named[1]?.click as () => void)();
    expect(focused).toEqual([2]);
    expect(build(state, 'darwin').template.find((m) => m.id === 'window')?.role).toBe('windowMenu');
  });

  it('grays what this version cannot do, and what the state does not allow', () => {
    const none = (): ReturnType<typeof commandItems> =>
      commandItems(build(NO_STATE, 'darwin').template);
    const items = commandItems(build(state, 'darwin').template);
    for (const id of [
      'export.scad',
      'export.stl',
      'export.glb',
      'anaglyph',
      'anaglyph.swapped',
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
      'style.stl',
      // deferred part 6
      'import.ps3d',
      'import.scad',
      'convertGrid',
    ]) {
      expect(items.get(id)?.enabled, id).toBe(true);
    }
    // the STL-like style is the fourth style (deferred part 5)
    const stl = commandItems(build({ ...state, style: 'stl' }, 'darwin').template);
    expect(stl.get('style.stl')?.checked).toBe(true);
    expect(stl.get('style.drawing')?.checked).toBe(false);
    // the STL needs a shape with cells, the animation a solution with its moves shown
    const exports = commandItems(
      build({ ...state, shapes: true, scad: true, animation: true }, 'darwin').template,
    );
    expect(exports.get('export.stl')?.enabled).toBe(true);
    expect(exports.get('export.scad')?.enabled).toBe(true);
    // Puzzlecad only on the brick and sliding grids
    const prisms = commandItems(build({ ...state, shapes: true }, 'darwin').template);
    expect(prisms.get('export.stl')?.enabled).toBe(true);
    expect(prisms.get('export.scad')?.enabled).toBe(false);
    expect(exports.get('export.glb')?.enabled).toBe(true);
    // the imports replace the document: offered with none open, as Open
    expect(none().get('import.ps3d')?.enabled).toBe(true);
    expect(none().get('import.scad')?.enabled).toBe(true);
    expect(none().get('convertGrid')?.enabled).toBe(false);
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
    expect(solving.get('convertGrid')?.enabled).toBe(false);
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
    expect(sent).toContainEqual({ cmd: 'colorView', on: true });
  });

  it('has Manage Cache and Autosave Files under Reload in development, at the foot of View in a release', () => {
    const viewOf = (dev: boolean): MenuItemConstructorOptions[] => {
      const template = menuTemplate(state, {
        platform: 'darwin',
        name: 'ShapeLogic',
        recent: [],
        dev,
        send: () => undefined,
        openHelp: () => undefined,
        clearRecent: () => undefined,
        windows: [],
        focusWindow: () => undefined,
        copyDiagnostics: () => undefined,
      });
      return template.find((m) => m.id === 'view')?.submenu as MenuItemConstructorOptions[];
    };
    const names = (items: MenuItemConstructorOptions[]): string[] =>
      items.slice(-4).map((i) => i.role ?? i.id ?? i.type ?? '');
    expect(names(viewOf(true))).toEqual(['reload', 'cacheFiles', 'logFiles', 'toggleDevTools']);
    expect(names(viewOf(false)).slice(-3)).toEqual(['separator', 'cacheFiles', 'logFiles']);
    const { template, sent } = build(NO_STATE, 'win32');
    const item = commandItems(template).get('cacheFiles');
    expect(item?.label).toBe('Manage Cache and Autosave Files…');
    // without a document too: it is about the disk, not the puzzle
    expect(item?.enabled).toBe(true);
    (item?.click as () => void)();
    expect(sent).toEqual([{ cmd: 'cacheFiles' }]);
    expect(isAppCommand({ cmd: 'cacheFiles' })).toBe(true);
    // the Log File Manager below it, also without a document
    const log = commandItems(template).get('logFiles');
    expect(log?.label).toBe('Log File Manager…');
    expect(log?.enabled).toBe(true);
    (log?.click as () => void)();
    expect(sent).toEqual([{ cmd: 'cacheFiles' }, { cmd: 'logFiles' }]);
    expect(isAppCommand({ cmd: 'logFiles' })).toBe(true);
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
  it('check every field of a menu state, and take every style and theme (cleanup item 12.3)', () => {
    // NO_STATE is a MenuState (the compiler asks for every key): each of its
    // switches given a number is refused, so none goes unchecked
    const switches = Object.entries(NO_STATE).filter(([, v]) => typeof v === 'boolean');
    expect(switches.map(([k]) => k)).toContain('scad');
    for (const [k] of switches) {
      expect(isMenuState({ ...NO_STATE, [k]: 1 }), k).toBe(false);
    }
    for (const style of MESH_STYLE_VALUES) {
      expect(isMenuState({ ...NO_STATE, style }), style).toBe(true);
      expect(isAppCommand({ cmd: 'style', style }), style).toBe(true);
    }
    for (const theme of THEMES) {
      expect(isMenuState({ ...NO_STATE, theme }), theme).toBe(true);
    }
    for (const kind of [...OPEN_KINDS, ...EXPORT_KINDS]) {
      expect(isFileKind(kind), kind).toBe(true);
    }
  });

  it('takes the commands and states it knows, nothing else', () => {
    expect(isAppCommand({ cmd: 'save' })).toBe(true);
    expect(isAppCommand({ cmd: 'openPath', path: '' })).toBe(false);
    expect(isAppCommand({ cmd: 'style', style: 'stl' })).toBe(true);
    expect(isAppCommand({ cmd: 'style', style: 'anaglyph' })).toBe(false);
    expect(isMenuState({ ...NO_STATE, style: 'stl' })).toBe(true);
    expect(isMenuState({ ...NO_STATE, style: 'gouraud' })).toBe(false);
    expect(isAppCommand({ cmd: 'rm' })).toBe(false);
    expect(isAppCommand(null)).toBe(false);
    expect(isMenuState(NO_STATE)).toBe(true);
    expect(isMenuState({ ...NO_STATE, undo: 3 })).toBe(false);
    expect(isMenuState({ ...NO_STATE, keys: { new: 1 } })).toBe(false);
    expect(isMenuState({ ...NO_STATE, animation: 1 })).toBe(false);
    expect(isAppCommand({ cmd: 'exportGlb' })).toBe(true);
    expect(isFileKind('glb')).toBe(true);
    // the moves of a sliding puzzle played by hand are an export (decision 0039)
    expect(isFileKind('moves')).toBe(true);
    expect(isExportKind('moves')).toBe(true);
    expect(isFileKind('exe')).toBe(false);
    expect(isExportKind('puzzle')).toBe(false);
    expect(isRescueInfo({ path: null, name: 'x', revision: 1, time: 2 })).toBe(true);
    expect(isRescueInfo({ path: '', name: 'x', revision: 1, time: 2 })).toBe(false);
    expect(isCacheLink({ folders: ['/c/slide-1'], path: null, name: 'x', problem: 'P1' })).toBe(
      true,
    );
    expect(isCacheLink({ folders: [''], path: null, name: 'x', problem: 'P1' })).toBe(false);
    expect(isCacheLink({ folders: ['/a'], path: '', name: 'x', problem: 'P1' })).toBe(false);
    expect(isCacheLink({ folders: '/a', path: null, name: 'x', problem: 'P1' })).toBe(false);
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
        updates: 'no',
        cube: 'yes',
        rot: 'Top Up',
        zAxis: 'Z Up',
        gridPlane: 'XW',
        gone: 1,
      }),
    ).toEqual({ theme: 'Dark', undo: 100, tips: false, rot: 'Top Up', zAxis: 'Z Up' });
    // the rotation methods' names before Free Spin and Top Up: back to the default
    expect(validPrefs({ rot: 'Arc-ball' })).toEqual({});
    // Z Up's name before, and the Z axis directions dropped since: back to the default
    expect(validPrefs({ zAxis: 'Z Up + Y Away' })).toEqual({ zAxis: 'Z Up' });
    expect(validPrefs({ zAxis: 'Z Away' })).toEqual({});
    expect(validPrefs({ zAxis: 'Z Up + Y Toward' })).toEqual({});
    // a new shape's size: whole, 1 to NEW_SIZE_MAX
    expect(validPrefs({ newX: 0, newY: 7, newZ: NEW_SIZE_MAX + 1 })).toEqual({ newY: 7 });
    expect(validPrefs({ slideX: 2.5, slideY: 8, slideZ: 0 })).toEqual({ slideY: 8 });
    // the label length per voxel: 1 to 14, or 0 for unlimited; the old
    // labelChars (a length for every piece) is not read
    expect(validPrefs({ labelCharsPerVoxel: 0 })).toEqual({ labelCharsPerVoxel: 0 });
    expect(validPrefs({ labelCharsPerVoxel: 14 })).toEqual({ labelCharsPerVoxel: 14 });
    expect(validPrefs({ labelCharsPerVoxel: 15 })).toEqual({});
    expect(validPrefs({ labelCharsPerVoxel: 3.5 })).toEqual({});
    expect(validPrefs({ labelChars: 8 })).toEqual({});
    // the side cards: Static or Variable, widths whole, SIDE_MIN to SIDE_MAX
    expect(validPrefs({ sidebar: 'Variable' })).toEqual({ sidebar: 'Variable' });
    expect(validPrefs({ sidebar: 'Wide' })).toEqual({});
    expect(validPrefs({ sideL: SIDE_MIN - 1, sideR: SIDE_MAX })).toEqual({ sideR: SIDE_MAX });
    expect(validPrefs({ sideL: 400.5, sideR: SIDE_MAX + 1 })).toEqual({});
    // the font size: Small, Medium or Large
    expect(validPrefs({ fontSize: 'Large' })).toEqual({ fontSize: 'Large' });
    expect(validPrefs({ fontSize: 'Huge' })).toEqual({});
    // a sliding board starts 6 by 6 by 1
    expect([DEFAULTS.slideX, DEFAULTS.slideY, DEFAULTS.slideZ]).toEqual([6, 6, 1]);
    expect(validPrefs({ ...DEFAULTS })).toEqual(DEFAULTS);
  });

  it('every default passes its own check, and the values past a range do not', () => {
    for (const k of Object.keys(PREFS) as PrefKey[]) {
      const p = PREFS[k];
      expect(validPref(k, DEFAULTS[k]), k).toBe(true);
      if (p.kind === 'int') {
        expect(p.min <= p.def && p.def <= p.max, k).toBe(true);
        expect(validPref(k, p.min - 1), k).toBe(false);
        expect(validPref(k, p.max + 1), k).toBe(false);
        expect(clampPref(k as Parameters<typeof clampPref>[0], p.max + 10), k).toBe(p.max);
      }
    }
  });

  it('maps the engine’s settings once, with the engine’s bounds', () => {
    expect(Object.fromEntries(ENGINE_PREFS)).toEqual({
      undo: 'undoDepth',
      threads: 'threads',
      memGB: 'searchMemoryGB',
      slideDiskGB: 'slideDiskGB',
      panexDiskGB: 'panexDiskGB',
    });
    for (const [k, e] of ENGINE_PREFS) {
      expect(PREFS[k].max, k).toBe(LIMITS.settings[e]);
    }
    expect(engineSettingsOf({ undo: 50, tips: false, memGB: 8 })).toEqual({
      undoDepth: 50,
      searchMemoryGB: 8,
    });
    expect(
      prefsOfEngine({
        undoDepth: 30,
        threads: 2,
        solver: 'classic',
        slideFolder: '',
        slideDiskGB: 10,
        panexFolder: '',
        panexDiskGB: 20,
        searchMemoryGB: 4,
        log: 'off',
      }),
    ).toEqual({ undo: 30, threads: 2, memGB: 4, slideDiskGB: 10, panexDiskGB: 20 });
  });

  it('takes the Search Options’ defaults from the boxes’ table', () => {
    for (const b of SETTING_BOXES) {
      expect(DEFAULTS[optKey(b.name)], b.name).toBe(DEFAULT_OPTIONS[b.name]);
    }
    for (const b of BOXES) {
      expect(DEFAULT_OPTIONS[b.name], b.name).toBe(b.def);
    }
    expect(
      Object.keys(DEFAULTS)
        .filter((k) => /^opt[A-Z]/.test(k))
        .sort(),
    ).toEqual([...SETTING_BOXES.map((b) => optKey(b.name)), 'optKeepAtMost', 'optKeepEach'].sort());
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
    dir = mkdtempSync(join(tmpdir(), 'sl-export-'));
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
    // the file may be written, its folder not: no temporary file beside it
    chmodSync(d, 0o555);
    await expect(writeFileSafely(f, new Uint8Array([1]))).rejects.toThrow(
      'could not write e.png (EACCES)',
    );
    chmodSync(d, 0o755);
    expect(readFileSync(f, 'utf8')).toBe('old');
    expect(readdirSync(d)).toEqual(['e.png']);
  });

  it('never writes through a link at the old temporary file’s name, and leaves it', async () => {
    const d = fresh();
    const victim = join(d, 'victim.txt');
    writeFileSync(victim, 'untouched');
    const f = join(d, 'f.png');
    symlinkSync('victim.txt', `${f}.tmp`);
    await writeFileSafely(f, new TextEncoder().encode('PNG'));
    expect(readFileSync(victim, 'utf8')).toBe('untouched');
    expect(lstatSync(f).isSymbolicLink()).toBe(false);
    expect(readFileSync(f, 'utf8')).toBe('PNG');
    expect(readdirSync(d).sort()).toEqual(['f.png', 'f.png.tmp', 'victim.txt']);
  });

  it('lets two exports to one file at once both finish, the file one of them whole (cleanup item 7.4)', async () => {
    const d = fresh();
    const f = join(d, 'g.stl');
    const a = new Uint8Array(8_000_000).fill(65);
    const b = new Uint8Array(6_000_000).fill(66);
    for (let round = 0; round < 5; round++) {
      await Promise.all([writeFileSafely(f, a), writeFileSafely(f, b)]);
      const got = readFileSync(f);
      expect([a.length, b.length]).toContain(got.length);
      expect(got.every((x) => x === (got.length === a.length ? 65 : 66))).toBe(true);
      expect(readdirSync(d)).toEqual(['g.stl']);
    }
  });

  it('lets a window write only a path its dialog gave for an export, once, with the kind’s extension', () => {
    const ext = {
      image: '.png',
      vector: '.svg',
      stl: '.stl',
      glb: '.glb',
      scad: '.scad',
      moves: '.slmoves',
    } as const;
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

  it('lets a window write a solution’s pieces only into the folder its dialog gave last', () => {
    const folders = new FolderGrants<object>();
    const a = {};
    const b = {};
    expect(folders.has(a, '/x')).toBe(false);
    folders.grant(a, '/x');
    expect(folders.has(a, '/x')).toBe(true);
    expect(folders.has(b, '/x')).toBe(false);
    folders.grant(a, '/y');
    expect(folders.has(a, '/x')).toBe(false);
    expect(folders.has(a, '/y')).toBe(true);
  });

  it('takes only the names the engine gives a solution’s pieces', () => {
    for (const ok of ['Frame1.stl', 'S4.stl', 'Frame1-S4.stl', '__l.stl', 'a_b_c_d.stl']) {
      expect(isPieceFileName(ok), ok).toBe(true);
    }
    for (const bad of [
      '../x.stl',
      'a/b.stl',
      'a\\b.stl',
      '.stl',
      'x.STL',
      'x.xmpuzzle',
      'x.stl.png',
      'a b.stl',
      `${'x'.repeat(252)}.stl`,
    ]) {
      expect(isPieceFileName(bad), bad).toBe(false);
    }
  });
});

describe('a file replaced (replace-file.ts, cleanup item 7.4)', () => {
  it('lets two writes to one target at once both settle, the target one of them whole', async () => {
    const d = dir();
    const f = join(d, 'state.json');
    const a = 'a'.repeat(5_000_000);
    const b = 'b'.repeat(3_000_000);
    for (let round = 0; round < 5; round++) {
      const results = await Promise.allSettled([replaceFile(f, a), replaceFile(f, b)]);
      expect(results.map((r) => r.status)).toEqual(['fulfilled', 'fulfilled']);
      const got = readFileSync(f, 'utf8');
      expect([a, b]).toContain(got);
      expect(readdirSync(d)).toEqual(['state.json']);
    }
  });

  it('names each write’s temporary file its own, and knows them', () => {
    const one = tempName('/x/state.json');
    expect(one).not.toBe(tempName('/x/state.json'));
    expect(one).toMatch(/^\/x\/state\.json\.\d+\.[0-9a-f]{12}\.tmp$/);
    const name = one.slice('/x/'.length);
    expect(isTempOf(name, 'state.json')).toBe(true);
    expect(isTempOf(name, 'state')).toBe(false);
    expect(isTempOf('state.json.tmp', 'state.json')).toBe(false);
  });

  it('removes its temporary file and leaves the target when the rename fails', async () => {
    const d = dir();
    // a folder at the target's name: the rename cannot replace it
    const f = join(d, 'in-the-way');
    mkdirSync(join(f, 'inside'), { recursive: true });
    await expect(replaceFile(f, 'x')).rejects.toMatchObject({ syscall: 'rename' });
    expect(() => {
      replaceFileSync(f, 'x');
    }).toThrow();
    expect(readdirSync(d)).toEqual(['in-the-way']);
  });

  it('replaces synchronously too, leaving no temporary file', () => {
    const d = dir();
    const f = join(d, 'shapelogic.log');
    writeFileSync(f, 'old');
    replaceFileSync(f, new Uint8Array([110, 101, 119]));
    expect(readFileSync(f, 'utf8')).toBe('new');
    expect(readdirSync(d)).toEqual(['shapelogic.log']);
  });
});
