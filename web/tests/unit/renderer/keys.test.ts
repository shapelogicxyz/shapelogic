// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one keyboard table (shell/keys.ts): every row of docs/webui-spec.md
// section 6 is in it, no press does two things, and the presses match as
// the mockup's handler took them (Ctrl or Cmd, Shift free unless named).

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

import { actionOf, KEY_MAP, keyGroups, matches } from '../../../src/renderer/shell/keys.ts';

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
    expect(seen.size).toBe(30);
  });

  it('matches Ctrl or Cmd, by key or by physical key, Shift free', () => {
    expect(actionOf(press('2', { ctrl: true }))).toBe('workspace.puzzle');
    expect(actionOf(press('2', { meta: true }))).toBe('workspace.puzzle');
    expect(actionOf(press('2'))).toBe('tool.variable');
    expect(actionOf(press('2', { alt: true }))).toBeNull();
    expect(actionOf(press('ü', { ctrl: true }, 'BracketLeft'))).toBe('card.left');
    expect(actionOf(press(' ', { ctrl: true }, 'Space'))).toBe('focus3d');
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
    expect(actionOf(press('PageUp'))).toBe('layer.next');
    expect(actionOf(press('PageDown'))).toBe('layer.prev');
    expect(actionOf(press('ArrowUp'))).toBe('shape.prev');
    expect(actionOf(press('ArrowUp', { alt: true }))).toBe('shape.earlier');
    expect(actionOf(press('ArrowDown', { alt: true }))).toBe('shape.later');
    expect(actionOf(press('F2'))).toBe('shape.rename');
    expect(actionOf(press('Enter'))).toBe('shape.rename');
    expect(actionOf(press('Delete'))).toBe('delete');
    expect(actionOf(press('d', { ctrl: true }, 'KeyD'))).toBe('shape.duplicate');
    expect(actionOf(press('d'))).toBeNull();
  });

  it('prints in the mockup’s groups', () => {
    expect(keyGroups().map((g) => `${g.group}${g.sub === undefined ? '' : ` (${g.sub})`}`)).toEqual(
      [
        'Everywhere',
        '3D view (every workspace)',
        'Entities (drawing)',
        'Entities (shapes list)',
        'Puzzle',
        'Solver (focus on the solutions list, solution slider or move seekbar)',
        'Dialogs and menus',
      ],
    );
  });
});
