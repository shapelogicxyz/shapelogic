// SPDX-License-Identifier: GPL-3.0-or-later
//
// Picking over tests/data/puzzles: the view turned to the front (the view
// cube's +Z face) and made orthographic, a click on a face of a shape names
// the cell and side the frame's arrays hold for the triangle under it. The
// expected triangle is found in the frame's arrays, taken from an engine of
// the test's own through the client's views (meshArrays), as the front-most
// triangle at a point well inside it.

import { readdirSync } from 'node:fs';

import { expect, test } from '@playwright/test';

import { EngineClient } from '../../src/engine/client.ts';
import { meshArrays } from '../../src/engine/frames.ts';
import type { MeshArrays } from '../../src/engine/frames.ts';
import { NodeTransport } from '../../src/engine/node-transport.ts';
import { HOME, pixelsPerUnit, quatOf, toView } from '../../src/renderer/view3d/camera.ts';
import { gridExtent } from '../../src/renderer/view3d/extent.ts';
import { cellOf, FLAG } from '../../src/renderer/view3d/mesh.ts';
import {
  BUILT,
  camera,
  display,
  ENGINE,
  FIXTURES,
  launch,
  openFixture,
  ROOT,
  selectShape,
  settle,
  shapes,
} from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

const fixtures = readdirSync(FIXTURES)
  .filter((f) => f.endsWith('.xmpuzzle'))
  .sort();

type P3 = [number, number, number];

function corner(a: MeshArrays, t: number, j: number): P3 {
  const v = a.indices[3 * t + j] ?? 0;
  return [a.positions[3 * v] ?? 0, a.positions[3 * v + 1] ?? 0, a.positions[3 * v + 2] ?? 0];
}

/** The z of triangle t at (x, y), or null when (x, y) is not inside it (seen along -z). */
function zAt(a: MeshArrays, t: number, x: number, y: number): number | null {
  const [p, q, r] = [corner(a, t, 0), corner(a, t, 1), corner(a, t, 2)];
  const d = (q[1] - r[1]) * (p[0] - r[0]) + (r[0] - q[0]) * (p[1] - r[1]);
  if (Math.abs(d) < 1e-12) {
    return null;
  }
  const l1 = ((q[1] - r[1]) * (x - r[0]) + (r[0] - q[0]) * (y - r[1])) / d;
  const l2 = ((r[1] - p[1]) * (x - r[0]) + (p[0] - r[0]) * (y - r[1])) / d;
  const l3 = 1 - l1 - l2;
  const e = 1e-9;
  if (l1 < -e || l2 < -e || l3 < -e) {
    return null;
  }
  return l1 * p[2] + l2 * q[2] + l3 * r[2];
}

/**
 * A point (mesh units) and the triangle a ray along -z hits first there: a
 * face towards the viewer, large on screen, unambiguous (no other triangle
 * within 1e-3 in z at the point), near the middle of the view.
 */
function target(
  a: MeshArrays,
  ppu: number,
  centre: [number, number],
  allowed: (x: number, y: number) => boolean,
): { t: number; x: number; y: number } | null {
  const tris = a.flags.length;
  const cands: { t: number; x: number; y: number; z: number; d: number }[] = [];
  for (let t = 0; t < tris; t++) {
    if (
      ((a.flags[t] ?? 0) & FLAG.inside) !== 0 ||
      (a.normals[3 * (a.indices[3 * t] ?? 0) + 2] ?? 0) < 0.3
    ) {
      continue;
    }
    const [p, q, r] = [corner(a, t, 0), corner(a, t, 1), corner(a, t, 2)];
    const area = Math.abs((q[0] - p[0]) * (r[1] - p[1]) - (r[0] - p[0]) * (q[1] - p[1])) / 2;
    if (area * ppu * ppu < 40) {
      continue;
    }
    const x = (p[0] + q[0] + r[0]) / 3;
    const y = (p[1] + q[1] + r[1]) / 3;
    if (!allowed(x, y)) {
      continue;
    }
    cands.push({
      t,
      x,
      y,
      z: (p[2] + q[2] + r[2]) / 3,
      d: Math.hypot(x - centre[0], y - centre[1]),
    });
  }
  cands.sort((u, v) => v.z - u.z || u.d - v.d);
  for (const c of cands.slice(0, 200)) {
    let best = -Infinity;
    let bestT = -1;
    let close = 0;
    const zs: { t: number; z: number }[] = [];
    for (let t = 0; t < tris; t++) {
      if (((a.flags[t] ?? 0) & FLAG.inside) !== 0) {
        continue;
      }
      const z = zAt(a, t, c.x, c.y);
      if (z !== null) {
        zs.push({ t, z });
        if (z > best) {
          best = z;
          bestT = t;
        }
      }
    }
    for (const { z } of zs) {
      if (Math.abs(z - best) < 1e-3) {
        close++;
      }
    }
    if (close === 1 && bestT >= 0) {
      return { t: bestT, x: c.x, y: c.y };
    }
  }
  return null;
}

let a: App;
let engine: EngineClient;
test.beforeAll(async () => {
  const transport = await NodeTransport.start(ENGINE ?? '', { cwd: ROOT });
  engine = new EngineClient(transport);
  await engine.hello();
});
test.afterAll(async () => {
  await engine.call('engine.shutdown');
});
test.beforeEach(async () => {
  a = await launch();
});
test.afterEach(async () => {
  await a.close();
});

test('a click on a face names the cell and side the frame holds, on every fixture', async () => {
  test.setTimeout(300_000);
  const { page } = a;
  await openFixture(page, fixtures[0] ?? '');
  // the front view: the view cube's +Z face, where the cube draws it at the home view
  const cube = await page.locator('#vcube').boundingBox();
  const n = toView(quatOf(HOME), [0, 0, 1]);
  const k = 5.5 / (5.5 - n.z);
  await page.mouse.click((cube?.x ?? 0) + 78 + n.x * 30 * k, (cube?.y ?? 0) + 100 - n.y * 30 * k);
  await settle(page);
  expect((await camera(page)).slice(0, 3)).toEqual([0, 0, 0]);
  await display(page, 'ortho');
  await settle(page);

  const picked: string[] = [];
  let discs = 0;
  for (const f of fixtures) {
    await openFixture(page, f);
    const doc = await engine.call('document.open', { path: `tests/data/puzzles/${f}` });
    // a stacking puzzle's disc is drawn lying flat (the original's tilt) and
    // is not edited by its cells: nothing to pick from the front
    if (doc.grid === 'stacking') {
      discs++;
      continue;
    }
    const box = await page.locator('#cv').boundingBox();
    if (box === null) {
      throw new Error('no view');
    }
    // the shapes with cells, the largest first: the first with a clear target
    const list = doc.shapes
      .filter((s) => s.fixed + s.variable > 0)
      .sort((u, v) => v.fixed + v.variable - (u.fixed + u.variable));
    let done = false;
    for (const s of list.slice(0, 4)) {
      const ext = gridExtent(doc.grid, s.size);
      const ppu = pixelsPerUnit(box.width, box.height, ext, 1);
      const toScreen = (x: number, y: number): [number, number] => [
        box.x + box.width / 2 + (x - ext[0] / 2) * ppu,
        box.y + box.height / 2 - (y - ext[1] / 2) * ppu,
      ];
      // clear of the toolbar, the tag, the view cube and the hint
      const allowed = (x: number, y: number): boolean => {
        const [sx, sy] = toScreen(x, y);
        return (
          sy > box.y + 110 &&
          sy < box.y + box.height - 40 &&
          sx > box.x + 20 &&
          sx < box.x + box.width - 170
        );
      };
      const arrays = meshArrays(await engine.call('shape.mesh', { shape: s.id }));
      const hit = target(arrays, ppu, [ext[0] / 2, ext[1] / 2], allowed);
      if (hit === null) {
        continue;
      }
      expect((await shapes(page)).some((x) => x.id === s.id)).toBe(true);
      await selectShape(page, s.id);
      const [sx, sy] = toScreen(hit.x, hit.y);
      await page.mouse.click(sx, sy);
      const [cx, cy, cz] = cellOf(arrays.voxel[hit.t] ?? 0, s.size);
      const face = arrays.voxelFace[hit.t] ?? -1;
      const want = `Cell ${cx}, ${cy}, ${cz} · ${face < 0 ? 'edge' : `side ${face}`}`;
      await expect(page.locator('#stCursor'), `${f} ${s.label}`).toHaveText(want);
      picked.push(`${f} ${s.label}: ${want}`);
      // away, so the next shape's pick is its own
      await page.mouse.move(2, 2);
      done = true;
      break;
    }
    expect(done, `${f}: no clear face to click`).toBe(true);
  }
  expect(discs).toBe(1);
  expect(picked).toHaveLength(fixtures.length - discs);
});
