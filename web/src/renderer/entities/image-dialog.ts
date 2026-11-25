// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Apply image dialog (decisions 0038 and 0047): pictures from the
// computer for a sliding puzzle's piece or board, laid over its bounding
// box, upright, and drawn only on its filled cells. Crop cuts the picture
// to the box's proportions, centered; Stretch squashes the whole picture
// into it; Manual shows the whole picture over the piece with dots at its
// corners, sides and middle to size and move it by hand: larger or smaller
// than the box, and anywhere that leaves some of it over the box.
//
// Each picture chosen is a tile beside the button, with an X that takes
// it off; in Manual several lie one on another, the later tiles on top,
// and dragging a tile reorders them. Choose image… replaces the picture
// shown; in Manual, once there is one, the button is Additional image…,
// which puts a new one on top. Crop and Stretch show one picture, the
// active one (the tile last picked, or the picture last clicked in the
// preview); the others are kept while the dialog is open, so Manual
// brings them back, and Apply sends only what the fit shows.
//
// Snap to grid, under Manual (a setting kept across sessions, off until
// Manual is chosen), pulls the edges a drag moves onto the cells' lines
// when they come within SNAP pixels of one, Alt held letting go; the
// preview then draws the lines faintly over the whole of it.
//
// The Fit box is as narrow as its choices; beside it, Help holds the
// dialog's notes (how Manual's drags work, what the fit shows, where the
// picture goes), leaving the preview more of the height. The dialog is
// never narrower than where Help's text, wrapped, grows taller than the
// Fit box (which would leave room under Snap to grid), with Manual's text
// in it whatever the fit, so Crop, Stretch and Manual have one size: a
// narrower window past that only shrinks the preview's height. The preview
// draws the piece's cells with the pictures as they will look, as large
// as the window leaves room for beside the rest of the dialog
// (previewSize, with the height the dialog measures it takes), sized
// again as the window or the dialog's text changes; in a window too small
// even for the smallest preview the dialog's body scrolls, its buttons in
// view. A picture larger than MAX_SIDE pixels, or of a type the file
// cannot hold, is shrunk or turned into a PNG or JPEG first, here, so the
// file stays small. The engine lays the stack on the cells
// (shape.setImages); from then on it turns and moves with them.

import type { ImageFit, ImageLayer, ShapeImage } from '../../engine/api.generated.ts';
import { closeDialog, openDialog } from '../shell/dialog.ts';
import { esc } from '../shell/dom.ts';
import { prefs, setPrefs } from '../shell/prefs.ts';
import { decodeImage, holdPictures } from '../shell/piece-images.ts';
import type { PieceImage } from '../shell/piece-images.ts';
import {
  cellBox,
  dragRect,
  previewSize,
  rectFromSource,
  sourceFromRect,
  snapDrag,
  sourceRect,
} from '../shell/picture-math.ts';
import type { PictureHandle, PictureRect, PreviewSize } from '../shell/picture-math.ts';

/** The longest side a picture is kept at, in pixels. */
export const MAX_SIDE = 2048;
/** The most pictures a stack holds (the engine's kMaxShapeImages). */
export const MAX_LAYERS = 16;
/** The Manual fit's dots: how near a pointer grabs one, and their radius, in CSS pixels. */
const GRAB = 9;
const DOT = 4.5;
/** The room kept between the dots and the preview's edges, in CSS pixels. */
const EDGE = 7;
/** The share of the preview the box and the active picture take after each drag, which leaves room to make it larger. */
const ROOM = 0.85;
/** In the Manual fit, the smallest side a picture is sized to, and how much of it stays over the box, in CSS pixels. */
const MIN_SIDE = 12;
const OVERLAP = 2;
/** How near a grid line an edge snaps to it with Snap to grid, in CSS pixels. */
const SNAP = 8;
/** The room kept above and below the dialog in the window, in CSS pixels. */
const MARGIN = 32;
/** The room the dialog's card takes beside its body in the window's width: its margins and padding, in CSS pixels. */
const SIDES = 84;

/** What the dialog sends: the pictures, the bottom one first, each a new file or one the shape has. */
export interface ImageApply {
  layers: ImageLayer[];
}

/** A tray's outer rectangle, for its border's picture. */
export interface PictureFrame {
  box: [number, number, number, number];
  round: boolean;
}

export interface ImageDialogOptions {
  /** The piece's name, for the title. */
  label: string;
  /** A board's picture (on its floor, under the pieces) rather than a piece's. */
  board?: boolean;
  /**
   * The picture of a board's tray border (decision 0046) rather than a
   * shape's own: it lies over `box` (the tray's outer rectangle, in the
   * board's cells: [x, y, width, height]), whose corners are round or
   * square, and shows on all of it but `cells`, the floor.
   */
  frame?: PictureFrame;
  /** The piece's filled cells, [x, y, z]; only x and y count. */
  cells: readonly (readonly number[])[];
  /** The color of the piece's cells where no picture reaches. */
  color: string;
  /** The piece's pictures now, the bottom one first, decoded. */
  current: readonly { image: ShapeImage; picture: PieceImage }[];
  /**
   * Whether the document changed since the dialog opened (it then says
   * so): Apply and Remove close the dialog without doing anything, since
   * the new document's shape of the same id is another (cleanup item 6.3).
   */
  stale?: () => boolean;
  /** Apply: resolves true when the engine took it (the dialog then closes). */
  apply(a: ImageApply): Promise<boolean>;
  /** Remove the piece's pictures. */
  remove(): void;
}

/** A file as base64. */
function toBase64(bytes: Uint8Array): string {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(s);
}

/**
 * A picture file ready for the puzzle: a PNG or JPEG of at most MAX_SIDE
 * pixels a side, kept as it is when it already is one, else drawn smaller
 * and saved again (a JPEG as a JPEG, anything else as a PNG, which keeps
 * transparency).
 */
export async function preparePicture(
  file: Blob,
): Promise<{ type: string; data: string; picture: PieceImage }> {
  // the file is decoded once (cleanup item 6.12)
  const bitmap = await createImageBitmap(file);
  const fits = bitmap.width <= MAX_SIDE && bitmap.height <= MAX_SIDE;
  if (fits && (file.type === 'image/png' || file.type === 'image/jpeg')) {
    const data = toBase64(new Uint8Array(await file.arrayBuffer()));
    const picture: PieceImage = {
      hash: '',
      width: bitmap.width,
      height: bitmap.height,
      dataUrl: `data:${file.type};base64,${data}`,
      bitmap,
    };
    return { type: file.type, data, picture };
  }
  const k = Math.min(1, MAX_SIDE / Math.max(bitmap.width, bitmap.height));
  const w = Math.max(1, Math.round(bitmap.width * k));
  const h = Math.max(1, Math.round(bitmap.height * k));
  const canvas = new OffscreenCanvas(w, h);
  const g = canvas.getContext('2d');
  if (g === null) {
    throw new Error('The picture could not be drawn');
  }
  g.imageSmoothingQuality = 'high';
  g.drawImage(bitmap, 0, 0, w, h);
  // drawn smaller: the preview shows the bytes saved, decoded from them
  bitmap.close();
  const type = file.type === 'image/jpeg' ? 'image/jpeg' : 'image/png';
  const blob = await canvas.convertToBlob(
    type === 'image/jpeg' ? { type, quality: 0.9 } : { type },
  );
  const data = toBase64(new Uint8Array(await blob.arrayBuffer()));
  return { type, data, picture: await decodeImage('', type, data) };
}

/** The Manual fit's view in the preview: CSS pixels per cell, the box's top left corner and the preview's size. */
interface ManualView {
  k: number;
  ox: number;
  oy: number;
  size: PreviewSize;
}

/**
 * The Manual fit's view with the bw x bh box and the picture's rectangle
 * `r` together in the middle, ROOM of the preview: a picture far larger
 * than the box shows whole, and one far smaller leaves the box in view.
 */
function manualView(r: PictureRect, bw: number, bh: number, size: PreviewSize): ManualView {
  const x1 = Math.min(0, r[0]);
  const y1 = Math.min(0, r[1]);
  const x2 = Math.max(bw, r[0] + r[2]);
  const y2 = Math.max(bh, r[1] + r[3]);
  const { w, h } = size;
  const k = Math.min((ROOM * w) / (x2 - x1), (ROOM * h) / (y2 - y1));
  return { k, ox: w / 2 - ((x1 + x2) / 2) * k, oy: h / 2 - ((y1 + y2) / 2) * k, size };
}

/** The preview less EDGE all round, in the box's cells: where a drag may take the dots. */
function manualBounds(v: ManualView): [number, number, number, number] {
  return [
    (EDGE - v.ox) / v.k,
    (EDGE - v.oy) / v.k,
    (v.size.w - 2 * EDGE) / v.k,
    (v.size.h - 2 * EDGE) / v.k,
  ];
}

/** The dots of the picture's rectangle in the preview: corners and sides, then the middle. */
function handlePoints(r: PictureRect, v: ManualView): [PictureHandle, number, number][] {
  const x1 = v.ox + r[0] * v.k;
  const y1 = v.oy + r[1] * v.k;
  const x2 = x1 + r[2] * v.k;
  const y2 = y1 + r[3] * v.k;
  const mx = (x1 + x2) / 2;
  const my = (y1 + y2) / 2;
  return [
    ['nw', x1, y1],
    ['n', mx, y1],
    ['ne', x2, y1],
    ['e', x2, my],
    ['se', x2, y2],
    ['s', mx, y2],
    ['sw', x1, y2],
    ['w', x1, my],
    ['move', mx, my],
  ];
}

/** Whether (x, y) in the preview lies on the picture's rectangle. */
function onRect(r: PictureRect, v: ManualView, x: number, y: number): boolean {
  const px = (x - v.ox) / v.k;
  const py = (y - v.oy) / v.k;
  return px >= r[0] && px <= r[0] + r[2] && py >= r[1] && py <= r[1] + r[3];
}

/** The dot at (x, y) in the preview; 'move' anywhere else on the picture; null off it. */
function handleAt(r: PictureRect, v: ManualView, x: number, y: number): PictureHandle | null {
  for (const [h, hx, hy] of handlePoints(r, v)) {
    if (Math.hypot(x - hx, y - hy) <= GRAB) {
      return h;
    }
  }
  return onRect(r, v, x, y) ? 'move' : null;
}

/** The pointer over each dot. */
const CURSORS: Record<PictureHandle, string> = {
  move: 'move',
  n: 'ns-resize',
  s: 'ns-resize',
  e: 'ew-resize',
  w: 'ew-resize',
  ne: 'nesw-resize',
  sw: 'nesw-resize',
  nw: 'nwse-resize',
  se: 'nwse-resize',
};

/** A picture as the preview draws it: with Crop or Stretch its part shown over the box, in Manual the whole of it at `rect`. */
export interface PreviewLayer {
  picture: PieceImage;
  src: readonly number[];
  rect: PictureRect;
}

/** What the preview draws. */
export interface PreviewInput {
  cells: readonly (readonly number[])[];
  color: string;
  /** The pictures, the bottom one first. */
  layers: readonly PreviewLayer[];
  /** The Manual fit: its view, the picture with the dots (an index of `layers`) and whether the grid's lines show (Snap to grid); null for Crop and Stretch. */
  manual: { view: ManualView; active: number; grid?: boolean } | null;
  /** A tray's border: the pictures show on the frame's rectangle less the cells, the floor. */
  frame: PictureFrame | null;
  size: PreviewSize;
}

/**
 * Draws the piece's cells with the pictures on them, as the grid will:
 * the cells in the piece's color, then each picture over it, bottom
 * first. In the Manual fit each picture lies whole at its rectangle, the
 * active one also faint off the cells, with its edge and its dots.
 */
export function drawPreview(canvas: HTMLCanvasElement, input: PreviewInput): void {
  const { cells, color, layers, manual, frame, size } = input;
  const box = frame?.box ?? cellBox(cells);
  const ratio = window.devicePixelRatio || 1;
  const { w: pw, h: ph } = size;
  // a size set again reallocates the canvas: only when it changed (each move of a Manual drag draws)
  const cw = Math.round(pw * ratio);
  const ch = Math.round(ph * ratio);
  if (canvas.width !== cw || canvas.height !== ch) {
    canvas.width = cw;
    canvas.height = ch;
  }
  const g = canvas.getContext('2d');
  if (g === null || box === null) {
    return;
  }
  g.setTransform(ratio, 0, 0, ratio, 0, 0);
  g.clearRect(0, 0, pw, ph);
  const [bx, by, bw, bh] = box;
  const cs = manual === null ? Math.floor(Math.min((pw - 8) / bw, (ph - 8) / bh)) : manual.view.k;
  const ox = manual === null ? (pw - bw * cs) / 2 : manual.view.ox;
  const oy = manual === null ? (ph - bh * cs) / 2 : manual.view.oy;
  // where a whole picture lies in the Manual fit, in CSS pixels
  const whole = (r: PictureRect): [number, number, number, number] => [
    ox + r[0] * cs,
    oy + r[1] * cs,
    r[2] * cs,
    r[3] * cs,
  ];
  const active = manual === null ? undefined : layers[manual.active];
  if (active !== undefined) {
    // the active picture's parts off the cells, faint
    g.save();
    g.globalAlpha = 0.35;
    g.imageSmoothingQuality = 'high';
    g.drawImage(active.picture.bitmap, ...whole(active.rect));
    g.restore();
  }
  // the y of the grid points up
  const at = (x: number, y: number): [number, number] => [
    ox + (x - bx) * cs,
    oy + (by + bh - 1 - y) * cs,
  ];
  const filled = new Set(cells.map((c) => `${String(c[0])},${String(c[1])}`));
  g.save();
  g.beginPath();
  if (frame !== null) {
    // the tray's whole rectangle, its corners round by the rim's width
    g.roundRect(ox, oy, bw * cs, bh * cs, frame.round ? -bx * cs : 0);
  }
  for (const k of filled) {
    const [x = 0, y = 0] = k.split(',').map(Number);
    const [px, py] = at(x, y);
    g.rect(px, py, cs, cs);
  }
  // a border shows on the tray less its floor
  g.clip(frame === null ? 'nonzero' : 'evenodd');
  g.fillStyle = color;
  g.fillRect(ox, oy, bw * cs, bh * cs);
  g.imageSmoothingQuality = 'high';
  for (const l of layers) {
    if (manual !== null) {
      g.drawImage(l.picture.bitmap, ...whole(l.rect));
    } else {
      const [sx = 0, sy = 0, sw = 1, sh = 1] = l.src;
      g.drawImage(l.picture.bitmap, sx, sy, sw, sh, ox, oy, bw * cs, bh * cs);
    }
  }
  g.restore();
  // the cells' edges, as the grid draws them
  g.strokeStyle = getComputedStyle(canvas).getPropertyValue('--line2') || '#888';
  g.lineWidth = 1;
  for (const k of filled) {
    const [x = 0, y = 0] = k.split(',').map(Number);
    const [px, py] = at(x, y);
    g.strokeRect(px + 0.5, py + 0.5, cs - 1, cs - 1);
  }
  if (manual?.grid === true && cs >= 4) {
    // the lines a drag snaps to, over the whole preview, faint
    g.save();
    g.globalAlpha = 0.45;
    g.setLineDash([3, 3]);
    g.beginPath();
    for (let i = Math.ceil(-ox / cs); ox + i * cs <= pw; i++) {
      const x = Math.round(ox + i * cs) + 0.5;
      g.moveTo(x, 0);
      g.lineTo(x, ph);
    }
    for (let i = Math.ceil(-oy / cs); oy + i * cs <= ph; i++) {
      const y = Math.round(oy + i * cs) + 0.5;
      g.moveTo(0, y);
      g.lineTo(pw, y);
    }
    g.stroke();
    g.restore();
  }
  if (manual !== null && active !== undefined) {
    // the active picture's edge and its dots
    const style = getComputedStyle(canvas);
    const accent = style.getPropertyValue('--accent') || '#2f6df6';
    const panel = style.getPropertyValue('--panel') || '#fff';
    g.strokeStyle = accent;
    g.lineWidth = 1.5;
    g.strokeRect(...whole(active.rect));
    for (const [h, hx, hy] of handlePoints(active.rect, manual.view)) {
      g.beginPath();
      g.arc(hx, hy, h === 'move' ? DOT + 1.5 : DOT, 0, 2 * Math.PI);
      g.fillStyle = h === 'move' ? accent : panel;
      g.strokeStyle = h === 'move' ? panel : accent;
      g.fill();
      g.stroke();
    }
  }
}

/** A picture in the dialog: its tile's name, its file or number, and where it lies in Manual. */
interface Layer {
  /** The tile's id, for its element. */
  key: number;
  name: string;
  picture: PieceImage;
  /** A new picture's file (base64)… */
  data?: string;
  /** …or one the shape has. */
  image?: number;
  /** Where the whole picture lies in Manual, in the box's cells. */
  rect: PictureRect;
  /** Whether `rect` was placed in Manual (else Manual starts from what Crop or Stretch showed). */
  placed: boolean;
}

/** Opens the dialog for a piece or a board. */
export function openImageDialog(o: ImageDialogOptions): void {
  const frame = o.frame ?? null;
  const box = frame?.box ?? cellBox(o.cells);
  if (box === null) {
    return;
  }
  const n = frame !== null ? 'tray border' : o.board === true ? 'board' : 'piece';
  const [, , bw, bh] = box;
  let size = previewSize({ w: window.innerWidth, h: window.innerHeight }, bw / bh);
  let keys = 0;
  // the pictures, the bottom one first; the active one is the one Crop and
  // Stretch show and the one with the dots in Manual
  const layers: Layer[] = o.current.map((c, i) => ({
    key: keys++,
    name: o.current.length === 1 ? `The ${n}’s image` : `Image ${String(i + 1)}`,
    picture: c.picture,
    image: c.image.image,
    rect: rectFromSource(c.image.src, c.picture.width, c.picture.height, bw, bh),
    placed: c.image.fit === 'manual',
  }));
  let active = layers.length - 1;
  let fit: ImageFit = o.current.at(-1)?.image.fit ?? 'crop';
  let view: ManualView | null = null;
  const current = (): Layer | undefined => layers[active];
  // the pictures the fit shows: all in Manual, else the active one
  const shown = (): Layer[] => {
    const a = current();
    return fit === 'manual' ? layers : a === undefined ? [] : [a];
  };
  const srcOf = (l: Layer): [number, number, number, number] => {
    const { width: w, height: h } = l.picture;
    return fit === 'manual' ? sourceFromRect(l.rect, w, h, bw, bh) : sourceRect(fit, w, h, bw, bh);
  };
  // the Manual fit's view, fitted to the box and the active picture
  const fitView = (): void => {
    const a = current();
    view = a === undefined ? null : manualView(a.rect, bw, bh, size);
  };
  // the rectangle snapped to whole pixels of its picture
  const snap = (l: Layer): void => {
    const { width: w, height: h } = l.picture;
    l.rect = rectFromSource(sourceFromRect(l.rect, w, h, bw, bh), w, h, bw, bh);
  };
  if (fit === 'manual') {
    fitView();
  }
  const id = 'imageDlg';
  // the pictures chosen here (never the shape's own, which the picture cache serves): closed with the dialog
  const own: ImageBitmap[] = [];
  // the shape's own pictures stay in the cache while the dialog shows them
  const release = holdPictures(o.current.map((c) => c.picture));
  let closed = false;
  const shownDialog = openDialog({
    id,
    title: `Apply image to ${o.label}${frame === null ? '' : '’s tray border'}`,
    body: `<div class="form imgdlg" style="--imgw:${String(size.w)}px">
<div class="imgdlg-pick"><button class="btn" data-f="choose"></button><div class="imgdlg-tiles" data-f="tiles" role="list" aria-label="Images, the bottom one first"></div><input type="file" data-f="file" accept="image/png,image/jpeg,image/webp,image/gif" hidden></div>
<div class="imgdlg-row" data-f="row"><fieldset class="imgdlg-fit" data-f="fitBox"><legend>Fit</legend>
<label title="Cut the picture to the ${n}’s proportions, keeping its middle: nothing is squashed"><input type="radio" name="imgFit" value="crop" ${fit === 'crop' ? 'checked' : ''}> Crop</label>
<label title="Squash the whole picture to the ${n}’s rectangle"><input type="radio" name="imgFit" value="stretch" ${fit === 'stretch' ? 'checked' : ''}> Stretch</label>
<label title="Size and place each picture yourself: drag the dots at its corners and sides in the preview to resize it, and the one in its middle to move it; several pictures can lie one on another"><input type="radio" name="imgFit" value="manual" ${fit === 'manual' ? 'checked' : ''}> Manual</label>
<label class="imgdlg-snap" title="With Manual: while you drag, a picture's edges snap to the grid's lines when they come close to one (hold Alt to place it freely)"><input type="checkbox" data-f="snap" ${prefs.imageSnap ? 'checked' : ''}${fit === 'manual' ? '' : ' disabled'}> Snap to grid</label>
</fieldset>
<fieldset class="imgdlg-help" data-f="help"><legend>Help</legend>
<p class="imgdlg-note">${frame !== null ? 'The image covers the whole tray, its rim included, and shows on the border: the rim and the walls, not the floor. It turns with the board.' : `The image covers the ${n}’s ${esc(`${String(bw)} × ${String(bh)}`)} rectangle and shows only on its cells${o.board === true ? ', under the pieces. It turns with the board.' : '. It turns and moves with the piece.'}`}</p>
<p class="imgdlg-hint" data-f="manualNote"${fit === 'manual' ? '' : ' hidden'}>Drag a corner to resize the picture (Shift: free proportions), a side to stretch it, and the middle to move it; the arrow keys move it too. With Snap to grid its edges snap to the dashed lines (Alt: no snapping). Picture order can be changed.</p>
<p class="imgdlg-hint" data-f="singleNote" hidden></p>
</fieldset></div>
<canvas class="imgdlg-preview" data-f="preview" style="width:${String(size.w)}px;height:${String(size.h)}px" aria-label="Preview"></canvas>
<div class="scad-err" data-f="err"></div></div>`,
    buttons: [
      ...(o.current.length === 0
        ? []
        : [
            {
              key: 'remove',
              label: o.current.length === 1 ? 'Remove image' : 'Remove images',
              kind: 'danger' as const,
              tip: `Take the ${o.current.length === 1 ? 'image' : 'images'} off this ${n}`,
            },
          ]),
      { key: 'cancel', label: 'Cancel' },
      {
        key: 'apply',
        label: 'Apply',
        kind: 'pri',
        default: true,
        tip: `Put the image on the ${n}`,
      },
    ],
    onAnswer: (k, el) => {
      if ((k === 'remove' || k === 'apply') && o.stale?.() === true) {
        return true;
      }
      if (k === 'remove') {
        o.remove();
        return true;
      }
      if (k !== 'apply') {
        return true;
      }
      const err = el.querySelector<HTMLElement>('[data-f="err"]');
      const sent = shown();
      if (sent.length === 0) {
        if (o.current.length > 0) {
          // every tile taken off: the shape's pictures too
          o.remove();
          return true;
        }
        if (err !== null) {
          err.textContent = 'Choose an image first.';
        }
        return false;
      }
      const a: ImageApply = {
        layers: sent.map((l) => ({
          ...(l.data !== undefined ? { data: l.data } : { image: l.image ?? 0 }),
          fit,
          src: srcOf(l),
        })),
      };
      // the dialog stays open until the engine has taken it
      void o.apply(a).then((ok) => {
        if (ok) {
          closeDialog(id);
        } else if (err !== null) {
          err.textContent = 'The image could not be applied.';
        }
      });
      return false;
    },
  });
  const el = document.getElementById(id);
  if (el === null) {
    return;
  }
  const q = <T extends HTMLElement>(f: string): T | null => el.querySelector<T>(`[data-f="${f}"]`);
  const canvas = q<HTMLCanvasElement>('preview');
  const manualNote = q('manualNote');
  const snapBox = q<HTMLInputElement>('snap');
  const singleNote = q('singleNote');
  const choose = q<HTMLButtonElement>('choose');
  const tiles = q('tiles');
  // Additional image… only in Manual, once there is a picture
  const adding = (): boolean => fit === 'manual' && layers.length > 0;
  const drawTiles = (): void => {
    if (choose !== null) {
      const more = adding();
      choose.textContent = more ? 'Additional image…' : 'Choose image…';
      choose.title = more
        ? 'Choose another picture on this computer, laid on top of the others'
        : current() === undefined
          ? 'Choose a PNG, JPEG, WebP or GIF picture on this computer'
          : 'Choose a picture on this computer in place of this one';
      choose.disabled = more && layers.length >= MAX_LAYERS;
    }
    if (tiles !== null) {
      const list = shown();
      const a = current();
      tiles.innerHTML =
        list.length === 0
          ? '<span class="imgdlg-none">No image chosen</span>'
          : list
              .map(
                (l) =>
                  `<span class="imgtile${l === a ? ' on' : ''}" role="listitem" data-key="${String(l.key)}" draggable="${String(fit === 'manual' && list.length > 1)}" title="${esc(`${l.name} (${String(l.picture.width)} × ${String(l.picture.height)})`)}${fit === 'manual' && list.length > 1 ? ': drag to reorder; the last lies on top' : ''}"><span class="nm">${esc(`${l.name} (${String(l.picture.width)} × ${String(l.picture.height)})`)}</span><button class="x" data-rm aria-label="${esc(`Remove ${l.name}`)}" title="Take this image off">×</button></span>`,
              )
              .join('');
    }
    if (singleNote !== null) {
      const hidden = fit !== 'manual' && layers.length > 1;
      singleNote.hidden = !hidden;
      singleNote.textContent = hidden
        ? `${fit === 'crop' ? 'Crop' : 'Stretch'} shows one image, the one listed; Manual keeps all ${String(layers.length)}.`
        : '';
    }
  };
  // the Manual fit's drags: the dot grabbed, where, and the rectangle then
  let drag: { handle: PictureHandle; x: number; y: number; start: PictureRect } | null = null;
  const form = el.querySelector<HTMLElement>('.imgdlg');
  const card = el.querySelector<HTMLElement>('.cfm');
  const body = el.querySelector<HTMLElement>('.cb');
  // the preview sized to what the window leaves beside the rest of the
  // dialog as it is now laid out (its whole height, scrolled or not); a
  // second pass when the new width wraps the text again
  const row = q('row');
  const fitBox = q('fitBox');
  const help = q('help');
  // the narrowest the Fit and Help row may be: where Help's text, wrapped,
  // is no taller than the Fit box beside it (narrower, it would leave room
  // under Snap to grid); at most what the window holds. Measured with
  // Manual's text whatever the fit, so every fit has the same size
  const narrowest = (): number => {
    if (row === null || fitBox === null || help === null) {
      return 0;
    }
    const hid = [manualNote?.hidden, singleNote?.hidden];
    if (manualNote !== null) {
      manualNote.hidden = false;
    }
    if (singleNote !== null) {
      singleNote.hidden = true;
    }
    row.style.alignItems = 'flex-start';
    const tooTall = (w: number): boolean => {
      row.style.width = `${String(w)}px`;
      return help.offsetHeight > fitBox.offsetHeight;
    };
    // short enough at the narrowest; too tall even as wide as the window
    // allows; else the edge between, to the pixel
    let lo = fitBox.offsetWidth + 110;
    let hi = Math.max(lo, window.innerWidth - SIDES);
    if (!tooTall(lo)) {
      hi = lo;
    } else if (!tooTall(hi)) {
      while (hi - lo > 1) {
        const mid = Math.round((lo + hi) / 2);
        if (tooTall(mid)) {
          lo = mid;
        } else {
          hi = mid;
        }
      }
    }
    row.style.width = '';
    row.style.alignItems = '';
    if (manualNote !== null) {
      manualNote.hidden = hid[0] ?? false;
    }
    if (singleNote !== null) {
      singleNote.hidden = hid[1] ?? true;
    }
    return hi;
  };
  // the preview as large as the window's height leaves room for, and no
  // narrower than the row above it needs: past that a narrower window
  // only makes it shorter, the picture in it smaller
  const layout = (): void => {
    if (canvas === null || card === null || body === null || form === null) {
      return;
    }
    const least = narrowest();
    for (let pass = 0; pass < 2; pass++) {
      const whole = card.offsetHeight + body.scrollHeight - body.clientHeight;
      const chrome = whole - size.h + MARGIN;
      const fitted = previewSize({ w: window.innerWidth, h: window.innerHeight }, bw / bh, chrome);
      const next = { w: Math.max(fitted.w, least), h: fitted.h };
      if (next.w === size.w && next.h === size.h) {
        return;
      }
      size = next;
      form.style.setProperty('--imgw', `${String(size.w)}px`);
      canvas.style.width = `${String(size.w)}px`;
      canvas.style.height = `${String(size.h)}px`;
      if (fit === 'manual') {
        fitView();
      }
    }
  };
  const redraw = (): void => {
    drawTiles();
    if (canvas === null) {
      return;
    }
    // not while a drag holds the view
    if (drag === null) {
      layout();
    }
    const a = current();
    const manual =
      fit === 'manual' && view !== null && a !== undefined
        ? { view, active: layers.indexOf(a), grid: snapBox?.checked === true }
        : null;
    canvas.tabIndex = manual === null ? -1 : 0;
    if (manual === null) {
      canvas.style.cursor = '';
    }
    drawPreview(canvas, {
      cells: o.cells,
      color: o.color,
      layers: shown().map((l) => ({ picture: l.picture, src: srcOf(l), rect: l.rect })),
      manual,
      frame,
      size,
    });
  };
  /** The active picture snapped to whole pixels, and the view fitted to it again. */
  const settle = (): void => {
    const a = current();
    if (a !== undefined) {
      snap(a);
    }
    fitView();
    redraw();
  };
  const limits = (v: ManualView): { minSide: number; overlap: number } => ({
    minSide: MIN_SIDE / v.k,
    overlap: OVERLAP / v.k,
  });
  const local = (e: PointerEvent): [number, number] => {
    const b = canvas?.getBoundingClientRect();
    return b === undefined ? [0, 0] : [e.clientX - b.left, e.clientY - b.top];
  };
  canvas?.addEventListener('pointerdown', (e) => {
    if (fit !== 'manual' || view === null || e.button !== 0) {
      return;
    }
    const [x, y] = local(e);
    let a = current();
    let handle = a === undefined ? null : handleAt(a.rect, view, x, y);
    if (handle === null) {
      // another picture under the pointer, the top one first, picked and held by its middle
      const v = view;
      const hit = [...layers].reverse().find((l) => onRect(l.rect, v, x, y));
      if (hit !== undefined && hit !== a) {
        active = layers.indexOf(hit);
        a = hit;
        handle = 'move';
        redraw();
      }
    }
    if (handle !== null && a !== undefined) {
      drag = { handle, x, y, start: a.rect };
      a.placed = true;
      canvas.setPointerCapture(e.pointerId);
      e.preventDefault();
    }
  });
  canvas?.addEventListener('pointermove', (e) => {
    const a = current();
    if (fit !== 'manual' || view === null || a === undefined) {
      return;
    }
    const [x, y] = local(e);
    if (drag === null) {
      const h = handleAt(a.rect, view, x, y);
      const v = view;
      canvas.style.cursor =
        h !== null ? CURSORS[h] : layers.some((l) => onRect(l.rect, v, x, y)) ? 'pointer' : '';
      return;
    }
    let dx = (x - drag.x) / view.k;
    let dy = (y - drag.y) / view.k;
    if (snapBox?.checked === true && !e.altKey) {
      [dx, dy] = snapDrag(drag.start, drag.handle, dx, dy, SNAP / view.k, e.shiftKey);
    }
    a.rect = dragRect(
      drag.start,
      drag.handle,
      dx,
      dy,
      bw,
      bh,
      manualBounds(view),
      e.shiftKey,
      limits(view),
    );
    redraw();
  });
  const drop = (): void => {
    if (drag !== null) {
      drag = null;
      settle();
    }
  };
  canvas?.addEventListener('pointerup', drop);
  canvas?.addEventListener('pointercancel', drop);
  canvas?.addEventListener('keydown', (e) => {
    const step: Record<string, [number, number]> = {
      ArrowLeft: [-1, 0],
      ArrowRight: [1, 0],
      ArrowUp: [0, -1],
      ArrowDown: [0, 1],
    };
    const d = step[e.key];
    const a = current();
    if (fit !== 'manual' || view === null || a === undefined || d === undefined) {
      return;
    }
    // a tenth of a cell, a whole one with Shift
    const by = e.shiftKey ? 1 : 0.1;
    a.placed = true;
    a.rect = dragRect(
      a.rect,
      'move',
      d[0] * by,
      d[1] * by,
      bw,
      bh,
      manualBounds(view),
      false,
      limits(view),
    );
    e.preventDefault();
    e.stopPropagation();
    settle();
  });
  const file = q<HTMLInputElement>('file');
  choose?.addEventListener('click', () => file?.click());
  file?.addEventListener('change', () => {
    const f = file.files?.[0];
    const err = q('err');
    if (f === undefined) {
      return;
    }
    // the same file again is a change too
    file.value = '';
    preparePicture(f).then(
      (p) => {
        if (closed) {
          // the dialog went while the file was read
          p.picture.bitmap.close();
          return;
        }
        own.push(p.picture.bitmap);
        const { width: w, height: h } = p.picture;
        // over the whole box, as Crop lays it
        const rect = rectFromSource(sourceRect('crop', w, h, bw, bh), w, h, bw, bh);
        const layer: Layer = {
          key: keys++,
          name: f.name,
          picture: p.picture,
          data: p.data,
          rect,
          placed: false,
        };
        if (adding()) {
          // on top of the others, half the box's size in its middle so the
          // ones under it still show
          layer.rect = [rect[0] / 2 + bw / 4, rect[1] / 2 + bh / 4, rect[2] / 2, rect[3] / 2];
          layer.placed = true;
          layers.push(layer);
          active = layers.length - 1;
        } else if (current() === undefined) {
          layers.push(layer);
          active = 0;
        } else {
          // in place of the one shown
          layers.splice(active, 1, layer);
        }
        if (err !== null) {
          err.textContent = '';
        }
        settle();
      },
      () => {
        if (err !== null) {
          err.textContent = `${f.name} could not be read as a picture.`;
        }
      },
    );
  });
  // the tiles: a click picks one, its X takes it off, a drag reorders them
  const tileOf = (t: EventTarget | null): Layer | undefined => {
    const key = (t as Element | null)?.closest<HTMLElement>('.imgtile')?.dataset.key;
    return layers.find((l) => String(l.key) === key);
  };
  tiles?.addEventListener('click', (e) => {
    const l = tileOf(e.target);
    if (l === undefined) {
      return;
    }
    if ((e.target as Element).closest('[data-rm]') !== null) {
      layers.splice(layers.indexOf(l), 1);
      active = layers.length - 1;
    } else {
      active = layers.indexOf(l);
    }
    if (fit === 'manual') {
      fitView();
    }
    redraw();
  });
  let dragged: Layer | undefined;
  tiles?.addEventListener('dragstart', (e) => {
    dragged = tileOf(e.target);
    if (dragged === undefined || fit !== 'manual') {
      e.preventDefault();
      return;
    }
    e.dataTransfer?.setData('text/plain', String(dragged.key));
    if (e.dataTransfer !== null) {
      e.dataTransfer.effectAllowed = 'move';
    }
  });
  tiles?.addEventListener('dragover', (e) => {
    if (dragged !== undefined) {
      e.preventDefault();
    }
  });
  tiles?.addEventListener('drop', (e) => {
    const from = dragged;
    dragged = undefined;
    if (from === undefined) {
      return;
    }
    e.preventDefault();
    const onto = tileOf(e.target);
    const was = current();
    layers.splice(layers.indexOf(from), 1);
    if (onto === undefined || onto === from) {
      // past the tiles: on top
      layers.push(from);
    } else {
      // before the tile dropped on, or after it when dropped on its right half
      const r = (e.target as Element).closest('.imgtile')?.getBoundingClientRect();
      const after = r !== undefined && e.clientX > r.left + r.width / 2;
      layers.splice(layers.indexOf(onto) + (after ? 1 : 0), 0, from);
    }
    active = was === undefined ? layers.length - 1 : layers.indexOf(was);
    redraw();
  });
  tiles?.addEventListener('dragend', () => {
    dragged = undefined;
  });
  snapBox?.addEventListener('change', () => {
    setPrefs({ imageSnap: snapBox.checked });
    redraw();
  });
  for (const r of el.querySelectorAll<HTMLInputElement>('input[name="imgFit"]')) {
    r.addEventListener('change', () => {
      const a = current();
      const was = a === undefined ? null : srcOf(a);
      fit = r.value === 'stretch' ? 'stretch' : r.value === 'manual' ? 'manual' : 'crop';
      if (fit === 'manual' && a !== undefined && was !== null) {
        // Manual starts from what the fit before it showed, unless the
        // picture was placed in Manual already
        if (!a.placed) {
          a.rect = rectFromSource(was, a.picture.width, a.picture.height, bw, bh);
        }
        fitView();
      }
      if (manualNote !== null) {
        manualNote.hidden = fit !== 'manual';
      }
      if (snapBox !== null) {
        snapBox.disabled = fit !== 'manual';
      }
      redraw();
    });
  }
  // sized again with the window, until the dialog is gone
  const resized = (): void => {
    if (drag === null) {
      redraw();
    }
  };
  window.addEventListener('resize', resized);
  // however it closes (Apply, Cancel, Esc, another dialog of its id), the
  // listener goes and the pictures chosen here are let go
  void shownDialog.finally(() => {
    closed = true;
    window.removeEventListener('resize', resized);
    for (const b of own) {
      b.close();
    }
    release();
  });
  redraw();
}
