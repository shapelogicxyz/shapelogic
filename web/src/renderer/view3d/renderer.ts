// SPDX-License-Identifier: GPL-3.0-or-later
//
// three.js for the 3D view: the WebGL renderer on the viewport's canvas at
// the display's pixel ratio, resized with it, rendering only when asked
// (one frame per animation frame at most); the perspective and the
// orthographic camera framed as the mockup's render3d framed its
// projection (docs/mockup/index.html: `sc`, the 64-unit eye of `pr`, the
// pan as a shift of the picture), which replaces its canvas drawing.
// Each render is measured ("view3d:render", performance.measure).

import { Group, OrthographicCamera, PerspectiveCamera, Scene, WebGLRenderer } from 'three';
import type { Camera as Camera3 } from 'three';

import { EYE_DISTANCE, pixelsPerUnit } from './camera.ts';
import type { Camera } from './camera.ts';

type V3 = [number, number, number];

export class ViewRenderer {
  readonly gl: WebGLRenderer;
  readonly scene = new Scene();
  /** Turned by the camera's orientation. */
  readonly root = new Group();
  /** In mesh units, its space's centre at the root's origin. */
  readonly model = new Group();
  private readonly persp = new PerspectiveCamera();
  private readonly ortho = new OrthographicCamera();
  /** Perspective (the mockup's default) or orthographic. */
  perspective = true;
  /** The viewport's size in CSS pixels. */
  width = 1;
  height = 1;
  private pending = 0;
  private measured = 0;
  private draw: (() => void) | null = null;

  constructor(canvas: HTMLCanvasElement) {
    this.gl = new WebGLRenderer({ canvas, antialias: true, preserveDrawingBuffer: false });
    this.gl.setPixelRatio(window.devicePixelRatio || 1);
    this.root.add(this.model);
    this.scene.add(this.root);
  }

  /** The camera rendering now. */
  get camera(): Camera3 {
    return this.perspective ? this.persp : this.ortho;
  }

  /** The background, a CSS colour. */
  setBackground(colour: string): void {
    this.gl.setClearColor(colour, 1);
  }

  /** Follows the canvas's size in CSS pixels. */
  resize(width: number, height: number): void {
    this.width = Math.max(1, Math.round(width));
    this.height = Math.max(1, Math.round(height));
    this.gl.setPixelRatio(window.devicePixelRatio || 1);
    this.gl.setSize(this.width, this.height, false);
  }

  /** Places the cameras and the shape for the camera's state and the space's extent. */
  frame(cam: Camera, extent: V3): void {
    const w = this.width;
    const h = this.height;
    const sc = pixelsPerUnit(w, h, extent, cam.zoom);
    const d = EYE_DISTANCE;
    const reach = Math.hypot(...extent) + 2;
    const near = Math.max(0.05, d - reach);
    const far = d + reach;

    this.root.quaternion.copy(cam.q);
    this.model.position.set(-extent[0] / 2, -extent[1] / 2, -extent[2] / 2);

    const p = this.persp;
    p.position.set(0, 0, d);
    p.fov = (2 * Math.atan(h / (2 * sc * d)) * 180) / Math.PI;
    p.aspect = w / h;
    p.near = near;
    p.far = far;
    // the pan moves the picture, as the mockup's px, py did
    p.setViewOffset(w, h, -cam.px, -cam.py, w, h);
    p.updateProjectionMatrix();

    const o = this.ortho;
    o.position.set(0, 0, d);
    o.left = -w / (2 * sc);
    o.right = w / (2 * sc);
    o.top = h / (2 * sc);
    o.bottom = -h / (2 * sc);
    o.near = near;
    o.far = far;
    o.setViewOffset(w, h, -cam.px, -cam.py, w, h);
    o.updateProjectionMatrix();

    this.scene.updateMatrixWorld();
    p.updateMatrixWorld();
    o.updateMatrixWorld();
  }

  /** Sets what a render does (frame the cameras, then draw). */
  onDraw(draw: () => void): void {
    this.draw = draw;
  }

  /** Asks for a render at the next animation frame. */
  request(): void {
    if (this.pending !== 0) {
      return;
    }
    this.pending = requestAnimationFrame(() => {
      this.pending = 0;
      this.draw?.();
    });
  }

  /** Renders now. */
  render(): void {
    const t0 = performance.now();
    this.gl.render(this.scene, this.camera);
    performance.measure('view3d:render', { start: t0, end: performance.now() });
    // keep the timeline bounded: the newest renders are what is read
    if (++this.measured % 2000 === 0) {
      performance.clearMeasures('view3d:render');
    }
  }

  dispose(): void {
    cancelAnimationFrame(this.pending);
    this.gl.dispose();
  }
}
