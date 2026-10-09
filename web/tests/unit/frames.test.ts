// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frames as typed-array views: no copy, checked against their description.

import { describe, expect, it } from 'vitest';

import {
  attachFrame,
  FrameError,
  meshArrays,
  poseArrays,
  viewFrame,
} from '../../src/engine/frames.ts';

function bytesOf(...arrays: (Float32Array | Uint32Array | Int32Array | Uint8Array)[]): Uint8Array {
  const sizes = arrays.map((a) => (a.byteLength + 3) & ~3);
  const out = new Uint8Array(sizes.reduce((n, s) => n + s, 0));
  let at = 0;
  arrays.forEach((a, i) => {
    out.set(new Uint8Array(a.buffer, a.byteOffset, a.byteLength), at);
    at += sizes[i] ?? 0;
  });
  return out;
}

describe('a frame view', () => {
  const positions = new Float32Array([0, 1, 2, 3, 4, 5, 6, 7, 8]);
  const indices = new Uint32Array([0, 1, 2]);
  const colour = new Uint8Array([7]);
  const data = bytesOf(positions, indices, colour);
  const desc = {
    frame: 3,
    bytes: data.length,
    arrays: {
      positions: { type: 'f32', offset: 0, length: 9 },
      indices: { type: 'u32', offset: 36, length: 3 },
      colour: { type: 'u8', offset: 48, length: 1 },
    },
  };

  it('puts typed arrays over the frame bytes without copying', () => {
    const view = viewFrame(desc, data);
    expect(view.buffer).toBe(data.buffer);
    expect(Array.from(view.arrays.positions ?? [])).toEqual(Array.from(positions));
    expect(view.arrays.indices).toBeInstanceOf(Uint32Array);
    expect(Array.from(view.arrays.colour ?? [])).toEqual([7]);
    data[48] = 9;
    expect(view.arrays.colour?.[0]).toBe(9);
    data[48] = 7;
    expect(view.layout.indices).toEqual({ type: 'u32', offset: 36, length: 3 });
  });

  it('copies bytes that are a part of a bigger buffer, so the views line up', () => {
    const bigger = new Uint8Array(data.length + 2);
    bigger.set(data, 2);
    const view = viewFrame(desc, bigger.subarray(2));
    expect(view.buffer.byteLength).toBe(data.length);
    expect(Array.from(view.arrays.indices ?? [])).toEqual([0, 1, 2]);
  });

  it('refuses a frame that does not fit its description', () => {
    expect(() => viewFrame({ ...desc, bytes: data.length + 4 }, data)).toThrow(FrameError);
    const arrays = (a: unknown): unknown => ({ ...desc, arrays: { a } });
    expect(() => viewFrame(arrays({ type: 'f32', offset: 2, length: 1 }), data)).toThrow(
      /multiple of 4/,
    );
    expect(() => viewFrame(arrays({ type: 'f32', offset: 48, length: 2 }), data)).toThrow(
      /runs past/,
    );
    expect(() => viewFrame(arrays({ type: 'f64', offset: 0, length: 1 }), data)).toThrow(
      /unknown type/,
    );
    expect(() => viewFrame(arrays({ type: 'u8', offset: -4, length: 1 }), data)).toThrow(
      FrameError,
    );
    expect(() => viewFrame({ frame: 1 }, data)).toThrow(FrameError);
  });
});

describe('a frame method reply', () => {
  const frame = new Uint8Array(8);
  const result = { triangles: 0, frame: { frame: 5, bytes: 8, arrays: {} } };

  it('gets the view of the frame it names', () => {
    const { value, used } = attachFrame('shape.mesh', result, new Map([[5, frame]]));
    expect(used).toEqual([5]);
    expect((value as { frame: { buffer: ArrayBuffer } }).frame.buffer).toBe(frame.buffer);
    expect((value as { triangles: number }).triangles).toBe(0);
  });

  it('naming a frame that did not come is a protocol error', () => {
    expect(() => attachFrame('shape.mesh', result, new Map([[4, frame]]))).toThrow(/did not come/);
    expect(() => attachFrame('playback.poses', { samples: 1 }, new Map())).toThrow(
      /no frame description/,
    );
  });

  it('of another method is left as it is', () => {
    expect(attachFrame('shape.get', { a: 1 }, new Map())).toEqual({ value: { a: 1 }, used: [] });
  });
});

describe('mesh and pose arrays', () => {
  it('are checked against the counts', () => {
    const t = 1;
    const v = 3;
    const parts = {
      positions: new Float32Array(3 * v),
      normals: new Float32Array(3 * v),
      indices: new Uint32Array([0, 1, 2]),
      voxel: new Uint32Array(t),
      voxelFace: new Int32Array(t),
      flags: new Uint32Array(t),
      colour: new Uint8Array(t),
      polygon: new Uint32Array(t),
      edgeLines: new Float32Array(6),
    };
    const names = Object.keys(parts) as (keyof typeof parts)[];
    const types = {
      Float32Array: 'f32',
      Uint32Array: 'u32',
      Int32Array: 'i32',
      Uint8Array: 'u8',
    } as const;
    let offset = 0;
    const arrays: Record<string, unknown> = {};
    for (const name of names) {
      const a = parts[name];
      arrays[name] = {
        type: types[a.constructor.name as keyof typeof types],
        offset,
        length: a.length,
      };
      offset += (a.byteLength + 3) & ~3;
    }
    const data = bytesOf(...names.map((n) => parts[n]));
    const mesh = {
      triangles: t,
      vertices: v,
      frame: viewFrame({ frame: 1, bytes: data.length, arrays }, data),
    };
    expect(meshArrays(mesh).voxelFace).toBeInstanceOf(Int32Array);
    expect(() => meshArrays({ ...mesh, triangles: 2 })).toThrow(/indices has 3 elements/);
    expect(() => meshArrays({ ...mesh, vertices: 2 })).toThrow(FrameError);
    parts.indices[2] = 3;
    const bad = bytesOf(...names.map((n) => parts[n]));
    expect(() =>
      meshArrays({ ...mesh, frame: viewFrame({ frame: 2, bytes: bad.length, arrays }, bad) }),
    ).toThrow(/past the 3 vertices/);
  });

  it('of poses and tracks', () => {
    const samples = 2;
    const pieces = 1;
    const data = bytesOf(
      new Float32Array(2),
      new Float32Array(6),
      new Float32Array(8),
      new Uint8Array(2),
    );
    const arrays = {
      time: { type: 'f32', offset: 0, length: 2 },
      translation: { type: 'f32', offset: 8, length: 6 },
      rotation: { type: 'f32', offset: 32, length: 8 },
      visible: { type: 'u8', offset: 64, length: 2 },
    };
    const poses = {
      samples,
      pieces,
      frame: viewFrame({ frame: 1, bytes: data.length, arrays }, data),
    };
    expect(poseArrays(poses, 'time').rotation).toHaveLength(8);
    expect(() => poseArrays(poses, 'step')).toThrow(/no Float32Array step/);
  });
});
