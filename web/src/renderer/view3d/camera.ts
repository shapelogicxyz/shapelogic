// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view's camera: an orientation, a zoom and a pan. From
// docs/mockup/index.html: `cam`, `HOME`, the pointer handlers' orbit and
// pan (the "Drag" method: yaw and pitch from the drag, pitch held inside
// ±89°, the roll respected), `zoomBy` / `zStep` (smooth wheel zoom, 0.3 to
// 4), `animateTo` (cubic ease in and out, 360 ms), `homeView`, `fitView`,
// `dirToView`, `viewToWorld`, `mkRot`. The "Arc-ball" method is the
// original's (src/gui/arcball.cpp, arcBall_c: the drag's start and end on a
// virtual ball, their rotation added in front of the last). three.js's own
// controls follow neither map, so the logic is ported.
//
// The orientation is one quaternion `q` turning world (the mesh's
// coordinates) into view space (x right, y up, z towards the viewer); the
// mockup's yaw, pitch and roll are its Euler angles in the order Z, X, Y
// (view = Rz(roll) Rx(pitch) Ry(yaw) world), so both methods turn the same
// camera and the view cube reads it.

import { Euler, MathUtils, Quaternion, Vector3 } from 'three';

/** The mockup's camera angles, in degrees. */
export interface Angles {
  yaw: number;
  pitch: number;
  roll: number;
}

/** Where an animation goes; what it leaves out stays. */
export interface Target {
  q?: Quaternion;
  zoom?: number;
  px?: number;
  py?: number;
}

/** The home view: yaw −32°, pitch 26°, framed. */
export const HOME: Readonly<Angles & { zoom: number; px: number; py: number }> = {
  yaw: -32,
  pitch: 26,
  roll: 0,
  zoom: 1,
  px: 0,
  py: 0,
};

/** The zoom's range. */
export const ZOOM_MIN = 0.3;
export const ZOOM_MAX = 4;

/** The orientation of the mockup's angles. */
export function quatOf(a: Angles): Quaternion {
  return new Quaternion().setFromEuler(
    new Euler(
      MathUtils.degToRad(a.pitch),
      MathUtils.degToRad(a.yaw),
      MathUtils.degToRad(a.roll),
      'ZXY',
    ),
  );
}

/** The mockup's angles of an orientation. */
export function anglesOf(q: Quaternion): Angles {
  const e = new Euler().setFromQuaternion(q, 'ZXY');
  return {
    yaw: MathUtils.radToDeg(e.y),
    pitch: MathUtils.radToDeg(e.x),
    roll: MathUtils.radToDeg(e.z),
  };
}

/** A direction of the world as the view sees it (the mockup's mkRot). */
export function toView(q: Quaternion, v: readonly [number, number, number]): Vector3 {
  return new Vector3(v[0], v[1], v[2]).applyQuaternion(q);
}

/** A direction of the view in the world (the mockup's viewToWorld). */
export function toWorld(q: Quaternion, v: readonly [number, number, number]): Vector3 {
  return new Vector3(v[0], v[1], v[2]).applyQuaternion(q.clone().invert());
}

/**
 * The angles that look along -n (the mockup's dirToView): from the side
 * `n` points to; straight up or down keeps the yaw, rounded to 90°.
 */
export function dirToView(n: readonly [number, number, number], keepYaw: number): Angles {
  const r = Math.hypot(n[0], n[2]);
  return {
    yaw: r < 1e-6 ? Math.round(keepYaw / 90) * 90 : MathUtils.radToDeg(Math.atan2(-n[0], n[2])),
    pitch: Math.max(-89, Math.min(89, MathUtils.radToDeg(Math.atan2(n[1], r)))),
    roll: 0,
  };
}

/** A point of the viewport (pixels from its top left) on the arc-ball (arcBall_c::mapToSphere). */
export function arcballPoint(x: number, y: number, width: number, height: number): Vector3 {
  const aw = width > 1 && height > 1 ? 1 / ((width - 1) * 0.5) : 1;
  const ah = width > 1 && height > 1 ? 1 / ((height - 1) * 0.5) : 1;
  const px = x * aw - 1;
  const py = 1 - y * ah;
  const len = px * px + py * py;
  if (len > 1) {
    const norm = 1 / Math.sqrt(len);
    return new Vector3(px * norm, py * norm, 0);
  }
  return new Vector3(px, py, Math.sqrt(1 - len));
}

/** The arc-ball's turn from one point of the ball to another (arcBall_c::getDrag). */
export function arcballTurn(from: Vector3, to: Vector3): Quaternion {
  const perp = new Vector3().crossVectors(from, to);
  if (perp.length() > 1e-5) {
    return new Quaternion(perp.x, perp.y, perp.z, from.dot(to));
  }
  return new Quaternion();
}

const ease = (k: number): number => (k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2);

interface Animation {
  t0: number;
  ms: number;
  from: { q: Quaternion; zoom: number; px: number; py: number };
  to: { q: Quaternion; zoom: number; px: number; py: number };
}

/** The camera's state and its moves; the view renders after `step()` says it moved. */
export class Camera {
  /** World to view. */
  q = quatOf(HOME);
  zoom = HOME.zoom;
  /** The pan, in pixels of the viewport. */
  px = HOME.px;
  py = HOME.py;

  private anim: Animation | null = null;
  private zoomTarget: number | null = null;
  private zoomLast = 0;
  private arc: { start: Vector3; q0: Quaternion; w: number; h: number } | null = null;

  /** The mockup's angles of the orientation now. */
  get angles(): Angles {
    return anglesOf(this.q);
  }

  /** Whether an animation or a smooth zoom is under way. */
  get moving(): boolean {
    return this.anim !== null || this.zoomTarget !== null;
  }

  /** Stops the animations (a drag takes over). */
  stop(): void {
    this.anim = null;
    this.zoomTarget = null;
  }

  /** The "Drag" orbit by a drag of (dx, dy) pixels (the mockup's pointermove). */
  orbit(dx: number, dy: number): void {
    const a = this.angles;
    const rr = MathUtils.degToRad(a.roll);
    const cr = Math.cos(rr);
    const sr = Math.sin(rr);
    this.q = quatOf({
      yaw: a.yaw + (dx * cr - dy * sr) * 0.5,
      pitch: Math.max(-89, Math.min(89, a.pitch + (dx * sr + dy * cr) * 0.5)),
      roll: a.roll,
    });
  }

  /** Starts an arc-ball turn at a point of a viewport of this size (arcBall_c::click). */
  arcStart(x: number, y: number, w: number, h: number): void {
    this.arc = { start: arcballPoint(x, y, w, h), q0: this.q.clone(), w, h };
  }

  /** Turns by the arc-ball to a point (arcBall_c::drag and addTransform). */
  arcDrag(x: number, y: number): void {
    if (this.arc === null) {
      return;
    }
    const turn = arcballTurn(this.arc.start, arcballPoint(x, y, this.arc.w, this.arc.h));
    this.q = turn.multiply(this.arc.q0).normalize();
  }

  /** Ends an arc-ball turn (arcBall_c::clack). */
  arcEnd(): void {
    this.arc = null;
  }

  /** Pans by pixels. */
  pan(dx: number, dy: number): void {
    this.px += dx;
    this.py += dy;
  }

  /** Zooms by a factor, smoothly (the mockup's zoomBy). */
  zoomBy(f: number, now: number): void {
    this.anim = null;
    const base = this.zoomTarget ?? this.zoom;
    this.zoomTarget = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, base * f));
    this.zoomLast = now;
  }

  /** Moves to a target in `ms` milliseconds (the mockup's animateTo). */
  animateTo(t: Target, now: number, ms = 360): void {
    this.zoomTarget = null;
    const from = { q: this.q.clone(), zoom: this.zoom, px: this.px, py: this.py };
    this.anim = {
      t0: now,
      ms,
      from,
      to: {
        q: (t.q ?? from.q).clone(),
        zoom: t.zoom ?? from.zoom,
        px: t.px ?? from.px,
        py: t.py ?? from.py,
      },
    };
  }

  /** The home view, animated. */
  home(now: number): void {
    this.animateTo({ q: quatOf(HOME), zoom: HOME.zoom, px: HOME.px, py: HOME.py }, now);
  }

  /** Framed again, the orientation kept (the mockup's fitView). */
  fit(now: number): void {
    this.animateTo({ zoom: 1, px: 0, py: 0 }, now);
  }

  /** Advances the animations to `now`; true while something moved. */
  step(now: number): boolean {
    let moved = false;
    if (this.anim !== null) {
      const a = this.anim;
      const k = Math.min(1, (now - a.t0) / a.ms);
      const e = ease(k);
      this.q = a.from.q.clone().slerp(a.to.q, e);
      this.zoom = a.from.zoom + (a.to.zoom - a.from.zoom) * e;
      this.px = a.from.px + (a.to.px - a.from.px) * e;
      this.py = a.from.py + (a.to.py - a.from.py) * e;
      if (k >= 1) {
        this.anim = null;
      }
      moved = true;
    }
    if (this.zoomTarget !== null) {
      const dt = Math.min(50, now - this.zoomLast);
      this.zoomLast = now;
      this.zoom += (this.zoomTarget - this.zoom) * (1 - Math.exp(-dt / 90));
      if (Math.abs(Math.log(this.zoomTarget / this.zoom)) <= 0.0015) {
        this.zoom = this.zoomTarget;
        this.zoomTarget = null;
      }
      moved = true;
    }
    return moved;
  }
}

/**
 * Pixels per mesh unit at the centre (the mockup's `sc`): the space's
 * largest side, with a margin, fills the smaller side of the viewport at
 * zoom 1.
 */
export function pixelsPerUnit(
  width: number,
  height: number,
  extent: readonly number[],
  zoom: number,
): number {
  const big = Math.max(...extent, 1);
  return (Math.min(width, height) / (big * 2.05 + 1.5)) * zoom;
}

/** The eye's distance from the centre in mesh units: the mockup's gentle perspective (64 / (64 − z)). */
export const EYE_DISTANCE = 64;
