// SPDX-License-Identifier: GPL-3.0-or-later
//
// File ▸ Export ▸ Vector Image (stage 7): the view as an SVG file, drawn
// by three.js's SVGRenderer over the flat meshes (shape.mesh and
// playback.mesh in the "flat" style) of what the view shows: the shape,
// the pieces scene or the solution, each where the view has it, from the
// view's camera, at the view's size. SVG has no depth buffer: everything
// is painted from back to front, in the order of a BSP tree
// (depth-order.ts; three.js's own sort, by each triangle's centre, put
// large faces over smaller ones in front of them and the edge lines under
// their faces), which SVGRenderer then writes as it is. Only the faces
// turned towards the eye are kept (a shape's flat faces enclose it), each
// in the colour the view's shader gives it (material.ts: the piece colour
// darkened, the constraint colour in the colour view, the variable marker
// black, the dimmed layers, the original's light) computed here per
// triangle; the flat style's black edge lines are painted just after the
// faces they lie on, and the variable marks (a little above their faces)
// after those.
// A piece shown as a wire frame is its edge lines only. What is not
// drawn: the overlays (axes, bounds, the layer's slab), the labels of the
// pieces scene, the inside faces of x-ray. The original exported its
// view through gl2ps (vectorexportwindow.cpp), with a choice of formats;
// SVG is the one kept.

import {
  BufferAttribute,
  BufferGeometry,
  Color,
  LineBasicMaterial,
  LineSegments,
  Matrix3,
  Matrix4,
  Mesh,
  MeshBasicMaterial,
  PerspectiveCamera,
  Scene,
  ShaderMaterial,
  SRGBColorSpace,
  Vector3,
} from 'three';
import type { DataTexture } from 'three';
import { SVGRenderer } from 'three/addons/renderers/SVGRenderer.js';

import type { MeshArrays } from '../../engine/frames.ts';
import { paintingOrder } from './depth-order.ts';
import type { Face, P3, Segment } from './depth-order.ts';
import { buildMesh, FLAG } from './mesh.ts';
import type { MeshSource } from './mesh.ts';
import type { ViewRenderer } from './renderer.ts';

/** A mesh the view draws, as the vector image needs it. */
interface Drawn {
  source: MeshSource;
  size: [number, number, number];
  matrixWorld: Matrix4;
  uniforms: Record<string, { value: unknown }>;
  /** A wire-frame piece: its edge lines only. */
  wire: boolean;
}

/** The flat mesh of a source (shape.mesh or playback.mesh, style flat). */
export type FlatMesh = (source: MeshSource) => Promise<MeshArrays>;

/** What a face is painted with: its colour (three.js's linear numbers) and opacity. */
interface Paint {
  colour: [number, number, number];
  alpha: number;
}

function sourceKey(s: MeshSource): string {
  return s.kind === 'shape'
    ? `shape:${s.shape}:${String(s.revision)}:${String(s.transformation)}`
    : `playback:${s.handle}:${String(s.piece)}`;
}

// the meshes drawn now: faces with a source, visible all the way up
function drawnMeshes(scene: Scene): Drawn[] {
  const out: Drawn[] = [];
  scene.traverseVisible((o) => {
    if (!(o instanceof Mesh) || o.userData.faces !== true) {
      return;
    }
    const g = o.geometry as BufferGeometry;
    const source = g.userData.source as MeshSource | null | undefined;
    const m = o.material as ShaderMaterial;
    if (source == null || !(m instanceof ShaderMaterial)) {
      return;
    }
    out.push({
      source,
      size: (g.userData.size as [number, number, number] | undefined) ?? [1, 1, 1],
      matrixWorld: o.matrixWorld.clone(),
      uniforms: m.uniforms,
      wire: g.userData.style === 'wireframe',
    });
  });
  return out;
}

function num(u: Record<string, { value: unknown }>, name: string, fallback: number): number {
  const v = u[name]?.value;
  return typeof v === 'number' ? v : fallback;
}

// the shader's colour of a face (material.ts), before the light
function faceColour(
  u: Record<string, { value: unknown }>,
  flags: number,
  colour: number,
  cell: [number, number, number],
): [number, number, number] {
  const piece = u.uPiece?.value as Vector3 | undefined;
  const p: [number, number, number] = [piece?.x ?? 1, piece?.y ?? 1, piece?.z ?? 1];
  let c: [number, number, number];
  const marker = (flags & FLAG.variableFace) !== 0;
  if (num(u, 'uColourView', 0) > 0.5 && colour > 0 && !marker) {
    const data = (u.uPalette?.value as DataTexture | undefined)?.image.data as
      Uint8Array | undefined;
    c = [0, 1, 2].map((k) => (data?.[4 * colour + k] ?? 255) / 255) as [number, number, number];
  } else if (marker) {
    c = [0, 0, 0];
  } else if (num(u, 'uChecker', 1) > 0.5 && (flags & FLAG.light) !== 0) {
    c = p.map((x) => 1 - 0.9 * (1 - x)) as [number, number, number];
  } else {
    c = p.map((x) => 0.9 * x) as [number, number, number];
  }
  const axis = num(u, 'uDimAxis', -1);
  if (axis >= 0 && Math.abs((cell[axis] ?? 0) - num(u, 'uLayer', 0)) > 0.5) {
    c = c.map((x) => 1 - (1 - x) * 0.2) as [number, number, number];
  }
  return c;
}

/** The view as an SVG document (text), from the view's camera at its size. */
export async function vectorImage(
  renderer: ViewRenderer,
  flat: FlatMesh,
  background: string,
): Promise<string> {
  // the matrices as the view's last frame placed them
  renderer.drawNow();
  const camera = renderer.camera;
  const drawn = drawnMeshes(renderer.scene);
  const meshes = new Map<string, Promise<MeshArrays>>();
  for (const d of drawn) {
    const k = sourceKey(d.source);
    if (!meshes.has(k)) {
      meshes.set(k, flat(d.source));
    }
  }
  const arrays = new Map<string, MeshArrays>();
  for (const [k, p] of meshes) {
    arrays.set(k, await p);
  }
  // the camera may have moved while the meshes came: draw as it is now
  renderer.drawNow();
  const view = camera.matrixWorldInverse;
  const perspective = camera instanceof PerspectiveCamera;
  const faces: Face<Paint>[] = [];
  const segments: Segment<number>[] = [];
  const world = (x: number, y: number, z: number, m: Matrix4): P3 => {
    const p = new Vector3(x, y, z).applyMatrix4(m);
    return [p.x, p.y, p.z];
  };

  for (const d of drawn) {
    const a = arrays.get(sourceKey(d.source));
    if (a === undefined) {
      continue;
    }
    const u = d.uniforms;
    const alpha = Math.max(0, Math.min(1, num(u, 'uAlpha', 1)));
    const modelView = new Matrix4().multiplyMatrices(view, d.matrixWorld);
    const normalMatrix = new Matrix3().getNormalMatrix(modelView);
    const lit = num(u, 'uLit', 1) > 0.5;
    const light = (u.uLightPos?.value as Vector3 | undefined) ?? new Vector3(700, 200, 700);
    const sx = d.size[0];
    const sy = d.size[1];

    if (!d.wire) {
      const v = [new Vector3(), new Vector3(), new Vector3()];
      const n = new Vector3();
      const centre = new Vector3();
      const colour = new Color();
      const triangles = a.flags.length;
      for (let t = 0; t < triangles; t++) {
        const flags = a.flags[t] ?? 0;
        if ((flags & FLAG.inside) !== 0) {
          continue;
        }
        for (let j = 0; j < 3; j++) {
          const i = a.indices[3 * t + j] ?? 0;
          v[j]?.set(
            a.positions[3 * i] ?? 0,
            a.positions[3 * i + 1] ?? 0,
            a.positions[3 * i + 2] ?? 0,
          );
        }
        const i0 = a.indices[3 * t] ?? 0;
        n.set(a.normals[3 * i0] ?? 0, a.normals[3 * i0 + 1] ?? 0, a.normals[3 * i0 + 2] ?? 0);
        const [v0, v1, v2] = v as [Vector3, Vector3, Vector3];
        centre.copy(v0).add(v1).add(v2).divideScalar(3).applyMatrix4(modelView);
        const nv = n.clone().applyMatrix3(normalMatrix).normalize();
        // only the faces turned towards the eye (eye space: the eye at the origin)
        const toEye = perspective ? centre.clone().negate() : new Vector3(0, 0, 1);
        if (nv.dot(toEye) <= 0) {
          continue;
        }
        const cell = a.voxel[t] ?? 0;
        const rest = Math.floor(cell / sx);
        let c = faceColour(u, flags, a.colour[t] ?? 0, [
          cell % sx,
          rest % sy,
          Math.floor(rest / sy),
        ]);
        if (lit) {
          const l = light.clone().sub(centre).normalize();
          const k = 0.4 + 0.6 * Math.max(0, nv.dot(l));
          c = c.map((x) => x * k) as [number, number, number];
        }
        // the shader's numbers are written to the screen as they are: sRGB
        colour.setRGB(c[0], c[1], c[2], SRGBColorSpace);
        faces.push({
          corners: [v0, v1, v2].map((p) => world(p.x, p.y, p.z, d.matrixWorld)),
          paint: { colour: [colour.r, colour.g, colour.b], alpha },
        });
      }

      // the variable marks (black, a little above their faces)
      const built = buildMesh(a, [d.size[0], d.size[1], d.size[2]], 'flat');
      built.faces.dispose();
      if (built.marks !== null) {
        const mp = built.marks.getAttribute('position');
        for (let i = 0; i + 2 < mp.count; i += 3) {
          faces.push({
            corners: [i, i + 1, i + 2].map((k) =>
              world(mp.getX(k), mp.getY(k), mp.getZ(k), d.matrixWorld),
            ),
            paint: { colour: [0, 0, 0], alpha },
          });
        }
        built.marks.dispose();
      }
    }

    // the edge lines (a wire-frame piece: these alone)
    const e = a.edgeLines;
    for (let i = 0; i + 5 < e.length; i += 6) {
      segments.push({
        a: world(e[i] ?? 0, e[i + 1] ?? 0, e[i + 2] ?? 0, d.matrixWorld),
        b: world(e[i + 3] ?? 0, e[i + 4] ?? 0, e[i + 5] ?? 0, d.matrixWorld),
        paint: alpha,
      });
    }
  }

  // back to front, in objects of one kind and one opacity each, painted in that order
  const eye = new Vector3().setFromMatrixPosition(camera.matrixWorld);
  const towardEye = new Vector3(0, 0, 1).transformDirection(camera.matrixWorld);
  const order = perspective
    ? paintingOrder(faces, segments, [eye.x, eye.y, eye.z])
    : paintingOrder(faces, segments, [towardEye.x, towardEye.y, towardEye.z], true);
  const svgScene = new Scene();
  const disposables: { dispose(): void }[] = [];
  interface Run {
    kind: 'face' | 'segment';
    alpha: number;
    pos: number[];
    col: number[];
  }
  const runs: Run[] = [];
  for (const item of order) {
    const kind = 'face' in item ? 'face' : 'segment';
    const alpha = 'face' in item ? item.face.paint.alpha : item.segment.paint;
    let run = runs.at(-1);
    if (run?.kind !== kind || run.alpha !== alpha) {
      run = { kind, alpha, pos: [], col: [] };
      runs.push(run);
    }
    if ('face' in item) {
      const c = item.face.corners;
      const [r, g, b] = item.face.paint.colour;
      for (let k = 1; k + 1 < c.length; k++) {
        for (const p of [c[0], c[k], c[k + 1]]) {
          run.pos.push(...(p ?? [0, 0, 0]));
          run.col.push(r, g, b);
        }
      }
    } else {
      run.pos.push(...item.segment.a, ...item.segment.b);
    }
  }
  for (const run of runs) {
    const g = new BufferGeometry();
    g.setAttribute('position', new BufferAttribute(new Float32Array(run.pos), 3));
    const transparent = run.alpha < 1;
    if (run.kind === 'face') {
      g.setAttribute('color', new BufferAttribute(new Float32Array(run.col), 3));
      const m = new MeshBasicMaterial({ vertexColors: true, transparent, opacity: run.alpha });
      svgScene.add(new Mesh(g, m));
      disposables.push(g, m);
    } else {
      const m = new LineBasicMaterial({
        color: 0x000000,
        linewidth: 2,
        transparent,
        opacity: run.alpha,
      });
      svgScene.add(new LineSegments(g, m));
      disposables.push(g, m);
    }
  }

  const svg = new SVGRenderer();
  try {
    svg.setSize(renderer.width, renderer.height);
    svg.setQuality('high');
    // the order is the tree's
    svg.sortObjects = false;
    svg.sortElements = false;
    svgScene.updateMatrixWorld(true);
    svg.render(svgScene, camera);
    const el = svg.domElement;
    el.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    el.removeAttribute('style');
    // the view's background, as a rectangle (a viewer may ignore a style)
    const bg = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    const [vx, vy, vw, vh] = (el.getAttribute('viewBox') ?? '0 0 1 1').split(' ');
    bg.setAttribute('x', vx ?? '0');
    bg.setAttribute('y', vy ?? '0');
    bg.setAttribute('width', vw ?? '1');
    bg.setAttribute('height', vh ?? '1');
    bg.setAttribute('fill', background);
    el.insertBefore(bg, el.firstChild);
    return `<?xml version="1.0" encoding="UTF-8"?>\n${new XMLSerializer().serializeToString(el)}\n`;
  } finally {
    for (const x of disposables) {
      x.dispose();
    }
  }
}
