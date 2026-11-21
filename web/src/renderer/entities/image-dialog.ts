// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Apply image dialog (decision 0038): a picture from the computer for
// a sliding puzzle's piece, laid over the piece's bounding box, upright,
// and drawn only on its filled cells. Crop cuts the picture to the box's
// proportions, centred; Stretch squashes the whole picture into it. The
// preview draws the piece's cells with the picture as it will look. A
// picture larger than MAX_SIDE pixels, or of a type the file cannot hold,
// is shrunk or turned into a PNG or JPEG first, here, so the file stays
// small. The engine lays the picture on the cells (shape.setImage); from
// then on it turns and moves with them.

import type { ImageFit, ShapeImage } from '../../engine/api.generated.ts';
import { closeDialog, openDialog } from '../shell/dialog.ts';
import { esc } from '../shell/dom.ts';
import { decodeImage } from '../shell/piece-images.ts';
import type { PieceImage } from '../shell/piece-images.ts';
import { cellBox, sourceRect } from '../shell/picture-math.ts';

/** The longest side a picture is kept at, in pixels. */
export const MAX_SIDE = 2048;
/** The preview's size, in CSS pixels. */
const PREVIEW_W = 300;
const PREVIEW_H = 200;

/** What the dialog sends: a new picture's file or the one the piece has, the fit and the part shown. */
export interface ImageApply {
  data?: string;
  image?: number;
  fit: ImageFit;
  src: [number, number, number, number];
}

export interface ImageDialogOptions {
  /** The piece's name, for the title. */
  label: string;
  /** The piece's filled cells, [x, y, z]; only x and y count. */
  cells: readonly (readonly number[])[];
  /** The colour of the piece's cells where the picture does not reach. */
  colour: string;
  /** The piece's picture now, decoded (null: none, or not loaded). */
  current: { image: ShapeImage; picture: PieceImage } | null;
  /** Apply: resolves true when the engine took it (the dialog then closes). */
  apply(a: ImageApply): Promise<boolean>;
  /** Remove the piece's picture. */
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
  const bitmap = await createImageBitmap(file);
  const fits = bitmap.width <= MAX_SIDE && bitmap.height <= MAX_SIDE;
  if (fits && (file.type === 'image/png' || file.type === 'image/jpeg')) {
    const data = toBase64(new Uint8Array(await file.arrayBuffer()));
    return { type: file.type, data, picture: await decodeImage('', file.type, data) };
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
  const type = file.type === 'image/jpeg' ? 'image/jpeg' : 'image/png';
  const blob = await canvas.convertToBlob(
    type === 'image/jpeg' ? { type, quality: 0.9 } : { type },
  );
  const data = toBase64(new Uint8Array(await blob.arrayBuffer()));
  return { type, data, picture: await decodeImage('', type, data) };
}

/** Draws the piece's cells with the picture on them, as the grid will. */
export function drawPreview(
  canvas: HTMLCanvasElement,
  cells: readonly (readonly number[])[],
  colour: string,
  picture: PieceImage | null,
  src: readonly number[],
): void {
  const box = cellBox(cells);
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.round(PREVIEW_W * ratio);
  canvas.height = Math.round(PREVIEW_H * ratio);
  const g = canvas.getContext('2d');
  if (g === null || box === null) {
    return;
  }
  g.setTransform(ratio, 0, 0, ratio, 0, 0);
  g.clearRect(0, 0, PREVIEW_W, PREVIEW_H);
  const [bx, by, bw, bh] = box;
  const cs = Math.floor(Math.min((PREVIEW_W - 8) / bw, (PREVIEW_H - 8) / bh));
  const ox = (PREVIEW_W - bw * cs) / 2;
  const oy = (PREVIEW_H - bh * cs) / 2;
  // the y of the grid points up
  const at = (x: number, y: number): [number, number] => [
    ox + (x - bx) * cs,
    oy + (by + bh - 1 - y) * cs,
  ];
  const filled = new Set(cells.map((c) => `${String(c[0])},${String(c[1])}`));
  g.save();
  g.beginPath();
  for (const k of filled) {
    const [x = 0, y = 0] = k.split(',').map(Number);
    const [px, py] = at(x, y);
    g.rect(px, py, cs, cs);
  }
  g.clip();
  g.fillStyle = colour;
  g.fillRect(ox, oy, bw * cs, bh * cs);
  if (picture !== null) {
    const [sx = 0, sy = 0, sw = 1, sh = 1] = src;
    g.imageSmoothingQuality = 'high';
    g.drawImage(picture.bitmap, sx, sy, sw, sh, ox, oy, bw * cs, bh * cs);
  }
  g.restore();
  // the cells' edges, as the grid draws them
  g.strokeStyle = getComputedStyle(canvas).getPropertyValue('--line2') || '#888';
  g.lineWidth = 1;
  for (let y = by; y < by + bh; y++) {
    for (let x = bx; x < bx + bw; x++) {
      const [px, py] = at(x, y);
      if (filled.has(`${String(x)},${String(y)}`)) {
        g.strokeRect(px + 0.5, py + 0.5, cs - 1, cs - 1);
      }
    }
  }
}

/** Opens the dialog for a piece. */
export function openImageDialog(o: ImageDialogOptions): void {
  const box = cellBox(o.cells);
  if (box === null) {
    return;
  }
  const [, , bw, bh] = box;
  // the picture shown: the piece's own until another is chosen
  let chosen: { data: string; picture: PieceImage } | null = null;
  let fit: ImageFit = o.current?.image.fit ?? 'crop';
  const picture = (): PieceImage | null => chosen?.picture ?? o.current?.picture ?? null;
  const src = (): [number, number, number, number] => {
    const p = picture();
    return p === null ? [0, 0, 1, 1] : sourceRect(fit, p.width, p.height, bw, bh);
  };
  const id = 'imageDlg';
  void openDialog({
    id,
    title: `Apply image to ${o.label}`,
    body: `<div class="form imgdlg">
<div class="imgdlg-pick"><button class="btn" data-f="choose" title="Choose a PNG, JPEG, WebP or GIF picture on this computer">Choose image…</button><span data-f="name">${o.current === null ? 'No image chosen' : 'The piece’s image'}</span><input type="file" data-f="file" accept="image/png,image/jpeg,image/webp,image/gif" hidden></div>
<fieldset><legend>Fit</legend>
<label title="Cut the picture to the piece’s proportions, keeping its middle: nothing is squashed"><input type="radio" name="imgFit" value="crop" ${fit === 'crop' ? 'checked' : ''}> Crop</label>
<label title="Squash the whole picture to the piece’s rectangle"><input type="radio" name="imgFit" value="stretch" ${fit === 'stretch' ? 'checked' : ''}> Stretch</label>
</fieldset>
<canvas class="imgdlg-preview" data-f="preview" style="width:${String(PREVIEW_W)}px;height:${String(PREVIEW_H)}px" aria-label="Preview"></canvas>
<p class="imgdlg-note">The image covers the piece’s ${esc(`${String(bw)} × ${String(bh)}`)} rectangle and shows only on its cells. It turns and moves with the piece.</p>
<div class="scad-err" data-f="err"></div></div>`,
    buttons: [
      ...(o.current === null
        ? []
        : [
            {
              key: 'remove',
              label: 'Remove image',
              kind: 'danger' as const,
              tip: 'Take the image off this piece',
            },
          ]),
      { key: 'cancel', label: 'Cancel' },
      {
        key: 'apply',
        label: 'Apply',
        kind: 'pri',
        default: true,
        tip: 'Put the image on the piece',
      },
    ],
    onAnswer: (k, el) => {
      if (k === 'remove') {
        o.remove();
        return true;
      }
      if (k !== 'apply') {
        return true;
      }
      const err = el.querySelector<HTMLElement>('[data-f="err"]');
      const p = picture();
      if (p === null || (chosen === null && o.current === null)) {
        if (err !== null) {
          err.textContent = 'Choose an image first.';
        }
        return false;
      }
      const a: ImageApply =
        chosen !== null
          ? { data: chosen.data, fit, src: src() }
          : { image: o.current?.image.image ?? 0, fit, src: src() };
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
  const redraw = (): void => {
    if (canvas !== null) {
      drawPreview(canvas, o.cells, o.colour, picture(), src());
    }
  };
  const file = q<HTMLInputElement>('file');
  q('choose')?.addEventListener('click', () => file?.click());
  file?.addEventListener('change', () => {
    const f = file.files?.[0];
    const err = q('err');
    if (f === undefined) {
      return;
    }
    preparePicture(f).then(
      (p) => {
        chosen = { data: p.data, picture: p.picture };
        const name = q('name');
        if (name !== null) {
          name.textContent = `${f.name} (${String(p.picture.width)} × ${String(p.picture.height)})`;
        }
        if (err !== null) {
          err.textContent = '';
        }
        redraw();
      },
      () => {
        if (err !== null) {
          err.textContent = `${f.name} could not be read as a picture.`;
        }
      },
    );
  });
  for (const r of el.querySelectorAll<HTMLInputElement>('input[name="imgFit"]')) {
    r.addEventListener('change', () => {
      fit = r.value === 'stretch' ? 'stretch' : 'crop';
      redraw();
    });
  }
  redraw();
}
