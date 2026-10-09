// SPDX-License-Identifier: GPL-3.0-or-later
//
// The explorers' DOM-free parts: the movement tree as the client keeps it
// (nodes under their parents in the engine's order, Prune Tree keeping the
// path to a node and dropping its children, the rows depth first), the
// direction an Alt-drag asks for (the closest of the directions as they
// look on the screen, forward or reversed, within 45 degrees), and the
// debug panel's report (the original's rows, durations and notes); the
// placement of a piece (placedAt) as the assembled view's.

import { describe, expect, it } from 'vitest';

import type { MovementNode, SolveStats } from '../../../src/engine/api.generated.ts';
import { formatDuration, hasData, statsReport } from '../../../src/renderer/explore/debug-stats.ts';
import { dragDirection, MovementTree } from '../../../src/renderer/explore/tree.ts';
import { placedAt } from '../../../src/renderer/view3d/extent.ts';

function node(id: number, parent: number, children: number[] = [], label = ''): MovementNode {
  return { node: id, parent, label, pieces: [{ piece: 0, at: [id, 0, 0] }], children };
}

describe('the movement tree', () => {
  it('puts nodes under their parents in the order they came, once', () => {
    const t = new MovementTree(node(0, 0));
    t.put([node(1, 0, [], 'x: S1'), node(2, 0, [], '-x: S2')]);
    t.put([node(1, 0, [], 'x: S1')]);
    t.put([node(3, 1, [], 'y: S1')]);
    expect(t.get(0)?.children).toEqual([1, 2]);
    expect(t.get(1)?.children).toEqual([3]);
    expect(t.rows().map((r) => [r.node.node, r.depth])).toEqual([
      [0, 0],
      [1, 1],
      [3, 2],
      [2, 1],
    ]);
    expect(t.path(3)).toEqual([0, 1, 3]);
    expect(t.path(9)).toEqual([]);
    expect(t.size).toBe(4);
  });

  it('keeps a node the engine replied with as the engine has it (its own children)', () => {
    const t = new MovementTree(node(0, 0));
    t.put([node(1, 0)]);
    // explore.node's answer after an analyse elsewhere: the engine's children win
    t.put([node(0, 0, [1, 5])]);
    expect(t.get(0)?.children).toEqual([1, 5]);
  });

  it('prunes to the path from the root, the node’s children gone too (the engine’s Prune Tree)', () => {
    const t = new MovementTree(node(0, 0));
    t.put([node(1, 0), node(2, 0), node(3, 1), node(4, 1), node(5, 3), node(6, 2)]);
    t.prune(3);
    expect(t.rows().map((r) => r.node.node)).toEqual([0, 1, 3]);
    expect(t.get(0)?.children).toEqual([1]);
    expect(t.get(1)?.children).toEqual([3]);
    expect(t.get(3)?.children).toEqual([]);
    expect(t.get(5)).toBeUndefined();
    // pruning at a node not in the tree changes nothing
    t.prune(42);
    expect(t.size).toBe(3);
  });
});

describe('the direction of an Alt-drag', () => {
  // the cube's x, y, z as a view might show them
  const screen: [number, number][] = [
    [40, 5],
    [0, -40],
    [-20, 18],
  ];

  it('takes the closest direction, forward or reversed', () => {
    expect(dragDirection([60, 0], screen)).toEqual({ direction: 0, reverse: false });
    expect(dragDirection([-60, -10], screen)).toEqual({ direction: 0, reverse: true });
    expect(dragDirection([0, -60], screen)).toEqual({ direction: 1, reverse: false });
    expect(dragDirection([0, 50], screen)).toEqual({ direction: 1, reverse: true });
    expect(dragDirection([30, -27], screen)).toEqual({ direction: 2, reverse: true });
  });

  it('takes none further than 45 degrees from every direction, nor a click', () => {
    expect(dragDirection([10, 9.9], [[10, 0]])).toEqual({ direction: 0, reverse: false });
    expect(dragDirection([10, 11], [[10, 0]])).toBeNull();
    expect(dragDirection([0, 0], screen)).toBeNull();
  });

  it('skips a direction seen end on', () => {
    expect(
      dragDirection(
        [5, 5],
        [
          [0, 0],
          [10, 9],
        ],
      ),
    ).toEqual({ direction: 1, reverse: false });
  });
});

describe('a placed piece', () => {
  it('goes to its cell minus its hotspot, scaled, centred on the result', () => {
    expect(placedAt([3, 2, 1], [1, 1, 0], [1, 1, 1], [4, 4, 2])).toEqual([0, -1, 0]);
    // the prisms' spacing: a cell is half a unit along x
    const t = placedAt([4, 1, 0], [0, 0, 0], [0.5, Math.sqrt(3) / 2, 1], [3, Math.sqrt(3), 1]);
    expect(t[0]).toBeCloseTo(0.5, 10);
    expect(t[1]).toBeCloseTo(0, 10);
    expect(t[2]).toBeCloseTo(-0.5, 10);
  });
});

const STATS: SolveStats = {
  state: 'finished',
  solver: 'classic',
  disassemblyEnabled: true,
  rotationsEnabled: false,
  hardwareThreads: 10,
  assemblerThreads: 1,
  disasmWorkers: 4,
  assembliesFound: 50,
  solutionsFound: 5,
  inseparable: 45,
  iterations: 123456,
  assemblyProgress: 1,
  pending: 0,
  peakPending: 2,
  disasmCompleted: 50,
  elapsedMs: 65_000,
  prepareMs: 12,
  reduceMs: 900,
  assemblyMs: 61_000,
  disasmWorkMs: 4_000,
  linearSearchMs: 3_000,
  rotationSearchMs: 0,
  drainMs: 10,
  avgDisasmSeconds: 0.08,
  running: [],
};

describe('the debug statistics report', () => {
  it('writes durations as the original’s formatDuration', () => {
    expect(formatDuration(0)).toBe('0 ms');
    expect(formatDuration(999)).toBe('999 ms');
    expect(formatDuration(1000)).toBe('1.0 s');
    expect(formatDuration(59_940)).toBe('59.9 s');
    expect(formatDuration(65_000)).toBe('1.1 min');
    expect(formatDuration(3_600_000)).toBe('1.0 h');
  });

  it('has the idle rows for a solve never run', () => {
    const idle = { ...STATS, state: 'idle' as const, elapsedMs: 0, iterations: 0 };
    expect(hasData(idle)).toBe(false);
    const r = statsReport(idle, (s) => s);
    expect(r.sections[0]?.rows[0]).toEqual({ label: 'Solver:', value: 'Idle', indent: false });
    expect(r.sections.flatMap((s) => s.rows).every((x) => !x.indent || x.value === '—')).toBe(true);
    expect(r.notes).toHaveLength(1);
  });

  it('has the original’s rows and values for a finished run with take-apart', () => {
    const r = statsReport(STATS, (s) => (s === 'classic' ? 'BurrTools Classic' : s));
    const rows = Object.fromEntries(
      r.sections.flatMap((s) => s.rows).map((x) => [x.label, x.value]),
    );
    expect(r.sections.map((s) => s.title)).toEqual([null, 'Assembly', 'Disassembly']);
    expect(rows).toMatchObject({
      'Solver:': 'Finished',
      'Solver Type:': 'BurrTools Classic',
      'Assemblies considered:': '50',
      'Valid (disassemblable):': '5',
      'Inseparable assemblies:': '45',
      'DLX iterations:': '123456',
      'Assembly search progress:': '100.0 %',
      'Time finding assemblies:': '1.0 min',
      'Prepare time:': '12 ms',
      'Reduce / optimize time:': '900 ms',
      'Disassembly workers:': '4',
      'Average time per disassembly:': '0.080 s',
      'Rotation move search time:': 'off',
      'Elapsed wall time:': '1.1 min',
    });
    expect(r.notes[0]).toBe('The solver finished. These figures are from the completed run.');
    // the queue stayed small: assembly limited the run; many assemblies did not come apart
    expect(r.notes).toContain(
      'Bottleneck: disassembly workers stayed idle most of the time, so assembly search (DLX) limited the run.',
    );
    expect(r.notes.some((n) => n.startsWith('Many assemblies could not be taken apart'))).toBe(
      true,
    );
    expect(r.notes.at(-1)).toMatch(
      /^Worth considering for future performance work: parallel assembly/,
    );
  });

  it('marks the take-apart rows off without it, and has a row per take-apart under way', () => {
    const off = statsReport({ ...STATS, disassemblyEnabled: false }, (s) => s);
    const rows = off.sections.flatMap((s) => s.rows);
    expect(rows.find((x) => x.label === 'Disassembly workers:')?.value).toBe('off');
    expect(rows.find((x) => x.label === 'Queue pending:')?.value).toBe('—');
    const running = statsReport(
      {
        ...STATS,
        state: 'assembling',
        running: [
          {
            elapsedMs: 2000,
            pieces: 6,
            searchPieces: 4,
            separations: 1,
            depth: 3,
            level: 2,
            levelDone: 10,
            levelSize: 40,
            nextLevelSize: 7,
            nodes: 500,
            threads: 1,
          },
        ],
      },
      (s) => s,
    );
    const live = running.sections.flatMap((s) => s.rows);
    expect(live.find((x) => x.label === 'Solver:')?.value).toBe('Running');
    expect(live.find((x) => x.label === 'Disassembly 1 running:')?.value).toBe(
      '2.0 s, level 2: 10 of 40 positions (next level 7 so far), 500 positions in all (250 a second), 1 of 5 separations, searching 4 of 6 pieces, 1 thread',
    );
  });
});
