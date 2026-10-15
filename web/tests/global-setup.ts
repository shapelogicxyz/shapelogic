// SPDX-License-Identifier: GPL-3.0-or-later
//
// Says once, where the run's output shows it, when the engine tests are
// skipped for want of an engine.

import { existsSync } from 'node:fs';
import { resolve } from 'node:path';

export default function setup(): void {
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
}
