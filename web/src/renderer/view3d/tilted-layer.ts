// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Tilted Square puzzle's layer of the 3D view (layer.ts; decision 0045):
// the grid turned as it lies in the tray, whichever workspace draws it; the
// tray itself, its outline, walls and cells (tray3d.ts), in the shape
// view's model and around the Solver's pieces; the home view straight down
// on the tray; and the board as checker blocks in the pieces scene.

import { Quaternion } from 'three';

import type { Snapshot } from '../../engine/api.generated.ts';
import { byId, css } from '../shell/dom.ts';
import { tab } from '../shell/layout.ts';
import type { LayerHost, ViewLayer } from './layer.ts';
import type { PieceStandIn } from './pieces.ts';
import { CheckerCells, TrayScene } from './tray3d.ts';

// a Tilted Square board's checker colors, the board's Cells view's
const cellColors = (): readonly [string, string] => [css('--tcellA'), css('--tcellB')];

export class TiltedLayer implements ViewLayer {
  private readonly host: LayerHost;
  /** A Tilted Square puzzle's tray: around the shape, and around the Solver's pieces. */
  private readonly shapeTray = new TrayScene();
  private readonly solutionTray = new TrayScene();
  /** A Tilted Square board's cells as the Puzzle view draws its result. */
  private readonly boardCells = new CheckerCells();
  /** Whether the home view is the tilted puzzle's, straight down on the tray. */
  private tiltedHome = false;

  constructor(host: LayerHost) {
    this.host = host;
    host.renderer.model.add(this.shapeTray.group);
    this.solutionTray.group.visible = false;
    host.renderer.root.add(this.solutionTray.group);
  }

  reload(): void {
    // the tray is the snapshot's: sync() reads it
  }

  themed(): void {
    // its colors are read at each sync()
  }

  setResolution(width: number, height: number): void {
    this.shapeTray.setResolution(width, height);
    this.solutionTray.setResolution(width, height);
  }

  // a Tilted Square puzzle: the grid turned as it lies in the tray (the
  // board's views show it so), whichever workspace draws it
  sync(): void {
    const { store, renderer, camera } = this.host;
    const tray = store.snapshot?.tray;
    const turn = tray == null ? 0 : (tray.degrees * Math.PI) / 180;
    if (turn !== renderer.turn) {
      renderer.turn = turn;
      renderer.request();
    }
    // the tray itself, its walls as they are now: in the shape view's model
    // (the board's grid, every shape's), and around the Solver's pieces
    // (centered on the result, one layer)
    const t3 = tray == null ? null : { corners: tray.corners, walls: tray.wallPolygons };
    const rim = css('--text') || '#1a1f29';
    this.shapeTray.setRimColor(rim);
    this.solutionTray.setRimColor(rim);
    const wall = css('--twall') || '#2b2f37';
    const wallEdge = css('--twallEdge') || '#8a93a3';
    this.shapeTray.setWallColor(wall, wallEdge);
    this.solutionTray.setWallColor(wall, wallEdge);
    this.shapeTray.setCellColors(cellColors());
    const shape = store.shape;
    this.shapeTray.set(t3, 1, [0, 0, 0], shape?.board === true && tray != null ? tray : null);
    const [bx, by] = tray?.box ?? [1, 1];
    this.solutionTray.set(t3, 1, [-bx / 2, -by / 2, -0.5]);
    this.solutionTray.group.visible = t3 !== null && tab === 'solver';
    const vp = byId('vp');
    vp.dataset.trayWalls = String(this.shapeTray.walls);
    vp.dataset.trayCells = String(this.shapeTray.cells);
    // a tilted puzzle's home view looks straight down on the tray
    if ((tray != null) !== this.tiltedHome) {
      this.tiltedHome = tray != null;
      camera.setHome(this.tiltedHome ? new Quaternion() : null);
      camera.home(performance.now());
    }
    renderer.request();
  }

  drawPuzzle(): string | null {
    return null;
  }

  /** A Tilted Square board (the result) as checker blocks in the pieces scene, as in the shape view; null for any other shape. */
  standIn(snap: Snapshot | null, shape: string): PieceStandIn | null {
    const tray = snap?.tray;
    const s = snap?.shapes[this.host.store.indexOf(shape)];
    if (tray == null || s?.board !== true) {
      return null;
    }
    return {
      ...this.boardCells.geometry(tray, cellColors()),
      material: this.boardCells.material,
    };
  }
}
