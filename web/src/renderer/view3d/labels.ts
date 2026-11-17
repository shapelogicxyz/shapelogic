// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver view's piece labels (Display ▸ Labels ▸ Show Piece Names and
// Show Piece Labels): a sprite of a piece's label (S1, S1.2), its shape's
// name, or both (pieceText in solution.ts), black on a white box so that it
// reads on any piece colour, which solution.ts places on the piece. A
// sprite always faces the eye, so the text stays upright however the view
// turns. Kept out of solution.ts, which is also compiled without the DOM.

import { CanvasTexture, Sprite, SpriteMaterial } from 'three';

/** A one-line label's height on the screen, in CSS pixels. */
const LABEL_PX = 20;
/** What each further line adds to a label's height, in CSS pixels. */
const LINE_PX = 16;
/** The texels of a label per CSS pixel: sharp at the image export's 2x and on a Retina screen. */
const LABEL_RATIO = 4;

/**
 * A label's sprite: black text on a white box with a thin dark edge,
 * drawn over everything, a line for each line of `text` ("\n"), centred;
 * its `aspect` (width over height) and its height on the screen
 * (`pixels`) in `userData`. Null where there is no 2D canvas.
 */
export function labelSprite(text: string): Sprite | null {
  const r = LABEL_RATIO;
  const c = document.createElement('canvas');
  const g = c.getContext('2d');
  if (g === null) {
    return null;
  }
  const font = `700 ${String(13 * r)}px system-ui, -apple-system, 'Segoe UI', sans-serif`;
  g.font = font;
  const lines = text.split('\n');
  const px = LABEL_PX + (lines.length - 1) * LINE_PX;
  const h = px * r;
  const widest = Math.max(...lines.map((l) => g.measureText(l).width));
  const w = Math.max(LABEL_PX * r, Math.ceil(widest) + 12 * r);
  c.width = w;
  c.height = h;
  // the canvas's state went with its resize
  const edge = r;
  g.beginPath();
  g.roundRect(edge / 2, edge / 2, w - edge, h - edge, 5 * r);
  g.fillStyle = '#ffffff';
  g.fill();
  g.lineWidth = edge;
  g.strokeStyle = 'rgba(0, 0, 0, 0.55)';
  g.stroke();
  g.font = font;
  g.fillStyle = '#000000';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  lines.forEach((l, i) => {
    g.fillText(l, w / 2, (LABEL_PX / 2 + i * LINE_PX) * r + r / 2);
  });
  const m = new SpriteMaterial({
    map: new CanvasTexture(c),
    depthTest: false,
    depthWrite: false,
    transparent: true,
  });
  const s = new Sprite(m);
  s.renderOrder = 10;
  s.userData.aspect = w / h;
  s.userData.pixels = px;
  return s;
}
