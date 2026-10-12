// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the 3D view shows: one shape (stage 1b), with its overlays and
// the 3D edit's preview (stage 2); the pieces of a problem are
// pieces.ts's (stage 3), the poses of a solution come with stage 4. The
// shape is drawn in a style (drawing: the bevelled cells with
// the checker; wireframe: the frame of the original's "gridline" mode;
// flat: faces with black edge lines, the original's "edges" style), in a
// colour view (the piece colour, or the constraint colours), lit or not,
// with x-ray (the original's insideVisible: the inner faces drawn, the
// others faint and through) and the editor's layer dimmed or not.
// The edge lines are black, 2 px, as voxelFrame_c::drawShape drew them.

import { Mesh, MeshBasicMaterial, Vector2 } from 'three';
import type { Group } from 'three';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';

import type { MeshStyle } from '../../engine/api.generated.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { faceMaterial, faceUniforms, setPalette, setPiece } from './material.ts';
import type { FaceUniforms } from './material.ts';
import { hasChecker } from './mesh.ts';
import type { BuiltMesh } from './mesh.ts';
import { Overlays } from './overlays.ts';
import type { OverlayOptions } from './overlays.ts';
import { Preview } from './preview.ts';
import type { Ghost } from './preview.ts';

/** How the shape is shown. */
export interface ShapeLook {
  style: MeshStyle;
  /** The constraint colours instead of the piece colour. */
  colourView: boolean;
  lit: boolean;
  xray: boolean;
  piece: Rgb3;
  /** The constraint colours, `#rrggbb`, colour n at n − 1. */
  palette: readonly string[];
  /** The editor's layer: cells of other layers dimmed along this axis; null: none dimmed. */
  dim: { axis: 0 | 1 | 2; layer: number } | null;
}

export class ShapeScene {
  readonly overlays = new Overlays();
  /** What a 3D edit would do (the Entities workspace). */
  readonly preview = new Preview();
  private readonly uniforms: FaceUniforms = faceUniforms();
  private readonly mNormal = faceMaterial(this.uniforms, 'normal');
  private readonly mInside = faceMaterial(this.uniforms, 'inside');
  private readonly mOutside = faceMaterial(this.uniforms, 'outside');
  private readonly markMaterial = new MeshBasicMaterial({ color: 0x000000 });
  private readonly edgeMaterial = new LineMaterial({ color: 0x000000, linewidth: 2 });
  private readonly parent: Group;
  private built: BuiltMesh | null = null;
  /** The mesh picking casts against. */
  private faces: Mesh | null = null;
  private outer: Mesh | null = null;
  private marks: Mesh | null = null;
  private edges: LineSegments2 | null = null;
  private xray = false;

  constructor(parent: Group) {
    this.parent = parent;
    parent.add(this.overlays.group);
    parent.add(this.preview.group);
    this.edgeMaterial.resolution = new Vector2(1, 1);
  }

  /** The mesh shown, or null. */
  get mesh(): BuiltMesh | null {
    return this.built;
  }

  /** The object a raycast tests, or null when nothing is shown. */
  get target(): Mesh | null {
    return this.faces;
  }

  /** Whether the inside faces are shown (x-ray). */
  get insideShown(): boolean {
    return this.xray;
  }

  /** Shows another mesh (null: none); the old one is freed. */
  setMesh(built: BuiltMesh | null): void {
    this.clearMesh();
    this.built = built;
    if (built === null) {
      return;
    }
    this.faces = new Mesh(built.faces, this.xray ? this.mInside : this.mNormal);
    this.faces.userData.faces = true;
    this.parent.add(this.faces);
    this.outer = new Mesh(built.faces, this.mOutside);
    this.outer.renderOrder = 3;
    this.outer.visible = this.xray;
    this.parent.add(this.outer);
    if (built.marks !== null) {
      this.marks = new Mesh(built.marks, this.markMaterial);
      this.parent.add(this.marks);
    }
    if (built.edges !== null) {
      const g = new LineSegmentsGeometry().setPositions(built.edges);
      this.edges = new LineSegments2(g, this.edgeMaterial);
      this.parent.add(this.edges);
    }
  }

  /** Applies a look; nothing is rebuilt. */
  setLook(look: ShapeLook): void {
    const u = this.uniforms;
    setPiece(u, look.piece);
    setPalette(u, look.palette);
    u.uColourView.value = look.colourView ? 1 : 0;
    u.uLit.value = look.lit ? 1 : 0;
    // the checker is the bevelled style's and the wire frame's (drawShape's `checker`)
    u.uChecker.value = hasChecker(look.style) ? 1 : 0;
    u.uDimAxis.value = look.dim === null ? -1 : look.dim.axis;
    u.uLayer.value = look.dim === null ? 0 : look.dim.layer;
    this.xray = look.xray;
    if (this.faces !== null) {
      this.faces.material = look.xray ? this.mInside : this.mNormal;
    }
    if (this.outer !== null) {
      this.outer.visible = look.xray;
    }
  }

  /** Shows the 3D edit's preview (empty: none). */
  setPreview(ghosts: readonly Ghost[]): void {
    this.preview.set(ghosts, this.built);
  }

  /** The overlays' options. */
  setOverlays(o: OverlayOptions): void {
    this.overlays.update(o);
  }

  /** The viewport's size in pixels, for the lines' widths. */
  setResolution(w: number, h: number): void {
    this.edgeMaterial.resolution.set(w, h);
    this.overlays.setResolution(w, h);
  }

  private clearMesh(): void {
    for (const o of [this.faces, this.outer, this.marks, this.edges]) {
      if (o !== null) {
        this.parent.remove(o);
      }
    }
    this.built?.faces.dispose();
    this.built?.marks?.dispose();
    this.edges?.geometry.dispose();
    this.faces = null;
    this.outer = null;
    this.marks = null;
    this.edges = null;
    this.built = null;
  }

  dispose(): void {
    this.clearMesh();
    this.overlays.clear();
    this.preview.clear();
    for (const m of [
      this.mNormal,
      this.mInside,
      this.mOutside,
      this.markMaterial,
      this.edgeMaterial,
    ]) {
      m.dispose();
    }
    this.uniforms.uPalette.value.dispose();
  }
}
