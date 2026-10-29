// SPDX-License-Identifier: GPL-3.0-or-later
//
// The STL dialog's preview (deferred part 5): the original's
// stlExport_c had a 3D view of its own (LView3dGroup) showing the mesh the
// export would write, normally or with its insides visible. A small
// three.js view on the dialog's canvas: the mesh of export.stlMesh in the
// piece's colour, lit as the main view lights (material.ts), no checker;
// a drag turns it on the original's arc-ball, the wheel zooms; Insides is
// the main view's x-ray (the inner faces drawn, the outer ones faint).

import {
  Euler,
  Group,
  Mesh,
  PerspectiveCamera,
  Quaternion,
  Scene,
  Vector3,
  WebGLRenderer,
} from 'three';

import type { MeshArrays } from '../../engine/frames.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { arcballStep } from '../view3d/camera.ts';
import { faceMaterial, faceUniforms, setPiece } from '../view3d/material.ts';
import { buildMesh } from '../view3d/mesh.ts';
import type { BuiltMesh } from '../view3d/mesh.ts';

/** The view's opening turn: a little from above and from the side. */
const HOME = new Quaternion().setFromEuler(new Euler(0.45, -0.6, 0));

export class StlPreview {
  private readonly canvas: HTMLCanvasElement;
  private readonly gl: WebGLRenderer;
  private readonly scene = new Scene();
  private readonly turn = new Group();
  private readonly model = new Group();
  private readonly camera = new PerspectiveCamera(30, 1, 0.1, 10_000);
  private readonly uniforms = faceUniforms();
  private readonly mNormal = faceMaterial(this.uniforms, 'normal');
  private readonly mInside = faceMaterial(this.uniforms, 'inside');
  private readonly mOutside = faceMaterial(this.uniforms, 'outside');
  private built: BuiltMesh | null = null;
  private faces: Mesh | null = null;
  private outer: Mesh | null = null;
  private insides = false;
  private radius = 1;
  private zoom = 1;
  private drag: { x: number; y: number } | null = null;
  private frame = 0;
  private readonly observer: ResizeObserver;

  constructor(canvas: HTMLCanvasElement, background: string) {
    this.canvas = canvas;
    this.gl = new WebGLRenderer({ canvas, antialias: true });
    this.gl.setPixelRatio(window.devicePixelRatio || 1);
    this.gl.setClearColor(background, 1);
    this.uniforms.uChecker.value = 0;
    this.turn.add(this.model);
    this.turn.quaternion.copy(HOME);
    this.scene.add(this.turn);
    canvas.addEventListener('pointerdown', this.down);
    canvas.addEventListener('pointermove', this.move);
    canvas.addEventListener('pointerup', this.up);
    canvas.addEventListener('pointercancel', this.up);
    canvas.addEventListener('wheel', this.wheel, { passive: false });
    this.observer = new ResizeObserver(() => {
      this.render();
    });
    this.observer.observe(canvas);
  }

  /** Shows a mesh in a colour (null: nothing, as the original after a refusal). */
  setMesh(arrays: MeshArrays | null, colour: Rgb3): void {
    this.clear();
    setPiece(this.uniforms, colour);
    if (arrays !== null) {
      const built = buildMesh(arrays, [1, 1, 1], 'stl');
      this.built = built;
      built.faces.computeBoundingSphere();
      const sphere = built.faces.boundingSphere;
      this.radius = Math.max(sphere?.radius ?? 1, 1e-3);
      this.model.position.copy(sphere?.center ?? new Vector3()).negate();
      this.faces = new Mesh(built.faces, this.insides ? this.mInside : this.mNormal);
      this.outer = new Mesh(built.faces, this.mOutside);
      this.outer.renderOrder = 3;
      this.outer.visible = this.insides;
      this.model.add(this.faces, this.outer);
    }
    this.render();
  }

  /** The original's two modes: the object normally, or its insides. */
  setInsides(on: boolean): void {
    this.insides = on;
    if (this.faces !== null) {
      this.faces.material = on ? this.mInside : this.mNormal;
    }
    if (this.outer !== null) {
      this.outer.visible = on;
    }
    this.render();
  }

  /** Frees the GPU's resources and the listeners. */
  dispose(): void {
    cancelAnimationFrame(this.frame);
    this.observer.disconnect();
    this.clear();
    this.mNormal.dispose();
    this.mInside.dispose();
    this.mOutside.dispose();
    this.uniforms.uPalette.value.dispose();
    this.gl.dispose();
  }

  private clear(): void {
    if (this.faces !== null) {
      this.model.remove(this.faces);
    }
    if (this.outer !== null) {
      this.model.remove(this.outer);
    }
    this.faces = null;
    this.outer = null;
    this.built?.faces.dispose();
    this.built?.marks?.dispose();
    this.built = null;
  }

  private render(): void {
    cancelAnimationFrame(this.frame);
    this.frame = requestAnimationFrame(() => {
      const w = Math.max(1, this.canvas.clientWidth);
      const h = Math.max(1, this.canvas.clientHeight);
      this.gl.setSize(w, h, false);
      this.camera.aspect = w / h;
      // the whole mesh in view, from a distance that fits its sphere
      const fit = this.radius / Math.sin(((this.camera.fov / 2) * Math.PI) / 180);
      this.camera.position.set(0, 0, (fit * 1.1) / this.zoom);
      this.camera.near = Math.max(1e-3, this.camera.position.z - this.radius * 2);
      this.camera.far = this.camera.position.z + this.radius * 2;
      this.camera.updateProjectionMatrix();
      this.gl.render(this.scene, this.camera);
    });
  }

  private readonly down = (e: PointerEvent): void => {
    this.canvas.setPointerCapture(e.pointerId);
    this.drag = { x: e.offsetX, y: e.offsetY };
  };

  private readonly move = (e: PointerEvent): void => {
    if (this.drag === null) {
      return;
    }
    // step by step, as the 3D view turns (camera.ts's arcballStep)
    const step = arcballStep(
      this.drag.x,
      this.drag.y,
      e.offsetX,
      e.offsetY,
      this.canvas.clientWidth,
      this.canvas.clientHeight,
    );
    this.turn.quaternion.premultiply(step).normalize();
    this.drag.x = e.offsetX;
    this.drag.y = e.offsetY;
    this.render();
  };

  private readonly up = (): void => {
    this.drag = null;
  };

  private readonly wheel = (e: WheelEvent): void => {
    e.preventDefault();
    this.zoom = Math.min(8, Math.max(0.25, this.zoom * Math.exp(-e.deltaY / 500)));
    this.render();
  };
}
