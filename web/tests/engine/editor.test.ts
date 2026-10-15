// SPDX-License-Identifier: GPL-3.0-or-later
//
// The voxel editor's client-side pieces against the real engine: the
// preview's cells of mirror and span (editor-state.ts's expand, leaving out
// the cells the grid does not have) are the cells shape.setCells changes,
// on every grid; the cube grid's face table (edit3d.ts) is grid.neighbour;
// a stroke over several frames is one undo step whose every batch sends
// its document.changed.

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import type { Grid } from '../../src/engine/api.generated.ts';
import { BRICK_FACES, brickNeighbour } from '../../src/renderer/entities/tools.ts';
import { expand, key } from '../../src/renderer/entities/editor-state.ts';
import type { Axis, Cell } from '../../src/renderer/entities/editor-state.ts';
import { Stroke } from '../../src/renderer/entities/stroke.ts';
import { ENGINE, Session } from '../helpers/engine.ts';

describe.skipIf(ENGINE === null)('the voxel editor and the engine', () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('previews mirror and span as the engine applies them, on every grid', async () => {
    const c = s.client;
    const combos: { mirror: Axis[]; span: Axis[] }[] = [
      { mirror: ['x'], span: [] },
      { mirror: ['y', 'z'], span: [] },
      { mirror: [], span: ['x'] },
      { mirror: [], span: ['y', 'z'] },
      { mirror: ['x', 'y', 'z'], span: [] },
      { mirror: ['z'], span: ['x'] },
    ];
    for (const grid of ['bricks', 'prisms', 'spheres', 'rhombic', 'tetraOcta'] as Grid[]) {
      for (const n of [4, 5]) {
        await c.call('document.new', { grid });
        const unit = grid === 'rhombic' ? 5 : grid === 'tetraOcta' ? 3 : 1;
        const shape = (await c.call('shape.create', { size: [n * unit, n * unit, n] })).shape;
        const size = (await c.call('shape.get', { shape })).shape.size;
        const valid = new Set<string>();
        const cells: Cell[] = [];
        for (let z = 0; z < size[2]; z++) {
          for (const g of (await c.call('grid.geometry', { size, layer: z })).cells) {
            valid.add(key(g.at));
            cells.push([g.at[0], g.at[1], g.at[2]]);
          }
        }
        for (const cell of cells.filter((_, i) => i % 7 === 0)) {
          for (const { mirror, span } of combos) {
            const want = expand(cell, size, new Set(mirror), new Set(span))
              .map(key)
              .filter((k) => valid.has(k))
              .sort();
            await c.call('shape.setCells', {
              shape,
              tool: 'fixed',
              cells: [cell],
              ...(mirror.length > 0 ? { mirror } : {}),
              ...(span.length > 0 ? { span } : {}),
            });
            const got = (await c.call('shape.get', { shape })).cells.map((x) => key(x.at)).sort();
            expect(got, `${grid} ${n} ${key(cell)} ${mirror.join('')}/${span.join('')}`).toEqual(
              want,
            );
            await c.call('shape.setBox', {
              shape,
              tool: 'erase',
              from: [0, 0, 0],
              to: [size[0] - 1, size[1] - 1, size[2] - 1],
            });
          }
        }
      }
    }
  });

  it('has the cube grid’s faces as grid.neighbour has them', async () => {
    const c = s.client;
    await c.call('document.new', { grid: 'bricks' });
    const size: Cell = [3, 3, 3];
    for (let z = 0; z < 3; z++) {
      for (let y = 0; y < 3; y++) {
        for (let x = 0; x < 3; x++) {
          for (let face = 0; face < BRICK_FACES.length; face++) {
            const r = await c.call('grid.neighbour', { size, cell: [x, y, z], face });
            expect(brickNeighbour([x, y, z], face, size)).toEqual(r.cell);
          }
        }
      }
    }
  });

  it('makes a stroke over several frames one undo step, each batch a change', async () => {
    const c = s.client;
    await c.call('document.new', { grid: 'bricks' });
    const shape = (await c.call('shape.create', { size: [4, 4, 1] })).shape;
    const before = (await c.call('edit.history')).undo.length;
    const next: { frame: (() => void) | null } = { frame: null };
    const errors: unknown[] = [];
    const stroke = new Stroke(
      c,
      { shape, tool: 'variable', colour: null, mirror: [], span: [] },
      (e) => errors.push(e),
      (f) => {
        next.frame = f;
      },
    );
    const from = s.events.length;
    for (let x = 0; x < 4; x++) {
      stroke.add([[x, 0, 0]]);
      stroke.add([[x, 1, 0]]);
      // the frame comes once the batch before was answered
      await new Promise((r) => setTimeout(r, 20));
      const f = next.frame;
      next.frame = null;
      f?.();
      await new Promise((r) => setTimeout(r, 20));
    }
    const revision = await stroke.end();
    expect(errors).toEqual([]);
    expect(stroke.batches).toBeGreaterThan(1);
    await s.drain();
    const changes = s.events.slice(from).filter((e) => e.event === 'document.changed');
    // a change per batch, and one for the step reaching the undo stack
    // (edit.end), which names nothing: the menus read the history again
    expect(changes.length).toBe(stroke.batches + 1);
    expect(changes.at(-1)?.params.revision).toBe(revision);
    expect(changes.at(-1)?.params.what).toEqual([]);
    const h = await c.call('edit.history');
    expect(h.undo.length).toBe(before + 1);
    expect(h.undo.at(-1)).toBe('Draw variable voxels');
    const got = await c.call('shape.get', { shape });
    expect(got.cells.length).toBe(8);
    expect(got.cells.every((x) => x.state === 'variable')).toBe(true);
    await c.call('edit.undo');
    expect((await c.call('shape.get', { shape })).cells).toEqual([]);
  });
});
