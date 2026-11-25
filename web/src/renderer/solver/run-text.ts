// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's words for a run (cleanup item 13.2): the view's tag, the
// message over an empty scene, the status bar, the counters, the Activity
// and Details lines, and the status wording they are made of (the counts,
// the stacking, sliding and prepare lines, a stop's, a saved solve's).
// DOM-free, over `RunView`, a narrow read-only view of the workspace, so
// that the unit tests can run it. From docs/mockup/index.html: `svTag`,
// `svStatus` and renderSolverScene's message; the Details lines from the
// original's mainwindow.cpp (`shortCount`, finishedActivity).

import type {
  Methods,
  ProblemSummary,
  Snapshot,
  SolveProgress,
  SolveState,
  StackingSolver,
} from '../../engine/api.generated.ts';
import { esc } from '../shell/html.ts';
import { isActive } from '../state/solves.ts';
import type { SolveRecord } from '../state/solves.ts';
import { sizeText } from '../state/stacking.ts';
import { phaseTag, phaseText, PICKING_UP_NOTE, pickingUpText } from './performance.ts';
import type { SlidePhase } from './performance.ts';
import { solverLabel } from './settings.ts';
import type { Family, SearchOptions } from './settings.ts';
import { formatSpan } from './span.ts';

/** What the run's words read of the Solver workspace (SolverWorkspace is one). */
export interface RunView {
  readonly store: {
    readonly snapshot: Snapshot | null;
    readonly info: Methods['engine.info']['result'] | null;
  };
  /** The problem shown, its summary and its solve record. */
  readonly current: string | null;
  readonly problem: ProblemSummary | null;
  readonly record: SolveRecord | null;
  readonly options: Readonly<SearchOptions>;
  readonly family: Family;
  readonly sliding: boolean;
  readonly stacking: boolean;
  readonly analysing: boolean;
  /** A stacking problem's saved search (only whether there is one counts here). */
  readonly savedSearch: unknown;
  readonly continueStartsOver: boolean;
  readonly readyToSolve: boolean;
  readonly stackingSolver: StackingSolver;
  readonly pickingUpMs: number | null;
  readonly slidePhase: SlidePhase | null;
  readonly previewShown: boolean;
  /** The solution selected (its place in the list), the list's length. */
  readonly sel: number;
  readonly pager: { readonly total: number | null } | null;
  readonly player: { readonly row: unknown };
  readonly explorer: { readonly mode: unknown; tag(): string };
  readonly autosave: {
    carriesOn(problem: string): boolean;
    worksOn(problem: string): boolean;
  };
  stateOf(problem: string): SolveState;
  isAborting(problem: string): boolean;
  probChip(): string;
}

/** The counters of the problem shown (`counts`). */
export interface RunCounts {
  assemblies: number | null;
  solutions: number | null;
  /** The problem's time used, earlier runs of a continued solve included. */
  timeMs: number | null;
  secondsLeft: number | null;
  overall: number;
}

/** The view's tag (the mockup's svTag): the problem's chip and name, and the solution shown. */
export function tagHtml(v: RunView): string {
  const p = v.problem;
  if (p === null) {
    return '';
  }
  const n = v.pager?.total ?? 0;
  const cur = v.player.row !== null && v.sel >= 0 && n > 0;
  const ex = v.explorer.tag();
  // the picture stands still while the search visits nothing: why
  const phase = v.slidePhase;
  const live = v.previewShown
    ? `<span class="mute">· Live preview${phase === null ? '' : ` (on hold: ${phaseTag(phase)})`}</span>`
    : '';
  return `<span class="chip" style="${v.probChip()}">${esc(p.label)}</span><span class="nm">${esc(p.name) || 'Problem'}</span>${ex !== '' ? ex : cur ? `<span class="mute">· Solution ${String(v.sel + 1)} of ${String(n)}</span>` : live}`;
}

/** The message over an empty scene (the mockup's renderSolverScene's). */
export function emptyText(v: RunView): string {
  const snap = v.store.snapshot;
  if (snap === null) {
    return '';
  }
  if (snap.problems.length === 0) {
    return 'No problems — make one in the Puzzle workspace';
  }
  if (v.player.row !== null || v.explorer.mode !== null || v.previewShown) {
    return '';
  }
  if ((v.pager?.total ?? 0) > 0) {
    return 'Choose a solution in the list to see it here';
  }
  const state = v.current === null ? 'idle' : v.stateOf(v.current);
  if (isActive(state)) {
    const picking = v.pickingUpMs;
    if (picking !== null) {
      return `${pickingUpText(picking)}\n${PICKING_UP_NOTE}`;
    }
    const phase = v.slidePhase;
    const depth = v.record?.progress?.searchDepth ?? 0;
    return phase === null ? 'Searching…' : `Searching…\n${phaseText(phase, depth + 1)}`;
  }
  const p = v.problem;
  if (p !== null && (p.assemblies ?? 0) > 0 && p.saved === 0 && v.options.disassemble) {
    return 'No assembly can be taken apart — nothing to show';
  }
  return 'Run the solver to see solutions here';
}

/** The status bar in the Solver workspace (HTML): the mockup's svStatus. */
export function statusLine(v: RunView): string {
  const snap = v.store.snapshot;
  if (snap !== null && snap.problems.length === 0) {
    return 'No problems — make one in the <b>Puzzle</b> workspace';
  }
  const p = v.problem;
  if (p === null) {
    return '';
  }
  const c = counts(v);
  const dis = v.options.disassemble;
  return `Problem <b>${esc(p.label)}</b> — ${String(c.assemblies ?? 0)} assemblies found${dis ? `, ${String(c.solutions ?? 0)} can be disassembled` : ''} · Activity: ${esc(activity(v))}`;
}

/** The counters: a run's from its progress, else the problem's. */
export function counts(v: RunView): RunCounts {
  const p = v.problem;
  const r = v.record;
  const g = r?.progress ?? null;
  // a run's counters until the snapshot shows its end
  const shownRev = v.store.snapshot?.revision ?? -1;
  if (g !== null && r !== null && (r.phase !== 'none' || shownRev < r.endRevision)) {
    return {
      assemblies: g.assemblies,
      solutions: g.solutions,
      timeMs: g.usedMs,
      secondsLeft: g.secondsLeft,
      overall: g.overall,
    };
  }
  const done = p?.state === 'solved';
  return {
    assemblies: p?.assemblies ?? null,
    solutions: p?.solutions ?? null,
    timeMs: p?.timeMs ?? null,
    secondsLeft: null,
    overall: done ? 1 : (g?.overall ?? 0),
  };
}

/** Whether the run shown is paused by a save that carries it on (not by Pause, not aborted). */
export function savingFor(v: RunView, r: SolveRecord | null): boolean {
  if (r === null || !v.autosave.carriesOn(r.problem)) {
    return false;
  }
  // a sliding search asked for a save point searches on to its level's
  // end, and is shown searching until its run ends
  if (r.phase === 'active' && isActive(r.state) && v.autosave.worksOn(r.problem)) {
    return false;
  }
  const g = r.progress;
  return g?.stage !== 'stopping' || g.stopKind !== 'abort';
}

/** The name of the solver the options choose, from the engine's list. */
function solverName(v: RunView): string {
  return solverLabel(v.store.info, v.family, v.options, v.stackingSolver);
}

/** The Activity text: the solver's state (what it has found and gone through is details()). */
export function activity(v: RunView): string {
  const r = v.record;
  const p = v.problem;
  if (v.analysing) {
    return 'Analysing disassemblies';
  }
  if (v.current !== null && v.isAborting(v.current)) {
    return 'Aborting the search…';
  }
  // paused for a save (the crash recovery copy, File ▸ Save), to carry on
  if (savingFor(v, r)) {
    return 'Saving the search, then carrying on…';
  }
  if (r !== null && r.phase !== 'none' && r.progress !== null) {
    switch (r.progress.stage) {
      case 'stopping':
        return r.progress.stopKind === 'abort' ? 'Aborting the search…' : 'Pausing the search…';
      case 'paused':
        return 'Search paused';
      case 'done':
        return 'Search finished';
      case 'error':
        return 'Search failed';
      case 'idle':
        break;
      default:
        return searchActive(solverName(v));
    }
  }
  // a run started whose first progress has not come yet
  if (v.current !== null && isActive(v.stateOf(v.current))) {
    return searchActive(solverName(v));
  }
  // a stacking problem's search saved on disk (updateInterface)
  if (v.savedSearch !== null) {
    return 'Search paused and saved';
  }
  if (r?.failure !== null && r?.failure !== undefined && v.stateOf(r.problem) === 'error') {
    return 'Search failed';
  }
  if (p === null) {
    return 'No problem to solve';
  }
  switch (p.state) {
    case 'solved':
      return 'Search finished';
    case 'solving':
      return v.continueStartsOver ? 'Search paused, its saved search gone' : 'Search paused';
    default: {
      // what Solve's tooltip says when it is grayed (run-card.ts)
      if (!v.readyToSolve) {
        return 'Not ready to solve';
      }
      return p.state === 'unknown' ? 'Ready to Solve (the puzzle changed)' : 'Ready to Solve';
    }
  }
}

/**
 * The Details text: how much the search has gone through while it runs
 * (the original's Activity line), else what the problem's solves went
 * through, as kept in the file; empty when nothing is known.
 */
export function details(v: RunView): string {
  const r = v.record;
  const g = r?.progress ?? null;
  const running =
    r !== null &&
    r.phase !== 'none' &&
    g !== null &&
    g.stage !== 'idle' &&
    g.stage !== 'paused' &&
    g.stage !== 'done' &&
    g.stage !== 'error';
  if (running) {
    // a sliding or stacking search being stopped keeps its line, and says
    // what keeps it from stopping at once
    const stopping = g.stage === 'stopping';
    if (g.stage === 'search' || (stopping && v.family !== 'brick')) {
      const what = v.stacking
        ? stackingDetails(g.panexSearch, g.searched, g.searchDepth, g.searchTraced)
        : slidingDetails(g.searched, g.searchDepth);
      // between moves: what the search does that visits nothing
      const part =
        v.sliding && g.slidePhase !== null ? phaseText(g.slidePhase, g.searchDepth + 1) : '';
      if (part !== '') {
        return stopping
          ? `${what} ${part} ${stopDetails(g, v.family, savingFor(v, r))}`
          : `${what} ${part}`;
      }
      return stopping ? `${what} ${stopDetails(g, v.family, savingFor(v, r))}` : what;
    }
    // preparing: the piece, and counts that move while one piece takes minutes
    if (g.stage === 'prepare' || g.stage === 'reduce') {
      return prepareDetails(g);
    }
    // the engine's words for the stage (solveProgress_c::activity), and the search steps
    const what =
      g.activity === '' ? '' : `${g.activity.charAt(0).toUpperCase()}${g.activity.slice(1)}.`;
    const steps = g.iterations > 0 ? `Scanned ${shortCount(g.iterations)} search steps.` : '';
    return [what, steps].filter((x) => x !== '').join(' ');
  }
  const p = v.problem;
  return p === null
    ? ''
    : savedDetails(v.family, p.searched, p.searchDepth, p.triedApart, p.state === 'solving');
}

/** A count for the Details line: "999,999", then "125.3 million", "23.53 billion"… (mainwindow.cpp's shortCount). */
export function shortCount(n: number): string {
  if (n < 1_000_000) {
    return String(Math.trunc(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
  }
  const names = ['million', 'billion', 'trillion', 'quadrillion', 'quintillion'];
  let v = n / 1e6;
  let step = 0;
  // a value that rounds up to 1000 belongs to the next name
  while (step < 4 && Math.round(v * 10 ** (step + 1)) >= 1000 * 10 ** (step + 1)) {
    v /= 1000;
    step++;
  }
  return `${v.toFixed(step + 1)} ${names[step] ?? ''}`;
}

/**
 * What a stacking search went through: the states (stackings) it scanned,
 * and how deep it searched from each end. The depth kept is both ends'
 * moves together (the Panex search's), so each end's is half of it: 2,679
 * for a path of 5,359 moves. The Stacking Solver's own search keeps none.
 */
function stackingSearched(searched: number, depth: number): string {
  const half = Math.floor(depth / 2);
  const scanned = `Scanned ${shortCount(searched)} states.`;
  return half > 0 ? `${scanned} Search depth: ${shortCount(half)}` : scanned;
}

/**
 * The Details line while a stacking search runs (the Activity line names
 * the search): the states scanned and the depth from each end, or the
 * Panex search's trace back once its ends have met.
 */
export function stackingDetails(
  panex: boolean,
  searched: number,
  depth: number,
  traced: number,
): string {
  return panex && traced > 0
    ? `Tracing the path. Traced ${shortCount(traced)} of ${shortCount(depth)} moves.`
    : stackingSearched(searched, depth);
}

/**
 * The Details line while a brick solve prepares its search: the engine's
 * piece of pieces (`activity`, "prepare piece 6 of 9"), with counts that
 * move while one piece takes minutes. Preparing: the placements found so
 * far. Optimizing (the reduce): its first sweep over the cells, then its
 * passes over the placements, each with how much it has checked, what it
 * has removed and left, and the engine's rough time left in that pass
 * (another pass follows while one removes anything, so not of the step).
 */
export function prepareDetails(
  g: Pick<SolveProgress, 'stage' | 'activity' | 'piece' | 'pieces' | 'prepare'>,
): string {
  const what =
    g.activity === '' ? '' : `${g.activity.charAt(0).toUpperCase()}${g.activity.slice(1)}`;
  const r = g.prepare;
  if (r === null) {
    return what === '' ? '' : `${what}.`;
  }
  if (g.stage === 'prepare') {
    return r.placements > 0
      ? `${what}. ${shortCount(r.placements)} placements so far.`
      : `${what}.`;
  }
  const pct = r.total > 0 ? ` (${String(Math.floor((r.done * 100) / r.total))}%)` : '';
  const counts = `${shortCount(r.removed)} placements removed, ${shortCount(r.placements)} left.`;
  const step = r.pass === 0 ? 'the sweep' : 'this pass';
  const left =
    r.passSecondsLeft === null
      ? ''
      : ` About ${formatSpan(r.passSecondsLeft * 1000)} left in ${step}.`;
  if (r.pass === 0) {
    // the sweep goes over the cells, not one piece after another
    return `Optimize: sweeping the cells, ${shortCount(r.done)} of ${shortCount(r.total)} checked${pct}. ${counts}${left}`;
  }
  return `${what}, pass ${String(r.pass)}: ${shortCount(r.done)} of ${shortCount(r.total)} placements checked${pct}. ${counts}${left}`;
}

/**
 * The Details line while a sliding search runs (the engine's activity is
 * empty there): the original's "N moves deep. Scanned N arrangements."
 * without the search's name, and without the depth before the full
 * search's first move.
 */
export function slidingDetails(searched: number, depth: number): string {
  return depth > 0
    ? `${shortCount(depth)} moves deep. Scanned ${shortCount(searched)} arrangements.`
    : `Scanned ${shortCount(searched)} arrangements.`;
}

/**
 * What a run asked to stop is doing, after its search's Details line: a
 * sliding search finishes the level under way (merging it cannot be cut
 * short), then, for a pause, writes its levels to disk for Continue, so
 * much of so much; a Panex search saves itself; an abort keeps nothing.
 * `carryOn`: the pause is a save's (the crash recovery copy, File ▸ Save),
 * which carries the solve on.
 */
export function stopDetails(
  g: Pick<SolveProgress, 'stopKind' | 'slideStop' | 'panexSearch'>,
  family: Family,
  carryOn = false,
): string {
  const s = g.slideStop;
  if (g.stopKind === 'abort') {
    return s !== null
      ? 'Aborting: finishing the level under way; nothing is saved.'
      : 'Aborting: waiting for the search to stop.';
  }
  if (s?.saving === true) {
    const written = `${sizeText(s.doneBytes)} of ${sizeText(s.totalBytes)} written`;
    return carryOn
      ? `Saving the search to disk, ${written}; then it carries on.`
      : `Pausing: saving the search to disk for Continue, ${written}.`;
  }
  if (s !== null) {
    return carryOn
      ? 'Finishing the level under way, which can’t be cut short; then the search is saved to disk and carries on.'
      : 'Pausing: finishing the level under way, which can’t be cut short; then the search is saved to disk for Continue.';
  }
  if (family === 'stacking' && g.panexSearch) {
    return carryOn
      ? 'The search is saving itself; then it carries on.'
      : 'Pausing: the search is saving itself for Continue.';
  }
  return carryOn
    ? 'Waiting for the search to stop; then it carries on.'
    : 'Pausing: waiting for the search to stop.';
}

/**
 * The Details line of a problem not being solved: what its solves went
 * through, as kept in the file (mainwindow.cpp's finishedActivity without
 * its "Finished.", a stacking search's as it shows while it runs); empty
 * when nothing is known. `open`: the solve is not through (paused, or so
 * in the file), and a sliding one says what it said while it ran.
 */
export function savedDetails(
  family: Family,
  searched: number,
  depth: number,
  apart: number,
  open = false,
): string {
  if (family === 'stacking') {
    return searched > 0 || depth > 0 ? stackingSearched(searched, depth) : '';
  }
  if (family === 'sliding' && open) {
    return searched > 0 || depth > 0 ? slidingDetails(searched, depth) : '';
  }
  const what = family === 'sliding' ? 'arrangements' : 'search steps';
  const parts: string[] = [];
  if (searched > 0) {
    parts.push(`Scanned ${shortCount(searched)} ${what}.`);
  }
  if (depth > 0) {
    parts.push(`Searched ${shortCount(depth)} moves deep.`);
  } else if (apart > 0) {
    parts.push(`Tried to take apart ${shortCount(apart)} assemblies.`);
  }
  return parts.join(' ');
}

/**
 * The Activity line while a search runs: the solver's name from the
 * engine's list ("Complete (ShapeLogic)", "Sliding Puzzle Solver"…),
 * with "solver" after a name that does not say it.
 */
export function searchActive(label: string): string {
  return /solver/i.test(label)
    ? `Search active with the ${label}`
    : `Search active with the ${label} solver`;
}
