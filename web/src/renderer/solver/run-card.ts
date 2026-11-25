// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's left card: the problem to solve (a list of the problems,
// as the Puzzle workspace's, each with its solve's state, since several
// may run, or what its finished solve found: problem-note.ts; the one
// shown's result and piece shapes below it), what to check
// (assemblies, or assemblies and their disassembly, with Just Levels and
// Check Rotations), the search options (stored with the problem, the
// original's rules graying out what does not apply; Keep at most and Keep
// every n-th are not stored, as in the original), the run: Solve
// (Continue in its place while a solve runs, is paused or has a saved
// search; starting over is Abort, then Solve), Pause, Abort while a solve
// runs or is paused, else Reset (the original's setAbortLabel), the
// progress bar and the counters, a failure, and Sort by (no Prepare: the
// explorers build assemblers of their own). From
// docs/mockup/index.html: `renderSolver` (the left card's sections, their
// wording and tooltips, `SVTIP`, `SVOPTSORT`) and its handlers in `svWire`;
// the run's buttons and the option names are the original's
// (mainwindow_layout.cpp) where the mockup had none: Solve, Pause, Abort,
// Check Rotations, Strict Colors, the solver type. The original's
// Autosave box is not shown: the crash recovery journal in its place is
// always on (autosave.ts). A sliding
// puzzle (docs/webui-spec.md, section 8.3) shows the original's sliding
// boxes and solvers instead (mainwindow_layout.cpp's tooltips,
// mainwindow_help.cpp's solver help), "Find Solutions" for the second
// mode, and Solve grayed while its start/goal shape is not valid
// (mainwindow.cpp's Solve tooltip). A stacking puzzle (section 9.3): Find
// Solutions with Just Levels, Just count and the one solver of its board (syncSolverTypeMenu, mainwindow_help.cpp),
// Solve and Continue grayed without a board or with invalid
// stacks ("Fix the puzzle on the Puzzle tab first"), Continue in Solve's
// place for a saved search ("Carry on …").

import './run-card.css';
import { isActive } from '../state/solves.ts';
import { byId, esc, markSelected, setHtml } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { prefs, rangeOf, setPrefs } from '../shell/prefs.ts';
import { stepperHtml, switchHtml, wireSteppers } from '../shell/widgets.ts';
import { problemChipStyle } from '../puzzle/counts.ts';
import {
  KEEP_AT_MOST,
  KEEP_EACH,
  LABEL,
  LIVE_PREVIEW,
  SOLVER_TYPE_TIP,
  STACKING_TIP,
  TIP,
} from './option-text.ts';
import { isSub } from './boxes.ts';
import {
  boxesShown,
  brickSolvers,
  defaultLimits,
  defaultsOf,
  effectiveSort,
  kindOf,
  normalize,
  readOptions,
  slidingSolver,
  SORT_CHOICES,
  stackingSolverLabel,
} from './settings.ts';
import type { BoxName, SearchOptions } from './settings.ts';
import { problemNote } from './problem-note.ts';
import { activity, counts, details, savingFor, shortCount } from './run-text.ts';
import type { FinishedSolve } from './problem-note.ts';
import { formatSpan } from './span.ts';
import type { ProblemSummary } from '../../engine/api.generated.ts';
import type { StoreChange } from '../state/store.ts';
import type { SolverWorkspace } from './workspace.ts';

/** The stacking solvers' help (mainwindow_help.cpp, their first lines). */
const STACKING_SOLVER_TIP: Record<'stacking' | 'panex', string> = {
  stacking:
    'Finds the fewest rod transfers from the start stacking to the goal, following the rod set’s rules. Rod sets with Panex Style Columns use the Panex Solver instead.',
  panex:
    'Finds the fewest rod transfers for Panex Style Columns, built for large towers: it searches from both ends, uses every core, and Pause saves the search for Continue, even after quitting.',
};

/** The sliding solver's help (mainwindow_help.cpp, its first lines). */
const SLIDING_SOLVER_TIP =
  'Each start is searched for the fewest moves to the goal, with no limit: the solver either finds the fewest moves or proves there is no solution, keeping older moves on disk.';

/** A problem's counts in the snapshot: a read of its finished solve is redone when they move. */
function countsOf(q: ProblemSummary): string {
  return `${String(q.assemblies)}:${String(q.solutions)}`;
}

export class RunCard {
  private readonly ws: SolverWorkspace;
  /** What each solved problem's finished solve was read to have found, by the document's generation. */
  private readonly found = new Map<
    string,
    { generation: number; revision: number; counts: string; found: FinishedSolve }
  >();
  /** The reads of finished solves under way: the revision read, by generation and problem. */
  private readonly reading = new Map<string, number>();
  /** Problems a document.changed named with solutions or problems since their read: read again. */
  private readonly changed = new Set<string>();

  constructor(ws: SolverWorkspace) {
    this.ws = ws;
    this.wire();
  }

  /** Draws the card. */
  render(): void {
    const ws = this.ws;
    const left = byId('leftS');
    const frozen = ws.optionsFrozen;
    left.classList.toggle('slvfrozen', frozen);
    left.dataset.frozen = frozen ? '1' : '0';
    this.renderProblem();
    this.renderMode();
    // each mode's card says what it does as its tooltip too
    for (const m of byId('slvMode').querySelectorAll<HTMLElement>('.mcard')) {
      m.title = m.querySelector('.d')?.textContent ?? '';
    }
    this.renderOptions();
    this.renderRun();
    this.renderSort();
  }

  // the Live preview switch, first of the search options: on the grids
  // whose runs have a search or a take-apart to show (not the spheres);
  // it switches while a run is under way, when the options around it are frozen
  private liveRow(): string {
    const ws = this.ws;
    if (!(ws.sliding || ws.stacking || ws.canDisassemble || ws.tilted)) {
      return '';
    }
    const on = prefs.livePreview;
    const tip = LIVE_PREVIEW.tip;
    return `<div class="row2 live"><div class="t">${LIVE_PREVIEW.label}<small>${tip}</small></div>${switchHtml({ on, tip, data: { live: '1' } })}</div>`;
  }

  /** Draws the problems list (also when another problem's solve changes state). */
  renderProblems(): void {
    const ws = this.ws;
    const list = byId('slvProbs');
    const revision = ws.store.snapshot?.revision ?? -1;
    setHtml(
      list,
      (ws.store.snapshot?.problems ?? [])
        .map((q, n) => {
          const shown = q.id === ws.problem?.id;
          const seen = this.foundOf(q, revision);
          // the problem shown goes by its mode as chosen here, before it is stored
          const found =
            seen !== null && shown ? { ...seen, disassemble: ws.options.disassemble } : seen;
          const r = ws.solves.peek(q.id);
          const g = r?.progress ?? null;
          const note = problemNote(
            ws.stateOf(q.id),
            q,
            ws.family,
            found,
            shown && ws.stacking && ws.savedSearch !== null,
            ws.isAborting(q.id)
              ? 'abort'
              : savingFor(ws, r ?? null)
                ? 'save'
                : g?.stage === 'stopping'
                  ? (g.stopKind ?? 'pause')
                  : null,
          );
          return `<div class="row" role="option" aria-selected="false" data-id="${q.id}" title="Solve this problem"><span class="chip" style="${problemChipStyle(n)}">${esc(q.label)}</span><span class="lbl">${esc(q.name) || 'Problem'}</span>${note.kind === 'none' ? '' : `<span class="st ${note.kind}">${esc(note.text)}</span>`}</div>`;
        })
        .join(''),
    );
    markSelected(list, '.row', 'id', ws.problem?.id ?? null);
  }

  /**
   * What a solved problem's finished solve found, as last read; reads it
   * again when a document.changed named the problem with its solutions or
   * the problems (cleanup item 10.5: not at every revision), or its counts
   * in the snapshot moved. Null before the first read, and for a problem
   * not solved.
   */
  private foundOf(q: ProblemSummary, revision: number): FinishedSolve | null {
    const generation = this.ws.store.generation;
    const seen = this.found.get(q.id);
    if (q.state !== 'solved' || (seen !== undefined && seen.generation !== generation)) {
      this.found.delete(q.id);
      return null;
    }
    const key = `${String(generation)}:${q.id}`;
    const stale = seen === undefined || this.changed.has(q.id) || seen.counts !== countsOf(q);
    if (stale && this.reading.get(key) !== revision) {
      this.changed.delete(q.id);
      this.readFound(q, generation, revision);
    }
    return seen?.found ?? null;
  }

  // a document.changed comes before the snapshot of its revision: the problems it names are read again
  private noteChange(c: StoreChange): void {
    if (c.kind !== 'changed') {
      return;
    }
    const p = c.change;
    if (!p.what.includes('solutions') && !p.what.includes('problems')) {
      return;
    }
    for (const id of p.problems.length === 0 ? this.found.keys() : p.problems) {
      this.changed.add(id);
    }
  }

  /** Reads a problem's mode, and a sliding or stacking one's fewest moves (its first solution by moves). */
  private readFound(q: ProblemSummary, generation: number, revision: number): void {
    const id = q.id;
    const counts = countsOf(q);
    const ws = this.ws;
    const engine = ws.store.engine;
    const family = ws.family;
    const canDisassemble = ws.canDisassemble;
    const key = `${String(generation)}:${id}`;
    this.reading.set(key, revision);
    void engine
      .call('problem.get', { problem: id }, { coalesce: `slvNote:problem.get:${id}` })
      .then(async (d) => {
        const disassemble = readOptions(
          d.solverSettings,
          undefined,
          family,
          canDisassemble,
        ).disassemble;
        const page =
          family !== 'brick' && disassemble
            ? await engine.call(
                'solution.page',
                { problem: id, sortBy: 'moves', offset: 0, limit: 1 },
                { coalesce: `slvNote:solution.page:${id}` },
              )
            : null;
        return { disassemble, moves: page?.rows[0]?.moves ?? null };
      })
      .then(
        (found) => {
          if (this.reading.get(key) === revision) {
            this.reading.delete(key);
          }
          const was = this.found.get(id);
          if (
            ws.store.generation !== generation ||
            (was !== undefined && was.revision > revision)
          ) {
            return;
          }
          this.found.set(id, { generation, revision, counts, found });
          this.renderProblems();
        },
        () => {
          // superseded, gone, or the engine away: the next draw reads again
          if (this.reading.get(key) === revision) {
            this.reading.delete(key);
          }
        },
      );
  }

  private renderProblem(): void {
    const ws = this.ws;
    const p = ws.problem;
    const d = ws.detail;
    this.renderProblems();
    const el = byId('slvProbMeta');
    if (p === null) {
      setHtml(el, 'No problem — make one in the Puzzle workspace');
      return;
    }
    const pieces = d?.parts.filter((x) => x.max > 0).length ?? 0;
    // a stacking problem's board in the result's place (resultviewer.cpp)
    const board = ws.store.snapshot?.rodSets.find((r) => r.id === p.rodSet);
    const result = ws.stacking
      ? board === undefined
        ? 'No Start/Goal Rods'
        : `Result ${esc(board.label)}`
      : p.result === null
        ? 'No result'
        : `Result ${esc(ws.labelOf(p.result))}`;
    setHtml(el, `${result} · ${String(pieces)} piece shape${pieces === 1 ? '' : 's'}`);
  }

  private renderMode(): void {
    const ws = this.ws;
    const o = ws.options;
    const { disabled } = normalize(o, null, ws.family, ws.canDisassemble);
    const dis = o.disassemble;
    if (ws.stacking) {
      // Find Solutions (updateSolverOptionCheckboxes; Just Levels is in the search options)
      setHtml(
        byId('slvMode'),
        `<button class="mcard ${!dis ? 'on' : ''}" data-m="asm" ${o.justLevels ? 'disabled' : ''}><i class="rd"></i><div><b>Assemble Only</b><span class="d">Check the start stacking only.</span></div></button><button class="mcard ${dis ? 'on' : ''}" data-m="dis" ${o.justCount ? 'disabled' : ''}><i class="rd"></i><div><b>Find Solutions</b><span class="d">${STACKING_TIP.disassemble ?? ''}</span></div></button>`,
      );
      return;
    }
    if (ws.sliding) {
      // the second mode is Find Solutions (updateSolverOptionCheckboxes)
      setHtml(
        byId('slvMode'),
        `<button class="mcard ${!dis ? 'on' : ''}" data-m="asm"><i class="rd"></i><div><b>Assemble Only</b><span class="d">Find the start layouts: every way the pieces fit their starts.</span></div></button><button class="mcard ${dis ? 'on' : ''}" data-m="dis" ${o.justCount ? 'disabled' : ''}><i class="rd"></i><div><b>Find Solutions</b><span class="d">Keep a placement only when each marked piece can slide from its start cell to its goal cell.</span></div></button>`,
      );
      return;
    }
    const sw = (k: BoxName): string =>
      `<div class="row2"><div class="t">${LABEL[k]}<small>${TIP[k]}</small></div>${switchHtml({ on: o[k], off: disabled.has(k), tip: TIP[k], data: { o: k } })}</div>`;
    setHtml(
      byId('slvMode'),
      `<button class="mcard ${!dis ? 'on' : ''}" data-m="asm" ${o.justLevels ? 'disabled' : ''}><i class="rd"></i><div><b>Assemble Only</b><span class="d">Find every way the pieces fit the result.</span></div></button><button class="mcard ${dis ? 'on' : ''}" data-m="dis" ${o.justCount || !ws.canDisassemble ? 'disabled' : ''}><i class="rd"></i><div><b>Assemble and Disassemble</b><span class="d">${ws.canDisassemble ? 'Also check that each assembly can be taken apart, moving pieces along the voxel axes.' : ws.tilted ? 'A tilted tray’s pieces lie flat and lift straight out: the solver finds the assemblies only.' : 'This space grid has no disassembler: the solver finds the assemblies only.'}</span></div></button>` +
        (dis ? `<div class="slv-opts" style="margin:8px 2px 0">${sw('checkRotations')}</div>` : ''),
    );
  }

  private renderOptions(): void {
    const ws = this.ws;
    const o = ws.options;
    const lim = ws.limits;
    const { disabled } = normalize(o, null, ws.family, ws.canDisassemble);
    const tip = (k: BoxName): string => (ws.stacking ? (STACKING_TIP[k] ?? TIP[k]) : TIP[k]);
    const sw = (k: BoxName): string =>
      `<div class="row2${isSub(k) ? ' sub' : ''}"><div class="t">${LABEL[k]}<small>${tip(k)}</small></div>${switchHtml({ on: o[k], off: disabled.has(k), tip: tip(k), data: { o: k } })}</div>`;
    // how many differ from the defaults (the settings dialog's Search Options)
    const def = normalize(defaultsOf(ws.family), null, ws.family, ws.canDisassemble).options;
    const defLim = defaultLimits();
    const brick = !ws.sliding && !ws.stacking;
    // the card's boxes of the puzzle type, in the order it lists them (solver/boxes.ts)
    const shown = boxesShown(kindOf(ws.family, ws.tilted), 'options');
    // their titles, in the order the card lists them
    const changed = [
      ...shown.map((k) => (o[k] !== def[k] ? LABEL[k] : null)),
      brick && o.solver !== def.solver ? 'Solver type' : null,
      lim.keepEach !== defLim.keepEach ? KEEP_EACH.label : null,
      lim.keepAtMost !== defLim.keepAtMost ? KEEP_AT_MOST.label : null,
    ].filter((t) => t !== null);
    // folded: how many, and which, under the card's sentence
    const folded =
      !ws.optOpen && changed.length > 0
        ? `<span class="chg">${String(changed.length)} changed</span><span class="chl">${changed.map((t) => `<span>${esc(t)}</span>`).join('')}</span>`
        : '';
    setHtml(
      byId('slvOptHead'),
      `<span class="gi">${ic('gear', 16)}</span><div><b>Search Options</b><span class="d">Additional options for searching and filtering results</span>${folded}</div><span class="sp"></span><span id="slvOptTog" style="${ws.optOpen ? '' : 'transform:rotate(-90deg)'}">${ic('chevD', 16)}</span>`,
    );
    const opts = byId('slvOpts');
    opts.style.display = ws.optOpen ? '' : 'none';
    const switches = shown.map((k) => sw(k)).join('');
    const solvers = brickSolvers(ws.store.info);
    const limits =
      `<div class="row2"><div class="t">${KEEP_EACH.label}<small>${KEEP_EACH.tip}</small></div>${stepperHtml(
        {
          key: 'keepEach',
          value: lim.keepEach,
          min: rangeOf('optKeepEach').min,
          less: 'Keep more of the solutions: skip one fewer between kept ones',
          more: 'Keep fewer of the solutions: skip one more between kept ones',
          label: KEEP_EACH.label,
          tip: '1 keeps every solution; a higher number skips that many before the next one is kept',
        },
      )}</div>` +
      `<div class="row2"><div class="t">${KEEP_AT_MOST.label}<small>${KEEP_AT_MOST.tip}</small></div>${stepperHtml(
        {
          key: 'keepAtMost',
          value: lim.keepAtMost,
          min: rangeOf('optKeepAtMost').min,
          step: 10,
          less: 'Keep 10 fewer solutions',
          more: 'Keep 10 more solutions',
          label: KEEP_AT_MOST.label,
          tip: KEEP_AT_MOST.tip,
        },
      )}</div>`;
    if (ws.stacking) {
      // the one solver of the board
      const solver = ws.stackingSolver;
      const label = stackingSolverLabel(ws.store.info, solver);
      setHtml(
        opts,
        this.liveRow() +
          switches +
          `<div class="row2 wrap"><div class="t">Solver type<small>${STACKING_SOLVER_TIP[solver]}</small></div><select data-o="stackingSolver" title="${esc(STACKING_SOLVER_TIP[solver])}"><option value="${solver}" selected>${esc(label)}</option></select></div>` +
          limits,
      );
      return;
    }
    if (ws.sliding) {
      // the sliding boxes in the places of the symmetry options, its one solver
      const slider = slidingSolver(ws.store.info);
      setHtml(
        opts,
        this.liveRow() +
          switches +
          `<div class="row2 wrap"><div class="t">Solver type<small>${SLIDING_SOLVER_TIP}</small></div><select data-o="slidingSolver" title="${esc(SLIDING_SOLVER_TIP)}"><option value="${slider.solver}" selected>${esc(slider.label)}</option></select></div>` +
          limits,
      );
      return;
    }
    setHtml(
      opts,
      this.liveRow() +
        switches +
        `<div class="row2 wrap"><div class="t">Solver type<small>${SOLVER_TYPE_TIP}</small></div><select data-o="solver" title="${SOLVER_TYPE_TIP}">${solvers.map((s) => `<option value="${s.solver}" ${o.solver === s.solver ? 'selected' : ''}>${esc(s.label)}</option>`).join('')}</select></div>` +
        limits,
    );
  }

  // Sort by: the order the solver keeps the solutions in (brick puzzles only;
  // moves, level and rotations need the disassembly check)
  private renderSort(): void {
    const ws = this.ws;
    const o = ws.options;
    const sec = byId('slvSortSec');
    sec.style.display = ws.sliding || ws.stacking ? 'none' : '';
    if (ws.sliding || ws.stacking) {
      return;
    }
    const sort = effectiveSort(o);
    const tip = o.disassemble
      ? 'The order the solver keeps the solutions in'
      : 'The order the solver keeps the solutions in; moves, level and rotations need the disassembly check';
    setHtml(
      byId('slvSortBy'),
      `<select data-o="sortBy" aria-label="Sort by" title="${esc(tip)}">${SORT_CHOICES.map((c) => `<option value="${c.value}" ${sort === c.value ? 'selected' : ''} ${c.needsDisassembly && !o.disassemble ? 'disabled' : ''}>${c.label}${c.needsDisassembly && !o.disassemble ? ' (needs disassembly)' : ''}</option>`).join('')}</select>`,
    );
  }

  /** The run's buttons, the progress and the counters (often: the solve's progress events). */
  renderRun(): void {
    const ws = this.ws;
    const r = ws.record;
    const p = ws.problem;
    const s = ws.current === null ? 'idle' : ws.stateOf(ws.current);
    const busy = ws.calling || ws.running || ws.analysing;
    // a sliding puzzle whose start/goal shape is not valid, a stacking one
    // without a board or with invalid stacks (Solve's tooltip in the original)
    const why = ws.slideError ?? ws.stackError;
    const canStart = ws.readyToSolve && !busy;
    const saved = ws.savedSearch;
    const canCont =
      canStart && p !== null && (s === 'paused' || p.state === 'solving' || saved !== null);
    const fix = ws.stacking
      ? `Fix the puzzle on the Puzzle tab first: ${why ?? ''}`
      : `Fix the start and goal on the Puzzle tab first: ${why ?? ''}`;
    const canPause =
      !ws.calling &&
      r !== null &&
      r.phase === 'active' &&
      isActive(s) &&
      r.progress?.stage !== 'stopping';
    const aborting = ws.abortMode;
    const btn = (k: string, label: string, on: boolean, tip: string, pri = false): string =>
      `<button data-r="${k}" class="${pri ? 'pri' : ''}" ${on ? '' : 'disabled'} title="${tip}">${label}</button>`;
    // a paused sliding solve whose saved search is gone (deleted from the
    // cache): Continue would search from the beginning, as Solve does
    const startsOver = ws.continueStartsOver && !isActive(s);
    // Solve becomes Continue while the problem has a solve in progress
    // (running, paused or a saved search: when Abort stands for Reset);
    // starting over is Abort, then Solve
    const first =
      aborting && !startsOver
        ? btn(
            'continue',
            'Continue',
            canCont,
            why !== null
              ? esc(fix)
              : saved !== null
                ? esc(`Carry on ${saved.description}`)
                : 'Resume the paused search (to start over, Abort, then Solve)',
            true,
          )
        : btn(
            'start',
            'Solve',
            canStart,
            why !== null
              ? esc(fix)
              : startsOver
                ? 'The paused search’s saved files are gone, so it starts again from the beginning: run the search from the start, removing old results'
                : 'Run the search from the start, removing old results',
            true,
          );
    setHtml(
      byId('slvRun'),
      first +
        btn('pause', 'Pause', canPause, 'Pause the search where it can be continued') +
        (aborting
          ? btn(
              'abort',
              'Abort',
              ws.abortable,
              'Stop at once and throw away everything kept of this problem’s solve: its results, the paused state and a saved search',
            )
          : btn(
              'reset',
              'Reset',
              ws.resettable,
              'Throw away this problem’s solve: its solutions and what its search went through (Undo brings them back)',
            )),
    );
    const c = counts(ws);
    const pct = Math.max(0, Math.min(1, c.overall)) * 100;
    byId('slvBar').style.width = `${pct.toFixed(1)}%`;
    byId('slvPct').textContent = `${pct.toFixed(1)}%`;
    const left = c.secondsLeft;
    // what is left: the run's estimate, nothing of a solved problem
    const leftText =
      isActive(s) && left !== null && Number.isFinite(left)
        ? formatSpan(left * 1000)
        : !isActive(s) && p?.state === 'solved'
          ? 'none'
          : 'unknown';
    const fail =
      r?.failure !== null && r?.failure !== undefined && s === 'error' ? r.failure : null;
    // a sliding or stacking problem has one solution at most: its moves
    const moves = ws.sliding || ws.stacking ? ws.moves() : undefined;
    const movesLine =
      moves === undefined
        ? ''
        : `<span class="k">Moves</span><span class="v" id="slvMoves" style="grid-column:span 3">${moves === null ? '—' : shortCount(moves)}</span>`;
    setHtml(
      byId('slvStat'),
      `<span class="k">Activity</span><span class="v" id="slvActivity" style="grid-column:span 3">${esc(activity(ws))}</span>${movesLine}<span class="k">Details</span><span class="v" id="slvDetails" style="grid-column:span 3">${esc(details(ws)) || '—'}</span><span class="k">Assemblies</span><span class="v" id="slvAsm">${c.assemblies === null ? '—' : String(c.assemblies)}</span><span class="k">Solutions</span><span class="v" id="slvSols">${ws.options.disassemble || (c.solutions ?? 0) > 0 ? (c.solutions === null ? '—' : String(c.solutions)) : '—'}</span><span class="k">Time used</span><span class="v">${c.timeMs === null ? '—' : formatSpan(c.timeMs)}</span><span class="k">Time left</span><span class="v">${leftText}</span>` +
        (fail === null
          ? ''
          : `<span class="slv-fail" id="slvFail" style="grid-column:1 / -1">${ic('warn', 14)}<span>The solve failed: ${esc(fail.message)}</span></span>`),
    );
    const leftCard = byId('leftS');
    leftCard.dataset.state = s;
    leftCard.dataset.phase = r?.phase ?? 'none';
    leftCard.dataset.end = String(r?.endRevision ?? -1);
    leftCard.dataset.assemblies = c.assemblies === null ? '' : String(c.assemblies);
    leftCard.dataset.solutions = c.solutions === null ? '' : String(c.solutions);
  }

  private wire(): void {
    const ws = this.ws;
    ws.store.subscribe((c) => {
      this.noteChange(c);
    });
    byId('slvProbs').addEventListener('click', (e) => {
      const id = (e.target as Element).closest<HTMLElement>('.row')?.dataset.id;
      if (id !== undefined) {
        ws.selectProblem(id);
      }
    });
    byId('slvMode').addEventListener('click', (e) => {
      const t = e.target as Element;
      const m = t.closest<HTMLButtonElement>('[data-m]');
      if (m !== null && !m.disabled) {
        void ws.setOption({ disassemble: m.dataset.m === 'dis' }, 'disassemble');
        return;
      }
      this.toggle(t);
    });
    byId('slvOptHead').addEventListener('click', () => {
      ws.optOpen = !ws.optOpen;
      this.render();
    });
    const opts = byId('slvOpts');
    opts.addEventListener('click', (e) => {
      const t = e.target as Element;
      if (this.flipLive(t)) {
        return;
      }
      this.toggle(t);
    });
    // Keep every n-th and Limit (not stored with the problem)
    wireSteppers(opts, {
      set: (k, v) => {
        ws.setLimits({ [k]: v });
      },
    });
    opts.addEventListener('change', (e) => {
      const t = e.target as HTMLInputElement | HTMLSelectElement;
      if (t.dataset.o === 'solver') {
        void ws.setOption({ solver: t.value as SearchOptions['solver'] });
      }
    });
    const runClicked = (e: MouseEvent): void => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-r]');
      if (b === null || b.disabled) {
        return;
      }
      switch (b.dataset.r) {
        case 'start':
          void ws.run('start');
          break;
        case 'continue':
          void ws.run('continue');
          break;
        case 'pause':
          void ws.pause();
          break;
        case 'abort':
          ws.abort();
          break;
        case 'reset':
          ws.reset();
          break;
      }
    };
    byId('slvRun').addEventListener('click', runClicked);
    byId('slvSortBy').addEventListener('change', (e) => {
      const t = e.target as HTMLSelectElement;
      if (t.dataset.o === 'sortBy') {
        void ws.setOption({ sortBy: t.value as SearchOptions['sortBy'] });
      }
    });
  }

  // the Live preview switch clicked: true when it was
  private flipLive(t: Element): boolean {
    if (t.closest('.switch[data-live]') === null) {
      return false;
    }
    setPrefs({ livePreview: !prefs.livePreview });
    this.renderOptions();
    return true;
  }

  // a switch of the options clicked: true when it was one
  private toggle(t: Element): boolean {
    const sw = t.closest<HTMLElement>('.switch[data-o]');
    if (sw === null) {
      return false;
    }
    if (!sw.classList.contains('off')) {
      const k = sw.dataset.o as BoxName;
      void this.ws.setOption({ [k]: !this.ws.options[k] }, k);
    }
    return true;
  }
}
