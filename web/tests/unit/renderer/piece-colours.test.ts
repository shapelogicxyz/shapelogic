// SPDX-License-Identifier: GPL-3.0-or-later
//
// The default piece colours against the original: the values are
// src/gui/piececolor.cpp's pieceColorRi/Gi/Bi(x) and (x, sub) and
// contrastPieceColor, printed by a probe compiled against that file.

import { describe, expect, it } from 'vitest';

import {
  bytes,
  contrast,
  dark,
  hex,
  light,
  pieceColour,
  pieceSubBytes,
  pieceSubColour,
  shapeColour,
} from '../../../src/renderer/shell/piece-colours.ts';

describe('piece colours', () => {
  it('are the original table, then its sine formula', () => {
    const want: [number, [number, number, number]][] = [
      [0, [0, 0, 255]],
      [5, [255, 0, 255]],
      [17, [255, 0, 153]],
      [18, [131, 98, 207]],
      [19, [212, 239, 87]],
      [25, [3, 194, 250]],
      [40, [162, 115, 25]],
    ];
    for (const [x, rgb] of want) {
      expect(bytes(pieceColour(x)), `shape ${x}`).toEqual(rgb);
    }
  });

  it('vary for the copies of a piece as the original jitters them', () => {
    const want: [number, number, [number, number, number]][] = [
      [0, 0, [0, 0, 255]],
      [0, 1, [38, 30, 201]],
      [0, 2, [38, 30, 255]],
      [0, 3, [38, 0, 201]],
      [0, 10, [51, 0, 183]],
      [2, 1, [216, 30, 53]],
      [2, 10, [203, 0, 71]],
      [7, 1, [38, 171, 53]],
      [7, 3, [38, 171, 0]],
      [7, 10, [0, 134, 0]],
      [18, 1, [112, 79, 163]],
      [18, 3, [151, 116, 163]],
      [18, 10, [151, 98, 163]],
    ];
    for (const [x, sub, rgb] of want) {
      expect(pieceSubBytes(x, sub), `shape ${x} copy ${sub}`).toEqual(rgb);
      expect(pieceSubColour(x, sub)[0]).toBeCloseTo(rgb[0] / 255, 2);
    }
  });

  it('give the contrast colour, the checker shades and the own colour', () => {
    expect(contrast(pieceColour(0))).toBe('#ffffff');
    expect(contrast(pieceColour(1))).toBe('#000000');
    expect(contrast(pieceColour(12))).toBe('#000000');
    expect(dark([1, 0.5, 0])).toEqual([0.9, 0.45, 0]);
    expect(light([0, 1, 0.5])[0]).toBeCloseTo(0.1);
    expect(light([0, 1, 0.5])[2]).toBeCloseTo(0.55);
    expect(hex(shapeColour('#12ab34', 3))).toBe('#12ab34');
    expect(hex(shapeColour(null, 2))).toBe('#ff0000');
  });
});
