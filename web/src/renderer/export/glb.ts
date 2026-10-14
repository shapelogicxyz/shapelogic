// SPDX-License-Identifier: GPL-3.0-or-later
//
// File ▸ Export ▸ Solution Animation (stage 7): a solution's disassembly
// as a binary glTF (.glb) that any glTF viewer plays as the player does.
// Ported from the original's src/lib/gltfexport.cpp (solutionAnimation:
// the scene of a root node scaling cells to metres with one child per
// placed piece, each piece one mesh in its assembly orientation in its
// solution colour, linear in glTF's colours, metallic 0 and roughness
// 0.6, both sides; the keyframes of each channel thinned to those its
// interpolation does not reproduce; translation always, rotation when the
// piece turns, scale 0 or 1 as STEP when it is hidden at some time). The
// sampling (the holds on the assembled and the apart puzzle, the moves
// backwards to reassemble, the poses of hidden pieces, the quaternions'
// hemispheres) is the engine's playback.track (core/shapelogic/app/playback.cpp,
// ported from the same function); the meshes are playback.mesh's. The
// file is written by three.js's GLTFExporter, not by hand as the original
// did. DOM-free: the engine tests build and check it in Node.

import {
  AnimationClip,
  BufferAttribute,
  BufferGeometry,
  Color,
  DoubleSide,
  Group,
  InterpolateDiscrete,
  Mesh,
  MeshStandardMaterial,
  QuaternionKeyframeTrack,
  Scene,
  SRGBColorSpace,
  VectorKeyframeTrack,
} from 'three';
import type { KeyframeTrack } from 'three';
import { GLTFExporter } from 'three/addons/exporters/GLTFExporter.js';
import { mergeVertices } from 'three/addons/utils/BufferGeometryUtils.js';

import type { MeshArrays, PoseArrays } from '../../engine/frames.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';

/** The original's options (gltfexport.h's options_c) with their defaults. */
export interface AnimationOptions {
  /** The time of one move or one 90 degree turn. */
  secondsPerStep: number;
  /** The edge of a cell in metres (AR viewers show the puzzle this size). */
  cellSize: number;
  /** The moves backwards after the disassembly, so a looping viewer goes apart and back. */
  reassemble: boolean;
  /** The bevelled cells of the 3D view (the drawing mesh); off: the flat mesh, a fraction of the size. */
  bevel: boolean;
}

export const ANIMATION_DEFAULTS: AnimationOptions = {
  secondsPerStep: 0.5,
  cellSize: 0.01,
  reassemble: true,
  bevel: true,
};

/** Keyframes per step, as the original's samplesPerStep. */
export const ANIMATION_SAMPLES = 8;

/** A placed piece: its name, its colour (the 3D view's, 0 to 1) and its mesh. */
export interface AnimationPiece {
  name: string;
  colour: Rgb3;
  mesh: MeshArrays;
}

/** What the animation is made of. */
export interface AnimationInput {
  /** The scene's name ("Solution 3"). */
  name: string;
  pieces: readonly AnimationPiece[];
  /** playback.track's keyframes, the pieces in the same order. */
  track: PoseArrays;
  /** The edge of a cell in metres. */
  cellSize: number;
}

/** The FaceFlag of the inner side of a hollow shape (not exported, as the original's FF_INSIDE_FACE). */
const INSIDE = 128;

/** A piece's mesh as an indexed triangle list, flat shaded (a vertex shared only by one normal). */
export function pieceGeometry(arrays: MeshArrays): BufferGeometry {
  const { positions, normals, indices, flags } = arrays;
  const kept: number[] = [];
  for (let t = 0; t < flags.length; t++) {
    if (((flags[t] ?? 0) & INSIDE) === 0) {
      kept.push(t);
    }
  }
  const pos = new Float32Array(kept.length * 9);
  const nor = new Float32Array(kept.length * 9);
  kept.forEach((t, k) => {
    for (let j = 0; j < 3; j++) {
      const v = indices[3 * t + j] ?? 0;
      for (let a = 0; a < 3; a++) {
        pos[9 * k + 3 * j + a] = positions[3 * v + a] ?? 0;
        nor[9 * k + 3 * j + a] = normals[3 * v + a] ?? 0;
      }
    }
  });
  const g = new BufferGeometry();
  g.setAttribute('position', new BufferAttribute(pos, 3));
  g.setAttribute('normal', new BufferAttribute(nor, 3));
  const merged = mergeVertices(g, 1e-6);
  g.dispose();
  return merged;
}

/**
 * The keyframes a channel needs (gltfexport.cpp's addChannel): each kept
 * only when the interpolation from the last kept one to the next does not
 * reproduce every keyframe between them (within 1e-4); the first and the
 * last always. `kind`: linear values, rotations (slerp, as a viewer
 * interpolates them) or step values.
 */
export function keptKeys(
  times: ArrayLike<number>,
  values: ArrayLike<number>,
  comps: number,
  kind: 'linear' | 'slerp' | 'step',
): number[] {
  const n = times.length;
  const at = (k: number, m: number): number => values[k * comps + m] ?? 0;
  const reproduces = (a: number, c: number, k: number): boolean => {
    if (kind === 'step') {
      for (let m = 0; m < comps; m++) {
        if (at(a, m) !== at(k, m)) {
          return false;
        }
      }
      return true;
    }
    const ta = times[a] ?? 0;
    const f = ((times[k] ?? 0) - ta) / ((times[c] ?? 0) - ta);
    let w0 = 1 - f;
    let w1 = f;
    if (kind === 'slerp') {
      let d = 0;
      for (let m = 0; m < 4; m++) {
        d += at(a, m) * at(c, m);
      }
      if (d < 0.9999) {
        const th = Math.acos(Math.min(1, d));
        w0 = Math.sin((1 - f) * th) / Math.sin(th);
        w1 = Math.sin(f * th) / Math.sin(th);
      }
    }
    for (let m = 0; m < comps; m++) {
      if (Math.abs(w0 * at(a, m) + w1 * at(c, m) - at(k, m)) > 1e-4) {
        return false;
      }
    }
    return true;
  };
  const keep = [0];
  for (let k = 1; k + 1 < n; k++) {
    const last = keep.at(-1) ?? 0;
    let needed = false;
    for (let m = last + 1; m <= k && !needed; m++) {
      needed = !reproduces(last, k + 1, m);
    }
    if (needed) {
      keep.push(k);
    }
  }
  if (n > 1) {
    keep.push(n - 1);
  }
  return keep;
}

function pick(values: ArrayLike<number>, comps: number, keys: readonly number[]): number[] {
  const out: number[] = [];
  for (const k of keys) {
    for (let m = 0; m < comps; m++) {
      out.push(values[k * comps + m] ?? 0);
    }
  }
  return out;
}

/** The scene and its animation: a root node "Puzzle" scaled to metres, a node per piece. */
export function animationScene(input: AnimationInput): { scene: Scene; clip: AnimationClip } {
  const { track, pieces } = input;
  const keys = track.at.length;
  const count = pieces.length;
  const times = Array.from(track.at);
  const scene = new Scene();
  scene.name = input.name;
  const root = new Group();
  root.name = 'Puzzle';
  root.scale.setScalar(input.cellSize);
  scene.add(root);
  const tracks: KeyframeTrack[] = [];
  pieces.forEach((piece, i) => {
    const material = new MeshStandardMaterial({
      name: piece.name,
      color: new Color().setRGB(piece.colour[0], piece.colour[1], piece.colour[2], SRGBColorSpace),
      metalness: 0,
      roughness: 0.6,
      side: DoubleSide,
    });
    const node = new Mesh(pieceGeometry(piece.mesh), material);
    node.name = piece.name;
    node.geometry.name = piece.name;
    root.add(node);

    const tr: number[] = [];
    const rot: number[] = [];
    const sc: number[] = [];
    for (let k = 0; k < keys; k++) {
      const o = k * count + i;
      for (let m = 0; m < 3; m++) {
        tr.push(track.translation[3 * o + m] ?? 0);
      }
      for (let m = 0; m < 4; m++) {
        rot.push(track.rotation[4 * o + m] ?? 0);
      }
      const s = track.visible[o] === 1 ? 1 : 0;
      sc.push(s, s, s);
    }
    node.position.fromArray(tr, 0);
    node.quaternion.fromArray(rot, 0);
    node.scale.setScalar(sc[0] ?? 1);
    let rotates = false;
    let hides = false;
    for (let k = 1; k < keys; k++) {
      let d = 0;
      for (let m = 0; m < 4; m++) {
        d += Math.abs((rot[4 * k + m] ?? 0) - (rot[m] ?? 0));
      }
      rotates ||= d > 1e-6;
      hides ||= sc[3 * k] !== sc[0];
    }
    const name = node.uuid;
    const kt = keptKeys(times, tr, 3, 'linear');
    tracks.push(
      new VectorKeyframeTrack(
        `${name}.position`,
        kt.map((k) => times[k] ?? 0),
        pick(tr, 3, kt),
      ),
    );
    if (rotates) {
      const kr = keptKeys(times, rot, 4, 'slerp');
      tracks.push(
        new QuaternionKeyframeTrack(
          `${name}.quaternion`,
          kr.map((k) => times[k] ?? 0),
          pick(rot, 4, kr),
        ),
      );
    }
    if (hides) {
      const ks = keptKeys(times, sc, 3, 'step');
      tracks.push(
        new VectorKeyframeTrack(
          `${name}.scale`,
          ks.map((k) => times[k] ?? 0),
          pick(sc, 3, ks),
          InterpolateDiscrete,
        ),
      );
    }
  });
  const clip = new AnimationClip('Disassembly', times.at(-1) ?? 0, tracks);
  return { scene, clip };
}

/** The .glb's bytes. */
export async function animationGlb(input: AnimationInput): Promise<Uint8Array> {
  const { scene, clip } = animationScene(input);
  try {
    const out = await new GLTFExporter().parseAsync(scene, {
      binary: true,
      animations: [clip],
    });
    if (!(out instanceof ArrayBuffer)) {
      throw new Error('the exporter gave no binary file');
    }
    return new Uint8Array(out);
  } finally {
    scene.traverse((o) => {
      if (o instanceof Mesh) {
        (o.geometry as BufferGeometry).dispose();
        (o.material as MeshStandardMaterial).dispose();
      }
    });
  }
}
