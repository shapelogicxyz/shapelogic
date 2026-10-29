// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D view's camera: an orientation, a zoom and a pan. From
// docs/mockup/index.html: `cam`, `HOME`, the pointer handlers' orbit and
// pan (the "Top Up" method, the mockup's "Drag": yaw and pitch from the
// drag, pitch held inside ±89°, the roll respected), `zoomBy` / `zStep` (smooth wheel zoom, 0.3 to
// 4), `animateTo` (cubic ease in and out, 360 ms), `homeView`, `fitView`,
// `dirToView`, `viewToWorld`, `mkRot`. The "Free Spin" method is the
// original's arc-ball (src/gui/arcball.cpp, arcBall_c: two points of a
// virtual ball, their rotation added in front), turned step by step as the
// pointer moves rather than from the drag's start as the original did, on
// a ball that flattens smoothly towards its rim (arcballPoint): a drag past
// the far side no longer snaps back, and outside the rim a move about the
// centre rolls and a move away from it or towards it tilts, so the turn
// goes on however far the pointer goes. three.js's
// own controls follow neither map, so the logic is ported.
//
// The orientation is one quaternion `q` turning world (the mesh's
// coordinates) into view space (x right, y up, z towards the viewer); the
// mockup's yaw, pitch and roll are its Euler angles in the order Z, X, Y
// (view = Rz(roll) Rx(pitch) Ry(yaw) B world), so both methods turn the same
// camera and the view cube reads it. B is the "Z axis direction" setting's
// turn of the world into the upright frame, whose Y is up: the home view
// and the Top Up method's spin are about it. It is a turn, never a mirror,
// so the pieces keep their handedness.

import { Euler, MathUtils, Quaternion, Vector3 } from 'three';

import type { ZAxis } from '../shell/prefs.ts';

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

/** World to the upright frame of a "Z axis direction" (its Y is up, its Z towards the viewer). */
export function baseOf(z: ZAxis): Quaternion {
  const turn = (x: number, y: number, z: number, deg: number): Quaternion =>
    new Quaternion().setFromAxisAngle(new Vector3(x, y, z), MathUtils.degToRad(deg));
  switch (z) {
    case 'Z Towards':
      return new Quaternion();
    case 'Z Up':
      return turn(1, 0, 0, -90);
  }
}

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

/** A point of the viewport (pixels from its top left) in the arc-ball's plane: the centre 0, the rim 1, y up. */
function ballPlane(x: number, y: number, width: number, height: number): [number, number] {
  const aw = width > 1 && height > 1 ? 1 / ((width - 1) * 0.5) : 1;
  const ah = width > 1 && height > 1 ? 1 / ((height - 1) * 0.5) : 1;
  return [x * aw - 1, 1 - y * ah];
}

/**
 * A point of the viewport (pixels from its top left) on the arc-ball, as a
 * unit vector (arcBall_c::mapToSphere): on the ball near the centre, then
 * on Bell's sheet z = 1 / (2r) where the two meet (r² = ½), as three.js's
 * TrackballControls. The original's ball alone grows steep at its rim (a
 * small move there turned by a lot) and clamped every point outside onto
 * it (the turn stopped).
 */
export function arcballPoint(x: number, y: number, width: number, height: number): Vector3 {
  const [px, py] = ballPlane(x, y, width, height);
  const len = px * px + py * py;
  const pz = len <= 0.5 ? Math.sqrt(1 - len) : 0.5 / Math.sqrt(len);
  return new Vector3(px, py, pz).normalize();
}

/** The arc-ball's turn from one point of the ball to another (arcBall_c::getDrag). */
export function arcballTurn(from: Vector3, to: Vector3): Quaternion {
  const perp = new Vector3().crossVectors(from, to);
  if (perp.length() > 1e-5) {
    return new Quaternion(perp.x, perp.y, perp.z, from.dot(to));
  }
  return new Quaternion();
}

/** The sheet's tilt (radians per radius) and roll (per radian about the centre) at the rim, which carry on outside it. */
const RIM_TILT = 1.6;
const rimRoll = (r: number): number => (2 * r) / Math.sqrt(r * r + 0.25 / (r * r));

/**
 * The turn of one pointer move from (x0, y0) to (x1, y1) of a viewport of
 * this size: turned step by step, so it never snaps back. Inside the rim,
 * the arc-ball's between the two points. Outside it, where the sheet flattens
 * out and the turn would fade: the angle swept about the centre rolls and
 * the move away from it or towards it tilts, at the rim's rates, so the
 * turn goes on however far the pointer goes.
 */
export function arcballStep(
  x0: number,
  y0: number,
  x1: number,
  y1: number,
  width: number,
  height: number,
): Quaternion {
  const [ax, ay] = ballPlane(x0, y0, width, height);
  const [bx, by] = ballPlane(x1, y1, width, height);
  const ra = Math.hypot(ax, ay);
  const rb = Math.hypot(bx, by);
  if (ra <= 1 && rb <= 1) {
    return arcballTurn(arcballPoint(x0, y0, width, height), arcballPoint(x1, y1, width, height));
  }
  // the tilt is about the axis across the line to the centre, in the screen's plane
  const mx = (ax + bx) / 2;
  const my = (ay + by) / 2;
  const rm = Math.hypot(mx, my);
  const roll = new Quaternion().setFromAxisAngle(
    new Vector3(0, 0, 1),
    rimRoll(Math.max(rm, 1)) * Math.atan2(ax * by - ay * bx, ax * bx + ay * by),
  );
  if (rm < 1e-9) {
    return roll;
  }
  const tilt = new Quaternion().setFromAxisAngle(
    new Vector3(-my / rm, mx / rm, 0),
    RIM_TILT * (rb - ra),
  );
  return roll.multiply(tilt);
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
  /** World to the upright frame (baseOf); the angles are measured in it. */
  private base = new Quaternion();
  /** World to view. */
  q = quatOf(HOME);
  zoom = HOME.zoom;
  /** The pan, in pixels of the viewport. */
  px = HOME.px;
  py = HOME.py;

  private anim: Animation | null = null;
  private zoomTarget: number | null = null;
  private zoomLast = 0;
  private arc: { x: number; y: number; w: number; h: number } | null = null;

  /** The mockup's angles of the orientation now, in the upright frame. */
  get angles(): Angles {
    return anglesOf(this.q.clone().multiply(this.base.clone().invert()));
  }

  /** The orientation of angles in the upright frame. */
  fromAngles(a: Angles): Quaternion {
    return quatOf(a).multiply(this.base);
  }

  /** A direction of the world in the upright frame (for dirToView). */
  upright(v: readonly [number, number, number]): [number, number, number] {
    const u = new Vector3(v[0], v[1], v[2]).applyQuaternion(this.base);
    return [u.x, u.y, u.z];
  }

  /**
   * Another upright frame: the view goes home in it, animated from `now`,
   * or at once without a time (the first view).
   */
  setBase(b: Quaternion, now?: number): void {
    if (b.equals(this.base)) {
      return;
    }
    this.base = b.clone();
    if (now === undefined) {
      this.stop();
      this.q = this.fromAngles(HOME);
      this.zoom = HOME.zoom;
      this.px = HOME.px;
      this.py = HOME.py;
    } else {
      this.home(now);
    }
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

  /** The "Top Up" orbit by a drag of (dx, dy) pixels (the mockup's "Drag" pointermove). */
  orbit(dx: number, dy: number): void {
    const a = this.angles;
    const rr = MathUtils.degToRad(a.roll);
    const cr = Math.cos(rr);
    const sr = Math.sin(rr);
    this.q = this.fromAngles({
      yaw: a.yaw + (dx * cr - dy * sr) * 0.5,
      pitch: Math.max(-89, Math.min(89, a.pitch + (dx * sr + dy * cr) * 0.5)),
      roll: a.roll,
    });
  }

  /** Starts an arc-ball turn at a point of a viewport of this size (arcBall_c::click). */
  arcStart(x: number, y: number, w: number, h: number): void {
    this.arc = { x, y, w, h };
  }

  /** Turns by the arc-ball from the last point to this one (arcBall_c::drag and addTransform, step by step). */
  arcDrag(x: number, y: number): void {
    const a = this.arc;
    if (a === null) {
      return;
    }
    this.q = arcballStep(a.x, a.y, x, y, a.w, a.h).multiply(this.q).normalize();
    a.x = x;
    a.y = y;
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
    this.animateTo({ q: this.fromAngles(HOME), zoom: HOME.zoom, px: HOME.px, py: HOME.py }, now);
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
