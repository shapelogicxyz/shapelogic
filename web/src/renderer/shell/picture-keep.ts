// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the picture cache (piece-images.ts) keeps, apart from the page so
// the unit tests run it without a DOM (cleanup item 10.6): the pictures
// and stacks the open document names (namedPictures), and the ones an open
// dialog holds. At each new snapshot the rest are let go: the listeners of
// onPictureGone first (the 3D views' textures), then their bitmaps closed.

import type { ShapeImage, TrayBorder } from '../../engine/api.generated.ts';

/** Whether a picture's part shown lies inside it: it is drawn as it is, alone. */
export function inside(image: ShapeImage): boolean {
  const [sx, sy, sw, sh] = image.src;
  return sx >= 0 && sy >= 0 && sx + sw <= image.width && sy + sh <= image.height;
}

/** A stack's key: its pictures and parts shown, and its box's size. */
export function stackKey(images: readonly ShapeImage[]): string {
  const box = images[0]?.box ?? [];
  return `stack:${String(box[2])}x${String(box[3])}:${images.map((im) => `${im.hash}@${im.src.join(',')}`).join('|')}`;
}

/** The hashes and stack keys a snapshot's shapes name: their pictures, borders and stacks. */
export function namedPictures(
  shapes: readonly { images: readonly ShapeImage[]; border: TrayBorder | null }[],
): Set<string> {
  const out = new Set<string>();
  const add = (images: readonly ShapeImage[]): void => {
    const first = images[0];
    if (first === undefined) {
      return;
    }
    for (const im of images) {
      out.add(im.hash);
    }
    if (images.length > 1 || !inside(first)) {
      out.add(stackKey(images));
    }
  };
  for (const s of shapes) {
    add(s.images);
    add(s.border?.images ?? []);
  }
  return out;
}

const goneListeners = new Set<(hash: string) => void>();

/** Hears a picture let go, before its bitmap is closed; returns the unsubscribe. */
export function onPictureGone(listener: (hash: string) => void): () => void {
  goneListeners.add(listener);
  return () => {
    goneListeners.delete(listener);
  };
}

/** The decoded pictures by hash or stack key, kept while named or held. */
export class PictureKeep<T extends { bitmap: { close(): void } }> {
  /** The decoded pictures and stacks. */
  readonly ready = new Map<string, T>();
  /** The pictures that could not be read (not asked for again while named). */
  readonly failed = new Set<string>();
  /** Pictures an open dialog shows: how many holds each has. */
  private readonly held = new Map<string, number>();
  /** What the last snapshot named (null: not told yet, keep everything). */
  private named: ReadonlySet<string> | null = null;

  /**
   * Keeps only the pictures `keep` names, and those held: the rest are
   * forgotten, onPictureGone told, then their bitmaps closed.
   */
  keep(keep: ReadonlySet<string>): void {
    this.named = keep;
    for (const [hash, im] of this.ready) {
      if (keep.has(hash) || this.held.has(hash)) {
        continue;
      }
      this.ready.delete(hash);
      for (const l of goneListeners) {
        l(hash);
      }
      im.bitmap.close();
    }
    for (const hash of this.failed) {
      if (!keep.has(hash)) {
        this.failed.delete(hash);
      }
    }
  }

  /**
   * Holds pictures so they are never closed under an open dialog; returns
   * the release, after which they go if the last snapshot did not name them.
   */
  hold(pictures: readonly { hash: string }[]): () => void {
    const hashes = pictures.map((p) => p.hash);
    for (const h of hashes) {
      this.held.set(h, (this.held.get(h) ?? 0) + 1);
    }
    let done = false;
    return () => {
      if (done) {
        return;
      }
      done = true;
      for (const h of hashes) {
        const n = (this.held.get(h) ?? 1) - 1;
        if (n > 0) {
          this.held.set(h, n);
        } else {
          this.held.delete(h);
        }
      }
      if (this.named !== null) {
        this.keep(this.named);
      }
    };
  }
}
