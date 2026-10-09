// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Puzzle workspace's logic without a page: the counts as the chips
// write them, the count a field or a stepper asks for (the mockup's pzSet:
// never a minimum above the maximum, which the engine refuses), the
// groups read off the parts, the scene's layout (the mockup's puzLayout)
// and its labels.

import { describe, expect, it } from 'vitest';

import type { Part, ProblemDetail } from '../../../src/engine/api.generated.ts';
import {
  countText,
  groupOf,
  groupsOf,
  inGroup,
  membersOf,
  nextCount,
  nextGroup,
  partOf,
  rng,
  solveActive,
} from '../../../src/renderer/puzzle/counts.ts';
import { label, MAX_COPIES, puzLayout } from '../../../src/renderer/puzzle/layout.ts';

const part = (shape: string, min: number, max: number, groups: Part['groups'] = []): Part => ({
  shape,
  min,
  max,
  groups,
});

function detail(parts: Part[], result: string | null = 's1'): ProblemDetail {
  return {
    id: 'p1',
    label: 'P1',
    name: '',
    result,
    pieces: parts.reduce((n, p) => n + p.max, 0),
    state: 'unsolved',
    solve: 'idle',
    analysing: false,
    assemblies: null,
    solutions: null,
    saved: 0,
    timeMs: null,
    parts,
    colourRules: [],
    maxHoles: null,
    savedPosition: false,
    solverSettings: {},
  };
}

describe('the counts', () => {
  it('are written as the mockup wrote them', () => {
    expect(rng(3, 3)).toBe('3');
    expect(rng(0, 10)).toBe('0–10');
    expect(rng(60, 99, ' - ')).toBe('60 - 99');
    expect(countText(undefined)).toBe('0');
    expect(countText(part('s2', 0, 0))).toBe('0');
    expect(countText(part('s2', 2, 2))).toBe('×2');
    expect(countText(part('s2', 0, 10))).toBe('0–10');
  });

  it('a fixed count moves both ends, a range its own end', () => {
    const p = part('s2', 2, 2);
    expect(nextCount(p, 'max', 3, false)).toEqual({ min: 3, max: 3 });
    expect(nextCount(p, 'max', 1, false)).toEqual({ min: 1, max: 1 });
    expect(nextCount(part('s2', 1, 4), 'max', 6, true)).toEqual({ min: 1, max: 6 });
    expect(nextCount(part('s2', 3, 4), 'max', 2, true)).toEqual({ min: 2, max: 2 });
    expect(nextCount(part('s2', 1, 4), 'min', 9, true)).toEqual({ min: 4, max: 4 });
    expect(nextCount(part('s2', 1, 4), 'min', 0, true)).toEqual({ min: 0, max: 4 });
    // a piece not in the puzzle: one copy, or a range from 0
    expect(nextCount(undefined, 'max', 1, false)).toEqual({ min: 1, max: 1 });
    expect(nextCount(undefined, 'max', 1, true)).toEqual({ min: 0, max: 1 });
  });

  it('takes a piece out at a maximum of 0, and reads what a field holds', () => {
    expect(nextCount(part('s2', 1, 1), 'max', 0, false)).toEqual({ min: 0, max: 0 });
    expect(nextCount(part('s2', 0, 3), 'max', -2, true)).toEqual({ min: 0, max: 0 });
    expect(nextCount(part('s2', 1, 1), 'max', Number('abc'), false)).toEqual({ min: 0, max: 0 });
    expect(nextCount(part('s2', 1, 1), 'max', 2.6, false)).toEqual({ min: 3, max: 3 });
  });

  it('never asks for a minimum above the maximum (the engine refuses it)', () => {
    let seed = 7;
    const rnd = (n: number): number => {
      seed = (seed * 1103515245 + 12345) % 2147483648;
      return seed % n;
    };
    for (let i = 0; i < 2000; i++) {
      const min = rnd(6);
      const p = part('s2', min, min + rnd(6));
      const r = nextCount(p, rnd(2) === 0 ? 'min' : 'max', rnd(14) - 3, rnd(2) === 0);
      expect(r.min).toBeGreaterThanOrEqual(0);
      expect(r.min).toBeLessThanOrEqual(r.max);
    }
  });
});

describe('the groups', () => {
  const d = detail([
    part('s2', 1, 1, [{ group: 1, count: 1 }]),
    part('s3', 2, 2, [
      { group: 1, count: 1 },
      { group: 3, count: 1 },
    ]),
    part('s4', 1, 1),
    part('s5', 1, 1, [{ group: 2, count: 0 }]),
  ]);

  it('are the engine’s: groups with copies in them', () => {
    expect(groupsOf(d)).toEqual([1, 3]);
    expect(groupsOf(null)).toEqual([]);
    expect(nextGroup(d)).toBe(4);
    expect(nextGroup(detail([]))).toBe(1);
  });

  it('name a part’s group, its copies in each, and the members', () => {
    expect(groupOf(partOf(d, 's2'))).toBe(1);
    expect(groupOf(partOf(d, 's4'))).toBe(0);
    expect(groupOf(partOf(d, 's5'))).toBe(0);
    expect(inGroup(partOf(d, 's3'), 3)).toBe(1);
    expect(inGroup(partOf(d, 's3'), 2)).toBe(0);
    expect(membersOf(d, 1).map((p) => p.shape)).toEqual(['s2', 's3']);
    expect(membersOf(d, 3).map((p) => p.shape)).toEqual(['s3']);
    expect(partOf(d, 's9')).toBeUndefined();
    expect(partOf(d, null)).toBeUndefined();
  });

  it('a run under way freezes a problem; a paused or finished one does not', () => {
    expect(['preparing', 'assembling', 'draining'].every((s) => solveActive(s as never))).toBe(
      true,
    );
    expect(['idle', 'paused', 'finished', 'error'].some((s) => solveActive(s as never))).toBe(
      false,
    );
  });
});

describe('the pieces scene', () => {
  it('lays out the result, the selected piece and every copy as the mockup’s puzLayout', () => {
    const { items, hidden } = puzLayout('s1', 's3', [part('s2', 1, 1), part('s3', 1, 3)]);
    expect(hidden).toBe(0);
    expect(items.map((i) => [i.role, i.shape, i.copy, i.total, i.optional])).toEqual([
      ['result', 's1', 1, 1, false],
      ['selected', 's3', 1, 1, false],
      ['piece', 's2', 1, 1, false],
      ['piece', 's3', 1, 3, false],
      ['piece', 's3', 2, 3, true],
      ['piece', 's3', 3, 3, true],
    ]);
    expect(items[0]?.pos).toEqual([0, 0]);
    expect(items[1]?.pos).toEqual([7.5, 0]);
    // four copies in one row, centred on 3.75, 5.9 apart
    const xs = items.slice(2).map((i) => i.pos[0]);
    expect(xs.map((x) => Number(x.toFixed(2)))).toEqual([-5.1, 0.8, 6.7, 12.6]);
    expect(items.slice(2).every((i) => i.pos[1] === -7.2)).toBe(true);
  });

  it('wraps rows of five, leaves out the selected piece when it is the result', () => {
    const { items } = puzLayout('s1', 's1', [part('s2', 6, 6)]);
    expect(items.filter((i) => i.role === 'selected')).toEqual([]);
    const ys = items.filter((i) => i.role === 'piece').map((i) => i.pos[1]);
    expect(ys).toEqual([-7.2, -7.2, -7.2, -7.2, -7.2, -12.8]);
    expect(puzLayout(null, null, []).items).toEqual([]);
  });

  it('draws at most MAX_COPIES copies and counts the rest', () => {
    const { items, hidden } = puzLayout('s1', null, [part('s2', 0, 65535)]);
    expect(items.filter((i) => i.role === 'piece')).toHaveLength(MAX_COPIES);
    expect(hidden).toBe(65535 - MAX_COPIES);
  });

  it('labels the items as the mockup did', () => {
    const [r, s, a, b] = puzLayout('s1', 's3', [part('s3', 1, 2)]).items;
    if (r === undefined || s === undefined || a === undefined || b === undefined) {
      throw new Error('items missing');
    }
    expect(label(r, 'S1')).toBe('RESULT · S1');
    expect(label(s, 'S3')).toBe('SELECTED · S3');
    expect(label(a, 'S3')).toBe('S3  ·  1/2');
    expect(label(b, 'S3')).toBe('S3  ·  2/2\n(optional)');
    const [, one] = puzLayout('s1', null, [part('s2', 1, 1)]).items;
    expect(one === undefined ? '' : label(one, 'S2')).toBe('S2');
  });
});
