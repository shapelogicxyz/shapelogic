// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view draws every shape of every fixture in the three styles, and
// one shape of each grid in each look (drawing, wireframe, flat, the
// STL-like style of deferred part 5, unlit, orthographic, the colour view)
// as reference screenshots
// (tests/e2e/__screenshots__, compared with a tolerance; recorded with
// `npx playwright test --update-snapshots` after a look beside the
// original GUI, docs/handoff/web-1b.md).

import { readdirSync } from 'node:fs';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import {
  BUILT,
  display,
  ENGINE,
  FIXTURES,
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

const fixtures = readdirSync(FIXTURES)
  .filter((f) => f.endsWith('.xmpuzzle'))
  .sort();

/** The triangles the last render drew. */
async function drawn(page: Page): Promise<number> {
  return Number(await page.getAttribute('#vp', 'data-triangles'));
}

/** The voxels the status bar gives the shape shown. */
async function voxels(page: Page): Promise<number> {
  const t = (await page.textContent('#stText')) ?? '';
  return Number(/has (\d+) voxels/.exec(t)?.[1] ?? 'NaN');
}

test('every shape of every fixture is drawn in every style', async () => {
  test.setTimeout(600_000);
  const { page } = a;
  let checked = 0;
  for (const s of ['drawing', 'wireframe', 'flat'] as const) {
    for (const f of fixtures) {
      await openFixture(page, f);
      if (s !== 'drawing') {
        // the style is the view's: a new document keeps it
        await expect(page.locator('#vp')).toHaveAttribute('data-shown', new RegExp(`:${s}$`));
      }
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
    if (s !== 'flat') {
      await style(page, s === 'drawing' ? 'wireframe' : 'flat');
    }
  }
  expect(checked).toBe(3 * 170);
});

test('every fixture is drawn unlit, orthographic and in its colours', async () => {
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
  ['spheres', 'BallRoom.xmpuzzle', 'S1'],
  ['rhombic', 'AugmentedSecondStellation.xmpuzzle', 'S2'],
  ['tetraocta', 'FourPieceTetrahedron.xmpuzzle', 'S1'],
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
