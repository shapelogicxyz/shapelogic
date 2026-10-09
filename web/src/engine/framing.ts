// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine's stdout as messages and frames (decision 0012). The stream is
// lines of JSON, except that the line
//   {"jsonrpc":"2.0","method":"frame","params":{"frame":N,"bytes":L}}
// is followed at once by exactly L raw bytes (no newline after them); the
// reply that names frame N comes next. The parser has two states, reading
// a line and reading the L bytes of a frame; chunks may split anything
// anywhere. A frame's bytes are copied once, into a buffer of their own;
// frames are handed out with the reply that follows them.

/** A JSON-RPC message from the engine, with the frames that came just before it. */
export interface InboundMessage {
  kind: 'message';
  message: Record<string, unknown>;
  /** The frames between the previous message and this one (a reply's), by number. */
  frames: ReadonlyMap<number, Uint8Array>;
}

/** Why the stream broke the protocol. */
export type ProblemReason =
  /** A line that is not JSON (or not UTF-8): dropped. */
  | 'not-json'
  /** JSON, but not a JSON-RPC message object: dropped. */
  | 'not-message'
  /** A line longer than the limit: dropped up to its newline. */
  | 'line-too-long'
  /** A frame announcement without a number and a size: the stream cannot be followed. */
  | 'bad-frame'
  /** A frame larger than the limit: its bytes are skipped. */
  | 'frame-too-large'
  /** A frame number announced again before a reply: the later bytes are kept. */
  | 'frame-twice'
  /** Frames followed by something other than a reply: dropped. */
  | 'frame-unclaimed'
  /** The stream ended inside a line or a frame, or with frames no reply named. */
  | 'truncated';

/** A protocol problem found in the stream. */
export interface InboundProblem {
  kind: 'problem';
  reason: ProblemReason;
  detail: string;
  /** The stream cannot be followed after it: the engine must be restarted. */
  fatal: boolean;
}

export type Inbound = InboundMessage | InboundProblem;

export interface ParserOptions {
  /**
   * The longest line kept, in bytes. The default is just under the longest
   * string V8 makes (a base64 document of 256 MiB of XML fits); a longer
   * line is dropped and reported, and the stream goes on after it.
   */
  maxLineBytes?: number;
  /** The largest frame kept; a larger one is skipped and reported (default 1 GiB). */
  maxFrameBytes?: number;
}

export const DEFAULT_MAX_LINE_BYTES = 2 ** 29 - 1024;
export const DEFAULT_MAX_FRAME_BYTES = 2 ** 30;

const NEWLINE = 0x0a;

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function isCount(v: unknown): v is number {
  return typeof v === 'number' && Number.isSafeInteger(v) && v >= 0;
}

/** A reply: an id and no method (decision 0012's replies; events have a method and no id). */
export function isReply(m: Record<string, unknown>): boolean {
  return 'id' in m && !('method' in m);
}

// A copy in a buffer of its own (Node's Buffer.slice would give a view).
function copyOf(bytes: Uint8Array): Uint8Array {
  return new Uint8Array(bytes);
}

function excerpt(text: string): string {
  return text.length > 80 ? `${text.slice(0, 80)}...` : text;
}

/** Splits the engine's stdout into messages and frames. */
export class FrameParser {
  private readonly emit: (item: Inbound) => void;
  private readonly maxLine: number;
  private readonly maxFrame: number;
  private readonly decoder = new TextDecoder('utf-8', { fatal: true });

  // reading a line: its parts so far (copies), or dropping a long one
  private parts: Uint8Array[] = [];
  private partBytes = 0;
  private dropping = false;
  // reading a frame: its buffer (null when skipping one), how much is in it
  private frameNumber = 0;
  private frameSize = 0;
  private frameData: Uint8Array | null = null;
  private frameLeft = 0;
  private inFrame = false;
  // frames read since the last message
  private pending = new Map<number, Uint8Array>();
  private lost = false;
  private ended = false;

  /** `emit` gets every message and problem in stream order; it must not throw. */
  constructor(emit: (item: Inbound) => void, options: ParserOptions = {}) {
    this.emit = emit;
    this.maxLine = options.maxLineBytes ?? DEFAULT_MAX_LINE_BYTES;
    this.maxFrame = options.maxFrameBytes ?? DEFAULT_MAX_FRAME_BYTES;
  }

  /** Whether the stream broke in a way that cannot be followed (after a fatal problem). */
  get isLost(): boolean {
    return this.lost;
  }

  /** Feeds the next bytes of the stream. The chunk is not kept: what is needed is copied. */
  push(chunk: Uint8Array): void {
    let at = 0;
    while (at < chunk.length && !this.lost && !this.ended) {
      at = this.inFrame ? this.readFrame(chunk, at) : this.readLine(chunk, at);
    }
  }

  /** The stream ended: reports a line or frame cut short, and frames no reply named. */
  end(): void {
    if (this.ended) {
      return;
    }
    this.ended = true;
    if (this.lost) {
      return;
    }
    if (this.inFrame) {
      const got = this.frameSize - this.frameLeft;
      this.problem(
        'truncated',
        `the stream ended in frame ${this.frameNumber} (${got} bytes of ${this.frameSize})`,
      );
    } else if (this.partBytes > 0 || this.dropping) {
      this.problem('truncated', 'the stream ended inside a line');
    }
    if (this.pending.size > 0) {
      this.problem(
        'truncated',
        `the stream ended after frame ${[...this.pending.keys()].join(', ')} without the reply`,
      );
      this.pending = new Map();
    }
  }

  private problem(reason: ProblemReason, detail: string, fatal = false): void {
    if (fatal) {
      this.lost = true;
    }
    this.emit({ kind: 'problem', reason, detail, fatal });
  }

  // Reads line bytes from `at`; returns where it stopped.
  private readLine(chunk: Uint8Array, at: number): number {
    const nl = chunk.indexOf(NEWLINE, at);
    const end = nl < 0 ? chunk.length : nl;
    const size = end - at;
    if (!this.dropping && this.partBytes + size > this.maxLine) {
      this.dropping = true;
      this.parts = [];
      this.partBytes = 0;
    }
    if (nl < 0) {
      if (!this.dropping && size > 0) {
        this.parts.push(copyOf(chunk.subarray(at, end)));
        this.partBytes += size;
      }
      return chunk.length;
    }
    if (this.dropping) {
      this.dropping = false;
      this.problem('line-too-long', `a line over ${this.maxLine} bytes was dropped`);
      return nl + 1;
    }
    let line: Uint8Array;
    if (this.parts.length === 0) {
      line = chunk.subarray(at, end);
    } else {
      line = new Uint8Array(this.partBytes + size);
      let off = 0;
      for (const p of this.parts) {
        line.set(p, off);
        off += p.length;
      }
      line.set(chunk.subarray(at, end), off);
      this.parts = [];
      this.partBytes = 0;
    }
    this.handleLine(line);
    return nl + 1;
  }

  private handleLine(bytes: Uint8Array): void {
    let text: string;
    try {
      text = this.decoder.decode(bytes);
    } catch {
      this.problem('not-json', `a line of ${bytes.length} bytes that is not UTF-8`);
      return;
    }
    if (text.trim() === '') {
      return;
    }
    let value: unknown;
    try {
      value = JSON.parse(text);
    } catch {
      this.problem('not-json', `not JSON: ${excerpt(text)}`);
      return;
    }
    if (!isRecord(value)) {
      this.problem('not-message', `not a message: ${excerpt(text)}`);
      return;
    }
    if (value.method === 'frame' && !('id' in value)) {
      this.startFrame(value.params, text);
      return;
    }
    let frames: ReadonlyMap<number, Uint8Array> = new Map();
    if (this.pending.size > 0) {
      if (isReply(value)) {
        frames = this.pending;
      } else {
        this.problem(
          'frame-unclaimed',
          `frame ${[...this.pending.keys()].join(', ')} was followed by ${excerpt(text)}, not by a reply`,
        );
      }
      this.pending = new Map();
    }
    this.emit({ kind: 'message', message: value, frames });
  }

  private startFrame(params: unknown, text: string): void {
    if (!isRecord(params) || !isCount(params.frame) || params.frame < 1 || !isCount(params.bytes)) {
      this.problem(
        'bad-frame',
        `a frame announcement without a number and a size: ${excerpt(text)}`,
        true,
      );
      return;
    }
    this.frameNumber = params.frame;
    const size = params.bytes;
    this.frameSize = size;
    this.frameLeft = size;
    if (size > this.maxFrame) {
      this.problem(
        'frame-too-large',
        `frame ${this.frameNumber} has ${size} bytes, over the ${this.maxFrame} kept`,
      );
      this.frameData = null;
    } else {
      this.frameData = new Uint8Array(size);
    }
    this.inFrame = true;
    if (size === 0) {
      this.finishFrame();
    }
  }

  // Reads frame bytes from `at`; returns where it stopped.
  private readFrame(chunk: Uint8Array, at: number): number {
    const take = Math.min(this.frameLeft, chunk.length - at);
    if (this.frameData !== null) {
      this.frameData.set(chunk.subarray(at, at + take), this.frameData.length - this.frameLeft);
    }
    this.frameLeft -= take;
    if (this.frameLeft === 0) {
      this.finishFrame();
    }
    return at + take;
  }

  private finishFrame(): void {
    this.inFrame = false;
    if (this.frameData === null) {
      return;
    }
    if (this.pending.has(this.frameNumber)) {
      this.problem('frame-twice', `frame ${this.frameNumber} came twice before a reply`);
    }
    this.pending.set(this.frameNumber, this.frameData);
    this.frameData = null;
  }
}

/** The last bytes the engine wrote on stderr, for error reports. */
export class StderrRing {
  private readonly maxBytes: number;
  private chunks: Uint8Array[] = [];
  private bytes = 0;

  constructor(maxBytes = 64 * 1024) {
    this.maxBytes = maxBytes;
  }

  push(chunk: Uint8Array): void {
    const keep =
      chunk.length > this.maxBytes ? chunk.subarray(chunk.length - this.maxBytes) : chunk;
    this.chunks.push(copyOf(keep));
    this.bytes += keep.length;
    while (this.bytes > this.maxBytes) {
      const first = this.chunks[0];
      if (first === undefined) {
        break;
      }
      const over = this.bytes - this.maxBytes;
      if (first.length <= over) {
        this.chunks.shift();
        this.bytes -= first.length;
      } else {
        this.chunks[0] = first.subarray(over);
        this.bytes -= over;
      }
    }
  }

  /** What is kept, as text. */
  text(): string {
    const all = new Uint8Array(this.bytes);
    let off = 0;
    for (const c of this.chunks) {
      all.set(c, off);
      off += c.length;
    }
    return new TextDecoder('utf-8').decode(all);
  }

  /** The last `lines` lines kept. */
  tail(lines = 20): string {
    const all = this.text().replace(/\n$/, '').split('\n');
    return all.slice(-lines).join('\n');
  }
}
