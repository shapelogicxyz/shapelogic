// SPDX-License-Identifier: GPL-3.0-or-later
//
// The application's menus (stage 6), Electron's, built from the menu state
// the renderer sends. From the original's mainmenu.cpp (its macOS table:
// File with Import and Export submenus and Close, Edit, View with the
// display modes and render styles, Help; About and Settings in the
// application menu on macOS, at the foot of Help and Edit elsewhere) and
// the mockup's placeholders (File, Toggle 3D, Export, Status, Edit comment,
// Help, About). What this version cannot do is shown greyed: the
// anaglyph modes. The keys shown
// are the one keyboard table's (renderer/shell/keys.ts, sent in the
// state): the menus show them and do not register them, so a key press
// reaches the page and is handled once, by the table. Free of Electron but
// for its types: index.ts builds the menu from this template.

import type { MenuItemConstructorOptions } from 'electron';

import type { AppCommand, AppCommandName, HelpLink, MenuState } from '../engine/app-bridge.ts';

/** What the menus need besides the state. */
export interface MenuHost {
  platform: string;
  /** The app's name (macOS's application menu). */
  name: string;
  /** The recent files, the newest first. */
  recent: readonly string[];
  /** In development: Reload and the developer tools. */
  dev: boolean;
  send(command: AppCommand): void;
  openHelp(link: HelpLink): void;
  clearRecent(): void;
}

/** The state before the renderer has sent one: nothing open. */
export const NO_STATE: MenuState = {
  document: false,
  dirty: false,
  path: null,
  undo: null,
  redo: null,
  solving: false,
  paused: false,
  solutions: false,
  shapes: false,
  animation: false,
  colourView: false,
  style: 'drawing',
  focus3d: false,
  keys: {},
};

const SEP: MenuItemConstructorOptions = { type: 'separator' };

/** A path as a menu label: the file's name, then its folder. */
export function recentLabel(path: string): string {
  const parts = path.split(/[\\/]/);
  const name = parts.pop() ?? path;
  const dir = parts.join(path.includes('\\') ? '\\' : '/');
  return dir === '' ? name : `${name} — ${dir}`;
}

/** The menus' template. */
export function menuTemplate(s: MenuState, h: MenuHost): MenuItemConstructorOptions[] {
  const mac = h.platform === 'darwin';
  const doc = s.document;
  const item = (
    id: string,
    label: string,
    command: AppCommand | null,
    enabled = true,
    key?: AppCommandName,
  ): MenuItemConstructorOptions => {
    const accel = key === undefined ? undefined : s.keys[key];
    return {
      id,
      label,
      enabled: command !== null && enabled,
      ...(accel === undefined ? {} : { accelerator: accel, registerAccelerator: false }),
      ...(command === null
        ? {}
        : {
            click: () => {
              h.send(command);
            },
          }),
    };
  };
  const radio = (
    id: string,
    label: string,
    checked: boolean,
    command: AppCommand | null,
  ): MenuItemConstructorOptions => ({
    id,
    label,
    type: 'radio',
    checked,
    enabled: command !== null && doc,
    ...(command === null
      ? {}
      : {
          click: () => {
            h.send(command);
          },
        }),
  });

  const recent: MenuItemConstructorOptions[] =
    h.recent.length === 0
      ? [{ id: 'recent.none', label: 'No recent files', enabled: false }]
      : h.recent.map((path, i) =>
          item(`recent.${i}`, recentLabel(path), { cmd: 'openPath', path }),
        );

  const file: MenuItemConstructorOptions = {
    id: 'file',
    label: '&File',
    submenu: [
      item('new', 'New…', { cmd: 'new' }, true, 'new'),
      item('open', 'Open…', { cmd: 'open' }, true, 'open'),
      {
        id: 'recent',
        label: 'Open Recent',
        submenu: [
          ...recent,
          SEP,
          {
            id: 'recent.clear',
            label: 'Clear Menu',
            enabled: h.recent.length > 0,
            click: () => {
              h.clearRecent();
            },
          },
        ],
      },
      SEP,
      item('save', 'Save', { cmd: 'save' }, doc, 'save'),
      item('saveAs', 'Save As…', { cmd: 'saveAs' }, doc, 'saveAs'),
      SEP,
      {
        id: 'import',
        label: 'Import',
        submenu: [
          item('import.ps3d', 'PuzzleSolver3D…', { cmd: 'importPs3d' }),
          item('import.scad', 'Puzzlecad (OpenSCAD)…', { cmd: 'importScad' }),
          item('importPaused', 'Paused Solver State…', { cmd: 'importPaused' }),
        ],
      },
      {
        id: 'export',
        label: 'Export',
        submenu: [
          item('export.scad', 'Puzzlecad (OpenSCAD)…', { cmd: 'exportScad' }, doc && s.shapes),
          item('export.image', 'Image…', { cmd: 'exportImage' }, doc),
          item('export.vector', 'Vector Image…', { cmd: 'exportVector' }, doc),
          item('export.stl', 'STL…', { cmd: 'exportStl' }, doc && s.shapes),
          item('export.glb', 'Solution Animation (GLB)…', { cmd: 'exportGlb' }, doc && s.animation),
          item(
            'exportPaused',
            'Paused Solver State…',
            { cmd: 'exportPaused' },
            doc && s.paused && !s.solving,
          ),
        ],
      },
      SEP,
      item('close', 'Close', { cmd: 'close' }, true, 'close'),
      ...(mac ? [] : [SEP, { id: 'quit', label: 'E&xit', role: 'quit' as const }]),
    ],
  };

  const edit: MenuItemConstructorOptions = {
    id: 'edit',
    label: '&Edit',
    submenu: [
      item(
        'undo',
        s.undo === null ? 'Undo' : `Undo ${s.undo}`,
        { cmd: 'undo' },
        doc && s.undo !== null,
        'undo',
      ),
      item(
        'redo',
        s.redo === null ? 'Redo' : `Redo ${s.redo}`,
        { cmd: 'redo' },
        doc && s.redo !== null,
        'redo',
      ),
      SEP,
      // the text fields' own editing
      { role: 'cut' },
      { role: 'copy' },
      { role: 'paste' },
      { role: 'selectAll' },
      SEP,
      item('notes', 'Notes…', { cmd: 'notes' }, doc),
      item(
        'convertGrid',
        'Convert Brick Grid Type to Other…',
        { cmd: 'convertGrid' },
        doc && !s.solving,
      ),
      item(
        'toShapes',
        'Convert Assemblies to Pieces…',
        { cmd: 'toShapes' },
        doc && s.solutions && !s.solving,
      ),
      ...(mac ? [] : [SEP, item('settings', 'Settings…', { cmd: 'settings' }, true, 'settings')]),
    ],
  };

  const view: MenuItemConstructorOptions = {
    id: 'view',
    label: '&View',
    submenu: [
      item('statusWindow', 'Status Window…', { cmd: 'statusWindow' }, doc),
      {
        id: 'focus3d',
        label: 'Toggle 3D',
        type: 'checkbox',
        checked: s.focus3d,
        ...(s.keys.focus3d === undefined
          ? {}
          : { accelerator: s.keys.focus3d, registerAccelerator: false }),
        click: () => {
          h.send({ cmd: 'focus3d' });
        },
      },
      SEP,
      radio('colourView.off', 'Display Normally with Shape Colour', !s.colourView, {
        cmd: 'colourView',
        on: false,
      }),
      radio('colourView.on', 'Display with Colour Constraint Colours', s.colourView, {
        cmd: 'colourView',
        on: true,
      }),
      radio('anaglyph', 'Display in Anaglyph Mode', false, null),
      radio('anaglyph.swapped', 'Display in Anaglyph Mode with Glasses Swapped', false, null),
      SEP,
      radio('style.drawing', 'Draw Each Voxel Separately', s.style === 'drawing', {
        cmd: 'style',
        style: 'drawing',
      }),
      radio('style.flat', 'Draw Flat Faces with Edges', s.style === 'flat', {
        cmd: 'style',
        style: 'flat',
      }),
      radio('style.wireframe', 'Draw Wire Frames', s.style === 'wireframe', {
        cmd: 'style',
        style: 'wireframe',
      }),
      radio('style.stl', 'Draw Pieces like STL Export', s.style === 'stl', {
        cmd: 'style',
        style: 'stl',
      }),
      ...(h.dev ? [SEP, { role: 'reload' as const }, { role: 'toggleDevTools' as const }] : []),
    ],
  };

  const help: MenuItemConstructorOptions = {
    id: 'help',
    label: '&Help',
    role: 'help',
    submenu: [
      item('keys', 'Keyboard Shortcuts', { cmd: 'keys' }, true, 'keys'),
      SEP,
      {
        id: 'tutorial',
        label: 'Tutorial',
        click: () => {
          h.openHelp('tutorial');
        },
      },
      {
        id: 'guide',
        label: 'ShapeLogic User Guide',
        click: () => {
          h.openHelp('guide');
        },
      },
      ...(mac
        ? []
        : [
            SEP,
            item('cliTool', 'Command Line Tool…', { cmd: 'cliTool' }),
            item('about', `About ${h.name}`, { cmd: 'about' }),
          ]),
    ],
  };

  const appMenu: MenuItemConstructorOptions = {
    id: 'app',
    label: h.name,
    submenu: [
      item('about', `About ${h.name}`, { cmd: 'about' }),
      SEP,
      item('settings', 'Settings…', { cmd: 'settings' }, true, 'settings'),
      item('cliTool', 'Install Command Line Tool…', { cmd: 'cliTool' }),
      SEP,
      { role: 'services' },
      SEP,
      { role: 'hide' },
      { role: 'hideOthers' },
      { role: 'unhide' },
      SEP,
      { role: 'quit' },
    ],
  };

  return mac ? [appMenu, file, edit, view, help] : [file, edit, view, help];
}

/** The ids of the menu items that send a command, with the command (for the tests). */
export function commandItems(
  template: readonly MenuItemConstructorOptions[],
): Map<string, MenuItemConstructorOptions> {
  const out = new Map<string, MenuItemConstructorOptions>();
  const walk = (items: readonly MenuItemConstructorOptions[]): void => {
    for (const i of items) {
      if (i.id !== undefined && i.role === undefined && i.type !== 'separator') {
        out.set(i.id, i);
      }
      if (Array.isArray(i.submenu)) {
        walk(i.submenu);
      }
    }
  };
  walk(template);
  return out;
}
