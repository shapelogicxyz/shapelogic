// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer's CSS custom properties: each one the sheet, the page or a
// module reads (`var(--x)`) is set somewhere (the sheet, an inline style,
// `setProperty`), so no rule falls back on a color nobody defined; and each
// color token of the light theme has its value in both dark blocks (the
// system's dark theme and the one chosen), unless it is listed below as the
// same in both.

import { readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

const RENDERER = resolve(import.meta.dirname, '../../../src/renderer');

/** Every source of the renderer that can read or set a custom property. */
function sources(): { file: string; text: string }[] {
  const out: { file: string; text: string }[] = [];
  const walk = (dir: string): void => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      const p = join(dir, e.name);
      if (e.isDirectory()) {
        walk(p);
      } else if (/\.(ts|css|html)$/.test(e.name)) {
        out.push({ file: p.slice(RENDERER.length + 1), text: readFileSync(p, 'utf8') });
      }
    }
  };
  walk(RENDERER);
  return out;
}

/** The declarations of a block of the sheet, by name. */
function block(css: string, head: string): Map<string, string> {
  const at = css.indexOf(head);
  expect(at, head).toBeGreaterThanOrEqual(0);
  const body = css.slice(at + head.length, css.indexOf('}', at));
  const out = new Map<string, string>();
  for (const m of body.matchAll(/(--[\w-]+)\s*:\s*([^;]+);/g)) {
    out.set(m[1] ?? '', (m[2] ?? '').trim());
  }
  return out;
}

/** Light theme tokens with no dark value of their own, and why. */
const SAME_IN_BOTH: Record<string, string> = {
  '--error': 'var(--danger), which has its dark value',
  '--okFill': 'a banner under white text: the deep green reads in both themes',
  '--errorFill': 'a banner under white text: the deep red reads in both themes',
};

/** The names a module finishes at run time (`var(--a${axis})`), by their start. */
const COMPUTED: Record<string, string[]> = {
  '--a': ['--ax', '--ay', '--az'],
};

describe('CSS custom properties', () => {
  it('are each set where they are read', () => {
    const all = sources();
    const set = new Set<string>();
    for (const { text } of all) {
      for (const m of text.matchAll(/(--[a-zA-Z][\w-]*)\s*:/g)) {
        set.add(m[1] ?? '');
      }
      for (const m of text.matchAll(/setProperty\(\s*'(--[\w-]+)'/g)) {
        set.add(m[1] ?? '');
      }
    }
    const unset: string[] = [];
    for (const { file, text } of all) {
      for (const m of text.matchAll(/var\((--[\w-]+)(\$\{)?/g)) {
        const name = m[1] ?? '';
        // a name finished at run time: each it can become
        const names = m[2] === undefined ? [name] : (COMPUTED[name] ?? [`${name}…`]);
        for (const n of names) {
          if (!set.has(n)) {
            unset.push(`${file}: ${n}`);
          }
        }
      }
    }
    expect(unset).toEqual([]);
  });

  it('of the light theme have their dark values', () => {
    const css = readFileSync(join(RENDERER, 'styles.css'), 'utf8');
    const light = block(css, ':root{');
    const system = block(css, ':root:not([data-theme="light"]){');
    const chosen = block(css, ':root[data-theme="dark"]{');
    // the status colors are among them
    for (const t of ['--ok', '--warn', '--error']) {
      expect(light.has(t), t).toBe(true);
    }
    // the two dark blocks are one theme
    expect([...system]).toEqual([...chosen]);
    const missing = [...light.keys()].filter((t) => !system.has(t) && !(t in SAME_IN_BOTH));
    expect(missing).toEqual([]);
    for (const t of Object.keys(SAME_IN_BOTH)) {
      expect(light.has(t), `${t} is listed but not a token`).toBe(true);
    }
  });
});
