// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's left card: the problem to solve (a picker of the problems,
// each with its solve's state, since several may run), what to check
// (assemblies, or assemblies and their disassembly, with Just Levels and
// Check Rotations), the search options (stored with the problem, the
// original's rules greying out what does not apply; Keep at most and Keep
// every n-th are not stored, as in the original), the run: Solve, Pause,
// Continue, Clear (the original's Abort), the progress bar and the
// counters, a failure; Prepare in the explorers' section, and Sort by. From
// docs/mockup/index.html: `renderSolver` (the left card's sections, their
// wording and tooltips, `SVTIP`, `SVOPTSORT`) and its handlers in `svWire`;
// the run's buttons and the option names are the original's
// (mainwindow_layout.cpp) where the mockup had none: Solve, Pause, Abort,
// Check Rotations, Strict Colours, the solver type, Autosave. A sliding
// puzzle (docs/webui-spec.md, section 8.3) shows the original's sliding
// boxes and solvers instead (mainwindow_layout.cpp's tooltips,
// mainwindow_help.cpp's solver help), "Find Solutions" for the second
// mode, and Solve greyed while its start/goal shape is not valid
// (mainwindow.cpp's Solve tooltip). A stacking puzzle (section 9.3): Find
// Solutions with Just Levels, Just count, Enable High Memory and the one
// solver of its board (syncSolverTypeMenu, mainwindow_help.cpp), no
// Prepare, Solve and Continue greyed without a board or with invalid
// stacks ("Fix the puzzle on the Puzzle tab first"), Continue on for a
// saved search ("Carry on …").

import { isActive } from '../state/solves.ts';
import { byId, esc, setHtml } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { openPop } from '../shell/popover.ts';
import { onPrefs, prefs, setPrefs } from '../shell/prefs.ts';
import { problemChipStyle } from '../puzzle/counts.ts';
import {
  DEFAULT_LIMITS,
  DEFAULT_OPTIONS,
  defaultsOf,
  effectiveSort,
  normalize,
  SORT_CHOICES,
} from './settings.ts';
import type { BoxName, SearchOptions } from './settings.ts';
import { formatSpan } from './span.ts';
import type { SolverWorkspace } from './workspace.ts';

/** The switches' help (the mockup's SVTIP, the original's tooltips for the rest). */
const TIP: Record<BoxName, string> = {
  disassemble:
    'Check each assembly for a complete disassembly; only assemblies that can be taken apart are listed.',
  justLevels:
    'Check disassemblies but keep only their level, not the moves. Recompute them later with “Analyse”.',
  checkRotations: 'Also try 90 degree piece rotations while taking the assemblies apart.',
  justCount: 'Only count solutions; they are dropped as soon as they are found.',
  keepMirrors:
    'Do not remove solutions that are mirror images of others (useful with piece ranges).',
  keepRotations: 'Do not remove solutions that are only rotations of others.',
  deepSymmetry:
    'A more thorough, slower check for duplicate solutions: removes translations and rotations that are not in the symmetry of the result shape.',
  strictColors:
    'A voxel with a color fits only a result voxel of that same color, not a neutral one; a voxel with no color fits only a result voxel that also has none.',
  nestedSlides:
    'A piece may slide together with the pieces nested inside its outline, such as a piece in another piece’s pocket. Pieces that only touch never slide together.',
  partialNested:
    'With Allow Nested Slides: a piece may also slide together with a piece that sits partly in its outline, such as a piece in another piece’s pocket that sticks out of it. Pieces that only touch never slide together.',
  verticalMoves:
    'On a tray more than one layer deep, a piece may also move up and down: lifted out over the other pieces and put down somewhere else. Off, every piece slides only in its own layer.',
  highMemory:
    'Let a sliding search hold up to half of this computer’s memory in arrangements, instead of stopping at about 2 GB. The Sliding Full Solver keeps older moves on disk and needs memory only for the widest move.',
  autosave:
    'Twenty minutes into a solve and every twenty minutes after, the solve pauses where it can be saved, the puzzle is saved to its file, and the solve carries on.',
};

/** The stacking puzzles' tooltips where they differ (mainwindow_layout.cpp, mainwindow_solve.cpp). */
const STACKING_TIP: Partial<Record<BoxName, string>> = {
  disassemble:
    'Search for a shortest sequence of rod transfers from the start stacking to the goal.',
  highMemory:
    'Let a stacking search hold up to half of this computer’s memory, instead of stopping at about 2 GB.',
  autosave:
    'Save the search 20 minutes into the run and every 20 minutes after, so a crash loses little: Continue carries it on. Unchecked, the search is saved only when you press Pause.',
};

/** The stacking solvers' help (mainwindow_help.cpp, their first lines). */
const STACKING_SOLVER_TIP: Record<'stacking' | 'panex', string> = {
  stacking:
    'Finds the fewest rod transfers from the start stacking to the goal, following the rod set’s rules. Rod sets with Panex Style Columns use the Panex Solver instead.',
  panex:
    'Finds the fewest rod transfers for Panex Style Columns, built for large towers: it searches from both ends, uses every core, and Pause saves the search for Continue, even after quitting.',
};

/** The sliding solvers' help (mainwindow_help.cpp, its first lines). */
const SLIDING_SOLVER_TIP =
  'Each start is searched for the fewest moves to the goal. Fast looks at up to 250,000 arrangements per start, Deep up to 1,000,000; Full has no limit: it either finds the fewest moves or proves there is no solution, keeping older moves on disk.';

/** The switches' names: the original's (mainwindow_layout.cpp), so they match its documentation. */
const LABEL: Record<BoxName, string> = {
  disassemble: 'Assemblies and disassembly',
  justLevels: 'Just Levels',
  checkRotations: 'Check Rotations',
  justCount: 'Just Count',
  keepMirrors: 'Keep Mirror Solutions',
  keepRotations: 'Keep Rotated Solutions',
  deepSymmetry: 'Deep Symmetry Check',
  strictColors: 'Strict Color Restrictions',
  nestedSlides: 'Allow Nested Slides',
  partialNested: 'Allow Partially Nested',
  verticalMoves: 'Allow Vertical Moves',
  highMemory: 'Enable High Memory',
  autosave: 'Autosave every 20 min',
};

export class RunCard {
  private readonly ws: SolverWorkspace;

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

  private renderProblem(): void {
    const ws = this.ws;
    const p = ws.problem;
    const d = ws.detail;
    const el = byId('slvProb');
    if (p === null) {
      setHtml(
        el,
        '<span class="meta"><b>No problem</b><span>Make one in the Puzzle workspace</span></span>',
      );
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
    setHtml(
      el,
      `<span class="chip" style="${ws.probChip(p.id)}">${esc(p.label)}</span><span class="meta"><b>${esc(p.name) || 'Problem'}</b><span>${result} · ${String(pieces)} piece shape${pieces === 1 ? '' : 's'}</span></span>${ic('chevD', 16)}`,
    );
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
      `<div class="row2"><div class="t">${LABEL[k]}<small>${TIP[k]}</small></div><div class="switch ${o[k] ? 'on' : ''} ${disabled.has(k) ? 'off' : ''}" data-o="${k}" role="switch" tabindex="0" aria-checked="${String(o[k])}" title="${esc(TIP[k])}"><span class="tr"></span></div></div>`;
    setHtml(
      byId('slvMode'),
      `<button class="mcard ${!dis ? 'on' : ''}" data-m="asm" ${o.justLevels ? 'disabled' : ''}><i class="rd"></i><div><b>Assemble Only</b><span class="d">Find every way the pieces fit the result.</span></div></button><button class="mcard ${dis ? 'on' : ''}" data-m="dis" ${o.justCount || !ws.canDisassemble ? 'disabled' : ''}><i class="rd"></i><div><b>Assemble and Disassemble</b><span class="d">${ws.canDisassemble ? 'Also check that each assembly can be taken apart, moving pieces along the voxel axes.' : 'This space grid has no disassembler: the solver finds the assemblies only.'}</span></div></button>` +
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
      `<div class="row2"><div class="t">${LABEL[k]}<small>${tip(k)}</small></div><div class="switch ${o[k] ? 'on' : ''} ${disabled.has(k) ? 'off' : ''}" data-o="${k}" role="switch" tabindex="0" aria-checked="${String(o[k])}" title="${esc(tip(k))}"><span class="tr"></span></div></div>`;
    const def = defaultsOf(ws.family);
    const changed = [
      o.justCount !== DEFAULT_OPTIONS.justCount,
      o.justLevels !== DEFAULT_OPTIONS.justLevels,
      o.keepMirrors !== DEFAULT_OPTIONS.keepMirrors,
      o.keepRotations !== DEFAULT_OPTIONS.keepRotations,
      o.deepSymmetry !== DEFAULT_OPTIONS.deepSymmetry,
      o.strictColors !== DEFAULT_OPTIONS.strictColors,
      o.solver !== def.solver,
      o.autosave !== DEFAULT_OPTIONS.autosave,
      ws.sliding && o.nestedSlides,
      ws.sliding && o.partialNested,
      ws.sliding && o.verticalMoves,
      ws.family !== 'brick' && o.highMemory !== def.highMemory,
      ws.sliding && o.slidingSolver !== DEFAULT_OPTIONS.slidingSolver,
      lim.keepEach !== DEFAULT_LIMITS.keepEach,
      lim.keepAtMost !== DEFAULT_LIMITS.keepAtMost,
    ].filter(Boolean).length;
    setHtml(
      byId('slvOptHead'),
      `Search options${!ws.optOpen && changed > 0 ? `<span style="text-transform:none;letter-spacing:0;font-weight:400">· ${String(changed)} changed</span>` : ''}<span class="sp"></span><span id="slvOptTog" style="${ws.optOpen ? '' : 'transform:rotate(-90deg)'}">${ic('chevD', 16)}</span>`,
    );
    const opts = byId('slvOpts');
    opts.style.display = ws.optOpen ? '' : 'none';
    const solvers = ws.store.info?.solvers ?? [
      { solver: 'classic' as const, label: 'Complete (ShapeLogic)' },
    ];
    const limits =
      `<div class="row2"><div class="t">Keep every n-th<small>Keep a sample of a large number of solutions: 1 keeps each, higher numbers skip that many before the next one is kept</small></div><div class="step"><button data-st="keepEach" data-d="-1" title="Keep more of the solutions: skip one fewer between kept ones">${ic('minus', 12)}</button><input class="v" data-sti="keepEach" title="1 keeps every solution; a higher number skips that many before the next one is kept" value="${String(lim.keepEach)}"><button data-st="keepEach" data-d="1" title="Keep fewer of the solutions: skip one more between kept ones">${ic('plus', 12)}</button></div></div>` +
      `<div class="row2"><div class="t">Limit<small>Keep at most this many solutions (0: all)</small></div><div class="step"><button data-st="keepAtMost" data-d="-10" title="Keep 10 fewer solutions">${ic('minus', 12)}</button><input class="v" data-sti="keepAtMost" title="Keep at most this many solutions (0: all)" value="${String(lim.keepAtMost)}"><button data-st="keepAtMost" data-d="10" title="Keep 10 more solutions">${ic('plus', 12)}</button></div></div>`;
    // not a search option: a setting of the app, kept across sessions (prefs.ts)
    const labelTip =
      'Draw each piece’s label from the Shapes tab on it in the 3D view, always facing you';
    const labels = `<div class="row2"><div class="t">Show piece labels<small>${labelTip}</small></div><div class="switch ${prefs.pieceLabels ? 'on' : ''}" data-pref="pieceLabels" role="switch" tabindex="0" aria-checked="${String(prefs.pieceLabels)}" title="${esc(labelTip)}"><span class="tr"></span></div></div>`;
    if (ws.stacking) {
      // High Memory in the symmetry options' place, the one solver of the board
      const solver = ws.stackingSolver;
      const label =
        ws.store.info?.stackingSolvers.find((x) => x.solver === solver)?.label ??
        (solver === 'panex' ? 'Panex Solver' : 'Stacking Solver');
      setHtml(
        opts,
        sw('justCount') +
          sw('justLevels') +
          sw('highMemory') +
          `<div class="row2 wrap"><div class="t">Solver type<small>${STACKING_SOLVER_TIP[solver]}</small></div><select data-o="stackingSolver" title="${esc(STACKING_SOLVER_TIP[solver])}"><option value="${solver}" selected>${esc(label)}</option></select></div>` +
          limits +
          sw('autosave') +
          labels,
      );
      return;
    }
    if (ws.sliding) {
      // the sliding boxes in the places of the symmetry options, its solvers
      const sliders = ws.store.info?.slidingSolvers ?? [
        { solver: 'full' as const, label: 'Sliding Full Solver (full depth)' },
      ];
      setHtml(
        opts,
        sw('justCount') +
          sw('nestedSlides') +
          sw('partialNested') +
          sw('verticalMoves') +
          sw('highMemory') +
          `<div class="row2 wrap"><div class="t">Solver type<small>${SLIDING_SOLVER_TIP}</small></div><select data-o="slidingSolver" title="${esc(SLIDING_SOLVER_TIP)}">${sliders.map((x) => `<option value="${x.solver}" ${o.slidingSolver === x.solver ? 'selected' : ''}>${esc(x.label)}</option>`).join('')}</select></div>` +
          limits +
          sw('autosave') +
          labels,
      );
      return;
    }
    setHtml(
      opts,
      sw('justCount') +
        sw('justLevels') +
        sw('keepMirrors') +
        sw('keepRotations') +
        sw('deepSymmetry') +
        sw('strictColors') +
        `<div class="row2 wrap"><div class="t">Solver type<small>Which take-apart the solve uses: Complete (ShapeLogic) or Fast (Crowell).</small></div><select data-o="solver" title="Which take-apart the solve uses: Complete (ShapeLogic) or Fast (Crowell)">${solvers.map((s) => `<option value="${s.solver}" ${o.solver === s.solver ? 'selected' : ''}>${esc(s.label)}</option>`).join('')}</select></div>` +
        limits +
        sw('autosave') +
        labels,
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
    const board = ws.stacking ? p?.rodSet !== null : p?.result !== null;
    const can = p !== null && board && why === null;
    const canStart = can && !busy;
    const saved = ws.savedSearch;
    const canCont = canStart && (s === 'paused' || p.state === 'solving' || saved !== null);
    const fix = ws.stacking
      ? `Fix the puzzle on the Puzzle tab first: ${why ?? ''}`
      : `Fix the start and goal on the Shapes tab first: ${why ?? ''}`;
    const canPause =
      !ws.calling &&
      r !== null &&
      r.phase === 'active' &&
      isActive(s) &&
      r.progress?.stage !== 'stopping';
    const canAbort = ws.abortable;
    const btn = (k: string, label: string, on: boolean, tip: string, pri = false): string =>
      `<button data-r="${k}" class="${pri ? 'pri' : ''}" ${on ? '' : 'disabled'} title="${tip}">${label}</button>`;
    setHtml(
      byId('slvRun'),
      btn(
        'start',
        'Solve',
        canStart,
        why === null ? 'Run the search from the start, removing old results' : esc(fix),
        true,
      ) +
        btn('pause', 'Pause', canPause, 'Pause the search where it can be continued') +
        btn(
          'continue',
          'Continue',
          canCont,
          why !== null
            ? esc(fix)
            : saved !== null
              ? esc(`Carry on ${saved.description}`)
              : 'Resume a paused search',
        ) +
        btn(
          'abort',
          'Clear',
          canAbort,
          'Stop at once and throw away everything kept of this problem’s solve: its results, the paused state and a saved search',
        ),
    );
    // Prepare sits with the explorers it serves; a stacking search has nothing to prepare
    setHtml(
      byId('slvPrep'),
      ws.stacking
        ? ''
        : btn(
            'prepare',
            'Prepare',
            canStart,
            'Build the search (the placements of every piece) and pause before the first step, so Placements and Step have a search to show; this removes old results',
          ),
    );
    const c = ws.counts();
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
    setHtml(
      byId('slvStat'),
      `<span class="k">Activity</span><span class="v" id="slvActivity" style="grid-column:span 3">${esc(ws.activity())}</span><span class="k">Assemblies</span><span class="v" id="slvAsm">${c.assemblies === null ? '—' : String(c.assemblies)}</span><span class="k">Time used</span><span class="v">${c.timeMs === null ? '—' : formatSpan(c.timeMs)}</span><span class="k">Solutions</span><span class="v" id="slvSols">${ws.options.disassemble || (c.solutions ?? 0) > 0 ? (c.solutions === null ? '—' : String(c.solutions)) : '—'}</span><span class="k">Time left</span><span class="v">${leftText}</span>` +
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
    byId('slvProb').addEventListener('click', (e) => {
      const snap = ws.store.snapshot;
      if (snap === null || snap.problems.length === 0) {
        return;
      }
      const rows = snap.problems
        .map((q, n) => {
          const st = ws.stateOf(q.id);
          const note = isActive(st) ? ' · solving' : st === 'paused' ? ' · paused' : '';
          return `<button data-k="${q.id}" title="Solve this problem"><span class="chip" style="${problemChipStyle(n)};min-width:30px;height:20px;font-size:10px">${esc(q.label)}</span>${esc(q.name) || 'Problem'}<span class="kb">${note}</span></button>`;
        })
        .join('');
      openPop(e.currentTarget as Element, `<div class="ph">Solve problem</div>${rows}`, (k) => {
        ws.selectProblem(k);
      });
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
    // the labels' switch follows the setting (also reset from the settings dialog)
    onPrefs((changed) => {
      if (changed.includes('pieceLabels')) {
        this.renderOptions();
      }
    });
    byId('slvOptHead').addEventListener('click', () => {
      ws.optOpen = !ws.optOpen;
      this.render();
    });
    const opts = byId('slvOpts');
    opts.addEventListener('click', (e) => {
      const t = e.target as Element;
      if (this.toggle(t)) {
        return;
      }
      const b = t.closest<HTMLButtonElement>('button[data-st]');
      if (b !== null) {
        const k = b.dataset.st as 'keepEach' | 'keepAtMost';
        ws.setLimits({ [k]: ws.limits[k] + Number(b.dataset.d) });
      }
    });
    opts.addEventListener('change', (e) => {
      const t = e.target as HTMLInputElement | HTMLSelectElement;
      if (t.dataset.o === 'solver') {
        void ws.setOption({ solver: t.value as SearchOptions['solver'] });
      } else if (t.dataset.o === 'slidingSolver') {
        void ws.setOption({ slidingSolver: t.value as SearchOptions['slidingSolver'] });
      } else if (t.dataset.sti !== undefined) {
        const k = t.dataset.sti as 'keepEach' | 'keepAtMost';
        ws.setLimits({ [k]: Math.round(Number(t.value)) || 0 });
      }
    });
    // a focused switch toggles with Space or Enter (the dialogs' row of the keyboard map)
    for (const id of ['slvMode', 'slvOpts']) {
      byId(id).addEventListener('keydown', (e) => {
        if ((e.key === ' ' || e.key === 'Enter') && this.toggle(e.target as Element)) {
          e.preventDefault();
        }
      });
    }
    const runClicked = (e: MouseEvent): void => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-r]');
      if (b === null || b.disabled) {
        return;
      }
      switch (b.dataset.r) {
        case 'prepare':
          void ws.run('prepare');
          break;
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
      }
    };
    byId('slvRun').addEventListener('click', runClicked);
    byId('slvPrep').addEventListener('click', runClicked);
    byId('slvSortBy').addEventListener('change', (e) => {
      const t = e.target as HTMLSelectElement;
      if (t.dataset.o === 'sortBy') {
        void ws.setOption({ sortBy: t.value as SearchOptions['sortBy'] });
      }
    });
  }

  // a switch of the options clicked: true when it was one
  private toggle(t: Element): boolean {
    if (t.closest('.switch[data-pref="pieceLabels"]') !== null) {
      setPrefs({ pieceLabels: !prefs.pieceLabels });
      return true;
    }
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
