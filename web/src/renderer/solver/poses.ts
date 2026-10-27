// SPDX-License-Identifier: GPL-3.0-or-later
//
// The poses of a playback (playback.poses) as the player shows them: the
// engine samples each step SAMPLES times (turns tumble about their pivots
// there, so a turn's samples lie on its arc), the client interpolates
// between two samples: translations straight, rotations by slerp (the
// short way). The samples come in chunks of CHUNK steps, asked for around
// the step shown (`missing`); a pose needs the two samples around it. A
// piece shown at one sample and gone at the next whole step is being
// taken away during that step: with the fade setting its opacity goes from
// 1 to 0 over the step, as the original's disassembly animation faded it
// (disasmToMoves_c::setStep's alpha with fadeOut), else it stays opaque
// until it is gone. DOM-free, for the unit tests.

import type { PoseArrays } from '../../engine/frames.ts';

/** Samples per step asked of the engine. */
export const SAMPLES = 8;
/** Steps per chunk of samples. */
export const CHUNK = 32;

/** A piece's pose: its mesh turned by `q` (xyzw) and moved by `t`. */
export interface Pose {
  t: [number, number, number];
  q: [number, number, number, number];
  visible: boolean;
  /** 1 opaque, down to 0 while it fades away. */
  opacity: number;
}

/** The samples of one chunk. */
interface Chunk {
  translation: Float32Array;
  rotation: Float32Array;
  visible: Uint8Array;
}

/** The poses of a playback: `pieces` pieces over `steps` steps (0: the single pose). */
export class PoseTrack {
  readonly pieces: number;
  readonly steps: number;
  private readonly chunks = new Map<number, Chunk>();

  constructor(pieces: number, steps: number) {
    this.pieces = pieces;
    this.steps = steps;
  }

  /** The chunk a step's samples are in (a whole step that ends one is in the next too). */
  static chunkOf(step: number): number {
    return Math.max(0, Math.floor(step / CHUNK));
  }

  /** The steps of a chunk: playback.poses's from and to (both included). */
  range(chunk: number): { from: number; to: number } {
    const from = Math.min(chunk * CHUNK, this.steps);
    return { from, to: Math.min(from + CHUNK, this.steps) };
  }

  /** The number of chunks. */
  get chunkCount(): number {
    return Math.max(1, Math.ceil(this.steps / CHUNK));
  }

  /** Keeps a chunk's samples (a playback.poses reply over `range(chunk)`, SAMPLES a step). */
  add(chunk: number, a: PoseArrays): void {
    const { from, to } = this.range(chunk);
    const samples = (to - from) * SAMPLES + 1;
    if (a.at.length !== samples) {
      throw new Error(
        `chunk ${String(chunk)}: ${String(a.at.length)} samples, not ${String(samples)}`,
      );
    }
    this.chunks.set(chunk, {
      translation: a.translation.slice(),
      rotation: a.rotation.slice(),
      visible: a.visible.slice(),
    });
  }

  /** Whether a chunk came. */
  has(chunk: number): boolean {
    return this.chunks.has(chunk);
  }

  /** The chunks a pose at `step` needs and does not have. */
  missing(step: number): number[] {
    const s = Math.max(0, Math.min(this.steps, step));
    const out = new Set<number>();
    const c = Math.min(PoseTrack.chunkOf(s), this.chunkCount - 1);
    out.add(c);
    // the next whole step (whether a piece is gone then) may be the next chunk's
    if (Math.floor(s) + 1 > this.range(c).to && c + 1 < this.chunkCount) {
      out.add(c + 1);
    }
    return [...out].filter((k) => !this.chunks.has(k));
  }

  /** The poses at a step (fractions interpolated); null while a chunk it needs is missing. */
  at(step: number, fade: boolean): Pose[] | null {
    const s = Math.max(0, Math.min(this.steps, step));
    const k0 = Math.floor(s * SAMPLES + 1e-6);
    const k1 = Math.min(k0 + 1, this.steps * SAMPLES);
    const f = Math.min(1, Math.max(0, s * SAMPLES - k0));
    const a = this.sample(k0);
    // a whole step needs its own sample only (the one after may be the next chunk's)
    const b = f === 0 ? a : this.sample(k1);
    // the next whole step: whether a piece is gone then
    const whole = Math.min(this.steps, Math.floor(s + 1e-9) + 1);
    const next = Number.isInteger(s) && s === this.steps ? null : this.sample(whole * SAMPLES);
    if (a === null || b === null) {
      return null;
    }
    const out: Pose[] = [];
    for (let i = 0; i < this.pieces; i++) {
      const ta = a.chunk.translation.subarray((a.k * this.pieces + i) * 3);
      const tb = b.chunk.translation.subarray((b.k * this.pieces + i) * 3);
      const qa = a.chunk.rotation.subarray((a.k * this.pieces + i) * 4);
      const qb = b.chunk.rotation.subarray((b.k * this.pieces + i) * 4);
      const visible = (a.chunk.visible[a.k * this.pieces + i] ?? 0) !== 0;
      let opacity = 1;
      const goneNext = next !== null && (next.chunk.visible[next.k * this.pieces + i] ?? 0) === 0;
      if (visible && fade && goneNext && whole > s) {
        opacity = Math.min(1, Math.max(0, whole - s));
      }
      out.push({
        t: [lerp(ta[0], tb[0], f), lerp(ta[1], tb[1], f), lerp(ta[2], tb[2], f)],
        q: slerp(
          [qa[0] ?? 0, qa[1] ?? 0, qa[2] ?? 0, qa[3] ?? 1],
          [qb[0] ?? 0, qb[1] ?? 0, qb[2] ?? 0, qb[3] ?? 1],
          f,
        ),
        visible,
        opacity,
      });
    }
    return out;
  }

  /** Sample k (k / SAMPLES steps) and where it is, or null when its chunk is missing. */
  private sample(k: number): { chunk: Chunk; k: number } | null {
    const step = k / SAMPLES;
    let c = Math.min(PoseTrack.chunkOf(step), this.chunkCount - 1);
    // a whole step that ends a chunk is the next one's first sample too
    let chunk = this.chunks.get(c);
    if (chunk === undefined && c > 0 && step === this.range(c).from) {
      c -= 1;
      chunk = this.chunks.get(c);
    }
    if (chunk === undefined) {
      return null;
    }
    return { chunk, k: k - this.range(c).from * SAMPLES };
  }
}

/**
 * The chunks asked of the engine and not come yet. A pose that needs a chunk
 * already on its way waits for that load rather than finding nothing to ask
 * for: a pose retried as soon as there was nothing to wait for would run as
 * a microtask after microtask, and the reply that ends it would never be
 * read (the renderer hung, scrubbing into a chunk being prefetched).
 */
export class ChunkLoads {
  private readonly pending = new Map<number, Promise<void>>();

  /** The load of chunk c: the one on its way, else `load(c)` started now (it must not reject). */
  load(c: number, load: (c: number) => Promise<void>): Promise<void> {
    const on = this.pending.get(c);
    if (on !== undefined) {
      return on;
    }
    const p = load(c).finally(() => {
      if (this.pending.get(c) === p) {
        this.pending.delete(c);
      }
    });
    this.pending.set(c, p);
    return p;
  }

  /**
   * Loads the chunks the pose at `t` needs and prefetches the next one, ahead
   * of play. True when it waited for chunks and they all came: only then is
   * the pose worth trying again (not when nothing was missing, nor after a
   * failed load, which would ask again and again).
   */
  async ensure(track: PoseTrack, t: number, load: (c: number) => Promise<void>): Promise<boolean> {
    const want = track.missing(t);
    const ahead = PoseTrack.chunkOf(t) + 1;
    if (ahead < track.chunkCount && !track.has(ahead)) {
      void this.load(ahead, load);
    }
    await Promise.all(want.map((c) => this.load(c, load)));
    return want.length > 0 && want.every((c) => track.has(c));
  }

  /** Forgets the loads on their way (a new playback; theirs check the track they were for). */
  clear(): void {
    this.pending.clear();
  }
}

function lerp(a: number | undefined, b: number | undefined, f: number): number {
  const x = a ?? 0;
  return x + ((b ?? 0) - x) * f;
}

/** The spherical interpolation of two unit quaternions (xyzw), the short way. */
export function slerp(
  a: readonly [number, number, number, number],
  b: readonly [number, number, number, number],
  f: number,
): [number, number, number, number] {
  let [bx, by, bz, bw] = b;
  let d = a[0] * bx + a[1] * by + a[2] * bz + a[3] * bw;
  if (d < 0) {
    bx = -bx;
    by = -by;
    bz = -bz;
    bw = -bw;
    d = -d;
  }
  if (f <= 0) {
    return [a[0], a[1], a[2], a[3]];
  }
  if (f >= 1 || d > 0.99995) {
    // the same rotation, or close enough for a straight line
    const out: [number, number, number, number] = [
      a[0] + (bx - a[0]) * f,
      a[1] + (by - a[1]) * f,
      a[2] + (bz - a[2]) * f,
      a[3] + (bw - a[3]) * f,
    ];
    const n = Math.hypot(...out) || 1;
    return [out[0] / n, out[1] / n, out[2] / n, out[3] / n];
  }
  const th = Math.acos(Math.min(1, d));
  const s = Math.sin(th);
  const wa = Math.sin((1 - f) * th) / s;
  const wb = Math.sin(f * th) / s;
  return [a[0] * wa + bx * wb, a[1] * wa + by * wb, a[2] * wa + bz * wb, a[3] * wa + bw * wb];
}
