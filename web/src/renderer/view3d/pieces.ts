// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Puzzle workspace's pieces scene (FRONTEND-PLAN.md, stage 3): the
// result, the selected piece and every copy of every piece, laid out on
// the screen as the mockup's (puzzle/layout.ts), each turning about its
// own centre with the camera's orientation while the layout stays put;
// pan and zoom move the whole layout. One shape.mesh per shape: its
// geometry is shared by all its copies (one object each); the optional
// copies are drawn faded: their nearest surface only (a depth pass
// first), as the mockup drew the front faces of a copy at 0.42; blended
// on both sides through every bevel they would look solid. The labels and the selection boxes are HTML
// over the canvas, and a click picks the smallest box under it (the
// result is not picked). From docs/mockup/index.html: `renderPuzzleScene`
// (the scale, the anchors fixed on the screen, the boxes centred on them
// so they do not jitter while turning, the labels, the faded and dashed
// optional copies) and `puzPick`; the drawing is three.js over the
// engine's meshes, as the 3D view's (mesh.ts, material.ts), not the
// mockup's projected cubes.

import { DoubleSide, Group, Mesh, MeshBasicMaterial, Quaternion, Vector2, Vector3 } from 'three';
import type { Camera as Camera3 } from 'three';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';

import type { MeshStyle } from '../../engine/api.generated.ts';
import { arrange, label, RESULT_SIZE } from '../puzzle/layout.ts';
import type { SceneItem } from '../puzzle/layout.ts';
import { esc } from '../shell/dom.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { faceMaterial, faceUniforms, setPalette, setPiece } from './material.ts';
import type { FaceUniforms } from './material.ts';
import { hasChecker } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';

/** What the scene shows; `key` names it (the e2e tests wait for it). */
export interface PiecesModel {
  key: string;
  items: SceneItem[];
  /** Copies left out (puzzle/layout.ts's MAX_COPIES). */
  hidden: number;
  /** The selected piece (its copies get a box), or null. */
  selected: string | null;
  /** The shapes' labels (S1, S2...). */
  names: ReadonlyMap<string, string>;
  /** How much larger than the layout's own size each piece is drawn, in its place (1 when left out). */
  grow?: number;
}

/** How the pieces are drawn. */
export interface PiecesLook {
  style: MeshStyle;
  colourView: boolean;
  lit: boolean;
  palette: readonly string[];
  /** A shape's piece colour. */
  colourOf(shape: string): Rgb3;
}

/** A shape's mesh with its centre and size (from the frame's lo and hi). */
export interface PieceMesh {
  built: BuiltMesh;
  centre: [number, number, number];
  /** Half the diagonal of its box: no turn takes it further from its centre. */
  radius: number;
  /** Half its box's size in X, Y and Z. */
  half: [number, number, number];
}

/** The opacity of an optional copy (the mockup's 0.42). */
const FADED = 0.42;
/** The mockup's units per piece: its pieces are about this wide. */
const UNIT = 4.4;

interface ShapeDraw {
  uniforms: FaceUniforms;
  normal: ReturnType<typeof faceMaterial>;
  faded: ReturnType<typeof faceMaterial>;
  edges: LineSegmentsGeometry | null;
}

interface ItemDraw {
  item: SceneItem;
  group: Group;
  /** Its scale before the zoom. */
  size: number;
  /** Half the larger side of its outline at the home view, at scale 1. */
  half: number;
  /** Its anchor in the layout's world units. */
  at: Vector3;
  box: HTMLElement;
  /** Where its box is on the screen (CSS pixels of the viewport). */
  rect: { x0: number; y0: number; x1: number; y1: number };
}

export class PiecesScene {
  readonly group = new Group();
  private readonly shared: FaceUniforms = faceUniforms();
  private readonly markMaterial = new MeshBasicMaterial({ color: 0x000000 });
  /** The faded copies' depth pass: their nearest surface, offset as the faces' (material.ts). */
  private readonly depthOnly = new MeshBasicMaterial({
    colorWrite: false,
    side: DoubleSide,
    polygonOffset: true,
    polygonOffsetFactor: 1,
    polygonOffsetUnits: 1,
  });
  private readonly markFaded = new MeshBasicMaterial({
    color: 0x000000,
    transparent: true,
    opacity: FADED,
    depthWrite: false,
  });
  private readonly edgeMaterial = new LineMaterial({ color: 0x000000, linewidth: 2 });
  private readonly edgeFaded = new LineMaterial({
    color: 0x000000,
    linewidth: 1.5,
    transparent: true,
    opacity: FADED,
  });
  private readonly overlay: HTMLElement;
  private shapes = new Map<string, ShapeDraw>();
  private items: ItemDraw[] = [];
  private hover: string | null = null;
  private selected: string | null = null;
  /** The layout's size and the scale from the mockup's units to world units. */
  private bounds = { w: 1, h: 1, k: 1 };
  /** The items laid out (their own copies, moved by `arrange`), and the copies a row now. */
  private laid: SceneItem[] = [];
  private cols = 5;
  /** How much larger than its place each piece is drawn (PiecesModel.grow). */
  private grow = 1;
  /** The view's zoom: each piece grows or shrinks where it stands. */
  private zoom = 1;
  /** How much further the copies are from the result, in world units (DROP_PX on the screen). */
  private drop = 0;

  constructor(overlay: HTMLElement) {
    this.overlay = overlay;
    this.edgeMaterial.resolution = new Vector2(1, 1);
    this.edgeFaded.resolution = new Vector2(1, 1);
  }

  /** The items drawn now, with what is drawn for them (the e2e tests read it). */
  describe(): string {
    return this.items
      .map((d) => {
        const faces = d.group.children[0]?.children.find((o) => o.userData.faces === true);
        const faded =
          faces instanceof Mesh &&
          (faces.material as { uniforms?: FaceUniforms }).uniforms?.uAlpha.value !== 1;
        return `${d.item.role}:${d.item.shape}:${String(d.item.copy)}/${String(d.item.total)}${faded ? ':faded' : ''}`;
      })
      .join(' ');
  }

  /** The number of objects whose faces are drawn: one per item. */
  get meshCount(): number {
    let n = 0;
    this.group.traverse((o) => {
      if (o instanceof Mesh && o.userData.faces === true) {
        n++;
      }
    });
    return n;
  }

  /**
   * Shows a model: `meshes` holds the mesh of every shape it names. The
   * shapes' materials are kept while their shapes stay; the geometry
   * belongs to the caller. `home` is the view's home orientation: each
   * piece is sized by its outline seen so, which it fills its place with.
   */
  set(
    model: PiecesModel,
    meshes: ReadonlyMap<string, PieceMesh>,
    look: PiecesLook,
    home: Quaternion = new Quaternion(),
  ): void {
    this.clearItems();
    this.selected = model.selected;
    const used = new Set(model.items.map((i) => i.shape));
    for (const [shape, d] of [...this.shapes]) {
      if (!used.has(shape) || !meshes.has(shape)) {
        this.disposeShape(d);
        this.shapes.delete(shape);
      }
    }
    // the scale: the largest piece's outline at the home view takes the
    // mockup's four units; the result, whatever its size, RESULT_SIZE times
    // that (with no pieces, its own)
    const outline = new Map<string, number>();
    for (const [shape, m] of meshes) {
      outline.set(shape, outlineOf(m, home));
    }
    let big = 0;
    for (const it of model.items) {
      if (it.role !== 'result') {
        big = Math.max(big, outline.get(it.shape) ?? 0);
      }
    }
    const r = model.items.find((i) => i.role === 'result');
    const rr = r === undefined ? 0 : (outline.get(r.shape) ?? 0);
    if (big === 0) {
      big = rr;
    }
    const k = Math.max(big, 1) / UNIT;
    this.grow = model.grow ?? 1;
    this.laid = model.items.map((i) => ({ ...i, pos: [i.pos[0], i.pos[1]] }));
    this.cols = 5;
    arrange(this.laid, this.cols);
    const b = this.boundsOf(this.laid);
    this.bounds = { w: b.w, h: b.h, k };
    const frag = document.createDocumentFragment();
    for (const item of this.laid) {
      const m = meshes.get(item.shape);
      if (m === undefined) {
        continue;
      }
      const d = this.shapeDraw(item.shape, m);
      const group = new Group();
      const inner = new Group();
      inner.position.set(-m.centre[0], -m.centre[1], -m.centre[2]);
      group.add(inner);
      if (item.optional) {
        inner.add(new Mesh(m.built.faces, this.depthOnly));
      }
      const faces = new Mesh(m.built.faces, item.optional ? d.faded : d.normal);
      faces.userData.faces = true;
      faces.renderOrder = item.optional ? 2 : 0;
      inner.add(faces);
      if (m.built.marks !== null) {
        const marks = new Mesh(m.built.marks, item.optional ? this.markFaded : this.markMaterial);
        marks.renderOrder = item.optional ? 2 : 0;
        inner.add(marks);
      }
      if (d.edges !== null) {
        inner.add(new LineSegments2(d.edges, item.optional ? this.edgeFaded : this.edgeMaterial));
      }
      const at = new Vector3();
      // the result fills its larger place, the pieces keep their sizes to each other
      const size =
        this.grow * (item.role === 'result' && rr > 0 ? (RESULT_SIZE * Math.max(big, 1)) / rr : 1);
      group.scale.setScalar(size * this.zoom);
      this.group.add(group);
      const box = document.createElement('div');
      box.className = `pzbox ${item.role}${item.optional ? ' opt' : ''}`;
      box.dataset.shape = item.shape;
      box.dataset.role = item.role;
      box.innerHTML = `<span class="pzlab">${esc(label(item, model.names.get(item.shape) ?? item.shape))}</span>`;
      frag.appendChild(box);
      this.items.push({
        item,
        group,
        size,
        half: (outline.get(item.shape) ?? 2 * m.radius) / 2,
        at,
        box,
        rect: { x0: 0, y0: 0, x1: 0, y1: 0 },
      });
    }
    this.overlay.replaceChildren(frag);
    this.position();
    this.setLook(look);
    this.renderBoxes();
  }

  /** Applies a look to the shapes' materials. */
  setLook(look: PiecesLook): void {
    const u = this.shared;
    setPalette(u, look.palette);
    u.uColourView.value = look.colourView ? 1 : 0;
    u.uLit.value = look.lit ? 1 : 0;
    u.uChecker.value = hasChecker(look.style) ? 1 : 0;
    for (const [shape, d] of this.shapes) {
      setPiece(d.uniforms, look.colourOf(shape));
    }
  }

  /** The viewport's size in pixels, for the lines' widths. */
  setResolution(w: number, h: number): void {
    this.edgeMaterial.resolution.set(w, h);
    this.edgeFaded.resolution.set(w, h);
  }

  /**
   * The scale (pixels per world unit) for a viewport, the
   * mockup's `sc`, and the layout's largest extent in world units
   * (`tight`: less margin above and below).
   */
  scale(w: number, h: number, zoom: number, tight = false): { sc: number; reach: number } {
    // a band under a sliding board's trays keeps rows of five (its height
    // follows the layout); the whole view takes the rows that show the
    // pieces largest
    this.layOut(tight ? 5 : this.bestCols(w, h));
    this.setZoom(zoom);
    const { w: lw, h: lh, k } = this.bounds;
    // the copies DROP_PX lower under the result, the same pixels at any
    // size: the layout fits the height less that, the top row moved up
    // half of it and the copies down the other half
    const dropPx = !tight && this.laid.some((i) => i.role === 'result') ? DROP_PX : 0;
    const sc =
      Math.min(w / ((lw + PAD_X) * k), Math.max(1, h - dropPx) / ((lh + padY(tight)) * k)) * 0.92 ||
      1;
    const drop = dropPx / sc;
    if (Math.abs(drop - this.drop) > 1e-9) {
      this.drop = drop;
      this.position();
    }
    return { sc, reach: Math.hypot(lw, lh) * k * Math.max(1, zoom) };
  }

  // the zoom: each piece scaled where it stands, the layout kept
  private setZoom(zoom: number): void {
    if (zoom === this.zoom) {
      return;
    }
    this.zoom = zoom;
    for (const d of this.items) {
      d.group.scale.setScalar(d.size * zoom);
    }
  }

  // the copies a row that give the largest scale in a viewport
  private bestCols(w: number, h: number): number {
    const n = this.laid.filter((i) => i.role === 'piece').length;
    let best = this.cols;
    let most = 0;
    const trial = this.laid.map((i) => ({ ...i }));
    for (let c = 1; c <= Math.max(1, Math.min(n, 12)); c++) {
      arrange(trial, c);
      const b = this.boundsOf(trial);
      const sc = Math.min(w / (b.w + PAD_X), h / (b.h + padY(false)));
      // a wider row only for a clearly larger scale: no flicker between two
      if (sc > most * 1.001) {
        most = sc;
        best = c;
      }
    }
    return best;
  }

  // the items for `cols` copies a row, the groups moved to them
  private layOut(cols: number): void {
    if (cols === this.cols) {
      return;
    }
    this.cols = cols;
    arrange(this.laid, cols);
    const b = this.boundsOf(this.laid);
    this.bounds = { w: b.w, h: b.h, k: this.bounds.k };
    this.position();
  }

  // the layout's size and centre in the mockup's units, each item's place
  // grown as it is drawn
  private boundsOf(items: readonly SceneItem[]): { w: number; h: number; cx: number; cy: number } {
    let x0 = Infinity;
    let x1 = -Infinity;
    let y0 = Infinity;
    let y1 = -Infinity;
    for (const it of items) {
      const f = this.grow * (it.role === 'result' ? RESULT_SIZE : 1);
      x0 = Math.min(x0, it.pos[0] - 2 * f);
      x1 = Math.max(x1, it.pos[0] + 2 * f);
      y0 = Math.min(y0, it.pos[1] - 2.2 * f);
      y1 = Math.max(y1, it.pos[1] + 2.2 * f);
    }
    return items.length === 0
      ? { w: 1, h: 1, cx: 0, cy: 0 }
      : { w: x1 - x0, h: y1 - y0, cx: (x0 + x1) / 2, cy: (y0 + y1) / 2 };
  }

  // each item's group where the layout puts it, the layout centred
  private position(): void {
    const { cx, cy } = this.boundsOf(this.laid);
    const k = this.bounds.k;
    for (const d of this.items) {
      d.at.set(
        (d.item.pos[0] - cx) * k,
        (d.item.pos[1] - cy) * k + (d.item.role === 'piece' ? -this.drop : this.drop) / 2,
        0,
      );
      d.group.position.copy(d.at);
    }
  }

  /**
   * The height at which a viewport `w` wide shows the layout as large as
   * its width allows (`tight`: with the margin above and below cut, as
   * `scale` cuts it).
   */
  heightFor(w: number, tight = false): number {
    const { w: lw, h: lh } = this.bounds;
    return (w * (lh + padY(tight))) / (lw + PAD_X);
  }

  /** Turns every item about its own centre to the camera's orientation. */
  turn(q: Quaternion): void {
    for (const d of this.items) {
      d.group.quaternion.copy(q);
    }
  }

  /**
   * Places the boxes and labels for the camera (after it was framed), the
   * scene drawn `top` pixels down the viewport (a band under a sliding
   * puzzle's trays).
   */
  place(camera: Camera3, w: number, h: number, sc: number, top = 0): void {
    const v = new Vector3();
    for (const d of this.items) {
      v.copy(d.at).project(camera);
      const x = ((v.x + 1) / 2) * w;
      const y = top + ((1 - v.y) / 2) * h;
      const half = d.half * d.size * this.zoom * sc + 8;
      d.rect = { x0: x - half, y0: y - half, x1: x + half, y1: y + half };
      const s = d.box.style;
      s.left = `${String(Math.round(x - half))}px`;
      s.top = `${String(Math.round(y - half))}px`;
      s.width = `${String(Math.round(2 * half))}px`;
      s.height = `${String(Math.round(2 * half))}px`;
    }
  }

  /** The piece under a point of the viewport (the smallest box; never the result), or null. */
  pick(x: number, y: number): string | null {
    let best: string | null = null;
    let area = Infinity;
    for (const d of this.items) {
      const r = d.rect;
      if (d.item.role !== 'result' && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) {
        const a = (r.x1 - r.x0) * (r.y1 - r.y0);
        if (a < area) {
          area = a;
          best = d.item.shape;
        }
      }
    }
    return best;
  }

  /** The piece under the mouse (a dashed box), or null; true when it changed. */
  setHover(shape: string | null): boolean {
    if (shape === this.hover) {
      return false;
    }
    this.hover = shape;
    this.renderBoxes();
    return true;
  }

  /** Whether anything is drawn. */
  get empty(): boolean {
    return this.items.length === 0;
  }

  private renderBoxes(): void {
    for (const d of this.items) {
      const { role, shape } = d.item;
      const sel = role === 'selected' || (role === 'piece' && shape === this.selected);
      d.box.classList.toggle('sel', sel);
      d.box.classList.toggle('hov', !sel && role !== 'result' && shape === this.hover);
    }
  }

  private shapeDraw(shape: string, m: PieceMesh): ShapeDraw {
    const have = this.shapes.get(shape);
    if (have !== undefined) {
      if (have.edges !== null) {
        have.edges.dispose();
      }
      have.edges = edgeGeometry(m.built);
      return have;
    }
    // the shared uniforms (palette, light, view) with a colour of its own
    const uniforms: FaceUniforms = { ...this.shared, uPiece: { value: new Vector3(1, 1, 1) } };
    const normal = faceMaterial(uniforms, 'normal');
    const faded = faceMaterial({ ...uniforms, uAlpha: { value: FADED } }, 'normal');
    faded.transparent = true;
    faded.depthWrite = false;
    const d: ShapeDraw = { uniforms, normal, faded, edges: edgeGeometry(m.built) };
    this.shapes.set(shape, d);
    return d;
  }

  private disposeShape(d: ShapeDraw): void {
    d.normal.dispose();
    d.faded.dispose();
    d.edges?.dispose();
  }

  private clearItems(): void {
    for (const d of this.items) {
      this.group.remove(d.group);
    }
    this.items = [];
    this.overlay.replaceChildren();
  }

  dispose(): void {
    this.clearItems();
    for (const d of this.shapes.values()) {
      this.disposeShape(d);
    }
    this.shapes.clear();
    for (const m of [
      this.markMaterial,
      this.depthOnly,
      this.markFaded,
      this.edgeMaterial,
      this.edgeFaded,
    ]) {
      m.dispose();
    }
    this.shared.uPalette.value.dispose();
  }
}

// the larger side, in world units, of a piece's outline turned by `q`
// (seen along Z): its mesh's corners about its centre, or with no mesh,
// its box's
function outlineOf(m: PieceMesh, q: Quaternion): number {
  let x0 = Infinity;
  let x1 = -Infinity;
  let y0 = Infinity;
  let y1 = -Infinity;
  const v = new Vector3();
  const take = (): void => {
    v.applyQuaternion(q);
    x0 = Math.min(x0, v.x);
    x1 = Math.max(x1, v.x);
    y0 = Math.min(y0, v.y);
    y1 = Math.max(y1, v.y);
  };
  const p = m.built.faces.getAttribute('position');
  const [cx, cy, cz] = m.centre;
  if (p.count > 0) {
    for (let i = 0; i < p.count; i++) {
      v.set(p.getX(i) - cx, p.getY(i) - cy, p.getZ(i) - cz);
      take();
    }
  } else {
    for (let i = 0; i < 8; i++) {
      v.set(
        (i & 1 ? 1 : -1) * m.half[0],
        (i & 2 ? 1 : -1) * m.half[1],
        (i & 4 ? 1 : -1) * m.half[2],
      );
      take();
    }
  }
  return Math.max(x1 - x0, y1 - y0, 0.5);
}

/** How far the copies sit lower under the result than the layout puts them, in pixels. */
const DROP_PX = 35;

/** The layout's margin left and right, in the mockup's units. */
const PAD_X = 1.5;

// the layout's margin above and below, in the mockup's units: a band under
// a sliding board's trays keeps less
function padY(tight: boolean): number {
  return tight ? 1 : 3;
}

function edgeGeometry(built: BuiltMesh): LineSegmentsGeometry | null {
  return built.edges === null ? null : new LineSegmentsGeometry().setPositions(built.edges);
}
