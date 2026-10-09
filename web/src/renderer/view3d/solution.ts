// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver workspace's scene (FRONTEND-PLAN.md, stage 4): the pieces of
// a solution, each its own mesh in its own colour (the original's
// pieceColor of the shape, jittered per copy), placed by a pose: the
// assembled view (the mesh of the placement's transformation at the cell
// minus its hotspot) or the player's poses (playback.poses, interpolated).
// The coordinates are mesh units centred on the result, as the poses are,
// so the scene sits at the turned root's origin. A piece is shown solid
// (the view's style), as a wire frame (the wire-frame mesh) or not at all,
// as the mockup's piece chips cycle; with x-ray ("show through solid
// pieces") the wire frames are drawn over everything, as the mockup's
// renderSolverScene drew them in its second pass. A piece being taken
// away fades (the setting) through its opacity. From docs/mockup/index.html:
// `renderSolverScene` (what is drawn, the two passes); the drawing is
// three.js over the engine's meshes (mesh.ts, material.ts), as the other
// scenes'.

import { Group, Mesh, MeshBasicMaterial, Quaternion, Vector2, Vector3 } from 'three';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';

import type { MeshStyle } from '../../engine/api.generated.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { faceMaterial, faceUniforms, setPalette, setPiece } from './material.ts';
import type { FaceUniforms } from './material.ts';
import type { BuiltMesh } from './mesh.ts';

/** How a piece is shown (the piece chips: solid → wire frame → hidden). */
export type PieceMode = 'solid' | 'wire' | 'hidden';

/** A piece of the solution shown. */
export interface ScenePiece {
  /** The problem's piece number. */
  piece: number;
  shape: string;
  colour: Rgb3;
  mode: PieceMode;
  /** Its mesh in the view's style (solid) or the wire-frame style (wire); null when hidden. */
  built: BuiltMesh | null;
}

/** What the scene shows; `key` names it (the e2e tests read it). */
export interface SolutionModel {
  key: string;
  /** The result's extent in mesh units (the view is framed on it). */
  extent: [number, number, number];
  pieces: ScenePiece[];
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
  colourView: boolean;
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
}

export class SolutionScene {
  readonly group = new Group();
  private readonly shared: FaceUniforms = faceUniforms();
  private readonly markMaterial = new MeshBasicMaterial({ color: 0x000000 });
  private readonly edgeMaterial = new LineMaterial({ color: 0x000000, linewidth: 2 });
  private draws: PieceDraw[] = [];
  private look: SolutionLook = {
    style: 'drawing',
    colourView: false,
    lit: true,
    xray: false,
    palette: [],
  };
  private poses: ScenePose[] = [];

  constructor() {
    this.edgeMaterial.resolution = new Vector2(1, 1);
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
      };
      setPiece(uniforms, piece.colour);
      const group = new Group();
      const d: PieceDraw = {
        piece,
        group,
        uniforms,
        faces: null,
        edges: null,
        edgeGeometry: null,
        marks: null,
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
    this.setPoses(this.poses);
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
    u.uColourView.value = look.colourView ? 1 : 0;
    u.uLit.value = look.lit ? 1 : 0;
    for (const d of this.draws) {
      const wire = d.piece.mode === 'wire';
      // the checker is the bevelled style's and the wire frame's
      d.uniforms.uChecker.value = !wire && look.style === 'flat' ? 0 : 1;
      if (d.faces !== null) {
        const m = d.faces.material as ReturnType<typeof faceMaterial>;
        // wire frames over the solid pieces with x-ray (the mockup's second pass)
        m.depthTest = !(wire && look.xray);
        d.faces.renderOrder = wire && look.xray ? 5 : 0;
      }
    }
  }

  private setOpacity(d: PieceDraw, opacity: number): void {
    const o = Math.max(0, Math.min(1, opacity));
    d.uniforms.uAlpha.value = o;
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
    }
    this.draws = [];
  }

  dispose(): void {
    this.clear();
    this.markMaterial.dispose();
    this.edgeMaterial.dispose();
    this.shared.uPalette.value.dispose();
  }
}
