// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view draws every shape of a fixture of each grid (tests/helpers/
// fixtures.ts) in the styles made from the cells, and one shape of each
// grid in each look (drawing, wireframe, flat, the
// STL-like style of deferred part 5, unlit, orthographic, the colour view)
// as reference screenshots
// (tests/e2e/__screenshots__, compared with a tolerance; recorded with
// `npx playwright test --update-snapshots` after a look beside the
// original GUI, docs/handoff/web-1b.md).

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { SWEEP } from '../helpers/fixtures.ts';
import {
  BUILT,
  display,
  ENGINE,
  engineCall,
  launch,
  openFixture,
  selectShape,
  setting3d,
  settle,
  shapes,
  style,
} from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

let a: App;
const errors: string[] = [];
test.beforeEach(async () => {
  a = await launch();
  errors.length = 0;
  a.page.on('pageerror', (e) => errors.push(e.message));
  a.page.on('console', (m) => {
    if (m.type() === 'error') {
      errors.push(m.text());
    }
  });
});
test.afterEach(async () => {
  await a.close();
  expect(errors).toEqual([]);
});

const fixtures = SWEEP.map((f) => f.file);

/** The triangles the last render drew. */
async function drawn(page: Page): Promise<number> {
  return Number(await page.getAttribute('#vp', 'data-triangles'));
}

/** The voxels the status bar gives the shape shown. */
async function voxels(page: Page): Promise<number> {
  const t = (await page.textContent('#stText')) ?? '';
  return Number(/has (\d+) voxels/.exec(t)?.[1] ?? 'NaN');
}

// every shape the engine holds, in each of the four styles made from the
// cells (counted from the engine, so a fixture added to the list is counted
// too); the uniform one is the uniform mesher (decision 0022); a test a
// style, so the four run side by side
for (const s of ['drawing', 'wireframe', 'flat', 'uniform'] as const) {
  test(`every shape of a fixture of each grid is drawn in the ${s} style`, async () => {
    test.setTimeout(300_000);
    const { page } = a;
    let checked = 0;
    let want = 0;
    for (const [i, f] of fixtures.entries()) {
      await openFixture(page, f);
      if (i === 0 && s !== 'drawing') {
        await style(page, s);
      }
      want += (await engineCall<{ shapes: unknown[] }>(page, 'shape.list')).shapes.length;
      // the style is the view's: a new document keeps it
      await expect(page.locator('#vp')).toHaveAttribute('data-shown', new RegExp(`:${s}$`));
      for (const { id } of await shapes(page)) {
        await selectShape(page, id);
        await expect(page.locator('#vp')).toHaveAttribute('data-shown', `${id}:${s}`);
        const n = await voxels(page);
        if (n > 0) {
          expect(await drawn(page), `${f} ${id} ${s}`).toBeGreaterThan(0);
        }
        checked++;
      }
    }
    expect(want).toBeGreaterThan(0);
    expect(checked).toBe(want);
  });
}

test('a fixture of each grid is drawn unlit, orthographic and in its colours', async () => {
  test.setTimeout(300_000);
  const { page } = a;
  for (const f of fixtures) {
    await openFixture(page, f);
    await setting3d(page, 'lights');
    await settle(page);
    expect(await drawn(page)).toBeGreaterThan(0);
    await display(page, 'ortho');
    await expect(page.locator('#vp')).toHaveAttribute('data-projection', 'orthographic');
    await settle(page);
    expect(await drawn(page)).toBeGreaterThan(0);
    await display(page, 'cvox');
    await settle(page);
    expect(await drawn(page)).toBeGreaterThan(0);
    // back for the next
    await setting3d(page, 'lights');
    await display(page, 'persp');
    await display(page, 'cpiece');
  }
});

/** One shape per grid, and the bricks' with colours and with variable cells. */
const REFERENCES = [
  ['bricks', 'CubeInCage.xmpuzzle', 'S1'],
  ['bricks-colours', 'DraculasDentalDesaster.xmpuzzle', 'S1'],
  ['prisms', 'Bermuda.xmpuzzle', 'S1'],
  ['spheres', 'BallRoom_solved.xmpuzzle', 'S1'],
  ['rhombic', 'AugmentedSecondStellation.xmpuzzle', 'S2'],
  ['tetraocta', 'FourPieceTetrahedron.xmpuzzle', 'S1'],
  ['hexagonal', 'new-grids/HexSlab.xmpuzzle', 'S2'],
  ['cairo', 'new-grids/CairoSlab.xmpuzzle', 'S2'],
] as const;

for (const [name, file, label] of REFERENCES) {
  test(`reference screenshots: ${name}`, async () => {
    const { page } = a;
    await openFixture(page, file);
    const shape = (await shapes(page)).find((s) => s.label === label);
    expect(shape).toBeDefined();
    await selectShape(page, shape?.id ?? '');
    // nothing hovered: the view cube and the toolbar at rest
    await page.mouse.move(2, 2);
    const shot = async (look: string): Promise<void> => {
      await settle(page);
      await expect(page.locator('#vp')).toHaveScreenshot(`${name}-${look}.png`);
    };
    await shot('drawing');
    await style(page, 'wireframe');
    await shot('wireframe');
    await style(page, 'flat');
    await shot('flat');
    // the STL-like style (deferred part 5): the STL mesher's pieces in one colour
    await style(page, 'stl');
    await shot('stl');
    // the uniform mesher's solid (decision 0022)
    await style(page, 'uniform');
    await shot('uniform');
    await style(page, 'drawing');
    await setting3d(page, 'lights');
    await shot('unlit');
    await setting3d(page, 'lights');
    await display(page, 'ortho');
    await shot('ortho');
    await display(page, 'persp');
    await display(page, 'cvox');
    await shot('colours');
  });
}
