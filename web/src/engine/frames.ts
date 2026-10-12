// SPDX-License-Identifier: GPL-3.0-or-later
//
// Binary frames as typed arrays (decision 0012): a frame's bytes arrive
// before the reply that describes them; the client puts typed-array views
// over the bytes in the reply's place, without copying. The frame is
// little-endian, each array at a multiple of four bytes.

import { FRAME_PATHS } from './api.generated.ts';
import type { ArrayRef } from './api.generated.ts';

/** The typed arrays a frame can hold. */
export type TypedArray = Float32Array | Uint32Array | Int32Array | Uint8Array;

/** A frame as the client hands it out: its arrays as views of its bytes. */
export interface FrameView {
  /** The frame's number (from 1, per engine process). */
  readonly frame: number;
  /** Its size in bytes. */
  readonly bytes: number;
  /** The bytes, exactly `bytes` long; every array is a view of them. */
  readonly buffer: ArrayBuffer;
  /** The reply's description of each array. */
  readonly layout: Readonly<Record<string, ArrayRef>>;
  /** Each array, a typed-array view of `buffer` (no copy). */
  readonly arrays: Readonly<Record<string, TypedArray>>;
}

/** A frame that does not fit its description: a protocol error. */
export class FrameError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'FrameError';
  }
}

const ELEMENT_SIZE = { f32: 4, u32: 4, i32: 4, u8: 1 } as const;

const littleEndian = new Uint8Array(new Uint16Array([1]).buffer)[0] === 1;

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function isCount(v: unknown): v is number {
  return typeof v === 'number' && Number.isSafeInteger(v) && v >= 0;
}

/**
 * The view of a frame's bytes through its reply's description (a `Frame`
 * object). The bytes must be exactly as long as the description says and
 * every array must lie inside them at a multiple of four; throws FrameError
 * otherwise. Bytes that are not the whole of their own buffer are copied.
 */
export function viewFrame(desc: unknown, data: Uint8Array): FrameView {
  if (!littleEndian) {
    throw new FrameError('frames are little-endian; this platform is not');
  }
  if (!isRecord(desc) || !isCount(desc.frame) || !isCount(desc.bytes) || !isRecord(desc.arrays)) {
    throw new FrameError(
      `a frame description must be {frame, bytes, arrays}: ${JSON.stringify(desc)}`,
    );
  }
  const n = desc.frame;
  if (data.byteLength !== desc.bytes) {
    throw new FrameError(`frame ${n}: ${data.byteLength} bytes came, the reply says ${desc.bytes}`);
  }
  // the client's frames own their buffer; anything else is copied first, so
  // that the views are aligned and `buffer` is exactly the frame
  const own =
    data.byteOffset === 0 &&
    data.byteLength === data.buffer.byteLength &&
    data.buffer instanceof ArrayBuffer
      ? data
      : data.slice();
  const buffer = own.buffer as ArrayBuffer;
  const layout: Record<string, ArrayRef> = {};
  const arrays: Record<string, TypedArray> = {};
  for (const [name, ref] of Object.entries(desc.arrays)) {
    if (!isRecord(ref) || !isCount(ref.offset) || !isCount(ref.length)) {
      throw new FrameError(`frame ${n}: array ${name} is not {type, offset, length}`);
    }
    const type = ref.type;
    if (type !== 'f32' && type !== 'u32' && type !== 'i32' && type !== 'u8') {
      throw new FrameError(`frame ${n}: array ${name} has the unknown type ${String(type)}`);
    }
    const offset = ref.offset;
    const length = ref.length;
    if (offset % 4 !== 0) {
      throw new FrameError(`frame ${n}: array ${name} starts at ${offset}, not a multiple of 4`);
    }
    if (offset + length * ELEMENT_SIZE[type] > data.byteLength) {
      throw new FrameError(
        `frame ${n}: array ${name} runs past the frame's ${data.byteLength} bytes`,
      );
    }
    layout[name] = { type, offset, length };
    arrays[name] =
      type === 'f32'
        ? new Float32Array(buffer, offset, length)
        : type === 'u32'
          ? new Uint32Array(buffer, offset, length)
          : type === 'i32'
            ? new Int32Array(buffer, offset, length)
            : new Uint8Array(buffer, offset, length);
  }
  return { frame: n, bytes: data.byteLength, buffer, layout, arrays };
}

/**
 * A frame method's result with its frame description replaced by the view
 * of the frame it names (taken from `frames`, the frames that came before
 * the reply); the result of any other method as it is. Returns the frame
 * numbers used. Throws FrameError when the named frame did not come.
 */
export function attachFrame(
  method: string,
  result: unknown,
  frames: ReadonlyMap<number, Uint8Array>,
): { value: unknown; used: number[] } {
  const path = (FRAME_PATHS as Partial<Record<string, string>>)[method];
  if (path === undefined) {
    return { value: result, used: [] };
  }
  if (!isRecord(result) || !isRecord(result[path])) {
    throw new FrameError(`${method}: the result has no frame description at "${path}"`);
  }
  const desc = result[path];
  const n = desc.frame;
  const data = typeof n === 'number' ? frames.get(n) : undefined;
  if (data === undefined) {
    throw new FrameError(
      `${method}: the reply names frame ${String(n)}, which did not come before it`,
    );
  }
  return { value: { ...result, [path]: viewFrame(desc, data) }, used: [n as number] };
}

function typed<T extends TypedArray>(
  view: FrameView,
  name: string,
  kind: abstract new (...args: never[]) => T,
  length: number | ((n: number) => boolean),
): T {
  const a = view.arrays[name];
  if (!(a instanceof kind)) {
    throw new FrameError(`frame ${view.frame}: no ${kind.name} ${name}`);
  }
  const ok = typeof length === 'number' ? a.length === length : length(a.length);
  if (!ok) {
    throw new FrameError(`frame ${view.frame}: ${name} has ${a.length} elements`);
  }
  return a;
}

/** The arrays of a mesh frame (MeshFrame, decision 0012), checked against its counts. */
export interface MeshArrays {
  /** xyz per vertex. */
  positions: Float32Array;
  /** xyz per vertex (flat: each triangle's own vertices). */
  normals: Float32Array;
  /** Three vertex indices per triangle. */
  indices: Uint32Array;
  /** Per triangle: the cell index x + X*(y + Y*z). */
  voxel: Uint32Array;
  /** Per triangle: the cell's side, -1 for bevels and gaps. */
  voxelFace: Int32Array;
  /** Per triangle: FaceFlag bits (1 light checker, 2 variable mark, 4 marker face, 8 wire frame, 16 bevel, 32 gap, 128 inside). */
  flags: Uint32Array;
  /** Per triangle: the colour number, 0 none. */
  colour: Uint8Array;
  /** Per triangle: the polygon it belongs to. */
  polygon: Uint32Array;
  /** Two xyz points per line (the flat style; empty otherwise). */
  edgeLines: Float32Array;
}

/** The arrays of a mesh reply (shape.mesh, playback.mesh); throws FrameError when they do not fit. */
export function meshArrays(mesh: {
  triangles: number;
  vertices: number;
  frame: FrameView;
}): MeshArrays {
  const f = mesh.frame;
  const t = mesh.triangles;
  const out: MeshArrays = {
    positions: typed(f, 'positions', Float32Array, 3 * mesh.vertices),
    normals: typed(f, 'normals', Float32Array, 3 * mesh.vertices),
    indices: typed(f, 'indices', Uint32Array, 3 * t),
    voxel: typed(f, 'voxel', Uint32Array, t),
    voxelFace: typed(f, 'voxelFace', Int32Array, t),
    flags: typed(f, 'flags', Uint32Array, t),
    colour: typed(f, 'colour', Uint8Array, t),
    polygon: typed(f, 'polygon', Uint32Array, t),
    edgeLines: typed(f, 'edgeLines', Float32Array, (n) => n % 6 === 0),
  };
  for (const i of out.indices) {
    if (i >= mesh.vertices) {
      throw new FrameError(`frame ${f.frame}: an index (${i}) past the ${mesh.vertices} vertices`);
    }
  }
  return out;
}

/** The arrays of a poses or track reply (playback.poses, playback.track). */
export interface PoseArrays {
  /** Per sample: the step (playback.poses) or the time in seconds (playback.track). */
  at: Float32Array;
  /** xyz per sample and piece. */
  translation: Float32Array;
  /** A quaternion xyzw per sample and piece. */
  rotation: Float32Array;
  /** 1 or 0 per sample and piece. */
  visible: Uint8Array;
}

/** The arrays of a poses reply ("step") or a track reply ("time"); throws FrameError when they do not fit. */
export function poseArrays(
  poses: { samples: number; pieces: number; frame: FrameView },
  kind: 'step' | 'time',
): PoseArrays {
  const f = poses.frame;
  const n = poses.samples * poses.pieces;
  return {
    at: typed(f, kind, Float32Array, poses.samples),
    translation: typed(f, 'translation', Float32Array, 3 * n),
    rotation: typed(f, 'rotation', Float32Array, 4 * n),
    visible: typed(f, 'visible', Uint8Array, n),
  };
}

/** The bytes of a file frame (export.stl: one u8 array, `file`). */
export function fileBytes(reply: { frame: FrameView }): Uint8Array {
  return typed(reply.frame, 'file', Uint8Array, () => true);
}
