// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures on a sliding puzzle's pieces (decision 0038), decoded once
// and kept by their hash: the snapshot names each shape's picture by its
// number and hash, and its file comes from shape.imageData the first time
// a hash is seen. The 2D grid draws the data URL (the page's CSP allows
// `data:` images, not `blob:` ones); the 3D views and the dialog's preview
// draw the bitmap. A picture still loading is drawn as nothing, and the
// listeners hear when it is ready.

import type { ShapeImage } from '../../engine/api.generated.ts';
import type { EngineLink } from '../state/store.ts';
import type { PiecePicture } from './picture-math.ts';

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

const ready = new Map<string, PieceImage>();
const loading = new Set<string>();
const failed = new Set<string>();
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

/** The shape's picture for a 3D view, once decoded (null: none, or not yet). */
export function piecePicture(
  engine: EngineLink,
  shape: { image: ShapeImage | null } | undefined,
): PiecePicture | null {
  const image = shape?.image ?? null;
  const picture = pieceImage(engine, image);
  return image === null || picture === null ? null : { image, picture };
}

/** Hears when a picture asked for is ready; returns the unsubscribe. */
export function onPieceImage(listener: (hash: string) => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
