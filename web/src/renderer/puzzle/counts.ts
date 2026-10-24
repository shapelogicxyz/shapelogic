// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the Puzzle workspace reads off a problem as the engine gives it
// (ProblemDetail): a piece's part, its count as the chips write it, the
// exclusive groups and their members, and the count a stepper or a field
// asks for. From docs/mockup/index.html: `rng`, `pzCount`, `pzIn`, `pzSet`
// (the count rules of a field: a fixed count moves both ends, a range its
// own end, the minimum never above the maximum) and the group helpers of
// `renderPzSel` and `renderPzGroups`. The groups are the engine's: a
// group exists while a part has copies in it (the mockup kept a list of
// its own, with empty groups).

import { contrast, hex, pieceColour } from '../shell/piece-colours.ts';
import type { Part, ProblemDetail, SolveState } from '../../engine/api.generated.ts';

/** The colour of the problems' chips (the mockup's). */
export const PROBLEM_CHIP = '#2f6df6';

/** A problem's chip style: the colour of its place in the list, as a shape's. */
export function problemChipStyle(index: number): string {
  const c = pieceColour(Math.max(0, index));
  return `background:${hex(c)};color:${contrast(c)}`;
}

/** A range as the mockup writes it: one number when both ends are equal. */
export function rng(a: number, b: number, sep = '–'): string {
  return a === b ? String(a) : `${String(a)}${sep}${String(b)}`;
}

/** The part of a shape in a problem, or undefined when it is no piece of it. */
export function partOf(detail: ProblemDetail | null, shape: string | null): Part | undefined {
  return shape === null ? undefined : detail?.parts.find((p) => p.shape === shape);
}

/** The count of a part as the chips show it: ×n, min–max, or 0 (the mockup's pzCount). */
export function countText(part: Pick<Part, 'min' | 'max'> | undefined): string {
  if (part === undefined || part.max <= 0) {
    return '0';
  }
  return part.min === part.max ? `×${String(part.max)}` : rng(part.min, part.max);
}

/**
 * The count a field or a stepper asks for (the mockup's pzSet): `value`
 * for the minimum or the maximum, with a fixed count (`range` false)
 * moving both ends; a maximum of 0 takes the piece out.
 */
export function nextCount(
  part: Pick<Part, 'min' | 'max'> | undefined,
  field: 'min' | 'max',
  value: number,
  range: boolean,
): { min: number; max: number } {
  const v = Math.max(0, Math.round(value) || 0);
  let min = part?.min ?? 0;
  let max = part?.max ?? 0;
  if (field === 'max') {
    max = v;
    min = range ? Math.min(min, v) : v;
  } else {
    min = Math.min(v, max);
    if (!range) {
      max = min;
    }
  }
  if (max <= 0) {
    min = 0;
    max = 0;
  }
  return { min, max };
}

/** The exclusive groups of a problem, in order of their numbers. */
export function groupsOf(detail: ProblemDetail | null): number[] {
  const all = new Set<number>();
  for (const p of detail?.parts ?? []) {
    for (const g of p.groups) {
      if (g.count > 0) {
        all.add(g.group);
      }
    }
  }
  return [...all].sort((a, b) => a - b);
}

/** The group a part's copies are in: the first one (0: none). */
export function groupOf(part: Part | undefined): number {
  return part?.groups.find((g) => g.count > 0)?.group ?? 0;
}

/** How many copies of a part are in a group. */
export function inGroup(part: Part | undefined, group: number): number {
  return part?.groups.find((g) => g.group === group)?.count ?? 0;
}

/** The parts with copies in a group. */
export function membersOf(detail: ProblemDetail | null, group: number): Part[] {
  return (detail?.parts ?? []).filter((p) => inGroup(p, group) > 0);
}

/** The number a new group gets: one above the highest. */
export function nextGroup(detail: ProblemDetail | null): number {
  return Math.max(0, ...groupsOf(detail)) + 1;
}

/** Whether a solve state is a run under way (the core's isActive: the problem cannot be edited). */
export function solveActive(s: SolveState): boolean {
  return s === 'preparing' || s === 'assembling' || s === 'draining';
}
