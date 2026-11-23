// SPDX-License-Identifier: GPL-3.0-or-later
//
// The default piece colors against the original: the values are
// src/gui/piececolor.cpp's pieceColorRi/Gi/Bi(x) and (x, sub) and
// contrastPieceColor, printed by a probe compiled against that file.

import { describe, expect, it } from 'vitest';

import {
  bytes,
  contrast,
  copyColor,
  dark,
  hex,
  light,
  pieceColor,
  pieceSubBytes,
  pieceSubColor,
  shapeColor,
} from '../../../src/renderer/shell/piece-colors.ts';

describe('piece colors', () => {
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
      expect(bytes(pieceColor(x)), `shape ${x}`).toEqual(rgb);
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
      expect(pieceSubColor(x, sub)[0]).toBeCloseTo(rgb[0] / 255, 2);
    }
  });

  it('give the contrast color, the checker shades and the own color', () => {
    expect(contrast(pieceColor(0))).toBe('#ffffff');
    expect(contrast(pieceColor(1))).toBe('#000000');
    expect(contrast(pieceColor(12))).toBe('#000000');
    expect(dark([1, 0.5, 0])).toEqual([0.9, 0.45, 0]);
    expect(light([0, 1, 0.5])[0]).toBeCloseTo(0.1);
    expect(light([0, 1, 0.5])[2]).toBeCloseTo(0.55);
    expect(hex(shapeColor('#12ab34', 3))).toBe('#12ab34');
    expect(hex(shapeColor(null, 2))).toBe('#ff0000');
  });

  it("give a disc its palette color, else the copy's (diskTint)", () => {
    const palette = [
      { id: 'c1', rgb: '#ffdc5a' },
      { id: 'c2', rgb: '#aa1919' },
    ];
    const disc = { color: null, discColor: 'c2' };
    expect(hex(copyColor(disc, 0, 0, palette))).toBe('#aa1919');
    expect(hex(copyColor(disc, 0, 3, palette))).toBe('#aa1919');
    const plain = { color: '#12ab34', discColor: null };
    expect(copyColor(plain, 4, 2, palette)).toEqual(pieceSubColor(4, 2, shapeColor('#12ab34', 4)));
    // a color the palette no longer has: the copy's
    expect(copyColor({ color: null, discColor: 'c9' }, 7, 1, palette)).toEqual(pieceSubColor(7, 1));
    expect(copyColor(undefined, 7, 1, palette)).toEqual(pieceSubColor(7, 1));
  });
});
