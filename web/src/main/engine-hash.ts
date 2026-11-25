// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine's SHA-256, recorded at packaging and checked by the app
// (cleanup items 2.6 and 2.1): `web/tools/dist.ts` writes `engine.sha256`
// beside the engine in the app's resources, one `<hex>  <file>` line for
// the engine and one for the command line tool (sha256sum's format); the
// app hashes its engine after the first window shows and logs whether it
// matches, so a damaged or changed file (security software, a broken
// download) shows in the log. Free of Electron: the packaging tool and the
// main process both use it.

import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';

/** The hash file's name in the app's resources. */
export const ENGINE_HASH_FILE = 'engine.sha256';

/** A file's SHA-256 as lowercase hex, read as a stream (the engine is tens of MB). */
export function sha256File(path: string): Promise<string> {
  return new Promise((resolve, reject) => {
    const hash = createHash('sha256');
    createReadStream(path)
      .on('data', (chunk) => hash.update(chunk))
      .on('error', reject)
      .on('end', () => {
        resolve(hash.digest('hex'));
      });
  });
}

/** The hash file's text: one `<hex>  <file>` line per file. */
export function hashFileText(entries: readonly { file: string; hex: string }[]): string {
  return entries.map((e) => `${e.hex}  ${e.file}\n`).join('');
}

/** The hashes a hash file records, by file name; lines that are not `<hex>  <file>` are skipped. */
export function parseHashFile(text: string): Map<string, string> {
  const out = new Map<string, string>();
  for (const line of text.split(/\r?\n/)) {
    const m = /^([0-9a-f]{64}) [ *](.+)$/i.exec(line.trim());
    if (m?.[1] !== undefined && m[2] !== undefined) {
      out.set(m[2], m[1].toLowerCase());
    }
  }
  return out;
}
