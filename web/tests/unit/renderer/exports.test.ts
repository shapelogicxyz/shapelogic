// SPDX-License-Identifier: GPL-3.0-or-later
//
// The exports' plain parts (stage 7): the keyframes an animation channel
// keeps (gltfexport.cpp's thinning: only those its interpolation does not
// reproduce), and the vector image's painting order (a BSP tree: what is
// behind first, a face split where another's plane crosses it, a line
// after the face it lies on, the order turned round with the eye).

import { describe, expect, it } from 'vitest';

import { keptKeys } from '../../../src/renderer/export/glb.ts';
import { positive } from '../../../src/renderer/export/form.ts';
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
