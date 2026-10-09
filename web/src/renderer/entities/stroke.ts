// SPDX-License-Identifier: GPL-3.0-or-later
//
// A drag across cells as ONE undo step that never floods the engine: the
// cells entered are collected and sent once an animation frame, one
// `shape.setCells` at the engine at a time (the next batch waits for the
// reply of the one before), all inside `edit.begin` / `edit.end`, so the
// engine records one step (an empty group leaves none). A click is a drag
// of one cell. From docs/mockup/index.html: the #garea handlers' stroke
// (`down`, `strokeTool`, `applyCell` on every cell entered), whose voxel
// map is the engine here; the original grouped a stroke the same way
// (gridEditor_c's RS_STROKEBEGIN / RS_STROKEEND, shapehistory.cpp).

import type { CellTool } from '../../engine/api.generated.ts';
import type { EngineApi } from '../../engine/client-types.ts';
import { key } from './editor-state.ts';
import type { Axis, Cell } from './editor-state.ts';

/** What a stroke draws with. */
export interface StrokeTool {
  shape: string;
  tool: CellTool;
  /** The drawing colour (null: the shape's own). */
  colour: string | null;
  mirror: Axis[];
  span: Axis[];
}

/** The undo step's name of a tool's stroke. */
export function strokeLabel(tool: CellTool): string {
  switch (tool) {
    case 'fixed':
      return 'Draw fixed voxels';
    case 'variable':
      return 'Draw variable voxels';
    case 'erase':
      return 'Erase voxels';
    case 'paint':
      return 'Paint voxels';
  }
}

/** How a frame is waited for (requestAnimationFrame; a test's stand-in). */
export type FrameWait = (f: () => void) => void;

export class Stroke {
  readonly tool: StrokeTool;
  /** The cells entered and not sent yet. */
  private pending: Cell[] = [];
  /** Every cell entered, sent or not (the 2D grid marks them until the engine answers). */
  private readonly seen = new Set<string>();
  private readonly engine: EngineApi;
  private readonly frame: FrameWait;
  private readonly onError: (e: unknown) => void;
  /** The call the next batch waits for: edit.begin, then each batch. */
  private inFlight: Promise<unknown>;
  private busy = true;
  /** edit.begin was answered: the group is open and edit.end must close it. */
  private begun = false;
  private scheduled = false;
  private failed = false;
  private ending: Promise<number | null> | null = null;
  /** Batches sent, for the tests. */
  batches = 0;

  /** Starts a stroke: edit.begin is sent at once; `frame` waits for the next frame (requestAnimationFrame). */
  constructor(
    engine: EngineApi,
    tool: StrokeTool,
    onError: (e: unknown) => void,
    frame: FrameWait,
  ) {
    this.engine = engine;
    this.tool = tool;
    this.onError = onError;
    this.frame = frame;
    this.inFlight = engine.call('edit.begin', { label: strokeLabel(tool.tool) }).then(
      () => {
        this.begun = true;
        this.settled();
      },
      (e: unknown) => {
        this.fail(e);
      },
    );
  }

  /** Whether a cell was entered by this stroke. */
  has(cell: readonly number[]): boolean {
    return this.seen.has(key(cell));
  }

  /** Cells entered; each is sent once. */
  add(cells: readonly Cell[]): void {
    if (this.failed || this.ending !== null) {
      return;
    }
    for (const c of cells) {
      const k = key(c);
      if (!this.seen.has(k)) {
        this.seen.add(k);
        this.pending.push(c);
      }
    }
    this.schedule();
  }

  /**
   * Ends the stroke: what is left is sent, then edit.end; resolves with
   * the revision after it (null when the stroke failed).
   */
  end(): Promise<number | null> {
    this.ending ??= this.finish();
    return this.ending;
  }

  private async finish(): Promise<number | null> {
    // the last batch waits for the one before, as every batch does
    while (this.busy) {
      await this.inFlight;
    }
    if (!this.begun) {
      return null;
    }
    // a group that was opened is closed, whatever failed inside it
    const batch = this.failed ? null : this.take();
    const sent =
      batch === null
        ? null
        : this.send(batch).catch((e: unknown) => {
            this.fail(e);
          });
    try {
      const r = await this.engine.call('edit.end');
      await sent;
      return this.failed ? null : r.revision;
    } catch (e) {
      this.fail(e);
      return null;
    }
  }

  private schedule(): void {
    if (this.scheduled || this.pending.length === 0) {
      return;
    }
    this.scheduled = true;
    this.frame(() => {
      this.scheduled = false;
      this.flush();
    });
  }

  private flush(): void {
    if (this.busy || this.failed || this.ending !== null) {
      return;
    }
    const batch = this.take();
    if (batch === null) {
      return;
    }
    this.busy = true;
    this.inFlight = this.send(batch).then(
      () => {
        this.settled();
      },
      (e: unknown) => {
        this.fail(e);
      },
    );
  }

  private take(): Cell[] | null {
    if (this.pending.length === 0) {
      return null;
    }
    const batch = this.pending;
    this.pending = [];
    return batch;
  }

  private send(cells: Cell[]): Promise<unknown> {
    const t = this.tool;
    this.batches++;
    return this.engine.call('shape.setCells', {
      shape: t.shape,
      tool: t.tool,
      cells,
      ...(t.colour === null ? {} : { colour: t.colour }),
      ...(t.mirror.length === 0 ? {} : { mirror: t.mirror }),
      ...(t.span.length === 0 ? {} : { span: t.span }),
    });
  }

  private settled(): void {
    this.busy = false;
    this.schedule();
  }

  private fail(e: unknown): void {
    this.busy = false;
    if (!this.failed) {
      this.failed = true;
      this.pending = [];
      this.onError(e);
    }
  }
}
