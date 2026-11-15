// SPDX-License-Identifier: GPL-3.0-or-later
//
// Says once, where the run's output shows it, when the engine tests are
// skipped for want of an engine; and gives the engines of the run search
// folders of its own (the engine's defaults follow SHAPELOGIC_SLIDE_DIR
// and SHAPELOGIC_PANEX_DIR), so the tests neither write in the user's cache
// folder nor share a solve's folders with another suite (decision 0035).

import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

export default function setup(): () => void {
  const folders = mkdtempSync(join(tmpdir(), 'sl-vitest-'));
  process.env.SHAPELOGIC_SLIDE_DIR = join(folders, 'sliding');
  process.env.SHAPELOGIC_PANEX_DIR = join(folders, 'panex');
  const fromEnv = process.env.BT_ENGINE;
  const engine =
    fromEnv !== undefined && fromEnv !== ''
      ? resolve(fromEnv)
      : resolve(import.meta.dirname, '../../build/release/engine/shapelogic_engine');
  if (!existsSync(engine)) {
    process.stderr.write(
      `\nthe engine tests are SKIPPED: ${engine} does not exist (run \`just build release\` or set BT_ENGINE)\n\n`,
    );
  }
  return () => {
    rmSync(folders, { recursive: true, force: true });
  };
}
