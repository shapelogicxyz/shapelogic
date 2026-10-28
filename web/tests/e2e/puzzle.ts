// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Puzzle workspace for the e2e tests: waiting until it shows the
// engine's newest revision, what its chips, table, fit summary, status
// bar and pieces scene show, and what they must show by the engine's own
// answers (problem.get, problem.fit through the page's bridge).

import { expect } from '@playwright/test';
import type { Page } from '@playwright/test';

import { engineCall } from './app.ts';

/** A part as problem.get gives it. */
export interface Part {
  shape: string;
  min: number;
  max: number;
  groups: { group: number; count: number }[];
}

/** What problem.get answers (the fields the tests use). */
export interface Problem {
  id: string;
  label: string;
  name: string;
  result: string | null;
  parts: Part[];
  colourRules: { piece: string; result: string }[];
  maxHoles: number | null;
}

/** The engine's problem. */
export async function problemGet(page: Page, problem: string): Promise<Problem> {
  return engineCall<Problem>(page, 'problem.get', { problem });
}

/** The problem's definition: what a rebuilt problem must equal (result, parts, rules, holes). */
export function definition(p: Problem): unknown {
  return {
    result: p.result,
    parts: p.parts,
    colourRules: [...p.colourRules].sort(
      (a, b) => a.piece.localeCompare(b.piece) || a.result.localeCompare(b.result),
    ),
    maxHoles: p.maxHoles,
  };
}

/** The problem the workspace shows. */
export async function currentProblem(page: Page): Promise<string> {
  const id = await page.locator('#leftP').getAttribute('data-problem');
  expect(id).not.toBeNull();
  return id ?? '';
}

/**
 * Waits until the workspace's edits are done and its cards and scene show
 * the engine's newest revision.
 */
export async function pzSettled(page: Page): Promise<void> {
  await expect(page.locator('#stage')).toHaveAttribute('data-busy', '0');
  await expect
    .poll(async () => {
      const snap = await engineCall<{ revision: number }>(page, 'document.get');
      return page.evaluate((rev) => {
        const left = document.getElementById('leftP')?.dataset.revision;
        const scene = document.getElementById('vp')?.dataset.sceneKey ?? '';
        // a stacking puzzle draws its board, a sliding one its trays, not the pieces scene
        const stage = document.getElementById('stage');
        const board =
          stage?.classList.contains('stk') === true || stage?.classList.contains('sld') === true;
        return left === String(rev) && (board || scene.split(':')[2] === String(rev));
      }, snap.revision);
    })
    .toBe(true);
}

/** A part as a chip or a table row shows it. */
export interface Shown {
  shape: string;
  min: number;
  max: number;
  group: number;
}

function parseCount(t: string): { min: number; max: number } {
  const s = t.trim();
  if (s.startsWith('×')) {
    const n = Number(s.slice(1));
    return { min: n, max: n };
  }
  const [a, b] = s.split('–').map(Number);
  return { min: a ?? 0, max: b ?? a ?? 0 };
}

/** What the chips show, in order. */
export async function chips(page: Page): Promise<Shown[]> {
  const rows = await page.$$eval('#pzUsed .ut', (els) =>
    els.map((e) => ({
      shape: (e as HTMLElement).dataset.u ?? '',
      count: e.querySelector('.cnt')?.textContent ?? '',
      group: e.querySelector('.gb')?.textContent ?? '',
    })),
  );
  return rows.map((r) => ({
    shape: r.shape,
    ...parseCount(r.count),
    group: r.group === '' ? 0 : Number(r.group.slice(1)),
  }));
}

/** What the table shows, in order. */
export async function table(page: Page): Promise<Shown[]> {
  return page.$$eval('#pzGrpTable tr[data-u]', (els) =>
    els.map((e) => {
      const v = (sel: string): string => e.querySelector<HTMLInputElement>(sel)?.value ?? '';
      const range = e.querySelector<HTMLInputElement>('[data-range]')?.checked ?? false;
      const max = Number(v('[data-f=max]'));
      const g = e.querySelector<HTMLSelectElement>('[data-grp]')?.value ?? '';
      return {
        shape: (e as HTMLElement).dataset.u ?? '',
        min: range ? Number(v('[data-f=min]')) : max,
        max,
        group: g === '' ? 0 : Number(g),
      };
    }),
  );
}

/** The parts as the chips and the table must show them. */
export function expected(p: Problem): Shown[] {
  return p.parts.map((x) => ({
    shape: x.shape,
    min: x.min,
    max: x.max,
    group: x.groups.find((g) => g.count > 0)?.group ?? 0,
  }));
}

/** The pieces scene as it must be: the result, the selected piece, every copy (optional ones faded). */
export function expectedScene(p: Problem, selected: string | null, board = false): string {
  const out: string[] = [];
  // under a sliding board's trays: only the copies (the board is the result, drawn as the trays)
  if (p.result !== null && !board) {
    out.push(`result:${p.result}:1/1`);
  }
  if (selected !== null && selected !== p.result && !board) {
    out.push(`selected:${selected}:1/1`);
  }
  for (const x of p.parts.filter((q) => !board || q.shape !== p.result)) {
    for (let k = 0; k < x.max; k++) {
      out.push(`piece:${x.shape}:${String(k + 1)}/${String(x.max)}${k >= x.min ? ':faded' : ''}`);
    }
  }
  return out.join(' ');
}

/**
 * After an action: the chips and the table agree with each other and with
 * problem.get (the view switched to look at the other and back), the fit
 * summary and the status bar equal problem.fit, and the scene has one
 * mesh per copy with the optional ones faded.
 */
export async function checkShown(page: Page): Promise<Problem> {
  await pzSettled(page);
  const id = await currentProblem(page);
  const p = await problemGet(page, id);
  const fit = await engineCall<{ statusLine: string; verdict: string }>(page, 'problem.fit', {
    problem: id,
  });
  const want = expected(p);
  // the chips and the Groups table (drawn when collapsed too) show the same
  expect(await chips(page)).toEqual(want);
  expect(await table(page)).toEqual(want);
  expect(JSON.parse((await page.locator('#pzSummary').getAttribute('data-fit')) ?? 'null')).toEqual(
    fit,
  );
  if (fit.verdict !== 'noResult') {
    await expect(page.locator('#stText')).toHaveText(fit.statusLine);
  }
  const shown = await page.locator('#pzSel').getAttribute('data-shape');
  const sel = shown === null || shown === '' ? null : shown;
  // a sliding problem's result is a board when it is labelled B1, B2...
  const board =
    p.result !== null &&
    (await page.locator('#stage.sld').count()) > 0 &&
    /^B\d+$/.test(
      (await page.locator(`#shapeList .row[data-id="${p.result}"] .chip`).textContent()) ?? '',
    );
  const scene = expectedScene(p, sel, board);
  await expect(page.locator('#vp')).toHaveAttribute('data-scene', scene);
  await expect(page.locator('#vp')).toHaveAttribute(
    'data-scene-meshes',
    String(scene === '' ? 0 : scene.split(' ').length),
  );
  return p;
}
