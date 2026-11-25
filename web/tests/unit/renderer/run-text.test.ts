// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's words for a run (solver/run-text.ts) over a view made by
// hand: the module is DOM-free since cleanup item 13.2, so the tag, the
// empty scene's message, the status bar, the counters and the Activity and
// Details lines are checked without a page.

import { describe, expect, it } from 'vitest';

import type { ProblemSummary, Snapshot, SolveProgress } from '../../../src/engine/api.generated.ts';
import type { SolveRecord } from '../../../src/renderer/state/solves.ts';
import {
  activity,
  counts,
  details,
  emptyText,
  savingFor,
  statusLine,
  tagHtml,
} from '../../../src/renderer/solver/run-text.ts';
import type { RunView } from '../../../src/renderer/solver/run-text.ts';
import { DEFAULT_OPTIONS } from '../../../src/renderer/solver/settings.ts';

type Mutable<T> = { -readonly [K in keyof T]: T[K] };

const summary = (o: Partial<ProblemSummary> = {}): ProblemSummary =>
  ({
    id: 'p1',
    label: 'P1',
    name: 'Box',
    state: 'unknown',
    assemblies: 3,
    solutions: 2,
    timeMs: 1500,
    saved: 2,
    searched: 0,
    searchDepth: 0,
    triedApart: 0,
    ...o,
  }) as unknown as ProblemSummary;

function view(o: Partial<Mutable<RunView>> = {}): RunView {
  const problem = o.problem === undefined ? summary() : o.problem;
  const problems = problem === null ? [] : [problem];
  return {
    store: { snapshot: { problems, revision: 5 } as unknown as Snapshot, info: null },
    current: problem?.id ?? null,
    problem,
    record: null,
    options: { ...DEFAULT_OPTIONS, disassemble: true },
    family: 'brick',
    sliding: false,
    stacking: false,
    analysing: false,
    savedSearch: null,
    continueStartsOver: false,
    readyToSolve: true,
    stackingSolver: 'stacking',
    pickingUpMs: null,
    slidePhase: null,
    previewShown: false,
    sel: -1,
    pager: null,
    player: { row: null },
    explorer: { mode: null, tag: () => '' },
    autosave: { carriesOn: () => false, worksOn: () => false },
    stateOf: () => 'idle',
    isAborting: () => false,
    probChip: () => 'background:#123',
    ...o,
  };
}

const running = (progress: Partial<SolveProgress>): SolveRecord =>
  ({
    problem: 'p1',
    state: 'assembling',
    told: true,
    phase: 'active',
    launched: true,
    solver: 'classic',
    progress: {
      stage: 'assemble',
      assemblies: 40,
      solutions: 7,
      usedMs: 9000,
      secondsLeft: 12,
      overall: 0.5,
      activity: '',
      iterations: 0,
      ...progress,
    },
    failure: null,
    added: null,
    preview: null,
    analysis: null,
    endRevision: -1,
  }) as unknown as SolveRecord;

describe('run-text', () => {
  it('names the solve state on the Activity line', () => {
    expect(activity(view())).toBe('Ready to Solve (the puzzle changed)');
    expect(activity(view({ readyToSolve: false }))).toBe('Not ready to solve');
    expect(activity(view({ analysing: true }))).toBe('Analysing disassemblies');
    expect(activity(view({ isAborting: () => true }))).toBe('Aborting the search…');
    expect(activity(view({ problem: summary({ state: 'solved' }) }))).toBe('Search finished');
    expect(
      activity(view({ problem: summary({ state: 'solving' }), continueStartsOver: true })),
    ).toBe('Search paused, its saved search gone');
    expect(activity(view({ savedSearch: {} }))).toBe('Search paused and saved');
    expect(activity(view({ record: running({}) }))).toBe(
      'Search active with the Complete (ShapeLogic) solver',
    );
    expect(activity(view({ record: running({ stage: 'stopping', stopKind: 'pause' }) }))).toBe(
      'Pausing the search…',
    );
  });

  it('says a save that carries the run on', () => {
    const r = running({ stage: 'stopping', stopKind: 'pause' });
    const v = view({ record: r, autosave: { carriesOn: () => true, worksOn: () => false } });
    expect(savingFor(v, r)).toBe(true);
    expect(activity(v)).toBe('Saving the search, then carrying on…');
    // an abort is no save
    const a = running({ stage: 'stopping', stopKind: 'abort' });
    expect(savingFor(view({ autosave: v.autosave }), a)).toBe(false);
  });

  it('counts a run from its progress, else the problem', () => {
    expect(counts(view())).toEqual({
      assemblies: 3,
      solutions: 2,
      timeMs: 1500,
      secondsLeft: null,
      overall: 0,
    });
    expect(counts(view({ record: running({}) }))).toEqual({
      assemblies: 40,
      solutions: 7,
      timeMs: 9000,
      secondsLeft: 12,
      overall: 0.5,
    });
  });

  it('writes the status bar and the Details line', () => {
    expect(statusLine(view())).toBe(
      'Problem <b>P1</b> — 3 assemblies found, 2 can be disassembled · Activity: Ready to Solve (the puzzle changed)',
    );
    expect(statusLine(view({ problem: null }))).toBe(
      'No problems — make one in the <b>Puzzle</b> workspace',
    );
    expect(details(view({ problem: summary({ searched: 2_500_000, triedApart: 928 }) }))).toBe(
      'Scanned 2.5 million search steps. Tried to take apart 928 assemblies.',
    );
    expect(details(view({ record: running({ activity: 'assemble', iterations: 1234 }) }))).toBe(
      'Assemble. Scanned 1,234 search steps.',
    );
  });

  it('draws the tag and the empty scene', () => {
    expect(emptyText(view())).toBe('Run the solver to see solutions here');
    expect(emptyText(view({ pager: { total: 3 } }))).toBe(
      'Choose a solution in the list to see it here',
    );
    expect(emptyText(view({ problem: null }))).toBe(
      'No problems — make one in the Puzzle workspace',
    );
    const shown = tagHtml(
      view({
        problem: summary({ name: '<Box>' }),
        sel: 1,
        pager: { total: 3 },
        player: { row: {} },
      }),
    );
    expect(shown).toContain('&lt;Box&gt;');
    expect(shown).toContain('· Solution 2 of 3');
    expect(shown).toContain('background:#123');
  });
});
