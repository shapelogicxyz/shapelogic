// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the export tests need to check a .glb as a viewer would: Khronos's
// glTF validator (the gltf-validator package), a reader of the file's JSON
// and binary chunks written for the tests (independent of three.js, which
// wrote it), the animation evaluated at a time as glTF says (LINEAR
// translations and scales straight, rotations by slerp, STEP held), and a
// FileReader for Node (three.js's GLTFExporter reads its Blobs with one).

import { createRequire } from 'node:module';

/** The validator's report, the parts the tests read. */
export interface ValidationReport {
  issues: {
    numErrors: number;
    numWarnings: number;
    numInfos: number;
    messages: { code: string; message: string; severity: number; pointer?: string }[];
  };
}

const validator = createRequire(import.meta.url)('gltf-validator') as {
  validateBytes(data: Uint8Array, options?: Record<string, unknown>): Promise<ValidationReport>;
};

/** Khronos's validator over the bytes of a .glb. */
export function validateGlb(data: Uint8Array): Promise<ValidationReport> {
  return validator.validateBytes(data, { maxIssues: 50 });
}

/** A FileReader with readAsArrayBuffer and onloadend, enough for GLTFExporter's binary output. */
export function installFileReader(): void {
  const g = globalThis as { FileReader?: unknown };
  if (g.FileReader !== undefined) {
    return;
  }
  g.FileReader = class {
    result: ArrayBuffer | string | null = null;
    onloadend: (() => void) | null = null;
    readAsArrayBuffer(blob: Blob): void {
      void blob.arrayBuffer().then((b) => {
        this.result = b;
        this.onloadend?.();
      });
    }
    readAsDataURL(blob: Blob): void {
      void blob.arrayBuffer().then((b) => {
        this.result = `data:${blob.type};base64,${Buffer.from(b).toString('base64')}`;
        this.onloadend?.();
      });
    }
  };
}

interface Accessor {
  bufferView: number;
  byteOffset?: number;
  componentType: number;
  count: number;
  type: string;
}

interface Json {
  scene?: number;
  scenes: { name?: string; nodes: number[] }[];
  nodes: {
    name?: string;
    mesh?: number;
    children?: number[];
    translation?: number[];
    rotation?: number[];
    scale?: number[];
  }[];
  meshes: {
    primitives: { attributes: Record<string, number>; indices?: number; material?: number }[];
  }[];
  materials: { name?: string; pbrMetallicRoughness?: { baseColorFactor?: number[] } }[];
  animations: {
    name?: string;
    channels: { sampler: number; target: { node: number; path: string } }[];
    samplers: { input: number; output: number; interpolation?: string }[];
  }[];
  accessors: Accessor[];
  bufferViews: { buffer: number; byteOffset?: number; byteLength: number; byteStride?: number }[];
}

/** A .glb read back. */
export class Glb {
  readonly json: Json;
  private readonly bin: Uint8Array;

  constructor(data: Uint8Array) {
    const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
    if (dv.getUint32(0, true) !== 0x46546c67 || dv.getUint32(4, true) !== 2) {
      throw new Error('not a glTF 2 binary');
    }
    const jsonLength = dv.getUint32(12, true);
    this.json = JSON.parse(new TextDecoder().decode(data.subarray(20, 20 + jsonLength))) as Json;
    const binAt = 20 + jsonLength;
    const binLength = dv.getUint32(binAt, true);
    this.bin = data.subarray(binAt + 8, binAt + 8 + binLength);
  }

  /** An accessor's values as numbers (floats and unsigned integers, tightly packed). */
  accessor(i: number): number[] {
    const a = this.json.accessors[i];
    if (a === undefined) {
      throw new Error(`no accessor ${String(i)}`);
    }
    const view = this.json.bufferViews[a.bufferView];
    if (view === undefined) {
      throw new Error(`no buffer view ${String(a.bufferView)}`);
    }
    const comps = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }[a.type] ?? 0;
    const start = (view.byteOffset ?? 0) + (a.byteOffset ?? 0);
    const dv = new DataView(this.bin.buffer, this.bin.byteOffset + start);
    const out: number[] = [];
    for (let k = 0; k < a.count * comps; k++) {
      switch (a.componentType) {
        case 5126:
          out.push(dv.getFloat32(4 * k, true));
          break;
        case 5125:
          out.push(dv.getUint32(4 * k, true));
          break;
        case 5123:
          out.push(dv.getUint16(2 * k, true));
          break;
        case 5121:
          out.push(dv.getUint8(k));
          break;
        default:
          throw new Error(`component type ${String(a.componentType)}`);
      }
    }
    return out;
  }

  /** The node's translation, rotation and scale at a time of the first animation. */
  trsAt(node: number, time: number): { t: number[]; r: number[]; s: number[] } {
    const n = this.json.nodes[node];
    const out = {
      t: n?.translation ?? [0, 0, 0],
      r: n?.rotation ?? [0, 0, 0, 1],
      s: n?.scale ?? [1, 1, 1],
    };
    const anim = this.json.animations[0];
    for (const c of anim?.channels ?? []) {
      if (c.target.node !== node) {
        continue;
      }
      const smp = anim?.samplers[c.sampler];
      if (smp === undefined) {
        continue;
      }
      const v = sample(
        this.accessor(smp.input),
        this.accessor(smp.output),
        c.target.path === 'rotation' ? 4 : 3,
        smp.interpolation ?? 'LINEAR',
        c.target.path === 'rotation',
        time,
      );
      if (c.target.path === 'translation') {
        out.t = v;
      } else if (c.target.path === 'rotation') {
        out.r = v;
      } else if (c.target.path === 'scale') {
        out.s = v;
      }
    }
    return out;
  }
}

// a sampler's value at a time, as glTF interpolates it
function sample(
  times: number[],
  values: number[],
  comps: number,
  interpolation: string,
  rotation: boolean,
  time: number,
): number[] {
  const value = (k: number): number[] => values.slice(k * comps, k * comps + comps);
  const last = times.length - 1;
  if (time <= (times[0] ?? 0)) {
    return value(0);
  }
  if (time >= (times[last] ?? 0)) {
    return value(last);
  }
  let k = 0;
  while (k + 1 < last && (times[k + 1] ?? 0) <= time) {
    k++;
  }
  if (interpolation === 'STEP') {
    return value(k);
  }
  const t0 = times[k] ?? 0;
  const f = (time - t0) / ((times[k + 1] ?? 0) - t0);
  const a = value(k);
  const b = value(k + 1);
  if (!rotation) {
    return a.map((x, m) => x + f * ((b[m] ?? 0) - x));
  }
  let d = a.reduce((sum, x, m) => sum + x * (b[m] ?? 0), 0);
  const bb = d < 0 ? b.map((x) => -x) : b;
  d = Math.abs(d);
  if (d > 0.9995) {
    const q = a.map((x, m) => x + f * ((bb[m] ?? 0) - x));
    const l = Math.hypot(...q);
    return q.map((x) => x / l);
  }
  const th = Math.acos(d);
  const w0 = Math.sin((1 - f) * th) / Math.sin(th);
  const w1 = Math.sin(f * th) / Math.sin(th);
  return a.map((x, m) => w0 * x + w1 * (bb[m] ?? 0));
}
