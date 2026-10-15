// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view cube in the 3D view's corner. From docs/mockup/index.html: `VC`,
// `HOME_C`, `ROLLS`, `cubeGeo`, `solveAB`, `cubeAligned`, `ARROWS`,
// `cubeHit`, `rollIcon`, `renderCube` and its pointer handlers: a canvas
// cube drawn as the main view turns (perspective as the view's projection),
// a face, edge or corner clicked turns the view to it, a face
// double-clicked stands its label upright, the house goes home, and when a
// face looks straight at the viewer arrows turn by 90° and two arcs roll.
// It is driven by the camera's quaternion (camera.ts) instead of the
// mockup's yaw, pitch and roll.

import { Quaternion } from 'three';

import { css } from '../shell/dom.ts';
import { anglesOf, dirToView, quatOf, toView, toWorld } from './camera.ts';
import type { Camera } from './camera.ts';

const VC = { w: 156, h: 170, cx: 78, cy: 100, s: 30, t: 0.56, D: 5.5 };
/**
 * The canvas's extra room left of and below the cube (styles.css's #vcube is
 * as large): the axes' lines and labels point any way from the corner there,
 * and must not be cut off at the canvas's edge.
 */
const PAD = { l: 16, b: 16 };
/** The axes' origin (in the canvas's own pixels), their lines' length and their labels' distance. */
const AXES = { x: 18 + PAD.l, y: 152, len: 20, label: 28 };
const HOME_C = [26, 40] as const;
const ROLLS = [
  { sg: -1, x: 103, y: 40 },
  { sg: 1, x: 134, y: 72 },
] as const;

type P2 = [number, number];
type V3 = [number, number, number];

interface Face {
  k: 0 | 1 | 2;
  sg: 1 | -1;
  n: V3;
  ua: number;
  va: number;
  nz: number;
  vis: boolean;
  qn: V3;
  pt(a: number, b: number): P2;
}

type Hit =
  | { type: 'home' }
  | { type: 'arrow'; nm: 'up' | 'down' | 'left' | 'right' }
  | { type: 'roll'; sg: number }
  | { type: 'face'; f: Face; ia: number; ib: number; dir: V3 };

function inPoly(pt: P2, poly: P2[]): boolean {
  let c = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i];
    const b = poly[j];
    if (a === undefined || b === undefined) {
      continue;
    }
    if (
      a[1] > pt[1] !== b[1] > pt[1] &&
      pt[0] < ((b[0] - a[0]) * (pt[1] - a[1])) / (b[1] - a[1]) + a[0]
    ) {
      c = !c;
    }
  }
  return c;
}

function shade(hex: string, f: number): string {
  const n = Number.parseInt(hex.slice(1), 16);
  let r = n >> 16;
  let g = (n >> 8) & 255;
  let b = n & 255;
  if (f > 1) {
    const t = f - 1;
    r += (255 - r) * t;
    g += (255 - g) * t;
    b += (255 - b) * t;
  } else {
    r *= f;
    g *= f;
    b *= f;
  }
  return `rgb(${r | 0},${g | 0},${b | 0})`;
}

/** What the cube asks of the view. */
export interface ViewCubeHost {
  readonly camera: Camera;
  /** Whether the view is in perspective (the cube follows). */
  perspective(): boolean;
  /** Whether the theme is dark. */
  dark(): boolean;
  /** The view moved: render. */
  moved(): void;
}

export class ViewCube {
  private readonly canvas: HTMLCanvasElement;
  private readonly host: ViewCubeHost;
  private hover: Hit | null = null;

  constructor(canvas: HTMLCanvasElement, host: ViewCubeHost) {
    this.canvas = canvas;
    this.host = host;
    const pos = (e: MouseEvent): P2 => {
      const r = canvas.getBoundingClientRect();
      return [e.clientX - r.left - PAD.l, e.clientY - r.top];
    };
    canvas.addEventListener('pointermove', (e) => {
      const [px, py] = pos(e);
      const h = this.hit(px, py);
      this.hover = h;
      canvas.style.cursor = h !== null ? 'pointer' : 'default';
      canvas.title =
        h === null
          ? ''
          : h.type === 'home'
            ? 'Home — default view, fitted'
            : h.type === 'arrow'
              ? 'Rotate 90°'
              : h.type === 'roll'
                ? 'Roll 90°'
                : h.ia !== 0 && h.ib !== 0
                  ? 'Snap to corner view'
                  : h.ia !== 0 || h.ib !== 0
                    ? 'Snap to edge view'
                    : 'Snap to face view — double-click to stand the label upright';
      this.draw();
    });
    canvas.addEventListener('pointerleave', () => {
      this.hover = null;
      this.draw();
    });
    canvas.addEventListener('dblclick', (e) => {
      const [px, py] = pos(e);
      const h = this.hit(px, py);
      if (h?.type !== 'face' || h.ia !== 0 || h.ib !== 0) {
        return;
      }
      const cam = this.host.camera;
      const v = dirToView(h.dir, cam.angles.yaw);
      const u: V3 = [0, 0, 0];
      u[h.f.ua] = 1;
      const q = toView(quatOf(v), u);
      v.roll = Math.round((-Math.atan2(q.y, q.x) * 180) / Math.PI / 90) * 90;
      this.go(quatOf(v));
    });
    canvas.addEventListener('click', (e) => {
      const [px, py] = pos(e);
      const h = this.hit(px, py);
      const cam = this.host.camera;
      if (h === null) {
        return;
      }
      if (h.type === 'home') {
        cam.home(performance.now());
        this.host.moved();
      } else if (h.type === 'face') {
        this.go(quatOf(dirToView(h.dir, cam.angles.yaw)));
      } else if (h.type === 'roll') {
        const a = cam.angles;
        this.go(quatOf({ ...a, roll: a.roll + (h.sg < 0 ? 90 : -90) }));
      } else {
        const v: V3 = {
          up: [0, 1, 0] as V3,
          down: [0, -1, 0] as V3,
          left: [-1, 0, 0] as V3,
          right: [1, 0, 0] as V3,
        }[h.nm];
        const w = toWorld(cam.q, v);
        this.go(
          quatOf(dirToView([Math.round(w.x), Math.round(w.y), Math.round(w.z)], cam.angles.yaw)),
        );
      }
    });
  }

  private go(q: Quaternion): void {
    this.host.camera.animateTo({ q }, performance.now());
    this.host.moved();
  }

  /** Shows or hides the cube (the "Show view cube" setting). */
  setVisible(on: boolean): void {
    this.canvas.style.display = on ? '' : 'none';
  }

  // the cube's faces as the main view turns them
  private geo(q: Quaternion = this.host.camera.q): Face[] {
    const persp = this.host.perspective();
    const proj = (v: V3): P2 => {
      const p = toView(q, v);
      const k = persp ? VC.D / (VC.D - p.z) : 1;
      return [VC.cx + p.x * VC.s * k, VC.cy - p.y * VC.s * k];
    };
    const faces: Face[] = [];
    for (const k of [0, 1, 2] as const) {
      for (const sg of [1, -1] as const) {
        const n: V3 = [0, 0, 0];
        n[k] = sg;
        const ua = (k + 1) % 3;
        const va = (k + 2) % 3;
        const qv = toView(q, n);
        const qn: V3 = [qv.x, qv.y, qv.z];
        faces.push({
          k,
          sg,
          n,
          ua,
          va,
          nz: qn[2],
          vis: persp ? VC.D * qn[2] - 1 > 0 : qn[2] > 1e-4,
          qn,
          pt: (a: number, b: number): P2 => {
            const v: V3 = [n[0], n[1], n[2]];
            v[ua] = (v[ua] ?? 0) + a;
            v[va] = (v[va] ?? 0) + b;
            return proj(v);
          },
        });
      }
    }
    return faces;
  }

  // the face coordinates (a, b) of a point of the cube's picture
  private static solveAB(f: Face, px: number, py: number): P2 {
    let a = 0;
    let b = 0;
    for (let i = 0; i < 10; i++) {
      const p = f.pt(a, b);
      const pa = f.pt(a + 1e-3, b);
      const pb = f.pt(a, b + 1e-3);
      const ja = [(pa[0] - p[0]) * 1e3, (pa[1] - p[1]) * 1e3] as const;
      const jb = [(pb[0] - p[0]) * 1e3, (pb[1] - p[1]) * 1e3] as const;
      const det = ja[0] * jb[1] - ja[1] * jb[0];
      if (Math.abs(det) < 1e-9) {
        break;
      }
      const ex = px - p[0];
      const ey = py - p[1];
      a += (ex * jb[1] - ey * jb[0]) / det;
      b += (ja[0] * ey - ja[1] * ex) / det;
    }
    return [a, b];
  }

  // a face looking straight at the viewer, the roll left out
  private aligned(): boolean {
    const a = anglesOf(this.host.camera.q);
    const q = quatOf({ ...a, roll: 0 });
    for (const k of [0, 1, 2]) {
      for (const sg of [1, -1]) {
        const n: V3 = [0, 0, 0];
        n[k] = sg;
        if (toView(q, n).z > 0.9995) {
          return true;
        }
      }
    }
    return false;
  }

  private static arrows(): [Extract<Hit, { type: 'arrow' }>['nm'], number, number][] {
    const d = VC.s + 20;
    return [
      ['up', VC.cx, VC.cy - d],
      ['down', VC.cx, VC.cy + d],
      ['left', VC.cx - d, VC.cy],
      ['right', VC.cx + d, VC.cy],
    ];
  }

  private hit(px: number, py: number): Hit | null {
    if (Math.abs(px - HOME_C[0]) <= 15 && Math.abs(py - HOME_C[1]) <= 15) {
      return { type: 'home' };
    }
    if (this.aligned()) {
      for (const [nm, x, y] of ViewCube.arrows()) {
        if (Math.hypot(px - x, py - y) <= 12) {
          return { type: 'arrow', nm };
        }
      }
      for (const r of ROLLS) {
        if (Math.hypot(px - r.x, py - r.y) <= 16) {
          return { type: 'roll', sg: r.sg };
        }
      }
    }
    for (const f of this.geo()
      .filter((x) => x.vis)
      .sort((a, b) => b.nz - a.nz)) {
      const quad = [f.pt(-1, -1), f.pt(1, -1), f.pt(1, 1), f.pt(-1, 1)];
      if (!inPoly([px, py], quad)) {
        continue;
      }
      const [a, b] = ViewCube.solveAB(f, px, py);
      const ia = a > VC.t ? 1 : a < -VC.t ? -1 : 0;
      const ib = b > VC.t ? 1 : b < -VC.t ? -1 : 0;
      const dir: V3 = [f.n[0], f.n[1], f.n[2]];
      dir[f.ua] = (dir[f.ua] ?? 0) + ia;
      dir[f.va] = (dir[f.va] ?? 0) + ib;
      return { type: 'face', f, ia, ib, dir };
    }
    return null;
  }

  private static rollIcon(g: CanvasRenderingContext2D, sg: number, col: string): void {
    g.save();
    g.strokeStyle = g.fillStyle = col;
    g.lineWidth = 2.2;
    g.lineCap = 'round';
    g.lineJoin = 'round';
    // two half-circle arrows around the cube's upper-right corner: an arch
    // over the top (head at its left end, pointing down) and a ")" down the
    // right side (head at its bottom end, pointing left)
    const r = 9;
    let hx: number;
    let hy: number;
    let tx: number;
    let ty: number;
    if (sg < 0) {
      g.beginPath();
      g.arc(103, 44, r, 0, -Math.PI, true);
      g.stroke();
      hx = 103 - r;
      hy = 44;
      tx = 0;
      ty = 1;
    } else {
      g.beginPath();
      g.arc(132, 72, r, -Math.PI / 2, Math.PI / 2, false);
      g.stroke();
      hx = 132;
      hy = 72 + r;
      tx = -1;
      ty = 0;
    }
    const nx = -ty;
    const ny = tx;
    g.beginPath();
    g.moveTo(hx + tx * 8.5, hy + ty * 8.5);
    g.lineTo(hx + nx * 6, hy + ny * 6);
    g.lineTo(hx - nx * 6, hy - ny * 6);
    g.closePath();
    g.fill();
    g.restore();
  }

  /** Draws the cube for the camera now. */
  draw(): void {
    const c = this.canvas;
    if (c.style.display === 'none') {
      return;
    }
    const r = window.devicePixelRatio || 1;
    const bw = Math.round((VC.w + PAD.l) * r);
    const bh = Math.round((VC.h + PAD.b) * r);
    if (c.width !== bw || c.height !== bh) {
      c.width = bw;
      c.height = bh;
    }
    const vp = c.parentElement;
    c.style.top = vp !== null && vp.clientWidth < 640 ? '76px' : '';
    const g = c.getContext('2d');
    if (g === null) {
      return;
    }
    g.setTransform(r, 0, 0, r, 0, 0);
    g.clearRect(0, 0, VC.w + PAD.l, VC.h + PAD.b);
    g.translate(PAD.l, 0);
    const dark = this.host.dark();
    const ink = dark ? '#e7eaf0' : '#2a2f3a';
    const faceC = dark ? '#4a5262' : '#d9dde4';
    const edge = dark ? '#aab1bf' : '#3a3f4a';
    const hi = '#4f7df0';
    const path = (pts: P2[]): void => {
      g.beginPath();
      pts.forEach((p, i) => {
        if (i === 0) {
          g.moveTo(p[0], p[1]);
        } else {
          g.lineTo(p[0], p[1]);
        }
      });
      g.closePath();
    };
    const reg = (f: Face, a0: number, a1: number, b0: number, b1: number): P2[] => [
      f.pt(a0, b0),
      f.pt(a1, b0),
      f.pt(a1, b1),
      f.pt(a0, b1),
    ];
    const al = this.aligned();
    const faces = this.geo()
      .filter((f) => f.vis)
      .sort((a, b) => a.nz - b.nz);
    const axc = ['--ax', '--ay', '--az'] as const;
    const L = [-0.35, 0.75, 0.55] as const;
    const ln = Math.hypot(...L);
    const hover = this.hover;
    // the direction pointed at (a face centre, an edge or a corner), painted on every face it touches
    const hd = hover?.type === 'face' ? hover.dir : null;
    if (hover?.type === 'home') {
      g.fillStyle = 'rgba(79,125,240,.18)';
      g.beginPath();
      g.arc(HOME_C[0], HOME_C[1], 15, 0, 7);
      g.fill();
    }
    g.save();
    g.translate(HOME_C[0] - 19, HOME_C[1] - 21);
    g.fillStyle = ink;
    g.beginPath();
    g.moveTo(19, 8);
    g.lineTo(32, 20);
    g.lineTo(28, 20);
    g.lineTo(28, 33);
    g.lineTo(10, 33);
    g.lineTo(10, 20);
    g.lineTo(6, 20);
    g.closePath();
    g.fill();
    g.fillStyle = dark ? '#1a1d24' : '#ffffff';
    g.fillRect(16, 24, 6, 9);
    g.restore();
    for (const f of faces) {
      const dot = (f.qn[0] * L[0] + f.qn[1] * L[1] + f.qn[2] * L[2]) / ln;
      path(reg(f, -1, 1, -1, 1));
      g.fillStyle = shade(faceC, 0.72 + 0.42 * Math.max(0, dot));
      g.fill();
      if (hd !== null && hd[f.k] === f.sg) {
        const t = VC.t;
        const ia = hd[f.ua] ?? 0;
        const ib = hd[f.va] ?? 0;
        const ra = ia === 0 ? [-t, t] : ia > 0 ? [t, 1] : [-1, -t];
        const rb = ib === 0 ? [-t, t] : ib > 0 ? [t, 1] : [-1, -t];
        path(reg(f, ra[0] ?? 0, ra[1] ?? 0, rb[0] ?? 0, rb[1] ?? 0));
        g.fillStyle = hi;
        g.fill();
      }
      path(reg(f, -1, 1, -1, 1));
      g.strokeStyle = edge;
      g.lineWidth = 1.2;
      g.lineJoin = 'round';
      g.stroke();
      // the label: the face's affine approximation at its centre
      const C = f.pt(0, 0);
      const U = f.pt(1, 0);
      const V = f.pt(0, 1);
      const ux = U[0] - C[0];
      const uy = U[1] - C[1];
      let vx = V[0] - C[0];
      let vy = V[1] - C[1];
      if (ux * vy - uy * vx < 0) {
        vx = -vx;
        vy = -vy;
      }
      const single =
        hd !== null &&
        hd[f.k] === f.sg &&
        Math.abs(hd[0]) + Math.abs(hd[1]) + Math.abs(hd[2]) === 1;
      g.save();
      g.transform(ux, uy, vx, vy, C[0], C[1]);
      g.font = '800 .5px system-ui,sans-serif';
      g.textAlign = 'center';
      g.textBaseline = 'middle';
      g.fillStyle = single ? '#fff' : css(axc[f.k]);
      g.fillText((f.sg > 0 ? '+' : '−') + 'XYZ'.charAt(f.k), 0, 0.04);
      g.restore();
    }
    if (al) {
      const tri = {
        up: [
          [0, -6],
          [-6, 4],
          [6, 4],
        ],
        down: [
          [0, 6],
          [-6, -4],
          [6, -4],
        ],
        left: [
          [-6, 0],
          [4, -6],
          [4, 6],
        ],
        right: [
          [6, 0],
          [-4, -6],
          [-4, 6],
        ],
      } as const;
      for (const [nm, x, y] of ViewCube.arrows()) {
        const h = hover?.type === 'arrow' && hover.nm === nm;
        g.fillStyle = h ? hi : ink;
        path(tri[nm].map(([a, b]) => [x + a, y + b] as P2));
        g.fill();
      }
      for (const rl of ROLLS) {
        ViewCube.rollIcon(g, rl.sg, hover?.type === 'roll' && hover.sg === rl.sg ? hi : ink);
      }
    }
    g.translate(-PAD.l, 0);
    const q = this.host.camera.q;
    for (const [n, e, cv] of [
      ['X', [1, 0, 0], '--ax'],
      ['Y', [0, 1, 0], '--ay'],
      ['Z', [0, 0, 1], '--az'],
    ] as const) {
      const v = toView(q, e);
      g.strokeStyle = css(cv);
      g.lineWidth = 2;
      g.beginPath();
      g.moveTo(AXES.x, AXES.y);
      g.lineTo(AXES.x + v.x * AXES.len, AXES.y - v.y * AXES.len);
      g.stroke();
      g.fillStyle = css(cv);
      g.font = '700 10px sans-serif';
      g.textAlign = 'center';
      g.textBaseline = 'middle';
      g.fillText(n, AXES.x + v.x * AXES.label, AXES.y - v.y * AXES.label);
    }
  }
}
