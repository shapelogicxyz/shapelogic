// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view's speed (docs/FRONTEND-PLAN.md, stage 1b's gate): the mesh of a
// shape of more than 1,000 cells is built in under 10 ms, and orbiting the
// largest fixture mesh (DraculasDentalDesaster's S10: 1,656 cells, 25,008
// triangles) keeps the display's rate. The numbers are printed for the
// handoff.

import { expect, test } from '@playwright/test';

import { BUILT, ENGINE, launch, openFixture, selectShape, settle, shapes } from './app.ts';
import type { App } from './app.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

let a: App;
test.beforeEach(async () => {
  a = await launch();
});
test.afterEach(async () => {
  await a.close();
});

const median = (xs: number[]): number => {
  const s = [...xs].sort((u, v) => u - v);
  return s[Math.floor(s.length / 2)] ?? NaN;
};
const p95 = (xs: number[]): number => {
  const s = [...xs].sort((u, v) => u - v);
  return s[Math.floor(s.length * 0.95)] ?? NaN;
};

test('the largest mesh builds in under 10 ms and orbits at the display rate', async () => {
  const { page } = a;
  await openFixture(page, 'DraculasDentalDesaster.xmpuzzle');
  const s10 = (await shapes(page)).find((s) => s.label === 'S10');
  expect(s10).toBeDefined();
  await page.evaluate(() => {
    performance.clearMeasures();
  });
  await selectShape(page, s10?.id ?? '');
  const build = await page.evaluate(() =>
    performance.getEntriesByName('view3d:mesh-build').map((e) => ({
      ms: e.duration,
      detail: (e as PerformanceMeasure).detail as { cells: number; triangles: number },
    })),
  );
  expect(build).toHaveLength(1);
  expect(build[0]?.detail.cells).toBe(1656);
  expect(build[0]?.detail.triangles).toBe(25008);

  // the display's rate, idle
  const idle = await page.evaluate(
    () =>
      new Promise<number[]>((done) => {
        const ts: number[] = [];
        const tick = (t: number): void => {
          ts.push(t);
          if (ts.length < 61) {
            requestAnimationFrame(tick);
          } else {
            done(ts.slice(1).map((v, i) => v - (ts[i] ?? 0)));
          }
        };
        requestAnimationFrame(tick);
      }),
  );
  const frame = median(idle);

  // orbit for about two seconds while the page records its frames and renders
  await page.evaluate(() => {
    performance.clearMeasures('view3d:render');
    const w = window as unknown as { frames: number[]; recording: boolean };
    w.frames = [];
    w.recording = true;
    const tick = (t: number): void => {
      w.frames.push(t);
      if (w.recording) {
        requestAnimationFrame(tick);
      }
    };
    requestAnimationFrame(tick);
  });
  const box = await page.locator('#cv').boundingBox();
  const x0 = (box?.x ?? 0) + (box?.width ?? 0) / 2;
  const y0 = (box?.y ?? 0) + (box?.height ?? 0) / 2;
  await page.mouse.move(x0, y0);
  await page.mouse.down();
  const t0 = Date.now();
  let i = 0;
  while (Date.now() - t0 < 2000) {
    i++;
    await page.mouse.move(x0 + 150 * Math.sin(i / 20), y0 + 80 * Math.sin(i / 31));
    await page.waitForTimeout(4);
  }
  await page.mouse.up();
  const r = await page.evaluate(() => {
    const w = window as unknown as { frames: number[]; recording: boolean };
    w.recording = false;
    const f = w.frames;
    return {
      intervals: f.slice(1).map((v, k) => v - (f[k] ?? 0)),
      renders: performance.getEntriesByName('view3d:render').map((e) => e.duration),
    };
  });
  await settle(page);
  const result = {
    buildMs: Number((build[0]?.ms ?? NaN).toFixed(2)),
    displayFrameMs: Number(frame.toFixed(2)),
    orbitFrameMedianMs: Number(median(r.intervals).toFixed(2)),
    orbitFrameP95Ms: Number(p95(r.intervals).toFixed(2)),
    renders: r.renders.length,
    frames: r.intervals.length,
    renderMedianMs: Number(median(r.renders).toFixed(2)),
    renderP95Ms: Number(p95(r.renders).toFixed(2)),
  };
  console.log('view3d performance', JSON.stringify(result));
  expect(result.buildMs).toBeLessThan(10);
  // at the display's rate: frames come as often as when idle, and a render fits in one
  expect(result.orbitFrameMedianMs).toBeLessThan(frame * 1.25);
  expect(result.renderP95Ms).toBeLessThan(frame);
  expect(result.renders).toBeGreaterThan(result.frames * 0.7);
});
