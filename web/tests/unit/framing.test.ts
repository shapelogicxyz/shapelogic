// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stdout parser: recordings of the real engine replayed in chunks of
// any size give the same messages and frames; frames at chunk edges, empty
// frames, lines that are not JSON, lines and frames over the limits, a
// stream that ends mid-frame.

import { describe, expect, it } from 'vitest';

import { loadRecording } from '../../fake-engine/recordings.ts';
import { FrameParser, StderrRing } from '../../src/engine/framing.ts';
import type { Inbound, ParserOptions } from '../../src/engine/framing.ts';
import { recordedStdout } from '../../src/engine/recorder.ts';
import { checksum } from '../../tools/sessions.ts';

const encoder = new TextEncoder();

/** What a parse gave, comparable with toEqual. */
type Plain =
  | { kind: 'message'; message: unknown; frames: [number, number, string][] }
  | { kind: 'problem'; reason: string; fatal: boolean };

function plain(items: Inbound[]): Plain[] {
  return items.map((i) =>
    i.kind === 'message'
      ? {
          kind: 'message',
          message: i.message,
          frames: [...i.frames].map(([n, b]): [number, number, string] => [
            n,
            b.length,
            checksum(b),
          ]),
        }
      : { kind: 'problem', reason: i.reason, fatal: i.fatal },
  );
}

/** Parses `bytes` cut at the given offsets (sorted); ends the stream unless told not to. */
function parse(bytes: Uint8Array, cuts: number[], options?: ParserOptions, end = true): Inbound[] {
  const items: Inbound[] = [];
  const p = new FrameParser((i) => items.push(i), options);
  let at = 0;
  for (const cut of [...cuts, bytes.length]) {
    p.push(bytes.subarray(at, cut));
    at = cut;
  }
  if (end) {
    p.end();
  }
  return items;
}

function inChunks(bytes: Uint8Array, size: number): number[] {
  const cuts: number[] = [];
  for (let at = size; at < bytes.length; at += size) {
    cuts.push(at);
  }
  return cuts;
}

function concat(...parts: (Uint8Array | string)[]): Uint8Array {
  const bytes = parts.map((p) => (typeof p === 'string' ? encoder.encode(p) : p));
  const out = new Uint8Array(bytes.reduce((n, b) => n + b.length, 0));
  let at = 0;
  for (const b of bytes) {
    out.set(b, at);
    at += b.length;
  }
  return out;
}

const line = (m: unknown): string => `${JSON.stringify(m)}\n`;
const announce = (n: number, bytes: number): string =>
  line({ jsonrpc: '2.0', method: 'frame', params: { frame: n, bytes } });
const reply = (id: number, result: unknown = {}): string => line({ jsonrpc: '2.0', id, result });
const event = (method: string): string => line({ jsonrpc: '2.0', method, params: {} });

/** A small deterministic random number generator (mulberry32). */
function rng(seed: number): () => number {
  let a = seed;
  return () => {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

describe.each(['basic', 'dominoes'])('the recording %s', (name) => {
  const stdout = recordedStdout(loadRecording(name));
  const whole = plain(parse(stdout, []));

  it('parses whole into messages, with every frame on the reply after it', () => {
    expect(whole.length).toBeGreaterThan(10);
    expect(whole.filter((i) => i.kind === 'problem')).toEqual([]);
    for (const i of whole) {
      if (i.kind === 'message' && i.frames.length > 0) {
        expect(i.message).toHaveProperty('id');
        expect(i.frames).toHaveLength(1);
      }
    }
    if (name === 'basic') {
      expect(
        whole.filter((i) => i.kind === 'message' && i.frames.length > 0).length,
      ).toBeGreaterThan(10);
    }
  });

  it.each([1, 7, 4096])('gives the same in chunks of %i bytes', (size) => {
    expect(plain(parse(stdout, inChunks(stdout, size)))).toEqual(whole);
  });

  it('gives the same cut at random places', () => {
    const random = rng(name.length);
    for (let round = 0; round < 50; round++) {
      const cuts = new Set<number>();
      const n = 1 + Math.floor(random() * 200);
      for (let k = 0; k < n; k++) {
        cuts.add(Math.floor(random() * stdout.length));
      }
      expect(
        plain(
          parse(
            stdout,
            [...cuts].sort((a, b) => a - b),
          ),
        ),
      ).toEqual(whole);
    }
  });
});

describe('frames', () => {
  // frame bytes full of newlines, so a parser that looked for lines in them would break
  const data = new Uint8Array([10, 0, 10, 10, 255, 10, 1, 2]);
  const stream = concat(
    event('solve.progress'),
    announce(1, data.length),
    data,
    reply(4),
    event('document.changed'),
  );
  const expected = plain(parse(stream, []));

  it('go to the reply after them', () => {
    expect(expected).toEqual([
      {
        kind: 'message',
        message: { jsonrpc: '2.0', method: 'solve.progress', params: {} },
        frames: [],
      },
      {
        kind: 'message',
        message: { jsonrpc: '2.0', id: 4, result: {} },
        frames: [[1, 8, checksum(data)]],
      },
      {
        kind: 'message',
        message: { jsonrpc: '2.0', method: 'document.changed', params: {} },
        frames: [],
      },
    ]);
  });

  it('are read alike however the stream is cut, once or twice', () => {
    for (let i = 1; i < stream.length; i++) {
      expect(plain(parse(stream, [i]))).toEqual(expected);
      for (let j = i + 1; j < stream.length; j += 3) {
        expect(plain(parse(stream, [i, j]))).toEqual(expected);
      }
    }
  });

  it('can be empty, and several can come before one reply', () => {
    const items = parse(concat(announce(1, 0), announce(2, 3), 'abc', reply(1)), []);
    expect(plain(items)).toEqual([
      {
        kind: 'message',
        message: { jsonrpc: '2.0', id: 1, result: {} },
        frames: [
          [1, 0, checksum(new Uint8Array())],
          [2, 3, checksum(encoder.encode('abc'))],
        ],
      },
    ]);
  });

  it('are copied once, into a buffer of their own', () => {
    const items: Inbound[] = [];
    const p = new FrameParser((i) => items.push(i));
    const chunk = concat(announce(1, 4), 'wxyz', reply(1));
    p.push(chunk.subarray(0, chunk.length - 3));
    p.push(chunk.subarray(chunk.length - 3));
    chunk.fill(0);
    const frame = items[0]?.kind === 'message' ? items[0].frames.get(1) : undefined;
    expect(frame).toBeDefined();
    expect(new TextDecoder().decode(frame)).toBe('wxyz');
    expect(frame?.byteOffset).toBe(0);
    expect(frame?.buffer.byteLength).toBe(4);
  });

  it('followed by an event are a protocol error, and dropped', () => {
    const items = plain(parse(concat(announce(1, 2), 'ab', event('solve.state'), reply(1)), []));
    expect(items.map((i) => (i.kind === 'problem' ? i.reason : i.frames.length))).toEqual([
      'frame-unclaimed',
      0,
      0,
    ]);
  });

  it('announced twice before a reply are a protocol error', () => {
    const items = plain(parse(concat(announce(1, 1), 'a', announce(1, 1), 'b', reply(1)), []));
    expect(items[0]).toEqual({ kind: 'problem', reason: 'frame-twice', fatal: false });
  });

  it('over the limit are skipped, and the stream goes on', () => {
    const items = plain(
      parse(concat(announce(1, 20), 'x'.repeat(20), reply(1), event('e')), [5, 30], {
        maxFrameBytes: 10,
      }),
    );
    expect(items).toEqual([
      { kind: 'problem', reason: 'frame-too-large', fatal: false },
      { kind: 'message', message: { jsonrpc: '2.0', id: 1, result: {} }, frames: [] },
      { kind: 'message', message: { jsonrpc: '2.0', method: 'e', params: {} }, frames: [] },
    ]);
  });

  it('without a size lose the stream', () => {
    const items: Inbound[] = [];
    const p = new FrameParser((i) => items.push(i));
    p.push(encoder.encode(line({ jsonrpc: '2.0', method: 'frame', params: { frame: 1 } })));
    p.push(encoder.encode(reply(1)));
    p.end();
    expect(plain(items)).toEqual([{ kind: 'problem', reason: 'bad-frame', fatal: true }]);
    expect(p.isLost).toBe(true);
  });
});

describe('lines', () => {
  it('that are not JSON, not UTF-8 or not objects are dropped, and the stream goes on', () => {
    const stream = concat(
      'this is not json\n',
      new Uint8Array([0xff, 0xfe, 10]),
      '[1,2]\n',
      '7\n',
      reply(1),
    );
    const items = plain(parse(stream, inChunks(stream, 3)));
    expect(
      items.map((i) => (i.kind === 'problem' ? `${i.reason}${i.fatal ? '!' : ''}` : 'message')),
    ).toEqual(['not-json', 'not-json', 'not-message', 'not-message', 'message']);
  });

  it('that are blank are skipped; a carriage return before the newline is whitespace', () => {
    const items = plain(
      parse(concat('\n  \n', `${JSON.stringify({ jsonrpc: '2.0', id: 2, result: 1 })}\r\n`), []),
    );
    expect(items).toEqual([
      { kind: 'message', message: { jsonrpc: '2.0', id: 2, result: 1 }, frames: [] },
    ]);
  });

  it('over the limit are dropped up to their newline, and the stream goes on', () => {
    const long = line({ jsonrpc: '2.0', id: 1, result: 'y'.repeat(200) });
    const exact = line({ jsonrpc: '2.0', id: 2, result: 'z'.repeat(70) });
    const limit = exact.length - 1;
    for (const size of [1, 7, 64, 4096]) {
      const stream = concat(long, exact, reply(3));
      const items = plain(parse(stream, inChunks(stream, size), { maxLineBytes: limit }));
      expect(
        items.map((i) => (i.kind === 'problem' ? i.reason : (i.message as { id: number }).id)),
      ).toEqual(['line-too-long', 2, 3]);
    }
  });

  it('cut short by the end of the stream are reported', () => {
    expect(plain(parse(encoder.encode('{"jsonrpc":"2.0"'), []))).toEqual([
      { kind: 'problem', reason: 'truncated', fatal: false },
    ]);
  });
});

describe('the end of the stream', () => {
  it('in the middle of a frame is reported once', () => {
    const items = parse(concat(announce(3, 10), 'abc'), []);
    expect(items).toEqual([
      {
        kind: 'problem',
        reason: 'truncated',
        detail: 'the stream ended in frame 3 (3 bytes of 10)',
        fatal: false,
      },
    ]);
  });

  it('after a frame with no reply is reported', () => {
    expect(plain(parse(concat(announce(1, 1), 'a'), []))).toEqual([
      { kind: 'problem', reason: 'truncated', fatal: false },
    ]);
  });

  it('after whole messages is quiet, and nothing is read after it', () => {
    const items: Inbound[] = [];
    const p = new FrameParser((i) => items.push(i));
    p.push(encoder.encode(reply(1)));
    p.end();
    p.end();
    p.push(encoder.encode(reply(2)));
    expect(plain(items).map((i) => i.kind)).toEqual(['message']);
  });
});

describe('the stderr ring', () => {
  it('keeps the last bytes and gives the last lines', () => {
    const ring = new StderrRing(16);
    ring.push(encoder.encode('first line\n'));
    ring.push(encoder.encode('second\nthird\n'));
    expect(ring.text()).toBe('first line\nsecond\nthird\n'.slice(-16));
    expect(ring.tail(2)).toBe('second\nthird');
    ring.push(encoder.encode('x'.repeat(40)));
    expect(ring.text()).toBe('x'.repeat(16));
  });
});
