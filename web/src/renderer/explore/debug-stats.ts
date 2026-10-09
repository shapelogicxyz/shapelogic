// SPDX-License-Identifier: GPL-3.0-or-later
//
// The debug statistics panel's report (FRONTEND-PLAN.md, stage 5): the
// rows and the analysis notes of a solve's statistics (solve.status's
// `stats`), worded as the original's panel. Ported from
// src/gui/debugstatspanel.cpp (`formatReport`, `appendIdleRows`,
// `buildAnalysis`, `formatDuration`, `statusTitle`); DOM-free, so the unit
// tests check it.

import type { SolveStats, Solver } from '../../engine/api.generated.ts';

/** A row of the report: a label and its value; `indent` for the rows under a header. */
export interface StatsRow {
  label: string;
  value: string;
  indent: boolean;
}

/** A section: the rows before the first header have no title. */
export interface StatsSection {
  title: string | null;
  rows: StatsRow[];
}

/** The report: its sections, then the analysis notes. */
export interface StatsReport {
  sections: StatsSection[];
  notes: string[];
}

/** A duration as the original wrote it: ms below a second, then s, min, h with one decimal. */
export function formatDuration(ms: number): string {
  if (ms < 1000) {
    return `${String(Math.round(ms))} ms`;
  }
  const s = ms / 1000;
  if (s < 60) {
    return `${s.toFixed(1)} s`;
  }
  if (s < 3600) {
    return `${(s / 60).toFixed(1)} min`;
  }
  return `${(s / 3600).toFixed(1)} h`;
}

function pct(part: number, total: number): number {
  return total === 0 ? 0 : (100 * part) / total;
}

function formatPct(part: number, total: number): string {
  return `${pct(part, total).toFixed(0)}%`;
}

/** Whether a run gave the statistics (the original's hasData): not a solve never run. */
export function hasData(s: SolveStats): boolean {
  return !(s.state === 'idle' && s.elapsedMs === 0 && s.iterations === 0);
}

/** The original's statusTitle. */
export function statusTitle(s: SolveStats): string {
  switch (s.state) {
    case 'preparing':
    case 'assembling':
    case 'draining':
      return 'Running';
    case 'paused':
      return 'Paused';
    case 'finished':
      return 'Finished';
    case 'error':
      return 'Error';
    case 'idle':
      return 'Idle';
  }
}

const DASH = '—';

function idleReport(): StatsReport {
  const kv = (label: string, value = DASH): StatsRow => ({ label, value, indent: true });
  return {
    sections: [
      {
        title: null,
        rows: [
          { label: 'Solver:', value: 'Idle', indent: false },
          { label: 'Solver Type:', value: DASH, indent: false },
        ],
      },
      {
        title: 'Assembly',
        rows: [
          kv('Assemblies considered:'),
          kv('Valid (disassemblable):'),
          kv('Inseparable assemblies:'),
          kv('DLX iterations:'),
          kv('Assembly search progress:'),
          kv('Time finding assemblies:'),
          kv('Prepare time:'),
          kv('Reduce / optimize time:'),
          kv('Assembler threads:'),
        ],
      },
      {
        title: 'Disassembly',
        rows: [
          kv('Host CPU threads:'),
          kv('Disassembly workers:'),
          kv('Queue pending:'),
          kv('Peak queue pending:'),
          kv('Disassemblies completed:'),
          kv('Average time per disassembly:'),
          kv('Disassembly work time (sum):'),
          kv('Linear move search time:'),
          kv('Rotation move search time:'),
          kv('Queue drain after assembly:'),
          kv('Elapsed wall time:'),
        ],
      },
    ],
    notes: [
      'No solve has been run yet. Start a search on the Solver tab; this pane keeps the latest run after it finishes or is paused.',
    ],
  };
}

/** The report of a solve's statistics; `solverLabel` names a solver as engine.info does. */
export function statsReport(s: SolveStats, solverLabel: (s: Solver) => string): StatsReport {
  if (!hasData(s)) {
    return idleReport();
  }
  const kv = (label: string, value: string | number): StatsRow => ({
    label,
    value: String(value),
    indent: true,
  });
  const assembly: StatsRow[] = [
    kv('Assemblies considered:', s.assembliesFound),
    kv('Valid (disassemblable):', s.solutionsFound),
    kv('Inseparable assemblies:', s.inseparable),
    kv('DLX iterations:', s.iterations),
    kv(
      'Assembly search progress:',
      s.assemblyProgress >= 0 ? `${(s.assemblyProgress * 100).toFixed(1)} %` : 'unknown',
    ),
    kv('Time finding assemblies:', formatDuration(s.assemblyMs)),
    kv('Prepare time:', formatDuration(s.prepareMs)),
    kv('Reduce / optimize time:', formatDuration(s.reduceMs)),
    kv('Assembler threads:', s.assemblerThreads),
  ];
  const dis: StatsRow[] = [kv('Host CPU threads:', s.hardwareThreads)];
  if (!s.disassemblyEnabled) {
    dis.push(
      kv('Disassembly workers:', 'off'),
      kv('Queue pending:', DASH),
      kv('Peak queue pending:', DASH),
      kv('Disassemblies completed:', DASH),
      kv('Average time per disassembly:', DASH),
      kv('Disassembly work time (sum):', DASH),
      kv('Linear move search time:', DASH),
      kv('Rotation move search time:', 'off'),
      kv('Queue drain after assembly:', DASH),
    );
  } else {
    dis.push(
      kv('Disassembly workers:', s.disasmWorkers),
      kv('Queue pending:', s.pending),
      kv('Peak queue pending:', s.peakPending),
      kv('Disassemblies completed:', s.disasmCompleted),
      kv('Average time per disassembly:', `${s.avgDisasmSeconds.toFixed(3)} s`),
      kv('Disassembly work time (sum):', formatDuration(s.disasmWorkMs)),
      kv('Linear move search time:', formatDuration(s.linearSearchMs)),
      kv(
        'Rotation move search time:',
        s.rotationsEnabled ? formatDuration(s.rotationSearchMs) : 'off',
      ),
      kv('Queue drain after assembly:', formatDuration(s.drainMs)),
    );
    // one row for each take-apart under way
    s.running.forEach((d, i) => {
      let row = `${formatDuration(d.elapsedMs)}, level ${String(d.level)}: ${String(d.levelDone)} of ${String(d.levelSize)} positions (next level ${String(d.nextLevelSize)} so far), ${String(d.nodes)} positions in all`;
      if (d.elapsedMs > 0) {
        row += ` (${((d.nodes * 1000) / d.elapsedMs).toFixed(0)} a second)`;
      }
      if (d.pieces > 1) {
        row += `, ${String(d.separations)} of ${String(d.pieces - 1)} separations, searching ${String(d.searchPieces)} of ${String(d.pieces)} pieces`;
      }
      row += `, ${String(d.threads)} thread${d.threads === 1 ? '' : 's'}`;
      dis.push(kv(`Disassembly ${String(i + 1)} running:`, row));
    });
  }
  dis.push(kv('Elapsed wall time:', formatDuration(s.elapsedMs)));
  return {
    sections: [
      {
        title: null,
        rows: [
          { label: 'Solver:', value: statusTitle(s), indent: false },
          { label: 'Solver Type:', value: solverLabel(s.solver), indent: false },
        ],
      },
      { title: 'Assembly', rows: assembly },
      { title: 'Disassembly', rows: dis },
    ],
    notes: analysis(s),
  };
}

// the original's buildAnalysis: what the figures say about where the time went
// (its divisions are of unsigned integers: truncated)
function analysis(s: SolveStats): string[] {
  const note: string[] = [];
  const title = statusTitle(s);
  if (title === 'Running') {
    note.push('The solver is still running. Figures below update while the search is in progress.');
  } else if (title === 'Paused') {
    note.push('The solver was paused. These figures are from the paused run.');
  } else if (title === 'Finished') {
    note.push('The solver finished. These figures are from the completed run.');
  } else if (title === 'Error') {
    note.push('The solver stopped with an error. Partial figures from that run are shown.');
  }

  const wall = s.elapsedMs !== 0 ? s.elapsedMs : 1;
  const phaseSum = s.prepareMs + s.reduceMs + s.assemblyMs + s.drainMs;
  {
    let line = `Share of elapsed wall time (${formatDuration(s.elapsedMs)}): `;
    line += `prepare ${formatPct(s.prepareMs, wall)} (${formatDuration(s.prepareMs)})`;
    line += `, reduce / optimize ${formatPct(s.reduceMs, wall)} (${formatDuration(s.reduceMs)})`;
    line += `, assembly search ${formatPct(s.assemblyMs, wall)} (${formatDuration(s.assemblyMs)})`;
    if (s.disassemblyEnabled) {
      line += `, queue drain after assembly ${formatPct(s.drainMs, wall)} (${formatDuration(s.drainMs)})`;
    }
    if (phaseSum + 50 < s.elapsedMs && s.elapsedMs > 0) {
      line += '. Other overhead is the remaining wall time.';
    }
    note.push(line);
  }

  {
    let line = `DLX iterations: ${String(s.iterations)}. Each iteration is one step of Knuth’s dancing-links covering search: it tries to place or backtrack a piece in the assembly. `;
    const workers0 = s.disasmWorkers !== 0 ? s.disasmWorkers : 1;
    if (s.assembliesFound === 0 && s.iterations > 0) {
      line +=
        'Iterations are happening but no assembly has been found yet, so time is still in the covering search (or in prepare/reduce).';
    } else if (
      s.assemblyMs >= s.prepareMs &&
      s.assemblyMs >= s.reduceMs &&
      s.assemblyMs >= s.drainMs &&
      (!s.disassemblyEnabled || s.peakPending <= workers0 + 1)
    ) {
      line +=
        'Assembly search used a large share of wall time while the take-apart queue stayed small, so DLX was a bottleneck.';
    } else if (
      s.iterations > 0 &&
      s.assembliesFound > 0 &&
      Math.trunc(s.iterations / s.assembliesFound) > 10000
    ) {
      line +=
        'Many iterations were needed per assembly found, so the covering tree is bushy; DLX work is significant even if it was not the only limiter.';
    } else {
      line +=
        'DLX does not look like the only limiter; compare the time shares above and the disassembly notes below.';
    }
    note.push(line);
  }

  const workers = s.disasmWorkers !== 0 ? s.disasmWorkers : 1;
  const queueIdle = s.disassemblyEnabled && s.peakPending <= workers + 1;
  const queueBacked =
    s.disassemblyEnabled &&
    (s.peakPending > workers * 4 ||
      s.pending > workers * 2 ||
      s.drainMs > Math.trunc(s.assemblyMs / 2));

  if (!s.disassemblyEnabled) {
    note.push(
      'Disassembly was off, so only assembly (DLX) search was measured. Take-apart time is not part of this run.',
    );
  } else {
    let line = `Disassembly CPU time (sum of workers) was ${formatDuration(s.disasmWorkMs)} across ${String(workers)}${workers === 1 ? ' worker.' : ' workers.'}`;
    const moveSearch = s.linearSearchMs + s.rotationSearchMs;
    if (moveSearch > 0) {
      line += ` Of move search, linear slides were ${formatPct(s.linearSearchMs, moveSearch)} (${formatDuration(s.linearSearchMs)})`;
      if (s.rotationsEnabled) {
        line += ` and 90° rotations were ${formatPct(s.rotationSearchMs, moveSearch)} (${formatDuration(s.rotationSearchMs)}).`;
      } else {
        line += '. Check Rotations was off, so rotation search was not used.';
      }
    } else if (s.rotationsEnabled) {
      line +=
        ' Linear and rotation move-search timers had not accumulated yet (or the run was too short).';
    }
    note.push(line);

    if (queueBacked) {
      note.push(
        'Bottleneck: take-apart search lagged behind assembly (the queue peaked or drain after assembly was large). DLX was waiting on disassembly rather than the other way around.',
      );
    } else if (queueIdle && s.assembliesFound > 0) {
      note.push(
        'Bottleneck: disassembly workers stayed idle most of the time, so assembly search (DLX) limited the run.',
      );
    } else if (s.assembliesFound === 0) {
      note.push(
        'Bottleneck: no assemblies found yet, so time is still in preparation, reduction, or covering search.',
      );
    } else {
      note.push(
        'Bottleneck: assembly and disassembly shared the run. The larger of “Time finding assemblies” and “Disassembly work time” is the main cost.',
      );
    }

    if (
      s.rotationsEnabled &&
      s.rotationSearchMs > s.linearSearchMs &&
      s.rotationSearchMs * 2 > s.linearSearchMs + s.rotationSearchMs
    ) {
      note.push(
        'Rotation move search used more time than sliding moves. That is expected with Check Rotations, and it is a likely place for future speedups.',
      );
    }
  }

  if (s.assembliesFound > 0 && s.solutionsFound * 5 < s.assembliesFound && s.inseparable > 0) {
    note.push(
      'Many assemblies could not be taken apart (valid solutions are much fewer than assemblies considered), so disassembly work was spent rejecting inseparable placements.',
    );
  }

  const items: string[] = [];
  if (
    !s.disassemblyEnabled ||
    (queueIdle && s.assembliesFound > 0) ||
    (s.assemblyMs >= s.drainMs && s.assemblyMs >= Math.trunc(s.disasmWorkMs / workers))
  ) {
    items.push(
      'parallel assembly / tree-split of the DLX covering search, because assembly time was a large share',
    );
  }
  if (s.disassemblyEnabled && queueBacked) {
    items.push(
      'faster take-apart search or more disassembly workers (Check Rotations currently keeps a single worker)',
    );
  }
  if (s.rotationsEnabled && s.rotationSearchMs >= s.linearSearchMs) {
    items.push('speeding up 90° rotation move generation, which dominated move search');
  }
  if (s.reduceMs > s.assemblyMs && s.reduceMs > Math.trunc(s.elapsedMs / 5)) {
    items.push('the reduce / optimize phase, which used a large share of this run');
  }
  if (items.length === 0) {
    items.push(
      'profile the larger of assembly search and disassembly; that is the first place extra CPU would help',
    );
  }
  note.push(`Worth considering for future performance work: ${items.join('; ')}.`);
  return note;
}
