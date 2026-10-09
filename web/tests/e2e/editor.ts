// SPDX-License-Identifier: GPL-3.0-or-later
//
// The voxel editor for the e2e tests: where a cell of the 2D grid is on
// the screen (inside its polygon or circle, not its box: a triangle's box
// centre is often its neighbour's), clicks and drags on cells, the
// workspace's controls by their data attributes, and the engine's own
// answers (shape.get, edit.history) through the page's bridge.

import { expect } from '@playwright/test';
import type { Page } from '@playwright/test';

import { engineCall } from './app.ts';

export type Cell = [number, number, number];

/** A cell as the engine gives it. */
export interface EngineCell {
  at: Cell;
  state: 'fixed' | 'variable';
  colour?: string;
}

/** What shape.get answers. */
export interface ShapeGet {
  shape: {
    id: string;
    label: string;
    name: string;
    size: Cell;
    fixed: number;
    variable: number;
    weight: number;
    colour: string | null;
  };
  cells: EngineCell[];
}

/** The engine's shape (cells sorted). */
export async function shapeGet(page: Page, shape: string): Promise<ShapeGet> {
  const r = await engineCall<ShapeGet>(page, 'shape.get', { shape });
  r.cells.sort((a, b) => a.at[2] - b.at[2] || a.at[1] - b.at[1] || a.at[0] - b.at[0]);
  return r;
}

/** The engine's undo labels. */
export async function history(page: Page): Promise<{ undo: string[]; redo: string[] }> {
  return engineCall(page, 'edit.history');
}

/** The selected shape's id. */
export async function selected(page: Page): Promise<string> {
  const id = await page.locator('#shapeList .row.sel').getAttribute('data-id');
  expect(id).not.toBeNull();
  return id ?? '';
}

/** A point inside the grid's cell at a place (client pixels), or null when the layer shown has none. */
export async function cellPoint(page: Page, at: Cell): Promise<{ x: number; y: number } | null> {
  return page.evaluate((key) => {
    const el = document.querySelector<SVGGraphicsElement>(`#garea .cell[data-at="${key}"]`);
    const svg = el?.ownerSVGElement ?? null;
    if (el === null || svg === null) {
      return null;
    }
    const r = svg.getBoundingClientRect();
    if (el instanceof SVGCircleElement) {
      return { x: r.left + el.cx.baseVal.value, y: r.top + el.cy.baseVal.value };
    }
    const pts = (el.getAttribute('points') ?? '')
      .trim()
      .split(/\s+/)
      .map((p) => p.split(',').map(Number));
    const x = pts.reduce((s, p) => s + (p[0] ?? 0), 0) / pts.length;
    const y = pts.reduce((s, p) => s + (p[1] ?? 0), 0) / pts.length;
    return { x: r.left + x, y: r.top + y };
  }, at.join(','));
}

async function pointOf(page: Page, at: Cell): Promise<{ x: number; y: number }> {
  // the layer's drawing comes a moment after its chip lights
  await expect
    .poll(async () => cellPoint(page, at), { message: `cell ${at.join(',')}` })
    .not.toBeNull();
  const p = await cellPoint(page, at);
  if (p === null) {
    throw new Error(`no cell ${at.join(',')} in the layer shown`);
  }
  return p;
}

/**
 * Waits until the workspace's edits are done (data-busy 0) and the page
 * shows the engine's newest revision.
 */
export async function settled(page: Page): Promise<void> {
  await expect(page.locator('#stage')).toHaveAttribute('data-busy', '0');
  await expect
    .poll(async () => {
      const snap = await engineCall<{ revision: number }>(page, 'document.get');
      return page.evaluate(
        (rev) => document.getElementById('stage')?.dataset.revision === String(rev),
        snap.revision,
      );
    })
    .toBe(true);
}

/** Clicks a cell of the grid (a stroke of one cell), with a button and modifiers. */
export async function clickCell(
  page: Page,
  at: Cell,
  opts: { button?: 'left' | 'right'; modifiers?: ('Shift' | 'Alt' | 'Control' | 'Meta')[] } = {},
): Promise<void> {
  const p = await pointOf(page, at);
  for (const m of opts.modifiers ?? []) {
    await page.keyboard.down(m);
  }
  await page.mouse.click(p.x, p.y, { button: opts.button ?? 'left' });
  for (const m of opts.modifiers ?? []) {
    await page.keyboard.up(m);
  }
  await settled(page);
}

/** Drags across cells, in order (one stroke), the mouse moving in small steps. */
export async function dragCells(
  page: Page,
  cells: Cell[],
  button: 'left' | 'right' = 'left',
): Promise<void> {
  const first = cells[0];
  if (first === undefined) {
    return;
  }
  const p0 = await pointOf(page, first);
  await page.mouse.move(p0.x, p0.y);
  await page.mouse.down({ button });
  for (const c of cells.slice(1)) {
    const p = await pointOf(page, c);
    await page.mouse.move(p.x, p.y, { steps: 4 });
  }
  await page.mouse.up({ button });
  await settled(page);
}

/** Chooses a tool by its button (fixed, variable, erase, paint). */
export async function tool(page: Page, t: string): Promise<void> {
  await page.click(`#toolStrip [data-t="${t}"]`);
  await expect(page.locator(`#toolStrip [data-t="${t}"]`)).toHaveClass(/\bon\b/);
}

/** Shows a layer by its chip. */
export async function layer(page: Page, l: number): Promise<void> {
  await page.click(`#lstrip .lchip[data-l="${l}"]`);
  await expect(page.locator(`#lstrip .lchip[data-l="${l}"]`)).toHaveClass(/\bon\b/);
  await expect(page.locator(`#garea .cell[data-at$=",${l}"]`).first()).toBeAttached();
}

/** The status bar's left text. */
export async function statusText(page: Page): Promise<string> {
  return (await page.locator('#stText').textContent()) ?? '';
}
