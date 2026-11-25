// SPDX-License-Identifier: GPL-3.0-or-later
//
// One layer of the 3D view per puzzle kind (cleanup item 13.3): a sliding
// puzzle's trays, walls and borders (sliding-layer.ts), a stacking
// puzzle's board (stacking-layer.ts), a Tilted Square puzzle's tray
// (tilted-layer.ts). The view (view.ts) walks them where each kind used to
// add its own branch: a resize, a reload, a theme, the look worked out
// once a task, the Puzzle workspace's drawing. Each writes its own `#vp`
// marks (`data-*`) when what it shows changes.

import type { Store } from '../state/store.ts';
import type { Camera } from './camera.ts';
import type { ViewRenderer } from './renderer.ts';
import type { ShapeMeshes } from './shape-meshes.ts';
import type { SolutionModel } from './solution.ts';
import type { Display } from './toolbar.ts';

/** What every layer reads of the view. */
export interface LayerHost {
  readonly store: Store;
  readonly renderer: ViewRenderer;
  readonly camera: Camera;
  /** The shape meshes, shared with the rest of the view. */
  readonly meshes: ShapeMeshes;
  readonly display: Display;
  /** The revision a shape last changed at in this document (its meshes are asked at it). */
  shapeRevision(shape: string): number;
  /** The Solver's solution shown (null: none). */
  solutionModel(): SolutionModel | null;
}

/** A puzzle kind's part of the 3D view. */
export interface ViewLayer {
  /** The document or the selection changed: what the layer draws is read again. */
  reload(): void;
  /** The theme changed. */
  themed(): void;
  /** The view's size in CSS pixels (the lines' widths). */
  setResolution(width: number, height: number): void;
  /** What shows and how, for the workspace shown (the view's look, worked out once a task). */
  sync(): void;
  /**
   * Draws the Puzzle workspace's view when this layer owns it, and gives
   * the HTML of its titles over the view ('' for none); null when the
   * view is not this layer's (the next one's, else the pieces scene's).
   */
  drawPuzzle(): string | null;
}
