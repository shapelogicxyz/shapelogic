// SPDX-License-Identifier: GPL-3.0-or-later
//
// A sliding puzzle's start/goal shape (a board) in the Puzzle view
// (docs/webui-spec.md, section 8.2): two trays side by side, START and
// GOAL, each copy of a piece placed on one standing a layer above it in
// its piece's color. From the original's voxelframe.cpp `showSingleShape`
// (the trays' gray, slidingColors::VIEW_GRAY, a gap of 4 units between
// them, the titles), which showed them in the Entities view. Both trays
// and the pieces are drawn here, from the engine's meshes, moved to where
// `sliding.tray` says the copies are.

import { Group, Vector3 } from 'three';

import { meshArrays } from '../../engine/frames.ts';
import type { MeshStyle } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { esc } from '../shell/dom.ts';
import { boardColor, hex, viewColor } from '../shell/piece-colors.ts';
import { borderPicture } from '../shell/piece-images.ts';
import { borderColor, borderRim, shownBorder } from '../shell/tray-border.ts';
import type { TrayCopy } from '../entities/tray.ts';
import { buildMesh } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';
import type { MeshCache } from './meshcache.ts';
import { SolutionScene } from './solution.ts';
import type { PiecePicture } from '../shell/picture-math.ts';
import type { ScenePiece, ScenePose, SolutionLook } from './solution.ts';
import { floorOfMesh, TrayBorderMesh } from './tray-border.ts';

/** The board and where the copies stand on it, on START and on GOAL. */
export interface TrayModel {
  tray: string;
  starts: readonly TrayCopy[];
  goals: readonly TrayCopy[];
}

/** The gap between the two trays, in cells (showSingleShape's). */
export const TRAY_GAP = 4;

/** What the trays ask of the view. */
export interface TraysHost {
  store: Store;
  cache: MeshCache;
  /** The revision a shape last changed at (its mesh is asked at it). */
  shapeRevision(shape: string): number;
  /** The style the view draws in. */
  style(): MeshStyle;
  /** Something changed: draw again. */
  changed(): void;
  failed(e: unknown): void;
}

export class TrayView {
  /** What is drawn, in the trays' cells: the view moves it as it frames them. */
  readonly group = new Group();
  private readonly host: TraysHost;
  private readonly scene = new SolutionScene();
  /** The trays' borders (decision 0046): round START and round GOAL. */
  private readonly border = new TrayBorderMesh();
  private lit = true;
  private model: TrayModel | null = null;
  /** The meshes this made (freed when replaced). */
  private built: BuiltMesh[] = [];
  private shownKey = '';
  private loads = 0;
  /** The tray shown: its size, or null when none is. */
  private size: readonly [number, number, number] | null = null;
  /** The room between the two trays, in cells: TRAY_GAP, more round a thick border. */
  private gap = TRAY_GAP;

  constructor(host: TraysHost, parent: Group) {
    this.host = host;
    this.group.add(this.scene.group);
    this.group.add(this.border.group);
    parent.add(this.group);
  }

  /** The space the view frames while two trays are shown (null: none). */
  get extent(): [number, number, number] | null {
    const s = this.size;
    return s === null ? null : [2 * s[0] + this.gap, s[1], s[2] + 1];
  }

  /** A piece's picture by its shape (the view's; null: none, or not decoded yet). */
  setPictureOf(of: (shape: string) => PiecePicture | null): void {
    this.scene.pictureOf = of;
  }

  /** Applies the pictures again (one was decoded). */
  applyPictures(): void {
    this.scene.applyPictures();
    this.applyBorder();
  }

  // the border's look, from the board as the snapshot has it now
  private applyBorder(): void {
    const store = this.host.store;
    const shapes = store.snapshot?.shapes ?? [];
    const i = shapes.findIndex((s) => s.id === this.model?.tray);
    const tray = shapes[i];
    const b = shownBorder(tray);
    if (tray === undefined || b === null || this.border.count === 0) {
      return;
    }
    this.border.setLook({
      color: borderColor(b, viewColor(tray, i)),
      material: b.material,
      lit: this.lit,
      picture: borderPicture(store.engine, tray),
    });
  }

  /** The borders drawn (the e2e tests read it): TrayBorderMesh.describe(). */
  describeBorder(): string {
    return this.border.describe();
  }

  /** Shows a board's trays (null: none). */
  set(model: TrayModel | null): void {
    this.model = model;
    this.load();
  }

  /** The look of the pieces (the view's). */
  setLook(look: SolutionLook): void {
    this.scene.setLook(look);
    this.lit = look.lit;
    this.applyBorder();
  }

  /** The lines' widths follow the viewport. */
  setResolution(w: number, h: number): void {
    this.scene.setResolution(w, h);
  }

  /** The style or a mesh may have changed: load again what differs. */
  load(): void {
    const store = this.host.store;
    const model = this.model;
    const tray = store.snapshot?.shapes.find((s) => s.id === model?.tray) ?? null;
    if (model === null || tray === null || store.snapshot?.grid !== 'sliding') {
      this.show(++this.loads, null, null, '');
      return;
    }
    const style = this.host.style();
    const gen = store.generation;
    const pieces = [...new Set([...model.starts, ...model.goals].map((c) => c.piece))];
    const shapes = [tray.id, ...pieces];
    // (the board's color with none of its own follows the theme)
    const key = `${hex(boardColor())}:${String(gen)}:${style}:${shapes.map((s) => `${s}@${String(this.host.shapeRevision(s))}`).join(',')}:${JSON.stringify(model.starts)}:${JSON.stringify(model.goals)}`;
    // a load in flight for another key is stale now, even when this one is shown already
    const n = ++this.loads;
    if (key === this.shownKey) {
      return;
    }
    const size = tray.size;
    Promise.all(
      shapes.map(async (s): Promise<[string, BuiltMesh, readonly number[]]> => {
        const rev = this.host.shapeRevision(s);
        const reply = await this.host.cache.get(s, style, rev);
        const ssize = store.snapshot?.shapes.find((x) => x.id === s)?.size ?? size;
        const built = buildMesh(meshArrays(reply), ssize, style, {
          kind: 'shape',
          shape: s,
          revision: rev,
          transformation: 0,
        });
        return [s, built, reply.lo];
      }),
    ).then(
      (list) => {
        if (n !== this.loads) {
          for (const [, b] of list) {
            disposeBuilt(b);
          }
          return;
        }
        this.show(n, model, list, key, size);
      },
      (e: unknown) => {
        if (n === this.loads) {
          this.host.failed(e);
        }
      },
    );
  }

  private show(
    n: number,
    model: TrayModel | null,
    meshes: [string, BuiltMesh, readonly number[]][] | null,
    key: string,
    size: readonly [number, number, number] | null = null,
  ): void {
    if (n !== this.loads) {
      return;
    }
    const old = this.built;
    this.built = meshes?.map((m) => m[1]) ?? [];
    this.shownKey = key;
    this.size = model === null ? null : size;
    if (model === null || meshes === null || size === null) {
      this.scene.set(null);
      this.border.set(null);
    } else {
      const store = this.host.store;
      const shapes = store.snapshot?.shapes ?? [];
      // the trays' borders, a layer higher than the floor: the pieces stand in them
      const b = shownBorder(shapes.find((x) => x.id === model.tray));
      // a thick border's rims, one on each tray, kept as far apart as thin ones
      this.gap = b !== null && b.thickness > 0 ? TRAY_GAP + 2 * b.thickness : TRAY_GAP;
      const shift = size[0] + this.gap;
      const pieces: ScenePiece[] = [];
      const poses: ScenePose[] = [];
      const pose = (t: [number, number, number]): ScenePose => ({
        t,
        q: [0, 0, 0, 1],
        visible: true,
        opacity: 1,
      });
      const [trayId, trayMesh] = meshes[0] ?? [];
      if (trayId !== undefined && trayMesh !== undefined) {
        // the START tray, then the GOAL tray
        for (const [piece, dx] of [
          [-1, 0],
          [-2, shift],
        ] as const) {
          pieces.push({
            piece,
            shape: trayId,
            color: viewColor(
              shapes.find((x) => x.id === trayId),
              0,
            ),
            mode: 'solid',
            built: trayMesh,
          });
          poses.push(pose([dx, 0, 0]));
        }
      }
      const meshOf = new Map(meshes.slice(1).map(([s, b, lo]) => [s, { b, lo }]));
      const place = (stamps: TrayModel['starts'], dx: number): void => {
        for (const { piece: id, cells } of stamps) {
          const m = meshOf.get(id);
          if (m === undefined || cells.length === 0) {
            continue;
          }
          const i = shapes.findIndex((s) => s.id === id);
          // the whole piece moved: its lowest corner onto the stamp's, standing
          // on the tray (a tray of layers is drawn whole, so the pieces stand
          // over it, each as many layers up as its stamp)
          const x = Math.min(...cells.map((c) => c[0])) - Math.round(m.lo[0] ?? 0);
          const y = Math.min(...cells.map((c) => c[1])) - Math.round(m.lo[1] ?? 0);
          const z = Math.min(...cells.map((c) => c[2])) - Math.round(m.lo[2] ?? 0);
          pieces.push({
            piece: pieces.length,
            shape: id,
            color: viewColor(shapes[i], i),
            mode: 'solid',
            built: m.b,
          });
          poses.push(pose([dx + x, y, size[2] + z]));
        }
      };
      place(model.starts, 0);
      place(model.goals, shift);
      this.border.set(
        b === null || trayMesh === undefined
          ? null
          : {
              size: [size[0], size[1]],
              floor: floorOfMesh(trayMesh.arrays.voxel, trayMesh.size),
              z0: 0,
              z1: size[2] + 1,
              round: b.corners === 'round',
              rim: borderRim(b),
              at: [
                [0, 0, 0],
                [shift, 0, 0],
              ],
            },
      );
      this.applyBorder();
      this.scene.set({ key, extent: [2 * size[0] + this.gap, size[1], size[2] + 1], pieces });
      this.scene.setPoses(poses);
    }
    for (const b of old) {
      disposeBuilt(b);
    }
    this.host.changed();
  }

  /**
   * The START and GOAL titles over the trays (showSingleShape's
   * setSpaceTitle), placed where `toScreen` puts a point of the model.
   */
  tags(model: Group, toScreen: (v: Vector3) => [number, number]): string {
    const s = this.size;
    if (s === null) {
      return '';
    }
    // clear of the trays' borders, when they have them
    const rim = this.border.count === 0 ? 0 : this.border.rim;
    const at = (x: number): [number, number] =>
      toScreen(model.localToWorld(new Vector3(x, s[1] + 0.7 + rim, s[2] + 1)));
    const [ax, ay] = at(s[0] / 2);
    const [bx, by] = at(s[0] + this.gap + s[0] / 2);
    const tag = (t: string, x: number, y: number): string =>
      `<span style="left:${x.toFixed(1)}px;top:${y.toFixed(1)}px">${esc(t)}</span>`;
    return tag('START', ax, ay) + tag('GOAL', bx, by);
  }

  /** What is drawn (the e2e tests read it): piece:shape:mode:on per mesh. */
  describe(): string {
    return this.scene.describe();
  }

  /** Where the meshes stand (the e2e tests read it). */
  posesShown(): string {
    return this.scene.posesShown();
  }
}

function disposeBuilt(b: BuiltMesh): void {
  b.faces.dispose();
  b.marks?.dispose();
}
