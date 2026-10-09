// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the Solver's e2e tests share: the workspace reached and settled,
// a run waited for by its end (the card's data-phase none, its end's
// revision shown), the rows the list draws, the engine's own answers
// (solution.page, playback.poses with its frame, bt --json), a box of
// dominoes made through the bridge.

import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';

import { expect } from '@playwright/test';
import type { Page } from '@playwright/test';

import { engineCall, ROOT } from './app.ts';

/** bt's summary of a fixture (`bt -q -n -d --json`): its assemblies and solutions. */
export function btJson(
  file: string,
  extra: string[] = [],
): { assemblies: number; solutions: number } {
  const out = execFileSync(
    resolve(ROOT, 'build/release/cli/bt'),
    ['-q', '-n', '-d', '--json', ...extra, file],
    { encoding: 'utf8' },
  );
  return JSON.parse(out) as { assemblies: number; solutions: number };
}

/** Switches to the Solver and waits until its cards show the document's revision. */
export async function toSolver(page: Page): Promise<void> {
  await page.keyboard.press('Control+3');
  await expect(page.locator('#stage')).toHaveAttribute('data-tab', 'solver');
  await solverSettled(page);
}

/**
 * Waits until nothing is under way: no edit, no run, the cards at the
 * engine's revision (what the bridge changed included).
 */
export async function solverSettled(page: Page): Promise<void> {
  await expect(page.locator('#stage')).toHaveAttribute('data-busy', '0');
  await expect(page.locator('#leftS')).toHaveAttribute('data-phase', 'none');
  await expect
    .poll(async () => {
      const snap = await engineCall<{ revision: number }>(page, 'document.get');
      const shown = Number(await page.getAttribute('#leftS', 'data-revision'));
      return shown >= snap.revision;
    })
    .toBe(true);
}

/** Clicks a run button (Solve, Prepare, Continue, Pause, Abort). */
export async function runButton(
  page: Page,
  k: 'start' | 'prepare' | 'continue' | 'pause' | 'abort',
): Promise<void> {
  const b = page.locator(`#slvRun [data-r="${k}"]`);
  await expect(b).toBeEnabled();
  await b.click();
}

/** Waits for a run of the problem shown to end in a state (by its revision, then shown). */
export async function runEnded(page: Page, state: string, timeout = 60_000): Promise<void> {
  await expect(page.locator('#leftS')).toHaveAttribute('data-state', state, { timeout });
  await expect(page.locator('#leftS')).toHaveAttribute('data-phase', 'none', { timeout });
  const end = Number(await page.getAttribute('#leftS', 'data-end'));
  await expect
    .poll(async () => Number(await page.getAttribute('#leftS', 'data-revision')))
    .toBeGreaterThanOrEqual(end);
  await solverSettled(page);
}

/** The problem shown in the Solver. */
export async function shownProblem(page: Page): Promise<string> {
  return (await page.getAttribute('#leftS', 'data-problem')) ?? '';
}

/** The solution ids of a problem as solution.page sorts them (all rows). */
export async function pageIds(
  page: Page,
  problem: string,
  sortBy = 'list',
  descending = false,
): Promise<string[]> {
  const r = await engineCall<{ rows: { id: string }[] }>(page, 'solution.page', {
    problem,
    sortBy,
    descending,
    limit: 10000,
  });
  return r.rows.map((x) => x.id);
}

/** The rows the list draws now: their place and id, in order (the rows read in). */
export async function listRows(page: Page): Promise<{ i: number; id: string }[]> {
  return page.$$eval('#slvList .lrow[data-id]', (rows) =>
    rows
      .map((r) => ({
        i: Number((r as HTMLElement).dataset.i),
        id: (r as HTMLElement).dataset.id ?? '',
      }))
      .sort((a, b) => a.i - b.i),
  );
}

/** Waits until the list shows its newest rows (no page of an old version in sight). */
export async function listCurrent(page: Page): Promise<void> {
  await expect(page.locator('#slvSpace')).toHaveAttribute('data-current', 'true');
}

/** A playback's poses at every whole step, as the engine samples them (a frame through the bridge). */
export async function enginePoses(
  page: Page,
  problem: string,
  solution: string,
): Promise<{ steps: number; pieces: number; t: number[]; q: number[]; v: number[] }> {
  const pb = await engineCall<{ handle: string; steps: number; pieces: unknown[] }>(
    page,
    'playback.open',
    { problem, solution },
  );
  const id = 2_000_000_000 + Math.floor(Math.random() * 1_000_000);
  const got = await page.evaluate(
    ([id, handle]) =>
      new Promise<{ samples: number; t: number[]; q: number[]; v: number[] }>((done, fail) => {
        const off = window.bt.onMessage((m) => {
          if ((m.kind === 'reply' || m.kind === 'error') && m.id === id) {
            off();
            if (m.kind === 'error' || m.frame === undefined) {
              fail(new Error('playback.poses failed'));
              return;
            }
            const arrays = m.frame.desc.arrays as Record<
              string,
              { offset: number; length: number }
            >;
            const f32 = (k: string): number[] => {
              const a = arrays[k];
              return a === undefined
                ? []
                : [...new Float32Array(m.frame?.data ?? new ArrayBuffer(0), a.offset, a.length)];
            };
            const vis = arrays.visible;
            done({
              samples: (m.result as { samples: number }).samples,
              t: f32('translation'),
              q: f32('rotation'),
              v: vis === undefined ? [] : [...new Uint8Array(m.frame.data, vis.offset, vis.length)],
            });
          }
        });
        window.bt.send({
          kind: 'call',
          id,
          method: 'playback.poses',
          params: { handle, samplesPerStep: 1 },
        });
      }),
    [id, pb.handle] as const,
  );
  await engineCall(page, 'playback.close', { handle: pb.handle });
  return { steps: pb.steps, pieces: pb.pieces.length, t: got.t, q: got.q, v: got.v };
}

/** The poses the view shows now (#vp[data-poses]): translation, rotation, shown, per piece. */
export async function shownPoses(page: Page): Promise<{ t: number[]; q: number[]; v: boolean }[]> {
  const s = (await page.getAttribute('#vp', 'data-poses')) ?? '';
  return s
    .split(' ')
    .filter((x) => x !== '')
    .map((p) => {
      const [t, q, v] = p.split(';');
      return {
        t: (t ?? '').split(',').map(Number),
        q: (q ?? '').split(',').map(Number),
        v: v === '1',
      };
    });
}

/** Compares the poses shown with the engine's at a whole step (4 decimals shown). */
export async function expectPosesAt(
  page: Page,
  engine: { pieces: number; t: number[]; q: number[]; v: number[] },
  step: number,
): Promise<void> {
  await expect(page.locator('#splayer')).toHaveAttribute('data-t', step.toFixed(3));
  const shown = await shownPoses(page);
  expect(shown).toHaveLength(engine.pieces);
  for (let i = 0; i < engine.pieces; i++) {
    const b = step * engine.pieces + i;
    const p = shown[i];
    for (let k = 0; k < 3; k++) {
      expect(Math.abs((p?.t[k] ?? NaN) - (engine.t[3 * b + k] ?? NaN))).toBeLessThan(6e-5);
    }
    for (let k = 0; k < 4; k++) {
      expect(Math.abs((p?.q[k] ?? NaN) - (engine.q[4 * b + k] ?? NaN))).toBeLessThan(6e-5);
    }
    expect(p?.v).toBe(engine.v[b] === 1);
  }
}

/** A box of x*y*z cells to fill with dominoes, made through the bridge in the open document. */
export async function dominoBox(
  page: Page,
  x: number,
  y: number,
  z: number,
): Promise<{ problem: string; box: string; domino: string }> {
  const box = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [x, y, z] }))
    .shape;
  await engineCall(page, 'shape.setBox', {
    shape: box,
    tool: 'fixed',
    from: [0, 0, 0],
    to: [x - 1, y - 1, z - 1],
  });
  const domino = (await engineCall<{ shape: string }>(page, 'shape.create', { size: [2, 1, 1] }))
    .shape;
  await engineCall(page, 'shape.setBox', {
    shape: domino,
    tool: 'fixed',
    from: [0, 0, 0],
    to: [1, 0, 0],
  });
  const problem = (await engineCall<{ problem: string }>(page, 'problem.create')).problem;
  await engineCall(page, 'problem.setResult', { problem, shape: box });
  const n = (x * y * z) / 2;
  await engineCall(page, 'problem.setPieceCount', { problem, shape: domino, min: n, max: n });
  return { problem, box, domino };
}

/** Clicks the Remove menu's item `k` of the Manage menu. */
export async function manage(page: Page, k: string): Promise<void> {
  await page.click('#slvAct [data-a="manage"]');
  await page.click(`#pop [data-k="${k}"]`);
}

/** Answers the confirmation: its title, and Cancel or the action. */
export async function confirm(page: Page, title: RegExp, ok: boolean): Promise<void> {
  const d = page.locator('.cfm');
  await expect(d.locator('h3')).toHaveText(title);
  await d.locator(`[data-x="${ok ? '1' : '0'}"]`).click();
  await expect(d).toHaveCount(0);
}
