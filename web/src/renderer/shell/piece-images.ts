// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures on a sliding puzzle's pieces (decision 0038), decoded once
// and kept by their hash: the snapshot names each shape's picture by its
// number and hash, and its file comes from shape.imageData the first time
// a hash is seen. The 2D grid draws the data URL (the page's CSP allows
// `data:` images, not `blob:` ones); the 3D views and the dialog's preview
// draw the bitmap. A picture still loading is drawn as nothing, and the
// listeners hear when it is ready. A shape's stack of pictures (decision
// 0047), or one picture smaller than its box, is drawn once into a single
// picture over the box (stackPicture), so each view draws one picture as
// before; transparent where no picture reaches, the shape's color then.
// The cache keeps what the open document names (picture-keep.ts,
// keepPicturesOf): at each new snapshot the rest is let go, its textures
// first, then its bitmap closed, except a picture an open dialog holds.

import type { ShapeImage, TrayBorder } from '../../engine/api.generated.ts';
import type { EngineLink, Store } from '../state/store.ts';
import { inside, namedPictures, PictureKeep, stackKey } from './picture-keep.ts';
import type { PiecePicture } from './picture-math.ts';
import { borderImage, borderRim } from './tray-border.ts';

/** A decoded picture. */
export interface PieceImage {
  hash: string;
  width: number;
  height: number;
  /** For an SVG `<image>`. */
  dataUrl: string;
  /** For a canvas or a WebGL texture. */
  bitmap: ImageBitmap;
}

const cache = new PictureKeep<PieceImage>();
const ready = cache.ready;
const loading = new Set<string>();
const failed = cache.failed;
const listeners = new Set<(hash: string) => void>();

/** The bytes of base64 text. */
export function fromBase64(data: string): Uint8Array<ArrayBuffer> {
  const bin = atob(data);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) {
    out[i] = bin.charCodeAt(i);
  }
  return out;
}

/** A picture from its file's bytes (the dialog's, before the engine has it). */
export async function decodeImage(hash: string, type: string, data: string): Promise<PieceImage> {
  const bitmap = await createImageBitmap(new Blob([fromBase64(data)], { type }));
  return {
    hash,
    width: bitmap.width,
    height: bitmap.height,
    dataUrl: `data:${type};base64,${data}`,
    bitmap,
  };
}

/**
 * The shape's picture when it is decoded; null while it loads (it is
 * asked for then, and `onPieceImage` hears when it is ready) or when it
 * could not be read.
 */
export function pieceImage(engine: EngineLink, image: ShapeImage | null): PieceImage | null {
  if (image === null) {
    return null;
  }
  const have = ready.get(image.hash);
  if (have !== undefined || loading.has(image.hash) || failed.has(image.hash)) {
    return have ?? null;
  }
  loading.add(image.hash);
  void engine
    .call('shape.imageData', { image: image.image })
    .then((r) => decodeImage(r.hash, r.type, r.data))
    .then(
      (im) => {
        ready.set(image.hash, im);
        loading.delete(image.hash);
        for (const l of listeners) {
          l(image.hash);
        }
      },
      () => {
        // a picture the browser cannot decode is drawn as the shape's color
        loading.delete(image.hash);
        failed.add(image.hash);
      },
    );
  return null;
}

/** The shape's pictures for a 3D view, as one, once decoded (null: none, or not yet). */
export function piecePicture(
  engine: EngineLink,
  shape: { images: readonly ShapeImage[] } | undefined,
): PiecePicture | null {
  return stackPicture(engine, shape?.images ?? []);
}

/**
 * The pictures of a board's tray border for a 3D view, as one, laid over
 * the tray's outer rectangle as it is now (decision 0046), once decoded
 * (null: none, or not yet).
 */
export function borderPicture(
  engine: EngineLink,
  shape: { size: readonly number[]; border: TrayBorder | null } | null | undefined,
): PiecePicture | null {
  const p = stackPicture(engine, shape?.border?.images ?? []);
  return shape == null || p === null
    ? null
    : { image: borderImage(p.image, shape.size, borderRim(shape.border)), picture: p.picture };
}

/** The longest side of a stack drawn as one picture, and the fewest pixels a cell. */
const STACK_MAX = 2048;
const STACK_MIN_CELL = 32;

/**
 * The size of a stack drawn as one picture over its box: as many pixels a
 * cell as its sharpest picture has, at least STACK_MIN_CELL, at most
 * STACK_MAX a side.
 */
export function stackSize(images: readonly ShapeImage[]): [number, number] {
  const [, , bw = 1, bh = 1] = images[0]?.box ?? [];
  let cell = STACK_MIN_CELL;
  for (const im of images) {
    cell = Math.max(cell, im.src[2] / bw, im.src[3] / bh);
  }
  const k = Math.min(cell, STACK_MAX / Math.max(bw, bh));
  return [Math.max(1, Math.round(bw * k)), Math.max(1, Math.round(bh * k))];
}

/** The bytes of a blob as base64. */
async function blobBase64(blob: Blob): Promise<string> {
  const bytes = new Uint8Array(await blob.arrayBuffer());
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(s);
}

/**
 * A shape's stack of pictures, the bottom one first, as one picture with
 * its placement (decision 0047): the only picture as it is when its part
 * shown lies inside it; else every picture drawn over the box into a new
 * one, transparent where none reaches, its part shown the whole of it.
 * Null for none, or while a picture or the drawing is not ready (asked
 * for then; the listeners hear when it is).
 */
export function stackPicture(
  engine: EngineLink,
  images: readonly ShapeImage[],
): { image: ShapeImage; picture: PieceImage } | null {
  const first = images[0];
  if (first === undefined) {
    return null;
  }
  if (images.length === 1 && inside(first)) {
    const picture = pieceImage(engine, first);
    return picture === null ? null : { image: first, picture };
  }
  const key = stackKey(images);
  const [w, h] = stackSize(images);
  const image: ShapeImage = { ...first, hash: key, src: [0, 0, w, h], width: w, height: h };
  const have = ready.get(key);
  if (have !== undefined) {
    return { image, picture: have };
  }
  // every picture first (one that cannot be read is left out)
  const pictures = images.map((im) => pieceImage(engine, im));
  if (images.some((im, k) => pictures[k] === null && !failed.has(im.hash)) || loading.has(key)) {
    return null;
  }
  loading.add(key);
  void drawStack(images, pictures, w, h, key).then(
    (im) => {
      ready.set(key, im);
      loading.delete(key);
      for (const l of listeners) {
        l(key);
      }
    },
    () => {
      loading.delete(key);
    },
  );
  return null;
}

// the stack drawn over a w x h picture of its box, bottom first
async function drawStack(
  images: readonly ShapeImage[],
  pictures: readonly (PieceImage | null)[],
  w: number,
  h: number,
  key: string,
): Promise<PieceImage> {
  const canvas = new OffscreenCanvas(w, h);
  const g = canvas.getContext('2d');
  if (g === null) {
    throw new Error('The pictures could not be drawn');
  }
  g.imageSmoothingQuality = 'high';
  images.forEach((im, k) => {
    const p = pictures[k];
    if (p == null) {
      return;
    }
    // the part shown fills the box: the picture's pixels to the drawing's
    const [sx, sy, sw, sh] = im.src;
    const fx = w / sw;
    const fy = h / sh;
    g.drawImage(p.bitmap, -sx * fx, -sy * fy, p.width * fx, p.height * fy);
  });
  const blob = await canvas.convertToBlob({ type: 'image/png' });
  const data = await blobBase64(blob);
  const bitmap = await createImageBitmap(blob, { premultiplyAlpha: 'none' });
  return { hash: key, width: w, height: h, dataUrl: `data:image/png;base64,${data}`, bitmap };
}

/**
 * The picture once decoded: at once when it is, else when it is loaded
 * (null when it cannot be read).
 */
export function loadPieceImage(engine: EngineLink, image: ShapeImage): Promise<PieceImage | null> {
  const now = pieceImage(engine, image);
  if (now !== null || failed.has(image.hash)) {
    return Promise.resolve(now);
  }
  return new Promise((done) => {
    const off = onPieceImage((hash) => {
      if (hash === image.hash) {
        off();
        done(ready.get(hash) ?? null);
      }
    });
    // asked for in the meantime by someone else, or failed
    window.setTimeout(() => {
      if (!loading.has(image.hash)) {
        off();
        done(ready.get(image.hash) ?? null);
      }
    }, 5000);
  });
}

/**
 * Keeps the cache to what the store's document names, at each new
 * snapshot. Subscribed after the views, so they have let go of the old
 * pictures first; returns the unsubscribe.
 */
export function keepPicturesOf(store: Store): () => void {
  return store.subscribe((c) => {
    if (c.kind === 'document' && store.snapshot !== null) {
      cache.keep(namedPictures(store.snapshot.shapes));
    }
  });
}

/**
 * Holds the pictures an open dialog shows, so they are never closed under
 * it; returns the release.
 */
export function holdPictures(pictures: readonly { hash: string }[]): () => void {
  return cache.hold(pictures);
}

/** Hears when a picture asked for is ready; returns the unsubscribe. */
export function onPieceImage(listener: (hash: string) => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
