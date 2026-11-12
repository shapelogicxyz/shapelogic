// SPDX-License-Identifier: GPL-3.0-or-later
//
// The note beside each problem in the Solver's problems list: its solve
// under way (or being paused or aborted) or paused, else what a finished solve found (a sliding or
// stacking puzzle's fewest moves, or no solution; the other puzzles'
// assemblies, or disassemblies when the problem takes them apart), its
// count as short as it goes (12k, 15m). Platform-free (no DOM), so the
// unit tests drive it.

import type { ProblemSummary, SolveState } from '../../engine/api.generated.ts';
import { isActive } from '../state/solves.ts';
import type { Family } from './settings.ts';

/**
 * A count at its shortest: as it is below a thousand, else its first group
 * of digits and the group's letter: 12,200 is 12k, 15,111,111 is 15m, then
 * b and t, and x past the trillions.
 */
export function compactCount(n: number): string {
  let lead = Math.max(0, Math.trunc(n));
  let groups = 0;
  while (lead >= 1000) {
    lead = Math.floor(lead / 1000);
    groups++;
  }
  return groups === 0 ? String(lead) : `${String(lead)}${['k', 'm', 'b', 't'][groups - 1] ?? 'x'}`;
}

/** What a finished solve's reads found: its mode, and a sliding or stacking one's moves. */
export interface FinishedSolve {
  /** Whether the problem takes its assemblies apart (Find Solutions on sliding and stacking). */
  disassemble: boolean;
  /** The moves of a sliding or stacking problem's solution; null when it has none. */
  moves: number | null;
}

/** A note of the problems list; its kind is its style. */
export interface ProblemNote {
  kind: 'solving' | 'paused' | 'found' | 'none';
  text: string;
}

/**
 * The note beside a problem. `found` is null until its finished solve has
 * been read; `pausedSearch` is a stacking problem's saved search (its
 * started solve, which the snapshot's state does not show); `stop` is what
 * a run under way was asked to stop for (its progress's stopKind).
 */
export function problemNote(
  state: SolveState,
  p: Pick<ProblemSummary, 'state' | 'assemblies' | 'solutions'>,
  family: Family,
  found: FinishedSolve | null,
  pausedSearch = false,
  stop: 'pause' | 'abort' | null = null,
): ProblemNote {
  if (isActive(state)) {
    const text = stop === 'pause' ? 'pausing' : stop === 'abort' ? 'aborting' : 'solving';
    return { kind: 'solving', text };
  }
  // paused in memory, or a started solve kept in the file
  if (
    state === 'paused' ||
    (state === 'idle' && (pausedSearch || (family !== 'stacking' && p.state === 'solving')))
  ) {
    return { kind: 'paused', text: 'paused' };
  }
  if (p.state !== 'solved' || found === null || state === 'error') {
    return { kind: 'none', text: '' };
  }
  const count = (n: number, one: string, many: string): ProblemNote => ({
    kind: 'found',
    text: `${compactCount(n)} ${n === 1 ? one : many}`,
  });
  if (family !== 'brick' && found.disassemble) {
    return found.moves === null
      ? { kind: 'found', text: 'no solution' }
      : count(found.moves, 'move', 'moves');
  }
  const n = found.disassemble ? p.solutions : p.assemblies;
  if (n === null) {
    return { kind: 'none', text: '' };
  }
  return found.disassemble
    ? count(n, 'disassembly', 'disassemblies')
    : count(n, 'assembly', 'assemblies');
}
