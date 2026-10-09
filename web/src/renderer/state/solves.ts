// SPDX-License-Identifier: GPL-3.0-or-later
//
// The solve records (decision 0013, "State": a record per problem being
// solved, since several problems may run): what the engine's events told
// of each problem's solve and analysis, kept by its id. Platform-free (no
// DOM), so the fake engine's recordings drive it in the unit tests.
//
// The rules of decision 0012 it follows:
// - The state is the events' (solve.state, and solve.progress's state),
//   fresher than the snapshot between two document.changed.
// - A run's end is its revision, not its state: after a run's last
//   solve.state (finished, paused, error, idle) comes its progress, its
//   solutionsAdded and then the run's own document.changed, which names
//   the problem; only then is the run over (`phase` none, `endRevision`),
//   and what waits for it (`whenEnded`) goes on. A run that starts and
//   ends between two polls shows no active state at all: the client knows
//   it started one (`launch`), or the state moved between two ends (idle
//   to finished), so it waits all the same. An abort of a run yields two
//   document.changed: the abort's own comes while the run is still active
//   here and does not end it; the run's end comes after its idle. An abort
//   of a solve paused in memory ends no run (one document.changed, before
//   the idle): paused to idle, not launched, is no run.
// - A new document ends the old one's work: its problems get a last idle
//   and analyseEnded {stopped} after the replacing call's reply, when the
//   store already shows the new document (whose ids may be the same); the
//   records are cleared then (`clear`), so a last idle met by no record is
//   no run, and an analyseEnded without an analysis under way is ignored.
// - solve.solutionsAdded is kept as told (`added`); whether the list must
//   be paged again is the pager's business (its solutionsVersion).

import type {
  SolveFailure,
  SolveProgress,
  SolveState,
  Solver,
} from '../../engine/api.generated.ts';
import type { EngineApi, EventParams } from '../../engine/client-types.ts';

/** Whether a run is under way: preparing, assembling, draining (the core's isActive). */
export function isActive(s: SolveState): boolean {
  return s === 'preparing' || s === 'assembling' || s === 'draining';
}

/** A run of a problem: none, under way, or over and waiting for its revision. */
export type RunPhase = 'none' | 'active' | 'ending';

/** An analysis (solution.analyse) as its events told it. */
export interface AnalysisRecord {
  active: boolean;
  total: number;
  done: number;
  separated: number;
  stopped: boolean;
  failure: string | null;
}

/** What the events told of one problem's solve. */
export interface SolveRecord {
  readonly problem: string;
  /** The state as the events last told it ('idle' until one did). */
  state: SolveState;
  /** Whether an event told the state (else the snapshot's is the better guess). */
  told: boolean;
  phase: RunPhase;
  /** This client started a run whose last state has not come yet. */
  launched: boolean;
  /** The solver this client last started or carried on the solve with; null when unknown. */
  solver: Solver | null;
  /** The last solve.progress (null before the first). */
  progress: SolveProgress | null;
  /** The failure of a run that ended in error (solve.error); cleared by the next launch. */
  failure: SolveFailure | null;
  /** The last solve.solutionsAdded. */
  added: EventParams<'solve.solutionsAdded'> | null;
  analysis: AnalysisRecord | null;
  /** The revision of the last run's end (its document.changed); -1 before one. */
  endRevision: number;
}

/** What changed in a record. */
export interface SolvesChange {
  problem: string | null;
  kind: 'state' | 'progress' | 'added' | 'error' | 'ended' | 'analysis' | 'launch' | 'cleared';
}

export class Solves {
  private readonly records = new Map<string, SolveRecord>();
  private readonly listeners = new Set<(c: SolvesChange) => void>();
  private ended: { problem: string; done: (revision: number) => void }[] = [];

  constructor(events: Pick<EngineApi, 'on'>) {
    events.on('solve.state', (p) => {
      this.state(p.problem, p.state);
    });
    events.on('solve.progress', (p) => {
      const r = this.get(p.problem);
      r.progress = p.progress;
      this.emit({ problem: p.problem, kind: 'progress' });
    });
    events.on('solve.solutionsAdded', (p) => {
      this.get(p.problem).added = p;
      this.emit({ problem: p.problem, kind: 'added' });
    });
    events.on('solve.error', (p) => {
      this.get(p.problem).failure = p.failure;
      this.emit({ problem: p.problem, kind: 'error' });
    });
    events.on('solution.analyseProgress', (p) => {
      const r = this.get(p.problem);
      r.analysis = { ...analysisOf(p), active: true };
      this.emit({ problem: p.problem, kind: 'analysis' });
    });
    events.on('solution.analyseEnded', (p) => {
      const r = this.records.get(p.problem);
      // a last analyseEnded of a replaced document meets no analysis here
      if (r?.analysis?.active !== true) {
        return;
      }
      r.analysis = { ...analysisOf(p), active: false };
      this.emit({ problem: p.problem, kind: 'analysis' });
    });
    events.on('document.changed', (p) => {
      for (const r of this.records.values()) {
        if (r.phase === 'ending' && (p.problems.length === 0 || p.problems.includes(r.problem))) {
          r.phase = 'none';
          r.endRevision = p.revision;
          this.emit({ problem: r.problem, kind: 'ended' });
          const ready = this.ended.filter((w) => w.problem === r.problem);
          this.ended = this.ended.filter((w) => w.problem !== r.problem);
          for (const w of ready) {
            w.done(p.revision);
          }
        }
      }
    });
  }

  /** The record of a problem (made when first asked for). */
  get(problem: string): SolveRecord {
    let r = this.records.get(problem);
    if (r === undefined) {
      r = {
        problem,
        state: 'idle',
        told: false,
        phase: 'none',
        launched: false,
        solver: null,
        progress: null,
        failure: null,
        added: null,
        analysis: null,
        endRevision: -1,
      };
      this.records.set(problem, r);
    }
    return r;
  }

  /** The record of a problem, if one was made. */
  peek(problem: string): SolveRecord | undefined {
    return this.records.get(problem);
  }

  /**
   * This client started or carried on a run (its call was answered): its
   * end is waited for even if no active state is seen.
   */
  launch(problem: string, solver: Solver | null): void {
    const r = this.get(problem);
    r.launched = true;
    r.failure = null;
    if (solver !== null) {
      r.solver = solver;
    }
    this.emit({ problem, kind: 'launch' });
  }

  /** This client started an analysis (its call was answered: total assemblies). */
  analysing(problem: string, total: number): void {
    this.get(problem).analysis = {
      active: true,
      total,
      done: 0,
      separated: 0,
      stopped: false,
      failure: null,
    };
    this.emit({ problem, kind: 'analysis' });
  }

  /** Forgets every record (another document: its ids are new). */
  clear(): void {
    this.records.clear();
    // nothing of the old document ends any more; resolve anyone waiting for
    // a run's end (autosave) with -1, since its document.changed will never
    // come now — a dropped waiter would hang the caller forever
    const waiting = this.ended;
    this.ended = [];
    for (const w of waiting) {
      w.done(-1);
    }
    this.emit({ problem: null, kind: 'cleared' });
  }

  /**
   * Resolves with the revision of the problem's run's end when the run is
   * over (at once when none is under way or ending).
   */
  whenEnded(problem: string): Promise<number> {
    const r = this.records.get(problem);
    // a run this client launched is waited for before its first state came
    if (r === undefined || (r.phase === 'none' && !r.launched)) {
      return Promise.resolve(r?.endRevision ?? -1);
    }
    return new Promise((done) => {
      this.ended.push({ problem, done });
    });
  }

  /** A problem whose BurrTools 2 search is paused in memory (decision 0011), or null. */
  pausedBt2(): string | null {
    for (const r of this.records.values()) {
      if (r.state === 'paused' && r.phase === 'none' && r.solver === 'bt2') {
        return r.problem;
      }
    }
    return null;
  }

  /** Whether any run is under way or ending (or launched and not yet told). */
  get running(): boolean {
    for (const r of this.records.values()) {
      if (r.phase !== 'none' || r.launched) {
        return true;
      }
    }
    return false;
  }

  /** Listens to changes; returns the unsubscribe. */
  subscribe(listener: (c: SolvesChange) => void): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  private state(problem: string, state: SolveState): void {
    const r = this.get(problem);
    const before = r.state;
    const wasTold = r.told;
    r.state = state;
    r.told = true;
    if (isActive(state)) {
      r.phase = 'active';
    } else if (r.phase === 'active' || r.launched) {
      r.phase = 'ending';
    } else if (wasTold && before !== state && !(before === 'paused' && state === 'idle')) {
      // the state moved between two ends: a run started and ended between
      // two polls (not an abort of a solve paused in memory, which ends no
      // run; a state told again, as a replaced document's last idle, is none)
      r.phase = 'ending';
    }
    if (!isActive(state)) {
      r.launched = false;
    }
    this.emit({ problem, kind: 'state' });
  }

  private emit(c: SolvesChange): void {
    for (const l of [...this.listeners]) {
      try {
        l(c);
      } catch (e) {
        console.error('solves: a listener threw', e);
      }
    }
  }
}

function analysisOf(p: EventParams<'solution.analyseEnded'>): Omit<AnalysisRecord, 'active'> {
  return {
    total: p.total,
    done: p.done,
    separated: p.separated,
    stopped: p.stopped,
    failure: p.failure,
  };
}
