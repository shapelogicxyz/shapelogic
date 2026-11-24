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
  dragRect,
  invert,
  pictureToCells,
  rectFromSource,
  sourceFromRect,
  sourceRect,
  turnMatrix,
} from '../../../src/renderer/shell/picture-math.ts';
import type { Affine, PictureRect } from '../../../src/renderer/shell/picture-math.ts';

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

  it('manual: the picture’s rectangle and the part shown go both ways', () => {
    // the whole 300 x 200 picture on a 3 x 2 box lies over just the box
    expect(rectFromSource([0, 0, 300, 200], 300, 200, 3, 2)).toEqual([0, 0, 3, 2]);
    // its middle half: the picture twice the box's size, a quarter of it left and up
    const r = rectFromSource([75, 50, 150, 100], 300, 200, 3, 2);
    expect(r).toEqual([-1.5, -1, 6, 4]);
    expect(sourceFromRect(r, 300, 200, 3, 2)).toEqual([75, 50, 150, 100]);
    // whole pixels, at least one wide, overlapping the picture
    expect(sourceFromRect([-0.001, 0.2, 3, 2], 300, 200, 3, 2)).toEqual([0, -20, 300, 200]);
    expect(sourceFromRect([-1e6, -1e6, 1e7, 1e7], 300, 200, 3, 2)).toEqual([30, 20, 1, 1]);
    // a picture half the box's size in its middle: the part shown reaches past its edges
    const small: PictureRect = [0.75, 0.5, 1.5, 1];
    expect(sourceFromRect(small, 300, 200, 3, 2)).toEqual([-150, -100, 600, 400]);
    expect(rectFromSource([-150, -100, 600, 400], 300, 200, 3, 2)).toEqual(small);
  });

  it('manual: drags move, stretch, scale and shrink the picture, which keeps over the box', () => {
    const big: [number, number, number, number] = [-100, -100, 200, 200];
    const r: PictureRect = [-1, -1, 4, 4]; // over a 2 x 2 box
    const limits = { minSide: 0.5, overlap: 0.25 };
    const drag = (
      handle: Parameters<typeof dragRect>[1],
      dx: number,
      dy: number,
      bounds = big,
      free = false,
    ): PictureRect => dragRect(r, handle, dx, dy, 2, 2, bounds, free, limits);
    // the middle moves it, until only the overlap is over the box
    expect(drag('move', 0.5, -0.25)).toEqual([-0.5, -1.25, 4, 4]);
    expect(drag('move', 5, -5)).toEqual([1.75, -3.75, 4, 4]);
    // a side moves that side, past the box's edge but not past the picture's smallest side
    expect(drag('e', 1, 7)).toEqual([-1, -1, 5, 4]);
    expect(drag('w', 2, 0)).toEqual([1, -1, 2, 4]);
    // the left side stops where only the overlap is left over the box
    expect(drag('w', 9, 0)).toEqual([1.75, -1, 1.25, 4]);
    expect(drag('e', -9, 0)).toEqual([-1, -1, 1.25, 4]);
    expect(drag('n', 0, -1)).toEqual([-1, -2, 4, 5]);
    // a corner scales from the opposite one, keeping the proportions, smaller than the box too
    expect(drag('se', 2, 2)).toEqual([-1, -1, 6, 6]);
    expect(drag('se', 2, 0)).toEqual([-1, -1, 5, 5]);
    expect(drag('se', -2, -2)).toEqual([-1, -1, 2, 2]);
    expect(drag('se', -9, -9)).toEqual([-1, -1, 1.25, 1.25]);
    expect(drag('nw', -1, -1)).toEqual([-2, -2, 5, 5]);
    // or freely
    expect(drag('se', 2, 0, big, true)).toEqual([-1, -1, 6, 4]);
    // the bounds hold the dots in reach
    expect(drag('e', 10, 0, [-2, -2, 6, 6])).toEqual([-1, -1, 5, 4]);
    expect(drag('se', 10, 10, [-2, -2, 6, 6])).toEqual([-1, -1, 5, 5]);
    expect(drag('move', -10, 0, [-1.5, -2, 6, 6])).toEqual([-1.5, -1, 4, 4]);
    // a small picture inside the box moves about it freely
    const s: PictureRect = [0.5, 0.5, 1, 1];
    expect(dragRect(s, 'move', 0.25, 0.25, 2, 2, big, false, limits)).toEqual([0.75, 0.75, 1, 1]);
    expect(dragRect(s, 'move', -9, 9, 2, 2, big, false, limits)).toEqual([-0.75, 1.75, 1, 1]);
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
