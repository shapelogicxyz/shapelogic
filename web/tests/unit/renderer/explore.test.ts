// SPDX-License-Identifier: GPL-3.0-or-later
//
// The explorers' DOM-free parts: the movement tree as the client keeps it
// (nodes under their parents in the engine's order, Prune Tree keeping the
// path to a node and dropping its children, the rows depth first), the
// direction an Alt-drag asks for (the closest of the directions as they
// look on the screen, forward or reversed, within 45 degrees), and the
// debug panel's report (the original's rows, durations and notes); the
// placement of a piece (placedAt) as the assembled view's; an assembler
// handle opened once, and closed when its reply comes after the drawer
// closed, an opening under way told when it is dropped (handle-slot.ts);
// the opening of an assembly explorer, which the engine prepares on a
// thread of its own (explore-open.ts: the call, explore.ready, the
// progress, the cancel).

import { describe, expect, it } from 'vitest';

import type { MovementNode, SolveStats } from '../../../src/engine/api.generated.ts';
import type {
  CallArgs,
  EventName,
  EventParams,
  MethodName,
  Reply,
} from '../../../src/engine/client-types.ts';
import { EngineError } from '../../../src/engine/errors.ts';
import { openAssemblyExplorer } from '../../../src/engine/explore-open.ts';
import { formatDuration, hasData, statsReport } from '../../../src/renderer/explore/debug-stats.ts';
import { HandleSlot } from '../../../src/renderer/explore/handle-slot.ts';
import type { Opening } from '../../../src/renderer/explore/handle-slot.ts';
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
  it('goes to its cell minus its hotspot, scaled, centerd on the result', () => {
    expect(placedAt([3, 2, 1], [1, 1, 0], 'bricks', [4, 4, 2])).toEqual([0, -1, 0]);
    // the prisms' spacing: a cell is half a unit along x
    const t = placedAt([4, 1, 0], [0, 0, 0], 'prisms', [3, Math.sqrt(3), 1]);
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
    const r = statsReport(STATS, (s) => (s === 'classic' ? 'Complete (ShapeLogic)' : s));
    const rows = Object.fromEntries(
      r.sections.flatMap((s) => s.rows).map((x) => [x.label, x.value]),
    );
    expect(r.sections.map((s) => s.title)).toEqual([null, 'Assembly', 'Disassembly']);
    expect(rows).toMatchObject({
      'Solver:': 'Finished',
      'Solver Type:': 'Complete (ShapeLogic)',
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

describe('an explorer handle', () => {
  const later = (): {
    opening: Opening;
    resolve: (h: string | null) => void;
    canceled: boolean[];
  } => {
    let resolve: (h: string | null) => void = () => undefined;
    const handle = new Promise<string | null>((r) => {
      resolve = r;
    });
    const canceled: boolean[] = [];
    return { opening: { handle, cancel: (close) => canceled.push(close) }, resolve, canceled };
  };
  const opened = (h: string): Opening => ({ handle: Promise.resolve(h), cancel: () => undefined });

  it('is opened once however often it is asked for, and closed when dropped', async () => {
    const closed: string[] = [];
    const slot = new HandleSlot((h) => closed.push(h));
    let opens = 0;
    const open = later();
    const a = slot.ensure(() => {
      opens++;
      return open.opening;
    });
    const b = slot.ensure(() => {
      opens++;
      return open.opening;
    });
    open.resolve('h1');
    expect(await a).toBe('h1');
    expect(await b).toBe('h1');
    expect(await slot.ensure(() => opened('h2'))).toBe('h1');
    expect(opens).toBe(1);
    slot.drop();
    expect(closed).toEqual(['h1']);
    expect(slot.handle).toBeNull();
    // an opening that ended is not told of the drop
    expect(open.canceled).toEqual([]);
  });

  it('that comes after the drawer closed is closed, not kept (cleanup item 6.7)', async () => {
    const closed: string[] = [];
    const slot = new HandleSlot((h) => closed.push(h));
    const open = later();
    const late = slot.ensure(() => open.opening);
    slot.drop();
    open.resolve('h1');
    expect(await late).toBeNull();
    expect(slot.handle).toBeNull();
    expect(closed).toEqual(['h1']);
    // forgotten (the engine dropped the open one with its document): a late
    // one is still given back (the explorer's close then sends nothing for
    // another document)
    const again = later();
    const gone = slot.ensure(() => again.opening);
    slot.drop(false);
    again.resolve('h2');
    expect(await gone).toBeNull();
    expect(closed).toEqual(['h1', 'h2']);
  });

  it('being opened is told to stop when the slot is dropped (cleanup item 18.1)', async () => {
    const slot = new HandleSlot(() => undefined);
    // closed: the engine's build is to end, and what it holds to go
    const open = later();
    const pending = slot.ensure(() => open.opening);
    slot.drop();
    expect(open.canceled).toEqual([true]);
    // the canceled opening ends without a handle
    open.resolve(null);
    expect(await pending).toBeNull();
    // forgotten: the engine dropped it with its document, nothing is to be sent
    const again = later();
    void slot.ensure(() => again.opening);
    slot.drop(false);
    expect(again.canceled).toEqual([false]);
    // once only, and the next opening is its own
    slot.drop();
    expect(again.canceled).toEqual([false]);
    expect(await slot.ensure(() => opened('h3'))).toBe('h3');
  });
});

// The engine as openAssemblyExplorer needs it: the calls recorded and
// answered by hand, the events sent by hand.
class ScriptedExplorerEngine {
  readonly calls: {
    method: string;
    params: unknown;
    answer: (r: unknown) => void;
    fail: (e: unknown) => void;
  }[] = [];
  private readonly handlers = new Map<string, Set<(p: never) => void>>();

  call<M extends MethodName>(method: M, ...args: CallArgs<M>): Promise<Reply<M>> {
    return new Promise<Reply<M>>((resolve, reject) => {
      this.calls.push({
        method,
        params: args[0],
        answer: (r) => {
          resolve(r as Reply<M>);
        },
        fail: reject,
      });
    });
  }

  on<E extends EventName>(event: E, handler: (params: EventParams<E>) => void): () => void {
    const set = this.handlers.get(event) ?? new Set();
    this.handlers.set(event, set);
    set.add(handler);
    return () => set.delete(handler);
  }

  send<E extends EventName>(event: E, params: EventParams<E>): void {
    for (const h of this.handlers.get(event) ?? []) {
      (h as (p: EventParams<E>) => void)(params);
    }
  }

  listeners(): number {
    return [...this.handlers.values()].reduce((n, s) => n + s.size, 0);
  }
}

const READY = { ready: true, placementsSupported: true, finished: 0, message: null };
const PROGRESS = {
  pass: 0,
  done: 0,
  total: 0,
  placements: 7,
  removed: 0,
  passMs: 0,
  passSecondsLeft: null,
};
const tick = (): Promise<void> => new Promise((r) => setTimeout(r, 0));

describe('opening an assembly explorer (cleanup item 18.1)', () => {
  it('gives the explorer when explore.ready comes, with the progress meanwhile', async () => {
    const engine = new ScriptedExplorerEngine();
    const told: number[] = [];
    const opening = openAssemblyExplorer(engine, { problem: 'p1' }, (p) =>
      told.push(p.progress.placements),
    );
    expect(engine.calls.map((c) => c.method)).toEqual(['explore.openAssembly']);
    // another explorer's events are not this one's
    engine.send('explore.ready', { handle: 'ax9', ...READY });
    engine.calls[0]?.answer({ handle: 'ax1', pieces: 3 });
    await tick();
    engine.send('explore.preparing', { handle: 'ax9', reducing: false, progress: PROGRESS });
    engine.send('explore.preparing', { handle: 'ax1', reducing: false, progress: PROGRESS });
    engine.send('explore.ready', { handle: 'ax1', ...READY, finished: 0.5 });
    expect(await opening.done).toEqual({
      handle: 'ax1',
      pieces: 3,
      placementsSupported: true,
      finished: 0.5,
    });
    expect(told).toEqual([7]);
    expect(engine.listeners()).toBe(0);
    // the explorer is the caller's now: a cancel sends nothing
    opening.cancel(true);
    expect(engine.calls).toHaveLength(1);
  });

  it('takes an explore.ready that came before the reply was seen', async () => {
    const engine = new ScriptedExplorerEngine();
    const opening = openAssemblyExplorer(engine, { problem: 'p1' });
    engine.send('explore.ready', { handle: 'ax1', ...READY });
    engine.calls[0]?.answer({ handle: 'ax1', pieces: 3 });
    expect((await opening.done)?.handle).toBe('ax1');
    expect(engine.listeners()).toBe(0);
  });

  it('fails with the engine’s reason when the matrix cannot be made', async () => {
    const engine = new ScriptedExplorerEngine();
    const opening = openAssemblyExplorer(engine, { problem: 'p1' });
    engine.calls[0]?.answer({ handle: 'ax1', pieces: 3 });
    await tick();
    engine.send('explore.ready', {
      handle: 'ax1',
      ready: false,
      placementsSupported: false,
      finished: 0,
      message: 'A piece cannot be placed in the target shape',
    });
    const e: unknown = await opening.done.catch((x: unknown) => x);
    expect(e).toBeInstanceOf(EngineError);
    expect((e as EngineError).errc).toBe('invalid_argument');
    expect((e as EngineError).detail).toBe('A piece cannot be placed in the target shape');
    expect(engine.listeners()).toBe(0);
  });

  it('canceled while it is prepared closes the engine’s handle, or sends nothing', async () => {
    const engine = new ScriptedExplorerEngine();
    const told: number[] = [];
    const opening = openAssemblyExplorer(engine, { problem: 'p1' }, () => told.push(1));
    engine.calls[0]?.answer({ handle: 'ax1', pieces: 3 });
    await tick();
    opening.cancel(true);
    expect(await opening.done).toBeNull();
    expect(engine.calls.map((c) => [c.method, c.params])).toEqual([
      ['explore.openAssembly', { problem: 'p1' }],
      ['explore.close', { handle: 'ax1' }],
    ]);
    engine.send('explore.preparing', { handle: 'ax1', reducing: true, progress: PROGRESS });
    expect(told).toEqual([]);
    expect(engine.listeners()).toBe(0);

    // forgotten (another document, another engine): nothing is sent
    const other = new ScriptedExplorerEngine();
    const gone = openAssemblyExplorer(other, { problem: 'p1' });
    other.calls[0]?.answer({ handle: 'ax1', pieces: 3 });
    await tick();
    gone.cancel(false);
    expect(await gone.done).toBeNull();
    expect(other.calls).toHaveLength(1);
  });

  it('canceled before the reply closes the handle as soon as the reply gives it', async () => {
    const engine = new ScriptedExplorerEngine();
    const opening = openAssemblyExplorer(engine, { problem: 'p1' });
    opening.cancel(true);
    expect(engine.calls).toHaveLength(1);
    engine.calls[0]?.answer({ handle: 'ax1', pieces: 3 });
    expect(await opening.done).toBeNull();
    expect(engine.calls.map((c) => c.method)).toEqual(['explore.openAssembly', 'explore.close']);

    const quiet = new ScriptedExplorerEngine();
    const forgotten = openAssemblyExplorer(quiet, { problem: 'p1' });
    forgotten.cancel(false);
    quiet.calls[0]?.answer({ handle: 'ax1', pieces: 3 });
    expect(await forgotten.done).toBeNull();
    expect(quiet.calls).toHaveLength(1);
  });

  it('passes the call’s own failure on', async () => {
    const engine = new ScriptedExplorerEngine();
    const opening = openAssemblyExplorer(engine, { problem: 'p1' });
    engine.calls[0]?.fail(new EngineError(-32005, 'unsupported', 'no assembler'));
    await expect(opening.done).rejects.toMatchObject({ errc: 'unsupported' });
    expect(engine.listeners()).toBe(0);
  });
});
