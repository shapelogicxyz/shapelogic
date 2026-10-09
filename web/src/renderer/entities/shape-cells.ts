// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cells of the shape the editor shows, as the engine has them: the XY
// layer drawn (`shape.get {layer}`) and the whole shape (`shape.get`: the
// layer strip's marks, the cube grid's XZ and YZ planes, the 3D preview).
// A mirror (decision 0013, "State"): reloaded when `document.changed`
// named the shape (or named no shapes) and the snapshot after it still has
// the shape (a deleted one is not asked for), when another shape is shown
// and when the layer moves; the reads are coalesced, so a drag asks for at
// most two of each. Never edited here: the editor calls the engine.

import type { CellState } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { reportError } from '../shell/status.ts';
import { key } from './editor-state.ts';
import type { Cell } from './editor-state.ts';

/** A non-empty cell: its state and its colour (null: the shape's own). */
export interface CellValue {
  at: Cell;
  state: CellState;
  colour: string | null;
}

type Cells = Map<string, CellValue>;

function toMap(cells: readonly { at: number[]; state: CellState; colour?: string }[]): Cells {
  const m: Cells = new Map();
  for (const c of cells) {
    const at: Cell = [c.at[0] ?? 0, c.at[1] ?? 0, c.at[2] ?? 0];
    m.set(key(at), { at, state: c.state, colour: c.colour ?? null });
  }
  return m;
}

export class ShapeCells {
  /** The shape the cells are of, or null. */
  shape: string | null = null;
  /** Every non-empty cell of the shape. */
  all: Cells = new Map();
  /** The non-empty cells of the XY layer `layer` (shape.get {layer}). */
  layerCells: Cells = new Map();
  layer = 0;
  private readonly store: Store;
  private readonly listeners = new Set<() => void>();
  private loadsAll = 0;
  private loadsLayer = 0;
  /** A change named the shape: read it again once the snapshot has come. */
  private stale = false;

  constructor(store: Store) {
    this.store = store;
    store.subscribe((c) => {
      if (c.kind === 'changed') {
        const p = c.change;
        if (
          this.shape !== null &&
          (p.what.includes('shapes') || p.what.includes('colours')) &&
          (p.shapes.length === 0 || p.shapes.includes(this.shape))
        ) {
          this.stale = true;
        }
      } else if (c.kind === 'document' && this.stale) {
        const shape = this.shape;
        if (shape !== null && store.snapshot?.shapes.some((s) => s.id === shape) === true) {
          this.stale = false;
          this.reload();
        }
      }
    });
  }

  /** Listens to new cells; returns the unsubscribe. */
  subscribe(l: () => void): () => void {
    this.listeners.add(l);
    return () => {
      this.listeners.delete(l);
    };
  }

  /** Shows a shape (null: none) at an XY layer; reads it when either changed. */
  show(shape: string | null, layer: number): void {
    if (shape === this.shape && layer === this.layer) {
      return;
    }
    const other = shape !== this.shape;
    this.shape = shape;
    this.layer = layer;
    if (other) {
      this.stale = false;
    }
    if (shape === null) {
      this.all = new Map();
      this.layerCells = new Map();
      this.emit();
      return;
    }
    if (other) {
      this.all = new Map();
      this.layerCells = new Map();
      this.reload();
    } else {
      this.reloadLayer();
    }
  }

  /** Reads everything again (another document, a change). */
  reload(): void {
    this.reloadAll();
    this.reloadLayer();
  }

  /** The cell at a place, from the whole shape. */
  at(c: readonly number[]): CellValue | undefined {
    return this.all.get(key(c));
  }

  /** The z layers (XY plane) holding cells, from the whole shape. */
  layersWithCells(axis: 0 | 1 | 2): Set<number> {
    const s = new Set<number>();
    for (const c of this.all.values()) {
      s.add(c.at[axis]);
    }
    return s;
  }

  private reloadAll(): void {
    const shape = this.shape;
    if (shape === null) {
      return;
    }
    const n = ++this.loadsAll;
    const generation = this.store.generation;
    this.store.engine.call('shape.get', { shape }, { coalesce: `cells:${shape}` }).then(
      (r) => {
        if (n === this.loadsAll && shape === this.shape && generation === this.store.generation) {
          this.all = toMap(r.cells);
          this.emit();
        }
      },
      (e: unknown) => {
        this.failed(e);
      },
    );
  }

  private reloadLayer(): void {
    const shape = this.shape;
    if (shape === null) {
      return;
    }
    const layer = this.layer;
    const n = ++this.loadsLayer;
    const generation = this.store.generation;
    this.store.engine.call('shape.get', { shape, layer }, { coalesce: `layer:${shape}` }).then(
      (r) => {
        if (
          n === this.loadsLayer &&
          shape === this.shape &&
          layer === this.layer &&
          generation === this.store.generation
        ) {
          this.layerCells = toMap(r.cells);
          this.emit();
        }
      },
      (e: unknown) => {
        this.failed(e);
      },
    );
  }

  private failed(e: unknown): void {
    // a shape deleted meanwhile, a newer read, an engine restarting: the
    // next snapshot says what there is
    if (
      e instanceof Error &&
      'errc' in e &&
      (e.errc === 'not_found' || e.errc === 'superseded' || e.errc === 'disconnected')
    ) {
      return;
    }
    reportError(e, 'The cells could not be read');
  }

  private emit(): void {
    for (const l of [...this.listeners]) {
      l();
    }
  }
}
