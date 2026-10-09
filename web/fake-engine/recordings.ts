// SPDX-License-Identifier: GPL-3.0-or-later
//
// The recorded sessions on disk (recordings/<name>.json) and what the
// client saw in them (recordings/<name>.transcript.json).

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

import type { Recording } from '../src/engine/recorder.ts';
import type { Transcript } from '../tools/sessions.ts';

export const RECORDINGS_DIR = resolve(import.meta.dirname, 'recordings');

export function loadRecording(name: string): Recording {
  return JSON.parse(readFileSync(resolve(RECORDINGS_DIR, `${name}.json`), 'utf8')) as Recording;
}

export function loadTranscript(name: string): Transcript {
  return JSON.parse(
    readFileSync(resolve(RECORDINGS_DIR, `${name}.transcript.json`), 'utf8'),
  ) as Transcript;
}

/** A recording as JSON, one entry a line (readable diffs). */
export function recordingJson(r: Recording): string {
  const head = JSON.stringify({ ...r, entries: [] }).replace(/"entries":\[\]\}$/, '"entries":[\n');
  return `${head}${r.entries.map((e) => JSON.stringify(e)).join(',\n')}\n]}\n`;
}
