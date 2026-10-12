// SPDX-License-Identifier: GPL-3.0-or-later
//
// The exports' plain parts (stage 7): the keyframes an animation channel
// keeps (gltfexport.cpp's thinning: only those its interpolation does not
// reproduce), and the vector image's painting order (a BSP tree: what is
// behind first, a face split where another's plane crosses it, a line
// after the face it lies on, the order turned round with the eye); and
// the STL dialogs' fields and words (deferred part 5, stl-form.ts).

import { describe, expect, it } from 'vitest';

import { keptKeys } from '../../../src/renderer/export/glb.ts';
import { positive } from '../../../src/renderer/export/form.ts';
import {
  exportedLine,
  formatParameter,
  overwriteQuestion,
  piecesText,
  readParameters,
  volumeLine,
} from '../../../src/renderer/export/stl-form.ts';
import { paintingOrder, polygonNormal } from '../../../src/renderer/view3d/depth-order.ts';
import type { Face, Segment } from '../../../src/renderer/view3d/depth-order.ts';

describe('the keyframes a channel keeps', () => {
  it('keeps the ends of a straight run and every corner', () => {
    const times = [0, 1, 2, 3, 4, 5];
    // still, then along x, then still again
    const values = [0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 0, 2, 0, 0, 2, 0, 0];
    expect(keptKeys(times, values, 3, 'linear')).toEqual([0, 1, 3, 5]);
    // a step channel keeps the keys where the value changes (held until then)
    expect(keptKeys(times, [1, 1, 1, 0, 0, 1], 1, 'step')).toEqual([0, 3, 5]);
    // two keys are kept whole
    expect(keptKeys([0, 1], [5, 6], 1, 'linear')).toEqual([0, 1]);
  });

  it('keeps a turn’s ends when slerp gives the keys between, and the middle when it does not', () => {
    const q = (deg: number): number[] => {
      const h = (deg * Math.PI) / 360;
      return [0, 0, Math.sin(h), Math.cos(h)];
    };
    const times = [0, 1, 2, 3, 4];
    const even = [0, 22.5, 45, 67.5, 90].flatMap(q);
    expect(keptKeys(times, even, 4, 'slerp')).toEqual([0, 4]);
    const uneven = [0, 10, 45, 67.5, 90].flatMap(q);
    expect(keptKeys(times, uneven, 4, 'slerp')).toContain(1);
  });

  it('reads the dialogs’ numbers: above 0, a comma as the point', () => {
    expect(positive('0.5')).toBe(0.5);
    expect(positive(' 2,5 ')).toBe(2.5);
    expect(positive('0')).toBeNull();
    expect(positive('-1')).toBeNull();
    expect(positive('x')).toBeNull();
  });
});

const square = (z: number, paint: string, size = 1): Face<string> => ({
  corners: [
    [-size, -size, z],
    [size, -size, z],
    [size, size, z],
    [-size, size, z],
  ],
  paint,
});

const names = (order: ReturnType<typeof paintingOrder<string, string>>): string[] =>
  order.map((x) => ('face' in x ? x.face.paint : x.segment.paint));

describe('the vector image’s painting order', () => {
  it('paints what is behind first, and turns round with the eye', () => {
    const faces = [square(1, 'near'), square(-1, 'far'), square(0, 'middle')];
    expect(names(paintingOrder<string, string>(faces, [], [0, 0, 10]))).toEqual([
      'far',
      'middle',
      'near',
    ]);
    expect(names(paintingOrder<string, string>(faces, [], [0, 0, -10]))).toEqual([
      'near',
      'middle',
      'far',
    ]);
    // an eye far away along a direction (an orthographic camera)
    expect(names(paintingOrder<string, string>(faces, [], [0, 0, 1], true))).toEqual([
      'far',
      'middle',
      'near',
    ]);
  });

  it('splits a face its neighbour’s plane crosses, so each part is painted in its place', () => {
    // a wall at x = 0 across a floor at y = 0: the floor's two halves lie on either side
    const wall: Face<string> = {
      corners: [
        [0, -1, -1],
        [0, -1, 1],
        [0, 1, 1],
        [0, 1, -1],
      ],
      paint: 'wall',
    };
    const floor: Face<string> = {
      corners: [
        [-2, 0, -2],
        [2, 0, -2],
        [2, 0, 2],
        [-2, 0, 2],
      ],
      paint: 'floor',
    };
    const order = paintingOrder<string, string>([wall, floor], [], [5, 3, 0]);
    expect(names(order)).toEqual(['floor', 'wall', 'floor']);
    // the half behind the wall first, the half on the eye's side last
    const first = order[0];
    const last = order[2];
    expect(
      first !== undefined && 'face' in first && first.face.corners.every((p) => p[0] <= 0),
    ).toBe(true);
    expect(last !== undefined && 'face' in last && last.face.corners.every((p) => p[0] >= 0)).toBe(
      true,
    );
  });

  it('paints a line after the face it lies on, and a line behind a face before it', () => {
    const on: Segment<string> = { a: [-1, 0, 0], b: [1, 0, 0], paint: 'on' };
    const behind: Segment<string> = { a: [-1, 0, -2], b: [1, 0, -2], paint: 'behind' };
    const order = paintingOrder([square(0, 'face')], [on, behind], [0, 0, 10]);
    expect(names(order)).toEqual(['behind', 'face', 'on']);
    // a line crossing the face's plane is cut there
    const across: Segment<string> = { a: [0, 0, -1], b: [0, 0, 1], paint: 'across' };
    expect(names(paintingOrder([square(0, 'face')], [across], [0, 0, 10]))).toEqual([
      'across',
      'face',
      'across',
    ]);
  });

  it('leaves out a face without area and keeps the lines', () => {
    const flat: Face<string> = {
      corners: [
        [0, 0, 0],
        [1, 0, 0],
        [2, 0, 0],
      ],
      paint: 'degenerate',
    };
    expect(polygonNormal(flat.corners)).toBeNull();
    const s: Segment<string> = { a: [0, 0, 0], b: [1, 1, 1], paint: 'line' };
    expect(names(paintingOrder([flat], [s], [0, 0, 5]))).toEqual(['line']);
    expect(polygonNormal(square(0, 'x').corners)).toEqual([0, 0, 1]);
  });
});

describe('the STL dialogs’ fields and words (stlexport.cpp, stlexportsolution.cpp)', () => {
  const unit = { name: 'Unit Size', type: 'positiveNumber', default: 10, tooltip: '' } as const;
  const bevel = { name: 'Bevel', type: 'positiveNumber', default: 0.3, tooltip: '' } as const;
  const rec = { name: 'Recursions', type: 'positiveInteger', default: 2, tooltip: '' } as const;
  const chamfers = { name: 'Interior chamfers', type: 'switch', default: 1, tooltip: '' } as const;

  it('shows a value as the original’s %2.2f and %i', () => {
    expect(formatParameter(unit, 10)).toBe('10.00');
    expect(formatParameter(bevel, 0.3)).toBe('0.30');
    expect(formatParameter(bevel, 0.01)).toBe('0.01');
    expect(formatParameter(rec, 2)).toBe('2');
    expect(formatParameter(chamfers, 1)).toBe('1');
  });

  it('reads the fields in order, refusing what is not a number', () => {
    const params = [unit, bevel, rec, chamfers];
    expect(readParameters(params, ['10.00', '0,5', '3', false])).toEqual({
      values: [10, 0.5, 3, 0],
    });
    expect(readParameters(params, ['12', ' .3 ', '0', true])).toEqual({ values: [12, 0.3, 0, 1] });
    expect(readParameters(params, ['', '0.3', '2', true])).toEqual({
      error: 'Unit Size must be a number',
    });
    expect(readParameters(params, ['10', 'x', '2', true])).toEqual({
      error: 'Bevel must be a number',
    });
    expect(readParameters(params, ['10', '0.3', '2.5', true])).toEqual({
      error: 'Recursions must be a whole number',
    });
    // the ranges are the engine's: a negative bevel is read, and refused there
    expect(readParameters(params, ['10', '-1', '2', true])).toEqual({ values: [10, -1, 2, 1] });
  });

  it('says the volume, the pieces, the question and the outcome in the original’s words', () => {
    expect(volumeLine(7936.04)).toBe('Volume: 7936.0 cubic-units');
    expect(
      piecesText(1, [
        { label: 'S2', name: 'Frame1', count: 1 },
        { label: 'S4', name: '', count: 2 },
      ]),
    ).toEqual([
      'Pieces placed in solution 1:',
      '  S2  Frame1  ×1',
      '  S4  ×2',
      'Total instances: 3',
    ]);
    expect(overwriteQuestion(5)).toBe('5 output file(s) already exist. Overwrite?');
    expect(exportedLine(5, 0)).toBe('Done — exported 5 piece type(s).');
    expect(exportedLine(4, 1)).toBe('Done — exported 4 piece type(s), 1 error(s).');
  });
});
