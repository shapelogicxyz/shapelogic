// SPDX-License-Identifier: GPL-3.0-or-later
//
// PNG files the tests make for pictures on pieces (an 8-bit RGB image, one
// IDAT chunk): the e2e specs and the guide's screenshots.

import { crc32, deflateSync } from 'node:zlib';

/** One PNG chunk: its length, name, data and CRC. */
function chunk(name: string, data: Buffer): Buffer {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(name, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}

/** A w x h RGB PNG whose pixel at (x, y) is `color(x, y)` ([r, g, b], 0 to 255). */
export function rgbPng(
  w: number,
  h: number,
  color: (x: number, y: number) => readonly [number, number, number],
): Buffer {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; // bits
  ihdr[9] = 2; // RGB
  // each row: filter byte 0, then the pixels
  const rows = Buffer.alloc(h * (w * 3 + 1));
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      rows.set(color(x, y), y * (w * 3 + 1) + 1 + x * 3);
    }
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(rows)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}
