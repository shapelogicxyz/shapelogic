// SPDX-License-Identifier: GPL-3.0-or-later
//
// The border of a sliding puzzle's tray in the 3D views (decision 0046):
// the tray's body round its floor, as the Play board draws it from above.
// A rim round the board's rectangle (BORDER_RIM wide when thin, else as
// many cells wide as the border is voxels thick), its outer corners
// round (a quarter circle of the rim's width) or square, and a block on
// every cell that is no floor, all between two heights; so the floor lies
// sunk in it. Built here, not by the engine: boxes and a ring from numbers
// the engine has already given (the board's size and which cells are
// floor), as tray3d.ts builds a tilted tray. One geometry in the board's
// cells, drawn once per tray shown (the Puzzle view shows two). The
// material is the views' light (material.ts's: the original's fixed
// light) over the border's color or its picture, which lies over the
// tray's whole outer rectangle, with the surface the material names: a
// fine grain, or wood's streaks along x. The geometry is DOM-free, for the
// tests.

import {
  BufferAttribute,
  BufferGeometry,
  DoubleSide,
  Group,
  Mesh,
  ShaderMaterial,
  Vector3,
} from 'three';
import type { Texture } from 'three';

import type { BorderMaterial } from '../../engine/api.generated.ts';
import { invert, pictureToCells } from '../shell/picture-math.ts';
import type { PiecePicture } from '../shell/picture-math.ts';
import { hex } from '../shell/piece-colors.ts';
import type { Rgb3 } from '../shell/piece-colors.ts';
import { BORDER_RIM } from '../shell/tray-border.ts';
import { pictureTexture } from './material.ts';

type V3 = readonly [number, number, number];

/** A tray's border to build, in the board's cells. */
export interface BorderShape {
  /** The board's size in x and y. */
  size: readonly [number, number];
  /** Whether a cell inside the board is floor (on any layer). */
  floor: (x: number, y: number) => boolean;
  /** The heights the border stands between. */
  z0: number;
  z1: number;
  /** Round outer corners (false: square). */
  round: boolean;
  /** The rim's width (BORDER_RIM when left out). */
  rim?: number;
}

/** The border's triangles: xyz per vertex, three vertices per triangle, with their normals. */
export interface BorderArrays {
  positions: Float32Array;
  normals: Float32Array;
}

/**
 * Which cells of a board are floor, from its mesh: a cell with a triangle
 * (`voxel`: each triangle's cell index x + X (y + Y z), the frame's) on
 * any layer.
 */
export function floorOfMesh(
  voxel: Uint32Array,
  size: readonly number[],
): (x: number, y: number) => boolean {
  const X = size[0] ?? 1;
  const layer = X * (size[1] ?? 1);
  const open = new Set<number>();
  for (const v of voxel) {
    open.add(v % layer);
  }
  return (x, y) => open.has(x + X * y);
}

/** The segments of a round corner's quarter circle. */
const ARC = 8;

/** The triangles of a tray's border (see the file's head). */
export function borderArrays(b: BorderShape): BorderArrays {
  const [sx, sy] = b.size;
  const r = b.rim ?? BORDER_RIM;
  const { z0, z1 } = b;
  const pos: number[] = [];
  const nor: number[] = [];
  const vertex = (p: V3, n: V3): void => {
    pos.push(p[0], p[1], p[2]);
    nor.push(n[0], n[1], n[2]);
  };
  const floorAt = (x: number, y: number): boolean =>
    x >= 0 && y >= 0 && x < sx && y < sy && b.floor(x, y);

  // a convex outline in x and y: the border's top over it and its bottom under it
  const flat = (pts: readonly (readonly [number, number])[]): void => {
    const [a] = pts;
    if (a === undefined) {
      return;
    }
    for (const [z, n] of [
      [z1, [0, 0, 1]],
      [z0, [0, 0, -1]],
    ] as const) {
      for (let i = 1; i + 1 < pts.length; i++) {
        const p = pts[i] ?? a;
        const q = pts[i + 1] ?? a;
        vertex([a[0], a[1], z], n);
        vertex([p[0], p[1], z], n);
        vertex([q[0], q[1], z], n);
      }
    }
  };
  // a wall from p to q between the heights, with a normal at each end
  const wall = (
    p: readonly [number, number],
    q: readonly [number, number],
    np: readonly [number, number],
    nq: readonly [number, number],
  ): void => {
    const a: V3 = [p[0], p[1], z0];
    const c: V3 = [q[0], q[1], z0];
    const d: V3 = [q[0], q[1], z1];
    const e: V3 = [p[0], p[1], z1];
    const n0: V3 = [np[0], np[1], 0];
    const n1: V3 = [nq[0], nq[1], 0];
    vertex(a, n0);
    vertex(c, n1);
    vertex(d, n1);
    vertex(a, n0);
    vertex(d, n1);
    vertex(e, n0);
  };

  // the cells that are no floor
  for (let y = 0; y < sy; y++) {
    for (let x = 0; x < sx; x++) {
      if (!floorAt(x, y)) {
        flat([
          [x, y],
          [x + 1, y],
          [x + 1, y + 1],
          [x, y + 1],
        ]);
      }
    }
  }
  // the rim's four sides, each with its outer wall
  flat([
    [0, -r],
    [sx, -r],
    [sx, 0],
    [0, 0],
  ]);
  wall([0, -r], [sx, -r], [0, -1], [0, -1]);
  flat([
    [sx, 0],
    [sx + r, 0],
    [sx + r, sy],
    [sx, sy],
  ]);
  wall([sx + r, 0], [sx + r, sy], [1, 0], [1, 0]);
  flat([
    [0, sy],
    [sx, sy],
    [sx, sy + r],
    [0, sy + r],
  ]);
  wall([sx, sy + r], [0, sy + r], [0, 1], [0, 1]);
  flat([
    [-r, 0],
    [0, 0],
    [0, sy],
    [-r, sy],
  ]);
  wall([-r, sy], [-r, 0], [-1, 0], [-1, 0]);
  // its corners: a quarter circle round the board's corner, or a square
  for (const [cx, cy, dx, dy] of [
    [0, 0, -1, -1],
    [sx, 0, 1, -1],
    [sx, sy, 1, 1],
    [0, sy, -1, 1],
  ] as const) {
    const pts: [number, number][] = [];
    const normals: [number, number][] = [];
    if (b.round) {
      for (let k = 0; k <= ARC; k++) {
        const t = (k / ARC) * (Math.PI / 2);
        const nx = dx * Math.cos(t);
        const ny = dy * Math.sin(t);
        pts.push([cx + r * nx, cy + r * ny]);
        normals.push([nx, ny]);
      }
    } else {
      pts.push(
        [cx + dx * r, cy],
        [cx + dx * r, cy + dy * r],
        [cx + dx * r, cy + dy * r],
        [cx, cy + dy * r],
      );
      normals.push([dx, 0], [dx, 0], [0, dy], [0, dy]);
    }
    flat([[cx, cy], ...pts]);
    for (let k = 0; k + 1 < pts.length; k++) {
      const p = pts[k];
      const q = pts[k + 1];
      const np = normals[k];
      const nq = normals[k + 1];
      if (p !== undefined && q !== undefined && np !== undefined && nq !== undefined) {
        if (p[0] !== q[0] || p[1] !== q[1]) {
          wall(p, q, np, nq);
        }
      }
    }
  }
  // the walls round the floor: each side of a floor cell that has no floor beyond it
  for (let y = 0; y < sy; y++) {
    for (let x = 0; x < sx; x++) {
      if (!floorAt(x, y)) {
        continue;
      }
      if (!floorAt(x - 1, y)) {
        wall([x, y], [x, y + 1], [1, 0], [1, 0]);
      }
      if (!floorAt(x + 1, y)) {
        wall([x + 1, y], [x + 1, y + 1], [-1, 0], [-1, 0]);
      }
      if (!floorAt(x, y - 1)) {
        wall([x, y], [x + 1, y], [0, 1], [0, 1]);
      }
      if (!floorAt(x, y + 1)) {
        wall([x, y + 1], [x + 1, y + 1], [0, -1], [0, -1]);
      }
    }
  }
  return { positions: new Float32Array(pos), normals: new Float32Array(nor) };
}

const vertexShader = /* glsl */ `
varying vec3 vNormal;
varying vec3 vViewPos;
varying vec3 vPos;
void main() {
  vec4 mv = modelViewMatrix * vec4(position, 1.0);
  vViewPos = mv.xyz;
  vNormal = normalize(normalMatrix * normal);
  vPos = position;
  gl_Position = projectionMatrix * mv;
}
`;

// the material's surface is a factor on the color, so it lies on a picture
// as on a color
const fragmentShader = /* glsl */ `
uniform vec3 uColor;
uniform float uLit;
uniform vec3 uLightPos;
uniform sampler2D uImage;
uniform float uImageOn;
uniform vec3 uUvX;
uniform vec3 uUvY;
uniform int uSurface;
varying vec3 vNormal;
varying vec3 vViewPos;
varying vec3 vPos;

float hash(vec3 p) {
  p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
  vec3 i = floor(x);
  vec3 f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(mix(hash(i), hash(i + vec3(1.0, 0.0, 0.0)), f.x),
        mix(hash(i + vec3(0.0, 1.0, 0.0)), hash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
    mix(mix(hash(i + vec3(0.0, 0.0, 1.0)), hash(i + vec3(1.0, 0.0, 1.0)), f.x),
        mix(hash(i + vec3(0.0, 1.0, 1.0)), hash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y),
    f.z);
}

void main() {
  vec3 c = uColor;
  if (uImageOn > 0.5) {
    vec3 p = vec3(vPos.xy, 1.0);
    vec2 st = vec2(dot(uUvX, p), dot(uUvY, p));
    // the color past the picture's edges and where it is clear
    if (all(greaterThanEqual(st, vec2(0.0))) && all(lessThanEqual(st, vec2(1.0)))) {
      vec4 t = texture2D(uImage, st);
      c = mix(c, t.rgb, t.a);
    }
  }
  if (uSurface == 1) {
    // a fine grain of light and dark specks
    float n = noise(vPos * 26.0) * 0.6 + noise(vPos * 61.0) * 0.4;
    c *= 0.86 + 0.28 * n;
  } else if (uSurface == 2) {
    // wood: long streaks along x, the rings across them bent a little
    float bend = noise(vec3(vPos.x * 0.45, vPos.y * 1.3, vPos.z * 1.3));
    float rings = sin((vPos.y + vPos.z * 0.8) * 11.0 + bend * 7.0);
    float fine = noise(vec3(vPos.x * 3.0, vPos.y * 70.0, vPos.z * 70.0));
    c *= 0.84 + 0.09 * rings + 0.14 * fine;
  }
  if (uLit > 0.5) {
    vec3 l = normalize(uLightPos - vViewPos);
    vec3 n = normalize(vNormal);
    c = c * (0.4 + 0.6 * max(0.0, dot(n, l)));
  }
  gl_FragColor = vec4(c, 1.0);
}
`;

/** How a border looks. */
export interface BorderLook {
  /** Its color (the picture's place while it has none, or is not decoded yet). */
  color: Rgb3;
  material: BorderMaterial;
  lit: boolean;
  /** Its picture laid over the tray's outer rectangle (shell/tray-border.ts's borderImage); null: none. */
  picture: PiecePicture | null;
}

/** Where a border stands: its shape, and the corner of each tray it is drawn round. */
export interface BorderPlace extends BorderShape {
  at: readonly V3[];
}

const SURFACE: Record<BorderMaterial, number> = { smooth: 0, textured: 1, wood: 2 };

/** The borders of the trays a view shows. */
export class TrayBorderMesh {
  readonly group = new Group();
  private readonly uniforms = {
    uColor: { value: new Vector3(1, 1, 1) },
    uLit: { value: 1 },
    uLightPos: { value: new Vector3(700, 200, 700) },
    uImage: { value: null as Texture | null },
    uImageOn: { value: 0 },
    uUvX: { value: new Vector3(1, 0, 0) },
    uUvY: { value: new Vector3(0, 1, 0) },
    uSurface: { value: 0 },
  };
  private readonly material = new ShaderMaterial({
    uniforms: this.uniforms,
    vertexShader,
    fragmentShader,
    side: DoubleSide,
    // behind a piece's face in the same plane, always (the faces are pushed back by 1)
    polygonOffset: true,
    polygonOffsetFactor: 2,
    polygonOffsetUnits: 4,
  });
  private geometry: BufferGeometry | null = null;
  private key = '';
  private round = true;
  private rimWidth = BORDER_RIM;
  private look = '';

  /** Shows the borders (null: none); built again only when they differ. */
  set(place: BorderPlace | null): void {
    let key = '';
    if (place !== null) {
      const [sx, sy] = place.size;
      let cells = '';
      for (let y = 0; y < sy; y++) {
        for (let x = 0; x < sx; x++) {
          cells += place.floor(x, y) ? '1' : '0';
        }
      }
      key = `${String(sx)}x${String(sy)}:${cells}:${String(place.z0)}:${String(place.z1)}:${String(place.round)}:${String(place.rim ?? BORDER_RIM)}:${JSON.stringify(place.at)}`;
    }
    if (key === this.key) {
      return;
    }
    this.key = key;
    this.group.clear();
    this.geometry?.dispose();
    this.geometry = null;
    if (place === null) {
      return;
    }
    this.round = place.round;
    this.rimWidth = place.rim ?? BORDER_RIM;
    const a = borderArrays(place);
    const g = new BufferGeometry();
    g.setAttribute('position', new BufferAttribute(a.positions, 3));
    g.setAttribute('normal', new BufferAttribute(a.normals, 3));
    this.geometry = g;
    for (const at of place.at) {
      const m = new Mesh(g, this.material);
      m.position.set(at[0], at[1], at[2]);
      this.group.add(m);
    }
  }

  /** Applies a look; nothing is rebuilt. */
  setLook(look: BorderLook): void {
    const u = this.uniforms;
    u.uColor.value.set(look.color[0], look.color[1], look.color[2]);
    u.uLit.value = look.lit ? 1 : 0;
    u.uSurface.value = SURFACE[look.material];
    const p = look.picture;
    if (p === null) {
      u.uImageOn.value = 0;
      u.uImage.value = null;
    } else {
      // a point of the tray to its place in the picture, 0 to 1 across the whole of it
      const t = invert(pictureToCells(p.image));
      const w = p.picture.width;
      const h = p.picture.height;
      u.uUvX.value.set(t[0] / w, t[2] / w, t[4] / w);
      u.uUvY.value.set(t[1] / h, t[3] / h, t[5] / h);
      u.uImage.value = pictureTexture(p);
      u.uImageOn.value = 1;
    }
    this.look = `${look.material}:${hex(look.color)}:${p === null ? '-' : p.picture.hash}`;
  }

  /** How many borders are drawn. */
  get count(): number {
    return this.group.children.length;
  }

  /** The rim's width of the borders drawn, in cells. */
  get rim(): number {
    return this.rimWidth;
  }

  /**
   * What is drawn (the e2e tests read it):
   * count:corners:material:color:picture, then :rim<width> for a thick
   * border; '' for none.
   */
  describe(): string {
    return this.count === 0
      ? ''
      : `${String(this.count)}:${this.round ? 'round' : 'sharp'}:${this.look}${this.rimWidth === BORDER_RIM ? '' : `:rim${String(this.rimWidth)}`}`;
  }

  dispose(): void {
    this.set(null);
    this.material.dispose();
  }
}
