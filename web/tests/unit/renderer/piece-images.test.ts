// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures on sliding pieces (decision 0038): the map from a picture's
// pixels to the cells (api ShapeImage's formula, the one the engine's
// shape_image.h keeps up to date), and the dialog's crop.

import { describe, expect, it } from 'vitest';

import type { ShapeImage } from '../../../src/engine/api.generated.ts';
import {
  cellBox,
  compose,
  invert,
  pictureToCells,
  sourceRect,
  turnMatrix,
} from '../../../src/renderer/shell/picture-math.ts';
import type { Affine } from '../../../src/renderer/shell/picture-math.ts';

const image = (over: Partial<ShapeImage>): ShapeImage => ({
  image: 0,
  hash: '0',
  width: 40,
  height: 20,
  fit: 'stretch',
  src: [0, 0, 40, 20],
  box: [0, 0, 2, 1],
  turn: 0,
  ...over,
});

// rounded to a millionth of a cell, so equal points compare equal
const apply = (t: Affine, x: number, y: number): [number, number] => [
  Math.round((t[0] * x + t[2] * y + t[4]) * 1e6) / 1e6 + 0,
  Math.round((t[1] * x + t[3] * y + t[5]) * 1e6) / 1e6 + 0,
];

describe('piece images', () => {
  it('turn matrices: quarter turns counter-clockwise after a mirror', () => {
    expect(turnMatrix(0)).toEqual([1, 0, 0, 1]);
    expect(turnMatrix(1)).toEqual([0, -1, 1, 0]);
    expect(turnMatrix(2)).toEqual([-1, 0, 0, -1]);
    expect(turnMatrix(4)).toEqual([-1, 0, 0, 1]);
    const seen = new Set([0, 1, 2, 3, 4, 5, 6, 7].map((t) => turnMatrix(t).join(',')));
    expect(seen.size).toBe(8);
  });

  it('upright, the picture covers the box with its top at the high y', () => {
    const t = pictureToCells(image({ box: [3, 1, 2, 1] }));
    expect(apply(t, 0, 0)).toEqual([3, 2]); // top left
    expect(apply(t, 40, 20)).toEqual([5, 1]); // bottom right
  });

  it('only the part shown is laid on the box', () => {
    const t = pictureToCells(image({ src: [10, 0, 20, 20], box: [0, 0, 1, 1] }));
    expect(apply(t, 10, 0)).toEqual([0, 1]);
    expect(apply(t, 30, 20)).toEqual([1, 0]);
  });

  it('a quarter turn puts the picture’s top on the left', () => {
    // the 2x1 piece turned counter-clockwise is 1x2; its picture turns with it
    const t = pictureToCells(image({ box: [0, 0, 1, 2], turn: 1 }));
    expect(apply(t, 0, 0)).toEqual([0, 0]); // top left goes to the bottom left
    expect(apply(t, 40, 0)).toEqual([0, 2]); // top right goes to the top left
    expect(apply(t, 40, 20)).toEqual([1, 2]);
  });

  it('maps compose and invert', () => {
    const t = pictureToCells(image({ box: [1, 2, 3, 4], turn: 6 }));
    const back = compose(invert(t), t);
    for (const [k, want] of [1, 0, 0, 1, 0, 0].entries()) {
      expect(back[k]).toBeCloseTo(want, 9);
    }
  });

  it('crop keeps the middle at the box’s proportions; stretch takes it all', () => {
    expect(sourceRect('stretch', 300, 200, 1, 1)).toEqual([0, 0, 300, 200]);
    expect(sourceRect('crop', 300, 200, 1, 1)).toEqual([50, 0, 200, 200]);
    expect(sourceRect('crop', 300, 200, 3, 1)).toEqual([0, 50, 300, 100]);
    expect(sourceRect('crop', 300, 200, 3, 2)).toEqual([0, 0, 300, 200]);
  });

  it('the cells’ box in x and y', () => {
    expect(cellBox([])).toBeNull();
    expect(
      cellBox([
        [1, 2, 0],
        [3, 2, 0],
        [1, 4, 1],
      ]),
    ).toEqual([1, 2, 3, 3]);
  });
});
