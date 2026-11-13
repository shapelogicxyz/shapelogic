// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's problems list notes (renderer/solver/problem-note.ts): a
// solve under way or paused, what a finished one found, the counts short.

import { describe, expect, it } from 'vitest';
import { compactCount, problemNote } from '../../../src/renderer/solver/problem-note.ts';

const solved = { state: 'solved' as const, assemblies: 12_200, solutions: 1 };

describe('the problems list notes', () => {
  it('shorten a count to its first group of digits and its letter', () => {
    expect(compactCount(0)).toBe('0');
    expect(compactCount(999)).toBe('999');
    expect(compactCount(1000)).toBe('1k');
    expect(compactCount(12_200)).toBe('12k');
    expect(compactCount(999_999)).toBe('999k');
    expect(compactCount(15_111_111)).toBe('15m');
    expect(compactCount(2_500_000_000)).toBe('2b');
    expect(compactCount(7_000_000_000_000)).toBe('7t');
    expect(compactCount(3e15)).toBe('3x');
  });

  it('say a solve under way or paused', () => {
    expect(problemNote('assembling', solved, 'brick', null).text).toBe('solving');
    // asked to stop: pausing, or aborting, until the run is through
    expect(problemNote('assembling', solved, 'sliding', null, false, 'pause').text).toBe('pausing');
    expect(problemNote('draining', solved, 'brick', null, false, 'abort').text).toBe('aborting');
    expect(problemNote('paused', solved, 'sliding', null, false, 'pause').text).toBe('paused');
    // paused by the crash recovery copy, to carry on: saving, not paused
    expect(problemNote('assembling', solved, 'sliding', null, false, 'save').text).toBe('saving');
    expect(problemNote('paused', solved, 'sliding', null, false, 'save').text).toBe('saving');
    expect(problemNote('paused', solved, 'sliding', null).text).toBe('paused');
    // a started solve kept in the file; a stacking one's is its saved search
    const started = { ...solved, state: 'solving' as const };
    expect(problemNote('idle', started, 'brick', null).text).toBe('paused');
    expect(problemNote('idle', started, 'stacking', null).kind).toBe('none');
    expect(problemNote('idle', started, 'stacking', null, true).text).toBe('paused');
  });

  it('say what a finished solve found, by the mode', () => {
    const asm = { disassemble: false, moves: null };
    const dis = { disassemble: true, moves: null };
    expect(problemNote('finished', solved, 'brick', asm).text).toBe('12k assemblies');
    expect(problemNote('idle', solved, 'brick', dis).text).toBe('1 disassembly');
    expect(problemNote('idle', { ...solved, solutions: 0 }, 'brick', dis).text).toBe(
      '0 disassemblies',
    );
    expect(problemNote('idle', solved, 'sliding', { disassemble: true, moves: 31 }).text).toBe(
      '31 moves',
    );
    expect(problemNote('idle', solved, 'stacking', { disassemble: true, moves: 5359 }).text).toBe(
      '5k moves',
    );
    expect(problemNote('idle', solved, 'stacking', dis).text).toBe('no solution');
    // optimized paths: neither the fewest moves nor none is proven
    const grid = { ...solved, slideGrid: 6 };
    expect(problemNote('idle', grid, 'sliding', { disassemble: true, moves: 40 }).text).toBe(
      '40 moves (optimized)',
    );
    expect(problemNote('idle', grid, 'sliding', dis).text).toBe('no solution (optimized)');
    expect(
      problemNote('idle', { ...solved, slideGrid: 0 }, 'sliding', { disassemble: true, moves: 1 })
        .text,
    ).toBe('1 move');
    expect(problemNote('idle', solved, 'sliding', asm).text).toBe('12k assemblies');
    // nothing before the read, nor for a problem not solved
    expect(problemNote('idle', solved, 'brick', null).kind).toBe('none');
    expect(problemNote('idle', { ...solved, state: 'unsolved' }, 'brick', asm).kind).toBe('none');
  });
});
