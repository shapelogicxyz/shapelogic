// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stacking puzzle's layer of the 3D view (layer.ts; docs/webui-spec.md,
// section 9): its board (board.ts), drawn tilted as the original's
// (renderer.ts): the rod set alone or a disc in the Entities workspace, the
// board with the start's or the goal's discs in the Puzzle workspace
// (instead of the pieces scene), the pegs around the player's discs.

import { EngineError } from '../../engine/errors.ts';
import { byId, esc } from '../shell/dom.ts';
import { tab } from '../shell/layout.ts';
import { ROD_CHIP } from '../shell/piece-colors.ts';
import { prefs } from '../shell/prefs.ts';
import { reportError } from '../shell/status.ts';
import { BoardView } from './board.ts';
import type { BoardModel } from './board.ts';
import type { LayerHost, ViewLayer } from './layer.ts';

export class StackingLayer implements ViewLayer {
  private readonly host: LayerHost;
  /** A stacking puzzle's board: the Entities' rod set, the Puzzle's stacking, the player's pegs. */
  private readonly board: BoardView;
  private readonly boards: { entities: BoardModel | null; puzzle: BoardModel | null } = {
    entities: null,
    puzzle: null,
  };

  constructor(host: LayerHost) {
    this.host = host;
    const renderer = host.renderer;
    this.board = new BoardView({
      store: host.store,
      meshes: host.meshes,
      shapeRevision: (s) => host.shapeRevision(s),
      style: () => host.display.style,
      changed: () => {
        const vp = byId('vp');
        vp.dataset.board = this.board.describe();
        vp.dataset.boardPoses = this.board.posesShown();
        vp.dataset.boardBoxes = String(this.board.boxMeshes);
        renderer.request();
      },
      failed: (e) => {
        if (e instanceof EngineError && e.errc === 'superseded') {
          queueMicrotask(() => {
            this.board.load();
          });
          return;
        }
        reportError(e, 'The board could not be drawn');
      },
    });
    this.board.group.visible = false;
    renderer.root.add(this.board.group);
  }

  private get stacking(): boolean {
    return this.host.store.snapshot?.grid === 'stacking';
  }

  /**
   * A stacking puzzle's board in a workspace (null: none): the Entities'
   * rod set shown alone instead of the disc, the Puzzle's start or goal.
   * False when nothing changed.
   */
  setBoard(model: BoardModel | null, where: 'entities' | 'puzzle'): boolean {
    if (model === null && this.boards[where] === null) {
      return false;
    }
    this.boards[where] = model;
    this.show();
    return true;
  }

  /** Whether the Entities workspace shows a rod set instead of its disc. */
  get rodSetShown(): boolean {
    return tab === 'entities' && this.boards.entities !== null;
  }

  /** The view's tag of the rod set shown alone (showSelectedRods); null when none is. */
  rodSetTag(): string | null {
    const rods = this.boards.entities;
    if (!this.rodSetShown || rods === null) {
      return null;
    }
    return `<span class="chip" style="background:${ROD_CHIP};color:#fff">${esc(rods.label ?? '')}</span><span>Rod set</span>`;
  }

  /** The size the Entities view frames: the rod set's when it is shown alone, else null. */
  get rodSetExtent(): [number, number, number] | null {
    return this.rodSetShown ? this.board.extent : null;
  }

  /** The board of the workspace shown (the player's: its pegs, no discs). */
  show(): void {
    let model: BoardModel | null = null;
    if (this.stacking) {
      if (tab === 'entities') {
        model = this.boards.entities;
      } else if (tab === 'puzzle') {
        model = this.boards.puzzle;
      } else {
        const s = this.host.solutionModel();
        const b = s?.board ?? null;
        model = b === null ? null : { key: `solver:${s?.key ?? ''}`, ...b, discs: [] };
      }
    }
    this.board.set(model);
    this.board.group.visible = model !== null;
  }

  reload(): void {
    this.board.load();
  }

  themed(): void {
    // the board's colors are its own
  }

  setResolution(width: number, height: number): void {
    this.board.setResolution(width, height);
  }

  sync(): void {
    // a stacking puzzle is drawn tilted to stand its stack up
    this.host.renderer.tilt = this.stacking;
    this.show();
    this.board.setLook({
      style: this.host.display.style,
      colorView: false,
      lit: prefs.lights,
      xray: false,
      palette: [],
    });
  }

  drawPuzzle(): string | null {
    if (!this.stacking) {
      return null;
    }
    // a stacking puzzle's Puzzle workspace draws its board, not the pieces scene
    this.host.renderer.frame(this.host.camera, this.board.extent ?? [1, 1, 1]);
    this.host.renderer.render();
    return '';
  }
}
