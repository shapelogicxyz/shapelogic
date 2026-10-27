// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one keyboard table (shell/keys.ts): every row of docs/webui-spec.md
// section 6 is in it, no press does two things, and the presses match as
// the mockup's handler took them (Ctrl or Cmd, Shift free unless named).

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

import {
  accelerator,
  actionOf,
  IN_TEXT,
  KEY_MAP,
  keyGroups,
  matches,
} from '../../../src/renderer/shell/keys.ts';

const press = (
  key: string,
  mods: Partial<Record<'ctrl' | 'meta' | 'shift' | 'alt', boolean>> = {},
  code = '',
) => ({
  key,
  code,
  ctrlKey: mods.ctrl ?? false,
  metaKey: mods.meta ?? false,
  shiftKey: mods.shift ?? false,
  altKey: mods.alt ?? false,
});

// the keys column of section 6, split into the presses it names
function specKeys(): { where: string; keys: string[] }[] {
  const md = readFileSync(resolve(import.meta.dirname, '../../../../docs/webui-spec.md'), 'utf8');
  const sec = md.slice(md.indexOf('## 6.'), md.indexOf('## 7.'));
  let where = '';
  const out: { where: string; keys: string[] }[] = [];
  for (const line of sec.split('\n')) {
    const cells = line.split('|').map((c) => c.trim());
    if (cells.length < 4 || cells[1] === 'Where' || /^-+$/.test(cells[1] ?? '')) {
      continue;
    }
    where = cells[1] === '' ? where : (cells[1] ?? '');
    out.push({
      where,
      keys: (cells[2] ?? '').split(/\s*[/,]\s*|\s+or\s+/).filter((k) => k !== ''),
    });
  }
  return out;
}

describe('the keyboard map', () => {
  it('has a row for every key of docs/webui-spec.md section 6', () => {
    const rows = specKeys();
    expect(rows.length).toBeGreaterThan(25);
    const text = KEY_MAP.map((r) => `${r.group} ${r.keys} ${r.does}`.toLowerCase()).join('\n');
    const missing: string[] = [];
    for (const { where, keys } of rows) {
      for (const k of keys) {
        // a key's words, e.g. "Ctrl+[" or "middle-drag" or "double-click view cube"
        const words = k
          .toLowerCase()
          .replace(/\(.*?\)/g, '')
          .split(/[\s+]+/)
          .filter((w) => w.length > 0 && !['click', 'left-drag', 'drag'].includes(w) && w !== '…');
        if (!words.every((w) => text.includes(w.replace(/^left-/, '')))) {
          missing.push(`${where}: ${k}`);
        }
      }
    }
    expect(missing).toEqual([]);
  });

  it('gives each press one action', () => {
    const seen = new Map<string, string>();
    for (const row of KEY_MAP) {
      for (const { chord, action } of row.chords ?? []) {
        const k = JSON.stringify(chord);
        expect(seen.get(k), k).toBeUndefined();
        seen.set(k, action);
      }
    }
    expect(seen.size).toBe(42);
  });

  it('matches Ctrl or Cmd, by key or by physical key, Shift free', () => {
    expect(actionOf(press('2', { ctrl: true }))).toBe('workspace.puzzle');
    expect(actionOf(press('2', { meta: true }))).toBe('workspace.puzzle');
    expect(actionOf(press('2'))).toBe('tool.variable');
    expect(actionOf(press('2', { alt: true }))).toBeNull();
    expect(actionOf(press('ü', { ctrl: true }, 'BracketLeft'))).toBe('card.left');
    expect(actionOf(press(' ', { ctrl: true }, 'Space'))).toBeNull();
    expect(actionOf(press('O', { shift: true }))).toBe('view.orbit');
    expect(actionOf(press('o', { alt: true }))).toBeNull();
    expect(actionOf(press('f', { ctrl: true }))).toBeNull();
    expect(actionOf(press('Escape'))).toBe('escape');
    expect(matches({ key: 'x', shift: true }, press('x'))).toBe(false);
  });

  it('has the Entities rows and undo', () => {
    expect(actionOf(press('z', { ctrl: true }))).toBe('undo');
    expect(actionOf(press('z', { meta: true }))).toBe('undo');
    expect(actionOf(press('Z', { ctrl: true, shift: true }))).toBe('redo');
    expect(actionOf(press('z'))).toBeNull();
    expect(actionOf(press('4'))).toBe('tool.paint');
    expect(actionOf(press('E', { shift: true }))).toBe('edit3d');
    expect(actionOf(press('PageUp'))).toBe('page.up');
    expect(actionOf(press('PageDown'))).toBe('page.down');
    expect(actionOf(press('ArrowUp'))).toBe('prev');
    expect(actionOf(press('ArrowUp', { alt: true }))).toBe('shape.earlier');
    expect(actionOf(press('ArrowDown', { alt: true }))).toBe('shape.later');
    expect(actionOf(press('F2'))).toBe('shape.rename');
    expect(actionOf(press('Enter'))).toBe('activate');
    expect(actionOf(press('Delete'))).toBe('delete');
    expect(actionOf(press('d', { ctrl: true }, 'KeyD'))).toBe('shape.duplicate');
    expect(actionOf(press('d'))).toBeNull();
  });

  it('has the Puzzle rows: + and − (= and _ too), Del the one delete', () => {
    expect(actionOf(press('+', { shift: true }, 'Equal'))).toBe('piece.more');
    expect(actionOf(press('=', {}, 'Equal'))).toBe('piece.more');
    expect(actionOf(press('+', {}, 'NumpadAdd'))).toBe('piece.more');
    expect(actionOf(press('-', {}, 'Minus'))).toBe('piece.fewer');
    expect(actionOf(press('_', { shift: true }, 'Minus'))).toBe('piece.fewer');
    expect(actionOf(press('-', {}, 'NumpadSubtract'))).toBe('piece.fewer');
    expect(actionOf(press('-', { ctrl: true }, 'Minus'))).toBeNull();
    expect(actionOf(press('Delete'))).toBe('delete');
  });

  it('has the Solver rows: Space and Enter play, the arrows step and jump, Home and End', () => {
    expect(actionOf(press(' ', {}, 'Space'))).toBe('activate');
    expect(actionOf(press('Enter'))).toBe('activate');
    expect(actionOf(press('ArrowLeft'))).toBe('move.back');
    expect(actionOf(press('ArrowRight'))).toBe('move.forward');
    expect(actionOf(press('ArrowLeft', { shift: true }))).toBe('move.start');
    expect(actionOf(press('ArrowRight', { shift: true }))).toBe('move.end');
    expect(actionOf(press('ArrowLeft', { alt: true }))).toBeNull();
    // the keys the Entities rows and the view hold: one action each
    expect(actionOf(press('ArrowUp'))).toBe('prev');
    expect(actionOf(press('ArrowDown', { shift: true }))).toBe('next');
    expect(actionOf(press('Home'))).toBe('home');
    expect(actionOf(press('End'))).toBe('end');
    expect(actionOf(press('PageUp'))).toBe('page.up');
    expect(actionOf(press('PageDown'))).toBe('page.down');
    expect(actionOf(press('Delete'))).toBe('delete');
    expect(actionOf(press('Escape'))).toBe('escape');
  });

  it('has the File menu’s keys, which act in a text field too', () => {
    expect(actionOf(press('n', { ctrl: true }, 'KeyN'))).toBe('file.new');
    expect(actionOf(press('o', { meta: true }, 'KeyO'))).toBe('file.open');
    expect(actionOf(press('s', { ctrl: true }, 'KeyS'))).toBe('file.save');
    expect(actionOf(press('S', { ctrl: true, shift: true }, 'KeyS'))).toBe('file.saveAs');
    expect(actionOf(press('w', { ctrl: true }, 'KeyW'))).toBe('file.close');
    expect(actionOf(press('s'))).toBeNull();
    expect([...IN_TEXT].sort()).toEqual([
      'file.close',
      'file.new',
      'file.open',
      'file.save',
      'file.saveAs',
    ]);
  });

  it('writes the menus’ keys as Electron accelerators', () => {
    expect(accelerator('file.new')).toBe('CmdOrCtrl+N');
    expect(accelerator('file.saveAs')).toBe('Shift+CmdOrCtrl+S');
    expect(accelerator('undo')).toBe('CmdOrCtrl+Z');
    expect(accelerator('redo')).toBe('Shift+CmdOrCtrl+Z');
    expect(accelerator('settings')).toBe('CmdOrCtrl+,');
    expect(accelerator('help.keys')).toBe('F1');
    expect(accelerator('focus3d')).toBe(null);
    expect(accelerator('file.close')).toBe('CmdOrCtrl+W');
    expect(accelerator('home')).toBe(null);
  });

  it('prints in the mockup’s groups', () => {
    expect(keyGroups().map((g) => `${g.group}${g.sub === undefined ? '' : ` (${g.sub})`}`)).toEqual(
      [
        'Everywhere',
        '3D view (every workspace)',
        'Shapes (drawing)',
        'Shapes (shapes list)',
        'Puzzle',
        'Solver (focus on the solutions list, solution slider or move seekbar)',
        'Explorers (Solver; focus in the explorer below the 3D view)',
        'Dialogs and menus',
      ],
    );
  });
});
