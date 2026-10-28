// SPDX-License-Identifier: GPL-3.0-or-later
//
// three.js for the 3D view: the WebGL renderer on the viewport's canvas at
// the display's pixel ratio, resized with it, rendering only when asked
// (one frame per animation frame at most); the perspective and the
// orthographic camera framed as the mockup's render3d framed its
// projection (docs/mockup/index.html: `sc`, the 64-unit eye of `pr`, the
// pan as a shift of the picture), which replaces its canvas drawing.
// Each render is measured ("view3d:render", performance.measure). The
// view's picture as a PNG at a scale of its size (File ▸ Export ▸ Image,
// stage 7) is a render at that pixel ratio, read back at once. A stacking
// puzzle's view is tilted as the original's (`tilt`).

import {
  Group,
  OrthographicCamera,
  PerspectiveCamera,
  Quaternion,
  Scene,
  Vector3,
  WebGLRenderer,
} from 'three';
import type { Camera as Camera3 } from 'three';

import { EYE_DISTANCE, pixelsPerUnit } from './camera.ts';
import type { Camera } from './camera.ts';

type V3 = [number, number, number];

/** -90° about X: voxel (x, y, z) to (x, z, -y), the original's tiltPlates. */
const TILT = new Quaternion().setFromAxisAngle(new Vector3(1, 0, 0), -Math.PI / 2);

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
  /**
   * A stacking puzzle's view: the model turned -90° about X before the
   * camera's turn, voxel +Z (the stack) up the screen, so a disc lies flat
   * (the original's voxelFrame_c::tiltPlates; its view cube does not share
   * the tilt, nor does this one).
   */
  tilt = false;
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

  /**
   * Draws into a band of the view the whole width, `top` CSS pixels down
   * and `height` tall: while `draw` runs the renderer's size is the band's,
   * so framing and rendering fill it alone.
   */
  band(top: number, height: number, draw: () => void): void {
    const full = this.height;
    const h = Math.max(1, Math.round(height));
    const y = full - Math.round(top) - h;
    this.height = h;
    this.gl.setViewport(0, y, this.width, h);
    this.gl.setScissor(0, y, this.width, h);
    this.gl.setScissorTest(true);
    try {
      draw();
    } finally {
      this.height = full;
      this.gl.setScissorTest(false);
      this.gl.setViewport(0, 0, this.width, full);
    }
  }

  /**
   * Places the cameras and the shape for the camera's state and the space's
   * extent; `sc`, pixels per unit, in place of the view's own fit.
   */
  frame(
    cam: Pick<Camera, 'q' | 'zoom' | 'px' | 'py'>,
    extent: V3,
    sc = pixelsPerUnit(this.width, this.height, extent, cam.zoom),
  ): void {
    this.root.quaternion.copy(cam.q);
    if (this.tilt) {
      this.root.quaternion.multiply(TILT);
    }
    this.model.position.set(-extent[0] / 2, -extent[1] / 2, -extent[2] / 2);
    this.place(cam, sc, EYE_DISTANCE, Math.hypot(...extent) + 2);
  }

  /**
   * Places the cameras for a scene that is not turned as a whole (the
   * pieces scene: each piece turns itself): `sc` pixels per unit at the
   * scene's plane, the eye `eye` units away, nothing further than `reach`
   * from the centre; `cam` gives the pan.
   */
  frameScene(cam: Pick<Camera, 'px' | 'py'>, sc: number, eye: number, reach: number): void {
    this.place(cam, sc, eye, reach);
  }

  private place(cam: Pick<Camera, 'px' | 'py'>, sc: number, d: number, reach: number): void {
    const w = this.width;
    const h = this.height;
    const near = Math.max(0.05, d - reach);
    const far = d + reach;

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

  /** Frames and renders now, as an animation frame would. */
  drawNow(): void {
    this.draw?.();
  }

  /**
   * The view as a PNG, `scale` pixels to its CSS pixel (1 and 2: the
   * export's sizes): drawn at that pixel ratio and read in the same task,
   * before the page shows it; the display's ratio is then restored.
   */
  capture(scale: number): { data: string; width: number; height: number } {
    const gl = this.gl;
    try {
      gl.setPixelRatio(scale);
      gl.setSize(this.width, this.height, false);
      this.drawNow();
      const c = gl.domElement;
      return { data: c.toDataURL('image/png'), width: c.width, height: c.height };
    } finally {
      gl.setPixelRatio(window.devicePixelRatio || 1);
      gl.setSize(this.width, this.height, false);
      this.request();
    }
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
