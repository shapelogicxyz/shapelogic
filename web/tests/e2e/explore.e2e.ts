// SPDX-License-Identifier: GPL-3.0-or-later
//
// The explorers, the debug statistics and the status window
// (FRONTEND-PLAN.md, stage 5's gate) in the built app with the release
// engine, on Bermuda (a prism puzzle): every piece's placements listed as
// many as explore.placements gives and drawn where the engine says (the
// mesh of the placement's transformation at its cell minus that mesh's
// hotspot); the assembler stepped until a placement set is reached, its
// `finished` growing, its pieces those of the engine's own explorer
// stepped as far; the movement tree's root, Analyse, Add move, a matching
// move found by Alt-dragging a piece in the view and Prune tree equal to
// the replies of the contract session's calls on an explorer of the
// test's own; the handles closed with the drawer; the debug panel's
// numbers solve.status's; the status window's rows shape.table's on every
// fixture, its selections, and Remove selected as one undo step. Each
// test's call log holds no refusal the UI could have avoided.

import { mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import { statsReport } from '../../src/renderer/explore/debug-stats.ts';
import type { MovementNode, Placement, SolveStats } from '../../src/engine/api.generated.ts';
import { cellScale, gridExtent, placedAt } from '../../src/renderer/view3d/extent.ts';
import {
  BUILT,
  ENGINE,
  engineCall,
  FIXTURES,
  launch,
  menu,
  openFixture,
  readCallLog,
} from './app.ts';
import {
  runButton,
  runEnded,
  shownPoses,
  shownProblem,
  solverSettled,
  toSolver,
} from './solver.ts';

test.skip(ENGINE === null || !BUILT, 'needs the release engine and `npm run build`');

type V3 = [number, number, number];

function tmpLog(): { dir: string; log: string } {
  const dir = mkdtempSync(join(tmpdir(), 'bt-e2e-'));
  return { dir, log: join(dir, 'calls.log') };
}

/** No call failed but the client's own superseded reads (and what the test itself expected to fail). */
function expectNoRefusal(log: string): void {
  const bad = readCallLog(log).filter((c) => c.errc !== null && c.errc !== 'superseded');
  expect(bad).toEqual([]);
}

async function attr(page: Page, sel: string, name: string): Promise<string> {
  return (await page.getAttribute(sel, name)) ?? '';
}

/** Where the engine says a piece goes: its turned mesh's hotspot on the cell, centred on the result. */
async function expectedAt(
  page: Page,
  shape: string,
  transformation: number,
  at: V3,
  grid: Parameters<typeof cellScale>[0],
  extent: V3,
): Promise<V3> {
  const mesh = await engineCall<{ hotspot: V3 }>(page, 'shape.mesh', { shape, transformation });
  return placedAt(at, mesh.hotspot, cellScale(grid), extent);
}

/** The pieces of the problem by number: their shapes (numbered part by part). */
async function pieceShapes(page: Page, problem: string): Promise<string[]> {
  const d = await engineCall<{ parts: { shape: string; max: number }[] }>(page, 'problem.get', {
    problem,
  });
  return d.parts.flatMap((p) => Array.from({ length: p.max }, () => p.shape));
}

async function resultExtent(
  page: Page,
  problem: string,
): Promise<{ grid: Parameters<typeof cellScale>[0]; extent: V3 }> {
  const snap = await engineCall<{
    grid: Parameters<typeof cellScale>[0];
    shapes: { id: string; size: V3 }[];
  }>(page, 'document.get');
  const d = await engineCall<{ result: string }>(page, 'problem.get', { problem });
  const size = snap.shapes.find((s) => s.id === d.result)?.size ?? [1, 1, 1];
  return { grid: snap.grid, extent: gridExtent(snap.grid, size) };
}

/** The explorer's pose of a piece in the view (#vp[data-poses], in the model's order). */
async function poseOf(page: Page, piece: number): Promise<number[] | null> {
  const pieces = (await attr(page, '#vp', 'data-solution-pieces')).split(' ');
  const i = pieces.findIndex((p) => p.startsWith(`${String(piece)}:`));
  const poses = await shownPoses(page);
  return i < 0 ? null : (poses[i]?.t ?? null);
}

async function expectPose(page: Page, piece: number, want: V3): Promise<void> {
  await expect
    .poll(async () => {
      const t = await poseOf(page, piece);
      return t?.every((v, k) => Math.abs(v - (want[k] ?? 0)) < 2e-4) === true;
    })
    .toBe(true);
}

test('Bermuda prepared: every placement listed and drawn, the assembler stepped, the debug panel', async () => {
  test.setTimeout(240_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await openFixture(page, 'Bermuda.xmpuzzle');
    await toSolver(page);
    const p = await shownProblem(page);
    // closed until the solve is prepared or paused
    await expect(page.locator('#slvAdv [data-x="placements"]')).toBeDisabled();
    await expect(page.locator('#slvAdv [data-x="step"]')).toBeDisabled();
    await runButton(page, 'prepare');
    await runEnded(page, 'paused');

    // the debug statistics: the panel's numbers are solve.status's
    await page.click('#slvStats');
    await expect(page.locator('#statsWin')).toHaveAttribute('data-state', 'paused');
    await expect(page.locator('#statsWin')).toHaveAttribute('data-stats', /elapsedMs/);
    const status = await engineCall<{ stats: SolveStats }>(page, 'solve.status', { problem: p });
    // the panel holds the figures of a stopped run as the original's did
    // (freezeLive): every number the engine's, but the wall time, which the
    // session counts on from the run's start (the original's statsOrigin)
    const held = JSON.parse(await attr(page, '#statsWin', 'data-stats')) as SolveStats;
    expect({ ...held, elapsedMs: 0 }).toEqual({ ...status.stats, elapsedMs: 0 });
    expect(held.elapsedMs).toBeLessThanOrEqual(status.stats.elapsedMs);
    const info = await engineCall<{ solvers: { solver: string; label: string }[] }>(
      page,
      'engine.info',
    );
    const report = statsReport(held, (v) => info.solvers.find((x) => x.solver === v)?.label ?? v);
    for (const sec of report.sections) {
      for (const r of sec.rows) {
        await expect(page.locator(`#statsWin td[data-k="${r.label}"]`)).toHaveText(r.value);
      }
    }
    await expect(page.locator('#statsWin .notes li')).toHaveCount(report.notes.length);
    await page.click('#statsWin [data-a="close"]');
    await expect(page.locator('#statsWin')).toHaveCount(0);

    // Placements: every piece's count, each piece's first and last placement drawn
    const { grid, extent } = await resultExtent(page, p);
    const shapes = await pieceShapes(page, p);
    await page.click('#slvAdv [data-x="placements"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'placements');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-handle', /^ax/);
    const ax = await attr(page, '#sdrawer', 'data-handle');
    await expect(page.locator('#sdrawer .dl [data-exr]')).toHaveCount(shapes.length);
    for (let piece = 0; piece < shapes.length; piece++) {
      const engine = await engineCall<{ placements: { transformation: number; at: V3 }[] }>(
        page,
        'explore.placements',
        { handle: ax, piece },
      );
      const row = page.locator(`#sdrawer .dl [data-exr="${String(piece)}"]`);
      await expect(row).toHaveAttribute('data-count', String(engine.placements.length));
      await row.click();
      await expect(page.locator('#sdrawer')).toHaveAttribute('data-piece', String(piece));
      const shape = shapes[piece] ?? '';
      for (const [key, i] of [
        ['Home', 0],
        ['End', engine.placements.length - 1],
      ] as const) {
        await page.keyboard.press(key);
        await expect(page.locator('#sdrawer')).toHaveAttribute('data-placement', String(i));
        const q = engine.placements[i];
        if (q === undefined) {
          continue;
        }
        // the result as a wire frame, the piece solid
        await expect(page.locator('#vp')).toHaveAttribute(
          'data-solution-pieces',
          new RegExp(`^-1:[^ ]+:wire:on ${String(piece)}:${shape}:solid:on$`),
        );
        await expectPose(
          page,
          piece,
          await expectedAt(page, shape, q.transformation, q.at, grid, extent),
        );
      }
    }
    // the keys: ← → the piece, ↑ ↓ the placement
    await page.click('#sdrawer .dl [data-exr="0"]');
    await page.keyboard.press('ArrowDown');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-placement', '1');
    await page.keyboard.press('ArrowUp');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-placement', '0');
    await page.keyboard.press('ArrowRight');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-piece', '1');
    await page.keyboard.press('ArrowLeft');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-piece', '0');

    // Step: the assembler until a placement set, `finished` growing; the
    // engine's own explorer stepped as far places the same
    await page.click('#slvAdv [data-x="step"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'step');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-handle', /^ax/);
    await expect.poll(() => attr(page, '#sdrawer', 'data-busy')).toBe('0');
    const own = await engineCall<{ handle: string }>(page, 'explore.openAssembly', {
      problem: p,
      solver: 'classic',
      keepMirrors: false,
      keepRotations: false,
      completeRotations: false,
      strictColours: false,
    });
    // `finished` is the assembler's estimate: 0 at the start, it may dip
    // between steps, and it grows as the search goes on
    const steps = (): Promise<number> => attr(page, '#sdrawer', 'data-steps').then(Number);
    await expect.poll(steps).toBe(1);
    // where the engine's own explorer first has every piece placed (Bermuda has one assembly)
    let full = 0;
    let ownFinished = 0;
    let ownPlacements: Placement[] = [];
    for (let n = 1; n <= 20_000 && full === 0; n++) {
      const r = await engineCall<{ finished: number; placements: Placement[] }>(
        page,
        'explore.step',
        { handle: own.handle },
      );
      if (r.placements.every((q) => q.placed)) {
        full = n;
        ownFinished = r.finished;
        ownPlacements = r.placements;
      }
    }
    expect(full).toBeGreaterThan(1);
    // the UI stepped as far: Run (1000), 100, 10 and Step (1) clicks
    let left = full - 1;
    for (const n of [1000, 100, 10, 1]) {
      while (left >= n) {
        const before = await steps();
        await page.click(`#sdrawer [data-exa="step"][data-n="${String(n)}"]`);
        await expect.poll(steps).toBe(before + n);
        left -= n;
      }
    }
    expect(Number(await attr(page, '#sdrawer', 'data-placed'))).toBe(shapes.length);
    const atSet = Number(await attr(page, '#sdrawer', 'data-finished'));
    expect(atSet).toBeGreaterThan(0);
    const shown = JSON.parse(await attr(page, '#sdrawer', 'data-step-placements')) as Placement[];
    expect(shown).toEqual(ownPlacements);
    expect(atSet).toBeCloseTo(ownFinished, 6);
    for (const q of ownPlacements) {
      await expectPose(
        page,
        q.piece,
        await expectedAt(page, shapes[q.piece] ?? '', q.transformation, q.at, grid, extent),
      );
    }
    await engineCall(page, 'explore.close', { handle: own.handle });
    // Run: a thousand steps at a time, the search further on
    for (let round = 0; round < 50; round++) {
      const before = await steps();
      await page.click('#sdrawer [data-exa="step"][data-n="1000"]');
      await expect.poll(steps).toBe(before + 1000);
      if (Number(await attr(page, '#sdrawer', 'data-finished')) > atSet) {
        break;
      }
    }
    expect(Number(await attr(page, '#sdrawer', 'data-finished'))).toBeGreaterThan(atSet);

    // Esc closes the drawer, and its handles with it
    const stepHandle = await attr(page, '#sdrawer', 'data-handle');
    await page.keyboard.press('Escape');
    await expect(page.locator('#sdrawer')).not.toHaveClass(/open/);
    await expect(page.locator('#vp')).toHaveAttribute('data-solution-layer', 'player');
    for (const h of [ax, stepHandle]) {
      await expect
        .poll(() =>
          engineCall(page, 'explore.close', { handle: h }).then(
            () => 'open',
            (e: unknown) => String(e),
          ),
        )
        .toMatch(/not_found/);
    }
    // the test's own failed closes are its own calls (the bridge's), not in the log
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('Bermuda solved: the movement tree as the contract session grows it', async () => {
  test.setTimeout(180_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    await openFixture(page, 'Bermuda.xmpuzzle');
    await toSolver(page);
    const p = await shownProblem(page);
    await page.click('#slvMode [data-m="dis"]');
    await solverSettled(page);
    await runButton(page, 'start');
    await runEnded(page, 'finished');
    await expect(page.locator('#splayer')).toHaveAttribute('data-ready', '1');
    const sol = await attr(page, '#splayer', 'data-solution');
    const { grid, extent } = await resultExtent(page, p);
    const shapes = await pieceShapes(page, p);
    const got = await engineCall<{ placements: Placement[] }>(page, 'solution.get', {
      problem: p,
      solution: sol,
    });
    // Placements and Step wait for a prepared or paused solve; Movements has a solution with its moves
    await expect(page.locator('#slvAdv [data-x="placements"]')).toBeDisabled();
    await page.click('#slvAdv [data-x="movements"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-mode', 'movements');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-handle', /^mx/);
    const mxUi = await attr(page, '#sdrawer', 'data-handle');
    // the contract session's calls on an explorer of the test's own
    const mine = await engineCall<{ handle: string; root: MovementNode; vectors: V3[] }>(
      page,
      'explore.openMovements',
      { problem: p, solution: sol },
    );
    const mx = mine.handle;
    const root = mine.root;
    const node = (h: string, n: number): Promise<MovementNode> =>
      engineCall<MovementNode>(page, 'explore.node', { handle: h, node: n });
    expect(await node(mxUi, root.node)).toEqual(root);
    await expect(page.locator('#sdrawer .dl [data-node]')).toHaveCount(1);
    // the root drawn as the solution's assembly
    for (const q of root.pieces) {
      const t = got.placements.find((x) => x.piece === q.piece)?.transformation ?? 0;
      await expectPose(
        page,
        q.piece,
        await expectedAt(page, shapes[q.piece] ?? '', t, q.at, grid, extent),
      );
    }

    // Analyse: every position one move away
    await page.click('#sdrawer [data-exa="analyse"]');
    const children = (
      await engineCall<{ nodes: MovementNode[] }>(page, 'explore.analyse', {
        handle: mx,
        node: root.node,
      })
    ).nodes;
    expect(children.length).toBeGreaterThan(0);
    await expect(page.locator('#sdrawer .dl [data-node]')).toHaveCount(1 + children.length);
    for (const c of children) {
      await expect(page.locator(`#sdrawer .dl [data-node="${String(c.node)}"]`)).toHaveAttribute(
        'data-label',
        c.label,
      );
      expect(await node(mxUi, c.node)).toEqual(c);
    }

    // Add move: the first piece one step in the first direction
    const first = root.pieces[0]?.piece ?? 0;
    await page.click('#sdrawer [data-exa="add"]');
    await page.selectOption('#sdrawer select[data-f="dir"]', '0');
    await page.check(`#sdrawer input[data-f="piece"][value="${String(first)}"]`);
    await page.click('#sdrawer [data-exa="addDone"]');
    const moved = await engineCall<MovementNode>(page, 'explore.addMove', {
      handle: mx,
      node: root.node,
      pieces: [first],
      direction: 0,
    });
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-node', String(moved.node));
    expect(await node(mxUi, moved.node)).toEqual(moved);
    // drawn where the move put the piece
    const mt = got.placements.find((x) => x.piece === first)?.transformation ?? 0;
    const mAt = moved.pieces.find((x) => x.piece === first)?.at ?? [0, 0, 0];
    await expectPose(
      page,
      first,
      await expectedAt(page, shapes[first] ?? '', mt, mAt, grid, extent),
    );

    // a matching move by Alt-dragging a piece from the root, along a direction it can move in
    await page.click('#sdrawer .dl [data-node="0"]');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-node', String(root.node));
    const cv = await page.locator('#cv').boundingBox();
    if (cv === null) {
      throw new Error('no canvas');
    }
    let matched: MovementNode | null = null;
    const tried: string[] = [];
    for (const q of root.pieces) {
      if (matched !== null) {
        break;
      }
      for (const [d, v] of mine.vectors.entries()) {
        if (matched !== null) {
          break;
        }
        for (const sign of [1, -1]) {
          const delta = v.map((x) => sign * x) as V3;
          let want: MovementNode;
          try {
            want = await engineCall<MovementNode>(page, 'explore.findMatching', {
              handle: mx,
              node: root.node,
              piece: q.piece,
              delta,
            });
          } catch {
            continue;
          }
          // where the piece is, if it is the one the mouse finds there
          const centres = (await attr(page, '#vp', 'data-piece-screen')).split(' ');
          const c = centres.find((x) => x.startsWith(`${String(q.piece)}:`));
          const [x, y] = (c?.split(':')[1] ?? '').split(',').map(Number);
          if (x === undefined || y === undefined || Number.isNaN(x)) {
            continue;
          }
          await page.mouse.move(cv.x + x, cv.y + y);
          if ((await attr(page, '#vp', 'data-hover-piece')) !== String(q.piece)) {
            tried.push(`${String(q.piece)} hidden`);
            break;
          }
          const dirs = JSON.parse(await attr(page, '#sdrawer', 'data-screen-dirs')) as Record<
            string,
            [number, number][]
          >;
          const s = dirs[String(q.piece)]?.[d] ?? [0, 0];
          const len = Math.hypot(s[0], s[1]);
          if (len < 5) {
            // seen end on: not draggable from this side
            tried.push(`${String(q.piece)} d${String(d)} end on`);
            continue;
          }
          const k = (sign * 60) / len;
          await page.keyboard.down('Alt');
          await page.mouse.down();
          await page.mouse.move(cv.x + x + (s[0] * k) / 2, cv.y + y + (s[1] * k) / 2);
          await page.mouse.move(cv.x + x + s[0] * k, cv.y + y + s[1] * k);
          await page.mouse.up();
          await page.keyboard.up('Alt');
          await expect(page.locator('#sdrawer')).toHaveAttribute('data-node', String(want.node));
          expect(await node(mxUi, want.node)).toEqual(want);
          matched = want;
          break;
        }
      }
    }
    expect(matched, tried.join(', ')).not.toBeNull();
    // the camera did not move: the drag was the piece's
    await expect(
      page.locator(`#sdrawer .dl [data-node="${String(matched?.node)}"]`),
    ).toHaveAttribute('data-label', matched?.label ?? '');

    // Prune tree at the first child of Analyse: only the path to it stays
    const child = children[0];
    if (child === undefined) {
      throw new Error('no child');
    }
    await page.click(`#sdrawer .dl [data-node="${String(child.node)}"]`);
    await page.click('#sdrawer [data-exa="prune"]');
    await engineCall(page, 'explore.prune', { handle: mx, node: child.node });
    await expect(page.locator('#sdrawer .dl [data-node]')).toHaveCount(2);
    for (const h of [mx, mxUi]) {
      expect((await node(h, root.node)).children).toEqual([child.node]);
      expect((await node(h, child.node)).children).toEqual([]);
      await expect(node(h, moved.node)).rejects.toThrow(/not_found/);
    }
    // the keys: ← the parent, → its first move
    await page.click(`#sdrawer .dl [data-node="${String(child.node)}"]`);
    await page.keyboard.press('ArrowLeft');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-node', String(root.node));
    await page.keyboard.press('ArrowRight');
    await expect(page.locator('#sdrawer')).toHaveAttribute('data-node', String(child.node));

    // closed with the drawer
    await page.click('#sdrawer [data-ex="close"]');
    await expect(page.locator('#sdrawer')).not.toHaveClass(/open/);
    await expect(node(mxUi, root.node)).rejects.toThrow(/not_found/);
    await engineCall(page, 'explore.close', { handle: mx });
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the status window: its rows are shape.table’s on every fixture; select and remove', async () => {
  test.setTimeout(180_000);
  const { dir, log } = tmpLog();
  const a = await launch({ callLog: log });
  const page = a.page;
  try {
    const files = readdirSync(FIXTURES)
      .filter((f) => f.endsWith('.xmpuzzle'))
      .sort();
    await openFixture(page, files[0] ?? '');
    await menu(page, 'statusWindow');
    await expect(page.locator('#statusWin')).toBeVisible();
    for (const f of files) {
      await openFixture(page, f);
      const snap = await engineCall<{ revision: number; grid: string; shapes: { id: string }[] }>(
        page,
        'document.get',
      );
      await expect(page.locator('#statusWin')).toHaveAttribute(
        'data-revision',
        String(snap.revision),
      );
      await expect(page.locator('#statusWin')).toHaveAttribute('data-loading', '0');
      const table = await engineCall<{
        rows: {
          shape: string;
          fixed: number;
          variable: number;
          identicalMirror: string | null;
          identicalShape: string | null;
          identicalComplete: string | null;
          connectedFace: boolean;
          connectedEdge: boolean;
          connectedCorner: boolean;
          holes2D: boolean;
          holes3D: boolean;
          notchable: boolean | null;
          millable: boolean | null;
          symmetry: number | null;
          statusLine: string;
        }[];
      }>(page, 'shape.table');
      const rows = page.locator('#statusWin tbody tr');
      await expect(rows).toHaveCount(table.rows.length);
      const label = (id: string | null): string => {
        if (id === null) {
          return '';
        }
        return `S${String(snap.shapes.findIndex((s) => s.id === id) + 1)}`;
      };
      const shown = await page.$$eval('#statusWin tbody tr', (trs) =>
        trs.map((tr) => ({
          shape: (tr as HTMLElement).dataset.shape ?? '',
          title: tr.getAttribute('title') ?? '',
          cells: Object.fromEntries(
            [...tr.querySelectorAll('td[data-k]')].map((td) => [
              (td as HTMLElement).dataset.k ?? '',
              td.textContent,
            ]),
          ),
        })),
      );
      table.rows.forEach((r, i) => {
        const s = shown[i];
        const where = `${f} row ${String(i)}`;
        expect(s?.shape, where).toBe(r.shape);
        expect(s?.title, where).toBe(r.statusLine);
        const want: Record<string, string> = {
          fixed: String(r.fixed),
          variable: String(r.variable),
          total: String(r.fixed + r.variable),
          identicalMirror: label(r.identicalMirror),
          identicalShape: label(r.identicalShape),
          identicalComplete: label(r.identicalComplete),
          connectedFace: r.connectedFace ? 'X' : '',
          connectedEdge: r.connectedEdge ? 'X' : '',
          connectedCorner: r.connectedCorner ? 'X' : '',
          holes2D: r.holes2D ? 'X' : '',
          holes3D: r.holes3D ? 'X' : '',
          symmetry: r.symmetry === null ? '---' : String(r.symmetry),
        };
        if (snap.grid === 'bricks') {
          want.notchable = r.notchable === true ? 'X' : '';
          want.millable = r.millable === true ? 'X' : '';
        }
        expect(s?.cells, where).toEqual(want);
      });
    }

    // SolidSixPieceBurrs: the shapes identical to an earlier one when mirrored, selected and removed, one undo step
    await openFixture(page, 'SolidSixPieceBurrs.xmpuzzle');
    await expect(page.locator('#statusWin')).toHaveAttribute('data-loading', '0');
    const table = await engineCall<{ rows: { shape: string; identicalMirror: string | null }[] }>(
      page,
      'shape.table',
    );
    const identical = table.rows.filter((r) => r.identicalMirror !== null).map((r) => r.shape);
    expect(identical.length).toBeGreaterThan(0);
    await page.click('#statusWin [data-a="mirror"]');
    const ticked = await page.$$eval('#statusWin tbody tr', (trs) =>
      trs
        .filter((tr) => tr.querySelector<HTMLInputElement>('input')?.checked === true)
        .map((tr) => (tr as HTMLElement).dataset.shape ?? ''),
    );
    expect(ticked).toEqual(identical);
    const before = await engineCall<{ shapes: unknown[] }>(page, 'document.get');
    await page.click('#statusWin [data-a="remove"]');
    await expect(page.locator('#statusWin tbody tr')).toHaveCount(
      before.shapes.length - identical.length,
    );
    const history = await engineCall<{ undo: string[] }>(page, 'edit.history');
    expect(history.undo.at(-1) ?? history.undo[0]).toMatch(/Remove shapes/);
    await page.keyboard.press('Escape');
    await expect(page.locator('#statusWin')).toHaveCount(0);
    await page.keyboard.press('Control+z');
    await expect
      .poll(
        async () => (await engineCall<{ shapes: unknown[] }>(page, 'document.get')).shapes.length,
      )
      .toBe(before.shapes.length);
    expectNoRefusal(log);
  } finally {
    await a.close();
    rmSync(dir, { recursive: true, force: true });
  }
});
