// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the explorers rely on, against the real engine: explore.placements
// gives cells in the frame of a solution's placements (every placed piece
// of the first solutions of every fixture, on all five grids, is among its
// piece's placements), so a placement is drawn as the assembled view draws
// a piece; the assembler stepped gives placements in that frame too (its
// first complete set: each piece at one of its placements); the client's
// movement tree is the engine's after Analyse, Add move, a matching move and Prune tree
// (every row's node as explore.node gives it, the pruned ones gone).

import { readdirSync } from 'node:fs';
import { resolve } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { MovementTree } from '../../src/renderer/explore/tree.ts';
import { ENGINE, FIXTURES, ROOT, Session } from '../helpers/engine.ts';

describe.skipIf(ENGINE === null)('the explorers and the engine', () => {
  let s: Session;
  beforeEach(async () => {
    s = await Session.start();
  });
  afterEach(async () => {
    await s.end();
    expect(s.problems).toEqual([]);
  });

  it('places in the frame of the solutions on every grid', async () => {
    const c = s.client;
    const grids = new Set<string>();
    let found = 0;
    const files = readdirSync(resolve(ROOT, FIXTURES)).filter((f) => f.endsWith('.xmpuzzle'));
    for (const f of files.sort()) {
      const snap = await c.call('document.open', { path: `${FIXTURES}/${f}` });
      if (snap.grid === 'sliding' || snap.grid === 'stacking') {
        continue;
      }
      for (const prob of snap.problems) {
        const page = await c.call('solution.page', { problem: prob.id, limit: 2 });
        if (page.rows.length === 0) {
          continue;
        }
        const ax = await c.call('explore.openAssembly', { problem: prob.id });
        expect(ax.placementsSupported).toBe(true);
        const lists = [];
        for (let piece = 0; piece < ax.pieces; piece++) {
          lists.push((await c.call('explore.placements', { handle: ax.handle, piece })).placements);
        }
        for (const row of page.rows) {
          const got = await c.call('solution.get', { problem: prob.id, solution: row.id });
          for (const pl of got.placements.filter((q) => q.placed)) {
            const among = (lists[pl.piece] ?? []).some(
              (q) => q.transformation === pl.transformation && q.at.every((v, k) => v === pl.at[k]),
            );
            expect(among, `${f} ${prob.id} ${row.id} piece ${String(pl.piece)}`).toBe(true);
            found++;
          }
        }
        await c.call('explore.close', { handle: ax.handle });
        grids.add(snap.grid);
      }
    }
    expect([...grids].sort()).toEqual(['bricks', 'prisms', 'rhombic', 'spheres', 'tetraOcta']);
    expect(found).toBeGreaterThan(200);
  });

  it('steps to a complete set in the placements’ frame', async () => {
    const c = s.client;
    const snap = await c.call('document.open', { path: `${FIXTURES}/Bermuda.xmpuzzle` });
    const p = snap.problems[0]?.id ?? 'p0';
    const lists = await c.call('explore.openAssembly', { problem: p });
    const ax = await c.call('explore.openAssembly', { problem: p });
    expect(ax.finished).toBe(0);
    let full = null;
    let last = 0;
    for (let n = 0; n < 200_000 && full === null; n++) {
      const r = await c.call('explore.step', { handle: ax.handle });
      last = r.finished;
      if (r.placements.every((q) => q.placed)) {
        full = r.placements;
      }
    }
    expect(full).not.toBeNull();
    expect(last).toBeGreaterThan(0);
    // every piece of the set where the placement browser has one of its placements
    for (const q of full ?? []) {
      const pl = (await c.call('explore.placements', { handle: lists.handle, piece: q.piece }))
        .placements;
      expect(
        pl.some(
          (x) => x.transformation === q.transformation && x.at.every((v, k) => v === q.at[k]),
        ),
        `piece ${String(q.piece)}`,
      ).toBe(true);
    }
    await c.call('explore.close', { handle: ax.handle });
    await c.call('explore.close', { handle: lists.handle });
  });

  it('keeps the movement tree as the engine has it', async () => {
    const c = s.client;
    const snap = await c.call('document.open', { path: `${FIXTURES}/CubeInCage.xmpuzzle` });
    const p = snap.problems[0]?.id ?? 'p0';
    const row = (await c.call('solution.page', { problem: p, limit: 1 })).rows[0];
    if (row === undefined) {
      throw new Error('no solution');
    }
    const mx = await c.call('explore.openMovements', { problem: p, solution: row.id });
    const h = mx.handle;
    const tree = new MovementTree(mx.root);
    const same = async (): Promise<void> => {
      for (const r of tree.rows()) {
        expect(await c.call('explore.node', { handle: h, node: r.node.node })).toEqual(r.node);
      }
    };
    const root = mx.root.node;
    tree.put((await c.call('explore.analyse', { handle: h, node: root })).nodes);
    await same();
    const first = tree.get(root)?.children[0] ?? -1;
    tree.put((await c.call('explore.analyse', { handle: h, node: first })).nodes);
    const piece = mx.root.pieces[0]?.piece ?? 0;
    const added = await c.call('explore.addMove', {
      handle: h,
      node: root,
      pieces: [piece],
      direction: 1,
      reverse: true,
    });
    tree.put([added]);
    let matched = null;
    for (const v of mx.vectors) {
      for (const sign of [1, -1]) {
        if (matched === null) {
          try {
            matched = await c.call('explore.findMatching', {
              handle: h,
              node: first,
              piece,
              delta: [sign * v[0], sign * v[1], sign * v[2]],
            });
          } catch {
            // that way is blocked
          }
        }
      }
    }
    if (matched !== null) {
      tree.put([matched]);
    }
    // the tree put by the replies alone is the engine's
    await same();
    const deep = tree.get(first)?.children[0] ?? first;
    await c.call('explore.prune', { handle: h, node: deep });
    const before = tree.rows().map((r) => r.node.node);
    tree.prune(deep);
    await same();
    for (const id of before.filter((n) => tree.get(n) === undefined)) {
      await expect(c.call('explore.node', { handle: h, node: id })).rejects.toMatchObject({
        errc: 'not_found',
      });
    }
    expect(tree.rows().map((r) => r.node.node)).toEqual(tree.path(deep));
    await c.call('explore.close', { handle: h });
  });
});
