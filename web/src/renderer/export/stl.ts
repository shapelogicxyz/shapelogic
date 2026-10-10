// SPDX-License-Identifier: GPL-3.0-or-later
//
// File ▸ Export ▸ STL (stage 7), an interim: a shape's flat mesh (the 3D
// view's "flat faces with edges" style, shape.mesh) as an STL file, binary
// or text, a cell's unit scaled to the size asked for (the original's
// "Unit Size", 10 by default). The original exported its STL meshes
// (stl_0.cpp ... with bevel and offset for printing, cubemesh.cpp and
// minkmesh.cpp); those are a later core feature, and its dialog says so.
// Each triangle is written with its corners in the order its normal
// gives (an STL viewer takes the normal from the order), through
// three.js's STLExporter. DOM-free: the engine tests check it in Node.

import { BufferAttribute, BufferGeometry, Mesh, MeshBasicMaterial, Scene } from 'three';
import { STLExporter } from 'three/addons/exporters/STLExporter.js';

import type { MeshArrays } from '../../engine/frames.ts';

/** The original's STL options this interim has. */
export interface StlOptions {
  /** The size of a cell's unit (millimetres, by convention). */
  unit: number;
  /** Binary STL (the original's "Binary STL"); text otherwise. */
  binary: boolean;
}

export const STL_DEFAULTS: StlOptions = { unit: 10, binary: true };

/** The flat mesh's triangles, scaled, each turned so its corners go counter-clockwise about its normal. */
export function stlGeometry(arrays: MeshArrays, unit: number): BufferGeometry {
  const { positions, normals, indices } = arrays;
  const count = indices.length / 3;
  const pos = new Float32Array(count * 9);
  const corner = (t: number, j: number, a: number): number =>
    positions[3 * (indices[3 * t + j] ?? 0) + a] ?? 0;
  for (let t = 0; t < count; t++) {
    const v0 = indices[3 * t] ?? 0;
    const n = [normals[3 * v0] ?? 0, normals[3 * v0 + 1] ?? 0, normals[3 * v0 + 2] ?? 0];
    const u = [0, 1, 2].map((a) => corner(t, 1, a) - corner(t, 0, a));
    const w = [0, 1, 2].map((a) => corner(t, 2, a) - corner(t, 0, a));
    const cross = [
      (u[1] ?? 0) * (w[2] ?? 0) - (u[2] ?? 0) * (w[1] ?? 0),
      (u[2] ?? 0) * (w[0] ?? 0) - (u[0] ?? 0) * (w[2] ?? 0),
      (u[0] ?? 0) * (w[1] ?? 0) - (u[1] ?? 0) * (w[0] ?? 0),
    ];
    const flip = cross.reduce((s, c, a) => s + c * (n[a] ?? 0), 0) < 0;
    const order = flip ? [0, 2, 1] : [0, 1, 2];
    order.forEach((j, k) => {
      for (let a = 0; a < 3; a++) {
        pos[9 * t + 3 * k + a] = corner(t, j, a) * unit;
      }
    });
  }
  const g = new BufferGeometry();
  g.setAttribute('position', new BufferAttribute(pos, 3));
  return g;
}

/** The STL file of a shape's flat mesh. */
export function stlBytes(arrays: MeshArrays, name: string, options: StlOptions): Uint8Array {
  const geometry = stlGeometry(arrays, options.unit);
  const material = new MeshBasicMaterial();
  try {
    const mesh = new Mesh(geometry, material);
    mesh.name = name;
    const scene = new Scene();
    scene.add(mesh);
    scene.updateMatrixWorld(true);
    const exporter = new STLExporter();
    if (options.binary) {
      const view = exporter.parse(scene, { binary: true });
      return new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
    }
    return new TextEncoder().encode(exporter.parse(scene));
  } finally {
    geometry.dispose();
    material.dispose();
  }
}
