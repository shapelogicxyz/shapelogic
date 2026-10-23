// SPDX-License-Identifier: GPL-3.0-or-later
//
// A sliding puzzle's start/goal shape (a tray) as the Entities workspace
// shows it (docs/webui-spec.md, section 8.1): what the engine says it
// holds (`sliding.tray`: each cell's start and goal, the validity bar's
// reason), read again at each revision while a tray is shown, the marks
// in the pieces' colours, the preview's colour, and the validity bar's
// words. From the original's grideditor.cpp (`drawStampPreview`'s
// colours, the marks drawn in the piece's colour), mainwindow_rods.cpp
// (`refreshSlideValid`'s words and colours) and tooltabs.cpp
// (`refreshPieceList`: every shape but the trays). DOM-free.

import type { Methods, ShapeSummary } from '../../engine/api.generated.ts';
import type { EngineApi } from '../../engine/client-types.ts';
import { isTray, shapeColour } from '../shell/piece-colours.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { key } from './editor-state.ts';

/** What `sliding.tray` answers. */
export type TrayReply = Methods['sliding.tray']['result'];

/** What `sliding.preview` answers. */
export type PreviewReply = Methods['sliding.preview']['result'];

/** The piece outlined under the mouse: its cells [x, y, z] and the outline's colour. */
export interface StampOutline {
  cells: [number, number, number][];
  colour: string;
}

/**
 * The outline's colour (drawStampPreview): orange where the piece already
 * is (a click takes it off), green where it fits, red where it does not.
 */
export function outlineColour(p: Pick<PreviewReply, 'fits' | 'there'>): string {
  if (p.there) {
    return 'rgb(245,150,30)';
  }
  return p.fits ? 'rgb(60,210,80)' : 'rgb(230,40,40)';
}

/** The validity bar's text and whether it is good news (refreshSlideValid). */
export function validity(error: string | null): { text: string; valid: boolean } {
  return error === null
    ? { text: 'Valid Start/Goal positions for pieces in Start and Goal of this shape', valid: true }
    : { text: `Invalid Start/Goal positions - ${error}`, valid: false };
}

/** The pieces a start or goal can be placed for: every shape but the trays, "S2 - name". */
export function stampPieces(shapes: readonly ShapeSummary[]): ShapeSummary[] {
  return shapes.filter((s) => !isTray(s));
}

/** A piece's name in the Start/Goal list (refreshPieceList). */
export function pieceText(s: ShapeSummary): string {
  return s.name === '' ? s.label : `${s.label} - ${s.name}`;
}

/**
 * Each marked cell's piece colour, by cell key (the starts, or the goals):
 * the mark is the piece's number, drawn in its colour.
 */
export function marksOf(
  t: TrayReply,
  goal: boolean,
  shapes: readonly ShapeSummary[],
): Map<string, Rgb3> {
  const out = new Map<string, Rgb3>();
  const [X, Y] = t.size;
  const marks = goal ? t.goals : t.starts;
  marks.forEach((id, i) => {
    if (id === null) {
      return;
    }
    const index = shapes.findIndex((s) => s.id === id);
    if (index < 0) {
      return;
    }
    const x = i % X;
    const y = Math.trunc(i / X) % Y;
    const z = Math.trunc(i / (X * Y));
    out.set(key([x, y, z]), shapeColour(shapes[index]?.colour ?? null, index));
  });
  return out;
}

/**
 * Where each piece stands on a tray (the 3D view's two trays): its cells
 * there, by piece. A stamp is the whole piece moved, so its cells' lowest
 * corner and the piece's place it.
 */
export function stampsOf(t: TrayReply, goal: boolean): Map<string, [number, number, number][]> {
  const out = new Map<string, [number, number, number][]>();
  const [X, Y] = t.size;
  (goal ? t.goals : t.starts).forEach((id, i) => {
    if (id === null) {
      return;
    }
    const cells = out.get(id) ?? [];
    cells.push([i % X, Math.trunc(i / X) % Y, Math.trunc(i / (X * Y))]);
    out.set(id, cells);
  });
  return out;
}

/**
 * The tray shown and what the engine says it holds, read again when the
 * document's revision moves (coalesced: a burst asks at most twice).
 */
export class TrayState {
  /** The reply for the tray shown, or null (no tray, or not read yet). */
  reply: TrayReply | null = null;
  private readonly engine: EngineApi;
  private readonly changed: () => void;
  private readonly failed: (e: unknown) => void;
  private wanted = '';
  /** The document and tray the reply is of. */
  private replyOf = '';
  private loads = 0;

  constructor(engine: EngineApi, changed: () => void, failed: (e: unknown) => void) {
    this.engine = engine;
    this.changed = changed;
    this.failed = failed;
  }

  /** Shows a tray (null: none) as of a document and revision. */
  show(tray: string | null, generation: number, revision: number): void {
    const want = tray === null ? '' : `${String(generation)}:${tray}:${String(revision)}`;
    if (want === this.wanted) {
      return;
    }
    this.wanted = want;
    const n = ++this.loads;
    if (tray === null) {
      if (this.reply !== null) {
        this.reply = null;
        this.changed();
      }
      return;
    }
    // what was read for another tray is not this one's
    const of = `${String(generation)}:${tray}`;
    if (this.replyOf !== of && this.reply !== null) {
      this.reply = null;
      this.changed();
    }
    this.engine.call('sliding.tray', { tray }, { coalesce: 'sliding.tray' }).then(
      (r) => {
        if (n === this.loads) {
          this.reply = r;
          this.replyOf = of;
          this.changed();
        }
      },
      (e: unknown) => {
        if (n === this.loads) {
          this.wanted = '';
          this.failed(e);
        }
      },
    );
  }
}
