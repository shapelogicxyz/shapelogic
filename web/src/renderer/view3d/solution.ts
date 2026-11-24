// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver workspace's scene (FRONTEND-PLAN.md, stage 4): the pieces of
// a solution, each its own mesh in its own color (the original's
// pieceColor of the shape, jittered per copy), placed by a pose: the
// assembled view (the mesh of the placement's transformation at the cell
// minus its hotspot) or the player's poses (playback.poses, interpolated).
// The coordinates are mesh units centerd on the result, as the poses are,
// so the scene sits at the turned root's origin. A piece is shown solid
// (the view's style), as a wire frame (the wire-frame mesh) or not at all,
// as the mockup's piece chips cycle; with x-ray ("show through solid
// pieces") the wire frames are drawn over everything, as the mockup's
// renderSolverScene drew them in its second pass. A piece being taken
// away fades (the setting) through its opacity. From docs/mockup/index.html:
// `renderSolverScene` (what is drawn, the two passes); the drawing is
// three.js over the engine's meshes (mesh.ts, material.ts), as the other
// scenes'. A sliding puzzle's tray has its walls drawn around its floor
// (walls.ts). A piece's label (S1, S1.2), its name from the Shapes tab
// (cut to the setting's characters for each voxel of the piece's width
// there, or all of it in lines that wide), or both ("S1 - name") can be
// drawn on it: a sprite, facing the eye however the view turns, a white
// box with black text of a fixed size on the screen, over everything, in
// the middle of the piece's widest layer, counted up from the bottom of
// the home view (the lowest of the widest; on the piece even when it is
// hollow).

import { Box3, Group, Mesh, MeshBasicMaterial, Quaternion, Vector2, Vector3, Vector4 } from 'three';
import type { BufferGeometry, Raycaster, Sprite } from 'three';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';

import type { BoardLayout, MeshStyle } from '../../engine/api.generated.ts';
import type { Rgb3 } from '../shell/piece-colors.ts';
import { LABEL_LINE_PER_VOXEL } from '../shell/prefs.ts';
import { faceMaterial, faceUniforms, setPalette, setPicture, setPiece } from './material.ts';
import type { PiecePicture } from '../shell/picture-math.ts';
import type { FaceUniforms } from './material.ts';
import { hasChecker } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';

/** How a piece is shown (the piece chips: solid → wire frame → hidden). */
export type PieceMode = 'solid' | 'wire' | 'hidden';

/** A piece of the solution shown. */
export interface ScenePiece {
  /** The problem's piece number. */
  piece: number;
  shape: string;
  color: Rgb3;
  mode: PieceMode;
  /** Its mesh in the view's style (solid) or the wire-frame style (wire); null when hidden. */
  built: BuiltMesh | null;
  /** Its label (S1, S1.2), drawn on it when labels are shown; absent for none. */
  label?: string;
  /** Its shape's name from the Shapes tab, drawn on it when names are shown; absent or empty for none. */
  name?: string;
}

/** What a piece's sprite says: its label (S1), its name, or both. */
export interface LabelShow {
  label: boolean;
  name: boolean;
  /**
   * The characters of the name drawn for each voxel of the piece's width
   * where its label sits, the rest cut off; 0 all of it, a line every
   * LABEL_LINE_PER_VOXEL per voxel.
   */
  chars: number;
  /** The scene's axis that is up in the home view: 1 (+Y) or 2 (+Z); X is across. */
  up: 1 | 2;
}

/**
 * The text drawn on a piece `width` voxels wide where its label sits:
 * "S2", "name" (its first `show.chars` characters per voxel), "S2 - name",
 * or '' for none; with `chars` 0 the whole name, a line ("\n") every
 * LABEL_LINE_PER_VOXEL characters per voxel.
 */
export function pieceText(
  p: { label?: string; name?: string },
  show: Pick<LabelShow, 'label' | 'name' | 'chars'>,
  width = 1,
): string {
  const label = show.label ? (p.label ?? '') : '';
  const voxels = Math.max(1, Math.round(width));
  // by code point, so that an emoji is not split
  const chars = Array.from(p.name ?? '');
  let name = '';
  if (show.name && show.chars > 0) {
    name = chars
      .slice(0, show.chars * voxels)
      .join('')
      .trim();
  } else if (show.name) {
    const lines: string[] = [];
    const each = LABEL_LINE_PER_VOXEL * voxels;
    for (let i = 0; i < chars.length; i += each) {
      const line = chars
        .slice(i, i + each)
        .join('')
        .trim();
      if (line !== '') {
        lines.push(line);
      }
    }
    name = lines.join('\n');
  }
  return label !== '' && name !== '' ? `${label} - ${name}` : label !== '' ? label : name;
}

/** What the scene shows; `key` names it (the e2e tests read it). */
export interface SolutionModel {
  key: string;
  /** The result's extent in mesh units (the view is framed on it). */
  extent: [number, number, number];
  pieces: ScenePiece[];
  /** The result shape's id (a sliding puzzle's tray: its walls are drawn); absent for none. */
  result?: string | null;
  /** A stacking puzzle's board: its pegs are drawn around the discs, centerd as the poses. */
  board?: { layout: BoardLayout; center: [number, number, number] } | null;
}

/** Where a piece is: translation, rotation (xyzw), shown, opacity. */
export interface ScenePose {
  t: readonly [number, number, number];
  q: readonly [number, number, number, number];
  visible: boolean;
  opacity: number;
}

/** How the pieces are drawn. */
export interface SolutionLook {
  style: MeshStyle;
  colorView: boolean;
  lit: boolean;
  /** Wire frames over the solid pieces (the Display menu's x-ray). */
  xray: boolean;
  palette: readonly string[];
}

interface PieceDraw {
  piece: ScenePiece;
  group: Group;
  uniforms: FaceUniforms;
  faces: Mesh | null;
  edges: LineSegments2 | null;
  edgeGeometry: LineSegmentsGeometry | null;
  marks: Mesh | null;
  /** The label's sprite, made when labels are first shown; null: none yet. */
  label: Sprite | null;
  /** The text its sprite says ('' for none). */
  text: string;
  /** Where its label goes and how wide the piece is there, found when first needed; null: not yet. */
  spot: LabelSpot | null;
}

/** Makes a label's sprite (labels.ts); null: none can be drawn. */
export type LabelMaker = (text: string) => Sprite | null;

export class SolutionScene {
  readonly group = new Group();
  private readonly shared: FaceUniforms = faceUniforms();
  private readonly markMaterial = new MeshBasicMaterial({ color: 0x000000 });
  private readonly edgeMaterial = new LineMaterial({ color: 0x000000, linewidth: 2 });
  /** The walls' lines (drawSlidingWalls: black, 5 pixels; white on the dark theme, setDark). */
  private readonly wallMaterial = new LineMaterial({ color: 0x000000, linewidth: 5 });
  private walls: LineSegments2 | null = null;
  /** Whether the walls' lines are drawn (not while the tray's border shows the walls). */
  private wallsShown = true;
  private draws: PieceDraw[] = [];
  private look: SolutionLook = {
    style: 'drawing',
    colorView: false,
    lit: true,
    xray: false,
    palette: [],
  };
  private poses: ScenePose[] = [];
  private labels: LabelShow = { label: false, name: false, chars: LABEL_LINE_PER_VOXEL, up: 1 };
  /** Pixels per mesh unit at the scene's plane (the labels' size). */
  private pixelsPerUnit = 1;
  /** Null: this scene draws no labels (the board's and the trays'). */
  private readonly makeLabel: LabelMaker | null;

  constructor(makeLabel: LabelMaker | null = null) {
    this.makeLabel = makeLabel;
    this.edgeMaterial.resolution = new Vector2(1, 1);
    this.wallMaterial.resolution = new Vector2(1, 1);
  }

  /** The walls' color for the theme: black on the light canvas, white on the dark one. */
  setDark(dark: boolean): void {
    this.wallMaterial.color.setHex(dark ? 0xffffff : 0x000000);
  }

  /** The walls' color now, as #rrggbb (the tests read it). */
  get wallColor(): string {
    return `#${this.wallMaterial.color.getHexString()}`;
  }

  /** A sliding tray's walls: line segments (x,y,z pairs) in the scene's frame; null: none. */
  setWalls(segments: Float32Array | null): void {
    if (this.walls !== null) {
      this.group.remove(this.walls);
      this.walls.geometry.dispose();
      this.walls = null;
    }
    if (segments === null || segments.length === 0) {
      return;
    }
    this.walls = new LineSegments2(
      new LineSegmentsGeometry().setPositions(segments),
      this.wallMaterial,
    );
    this.walls.visible = this.wallsShown;
    this.group.add(this.walls);
  }

  /** Draws the walls' lines or not (the tray's border, when shown, stands for them). */
  showWalls(on: boolean): void {
    this.wallsShown = on;
    if (this.walls !== null) {
      this.walls.visible = on;
    }
  }

  /** How many wall segments are drawn (the e2e tests read it). */
  get wallCount(): number {
    return this.walls === null ? 0 : (this.walls.geometry.attributes.instanceStart?.count ?? 0);
  }

  /** A sliding piece's picture by its shape, set by the view (null: none, or not decoded yet). */
  pictureOf: (shape: string) => PiecePicture | null = () => null;

  /** Applies the pictures again (one was decoded, or the document changed). */
  applyPictures(): void {
    for (const d of this.draws) {
      setPicture(d.uniforms, d.piece.mode === 'wire' ? null : this.pictureOf(d.piece.shape));
    }
  }

  /** Shows a model (null: nothing); the meshes belong to the caller. */
  set(model: SolutionModel | null): void {
    this.clear();
    if (model === null) {
      return;
    }
    for (const piece of model.pieces) {
      const uniforms: FaceUniforms = {
        ...this.shared,
        uPiece: { value: new Vector3(1, 1, 1) },
        uChecker: { value: 1 },
        uAlpha: { value: 1 },
        // a picture of its own
        uImage: { value: null },
        uImageOn: { value: 0 },
        uImageSrc: { value: new Vector4(0, 0, 1, 1) },
      };
      setPiece(uniforms, piece.color);
      setPicture(uniforms, piece.mode === 'wire' ? null : this.pictureOf(piece.shape));
      const group = new Group();
      const d: PieceDraw = {
        piece,
        group,
        uniforms,
        faces: null,
        edges: null,
        edgeGeometry: null,
        marks: null,
        label: null,
        text: '',
        spot: null,
      };
      const b = piece.built;
      if (b !== null && piece.mode !== 'hidden') {
        const m = faceMaterial(uniforms, 'normal');
        d.faces = new Mesh(b.faces, m);
        d.faces.userData.faces = true;
        group.add(d.faces);
        if (b.marks !== null) {
          d.marks = new Mesh(b.marks, this.markMaterial);
          group.add(d.marks);
        }
        if (b.edges !== null) {
          d.edgeGeometry = new LineSegmentsGeometry().setPositions(b.edges);
          d.edges = new LineSegments2(d.edgeGeometry, this.edgeMaterial);
          group.add(d.edges);
        }
      }
      group.visible = false;
      this.group.add(group);
      this.draws.push(d);
    }
    this.applyLook();
    this.applyLabels();
    this.setPoses(this.poses);
  }

  /** Shows or hides the pieces' labels and names. */
  setLabels(show: LabelShow): void {
    if (show.up !== this.labels.up) {
      // the widest layer is counted from another bottom
      for (const d of this.draws) {
        this.dropLabel(d);
        d.spot = null;
      }
    }
    this.labels = { ...show };
    this.applyLabels();
  }

  /** The view's scale, pixels per mesh unit at the scene's plane: the labels keep their size on the screen. */
  setLabelScale(pixelsPerUnit: number): void {
    this.pixelsPerUnit = pixelsPerUnit;
    for (const d of this.draws) {
      this.scaleLabel(d);
    }
  }

  /** The labels drawn now (the e2e tests read it): piece:text, of the pieces shown. */
  labelsShown(): string {
    return this.draws
      .filter((d) => d.label?.visible === true && d.group.visible)
      .map((d) => `${String(d.piece.piece)}:${d.text}`)
      .join(' ');
  }

  /** Places the pieces (a pose per piece of the model, in its order). */
  setPoses(poses: readonly ScenePose[]): void {
    this.poses = [...poses];
    const q = new Quaternion();
    this.draws.forEach((d, i) => {
      const p = poses[i];
      if (p === undefined || d.faces === null) {
        d.group.visible = false;
        return;
      }
      d.group.visible = p.visible && p.opacity > 0.001;
      d.group.position.set(p.t[0], p.t[1], p.t[2]);
      d.group.quaternion.copy(q.set(p.q[0], p.q[1], p.q[2], p.q[3]));
      this.setOpacity(d, p.opacity);
    });
  }

  /** Applies a look; nothing is rebuilt. */
  setLook(look: SolutionLook): void {
    this.look = look;
    this.applyLook();
  }

  /** The viewport's size in pixels, for the lines' widths. */
  setResolution(w: number, h: number): void {
    this.edgeMaterial.resolution.set(w, h);
    this.wallMaterial.resolution.set(w, h);
  }

  /** The piece a ray meets first (its faces, shown; null: none). */
  pick(raycaster: Raycaster): number | null {
    // a piece, not the result an explorer draws around them (piece -1)
    const shown = this.draws.filter(
      (d) => d.faces !== null && d.group.visible && d.piece.piece >= 0,
    );
    const hit = raycaster.intersectObjects(
      shown.flatMap((d) => (d.faces === null ? [] : [d.faces])),
      false,
    )[0];
    return shown.find((d) => d.faces === hit?.object)?.piece.piece ?? null;
  }

  /** The center of a piece's mesh as drawn, in world space (null: not drawn). */
  centerOf(piece: number): Vector3 | null {
    const d = this.draws.find((x) => x.piece.piece === piece);
    if (d?.faces == null || !d.group.visible) {
      return null;
    }
    d.faces.geometry.computeBoundingBox();
    const box = d.faces.geometry.boundingBox ?? new Box3();
    d.group.updateWorldMatrix(true, false);
    return box.getCenter(new Vector3()).applyMatrix4(d.group.matrixWorld);
  }

  /** The pieces drawn now (the e2e tests read it): piece:shape:mode, then shown or not. */
  describe(): string {
    return this.draws
      .map(
        (d) =>
          `${String(d.piece.piece)}:${d.piece.shape}:${d.piece.mode}:${d.group.visible ? 'on' : 'off'}`,
      )
      .join(' ');
  }

  /** The poses shown (the e2e tests compare them with the engine's samples). */
  posesShown(): string {
    return this.poses
      .map(
        (p) =>
          `${p.t.map((v) => v.toFixed(4)).join(',')};${p.q.map((v) => v.toFixed(4)).join(',')};${p.visible ? 1 : 0}`,
      )
      .join(' ');
  }

  private applyLook(): void {
    const u = this.shared;
    const look = this.look;
    setPalette(u, look.palette);
    u.uColorView.value = look.colorView ? 1 : 0;
    u.uLit.value = look.lit ? 1 : 0;
    for (const d of this.draws) {
      const wire = d.piece.mode === 'wire';
      // the checker is the bevelled style's and the wire frame's
      d.uniforms.uChecker.value = wire || hasChecker(look.style) ? 1 : 0;
      if (d.faces !== null) {
        const m = d.faces.material as ReturnType<typeof faceMaterial>;
        // wire frames over the solid pieces with x-ray (the mockup's second pass)
        m.depthTest = !(wire && look.xray);
        d.faces.renderOrder = wire && look.xray ? 5 : 0;
      }
    }
  }

  // the labels made when first shown, for the pieces drawn that have one;
  // a label whose text changed (the other part turned on or off) made again
  private applyLabels(): void {
    const shown = this.labels.label || this.labels.name;
    for (const d of this.draws) {
      if (shown && d.spot === null && d.faces !== null && this.makeLabel !== null) {
        d.spot = labelSpot(d.faces.geometry, this.labels.up);
      }
      const text = pieceText(d.piece, this.labels, d.spot?.width ?? 1);
      if (d.label !== null && text !== '' && text !== d.text) {
        this.dropLabel(d);
      }
      if (d.label === null && d.faces !== null && d.spot !== null && text !== '') {
        d.label = this.makeLabel?.(text) ?? null;
        if (d.label !== null) {
          d.text = text;
          d.label.position.copy(d.spot.at);
          this.scaleLabel(d);
          this.setOpacity(d, d.uniforms.uAlpha.value);
          d.group.add(d.label);
        }
      }
      if (d.label !== null) {
        d.label.visible = text !== '';
      }
    }
  }

  private dropLabel(d: PieceDraw): void {
    if (d.label !== null) {
      d.group.remove(d.label);
      d.label.material.map?.dispose();
      d.label.material.dispose();
      d.label = null;
      d.text = '';
    }
  }

  private scaleLabel(d: PieceDraw): void {
    const l = d.label;
    if (l !== null) {
      // its height in pixels, as made
      const h = (l.userData.pixels as number) / Math.max(1e-6, this.pixelsPerUnit);
      l.scale.set(h * (l.userData.aspect as number), h, 1);
    }
  }

  private setOpacity(d: PieceDraw, opacity: number): void {
    const o = Math.max(0, Math.min(1, opacity));
    d.uniforms.uAlpha.value = o;
    if (d.label !== null) {
      // a piece fading away takes its label with it
      d.label.material.opacity = o;
    }
    if (d.faces !== null) {
      const m = d.faces.material as ReturnType<typeof faceMaterial>;
      const fading = o < 0.999;
      if (m.transparent !== fading) {
        m.transparent = fading;
        m.depthWrite = !fading;
        m.needsUpdate = true;
      }
    }
  }

  private clear(): void {
    for (const d of this.draws) {
      this.group.remove(d.group);
      if (d.faces !== null) {
        (d.faces.material as ReturnType<typeof faceMaterial>).dispose();
      }
      d.edgeGeometry?.dispose();
      this.dropLabel(d);
    }
    this.draws = [];
  }

  dispose(): void {
    this.clear();
    this.setWalls(null);
    this.wallMaterial.dispose();
    this.markMaterial.dispose();
    this.edgeMaterial.dispose();
    this.shared.uPalette.value.dispose();
  }
}

/** Where a piece's label goes, in its mesh's frame, and how many voxels wide the piece is there. */
export interface LabelSpot {
  at: Vector3;
  width: number;
}

/** A cell's box in a mesh's frame. */
interface CellBox {
  min: [number, number, number];
  max: [number, number, number];
  /** Its box's middle. */
  mid: [number, number, number];
}

/** The middle value of some numbers (0 for none). */
function median(values: number[]): number {
  if (values.length === 0) {
    return 0;
  }
  const v = [...values].sort((a, b) => a - b);
  return v[Math.floor(v.length / 2)] ?? 0;
}

/** An unbroken run of cells across a layer: its cells, its extent across, its width in cells. */
interface Run {
  cells: CellBox[];
  lo: number;
  hi: number;
  width: number;
}

/**
 * A layer's runs across, left to right: boxes that touch or overlap (within
 * half a cell: a bevel or a gap between a piece's cells is less, a missing
 * cell more), each run's width the cells side by side in it (their
 * middles across, one for each column, whatever their depth).
 */
function runsAcross(layer: CellBox[], sizeX: number): Run[] {
  const sorted = [...layer].sort((a, b) => a.min[0] - b.min[0]);
  const runs: Run[] = [];
  for (const b of sorted) {
    const run = runs[runs.length - 1];
    if (run !== undefined && b.min[0] <= run.hi + 0.5 * sizeX) {
      run.cells.push(b);
      run.hi = Math.max(run.hi, b.max[0]);
    } else {
      runs.push({ cells: [b], lo: b.min[0], hi: b.max[0], width: 1 });
    }
  }
  for (const run of runs) {
    const xs = run.cells.map((b) => b.mid[0]).sort((a, b) => a - b);
    for (let i = 1; i < xs.length; i++) {
      if ((xs[i] ?? 0) - (xs[i - 1] ?? 0) > 0.25 * sizeX) {
        run.width++;
      }
    }
  }
  return runs;
}

/**
 * Where a piece's label goes, in its mesh's frame: in the middle of the
 * piece's widest layer, scanning up (`up`: the axis that is up in the home
 * view, 1 or 2; X is across) from the bottom, the lowest layer when several
 * are as wide. A layer is the cells whose boxes' middles lie at one height;
 * its width is its longest unbroken run of cells across (a gap of a cell
 * breaks it, so the label sits on the piece even when the layer has a
 * hole), counted in cells side by side, whatever their depth. The label
 * goes on the cell of that run nearest its middle, at the middle across,
 * the front one where two are as near. Grids whose cells do not line up in
 * rows (the spheres, the rhombic ones) are measured as well as their boxes
 * allow.
 */
export function labelSpot(faces: BufferGeometry, up: 1 | 2): LabelSpot {
  const pos = faces.getAttribute('position');
  const cell = faces.getAttribute('aCell') as typeof pos | undefined;
  const cells = new Map<string, CellBox>();
  const p: [number, number, number] = [0, 0, 0];
  for (let i = 0; i < pos.count; i++) {
    p[0] = pos.getX(i);
    p[1] = pos.getY(i);
    p[2] = pos.getZ(i);
    const k =
      cell === undefined
        ? ''
        : `${String(cell.getX(i))},${String(cell.getY(i))},${String(cell.getZ(i))}`;
    const c = cells.get(k);
    if (c === undefined) {
      cells.set(k, { min: [p[0], p[1], p[2]], max: [p[0], p[1], p[2]], mid: [0, 0, 0] });
    } else {
      for (let a = 0; a < 3; a++) {
        c.min[a] = Math.min(c.min[a] ?? 0, p[a] ?? 0);
        c.max[a] = Math.max(c.max[a] ?? 0, p[a] ?? 0);
      }
    }
  }
  const boxes = [...cells.values()];
  if (boxes.length === 0) {
    return { at: new Vector3(), width: 1 };
  }
  for (const b of boxes) {
    b.mid = [(b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2, (b.min[2] + b.max[2]) / 2];
  }
  const depth = up === 1 ? 2 : 1;
  // toward the eye in the home view: +Z with Z Toward, -Y with Z Up
  const toward = up === 1 ? 1 : -1;
  // a cell's size across and up, for the tolerances
  const sizeX = Math.max(1e-6, median(boxes.map((b) => b.max[0] - b.min[0])));
  const sizeUp = Math.max(1e-6, median(boxes.map((b) => b.max[up] - b.min[up])));

  // the layers, from the bottom up
  boxes.sort((a, b) => a.mid[up] - b.mid[up]);
  const layers: CellBox[][] = [];
  for (const b of boxes) {
    const layer = layers[layers.length - 1];
    const first = layer?.[0];
    if (layer !== undefined && first !== undefined && b.mid[up] - first.mid[up] < 0.25 * sizeUp) {
      layer.push(b);
    } else {
      layers.push([b]);
    }
  }

  let best: Run | null = null;
  for (const layer of layers) {
    for (const run of runsAcross(layer, sizeX)) {
      // only a wider one replaces it: the lowest layer, then the leftmost run
      if (best === null || run.width > best.width) {
        best = run;
      }
    }
  }
  if (best === null) {
    return { at: new Vector3(), width: 1 };
  }
  const middle = (best.lo + best.hi) / 2;
  // the cell nearest the middle across, the front one of two as near
  let on = best.cells[0] ?? boxes[0];
  let near = Infinity;
  for (const b of best.cells) {
    const d = Math.abs(b.mid[0] - middle);
    const front = on !== undefined && toward * (b.mid[depth] - on.mid[depth]) > 0;
    if (d < near - 1e-6 || (d <= near + 1e-6 && front)) {
      near = Math.min(near, d);
      on = b;
    }
  }
  const at = new Vector3(middle, 0, 0);
  at.setComponent(up, on?.mid[up] ?? 0);
  at.setComponent(depth, on?.mid[depth] ?? 0);
  return { at, width: best.width };
}
