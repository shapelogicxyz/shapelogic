// SPDX-License-Identifier: GPL-3.0-or-later
//
// Says which engine and which build the e2e tests run, or why they are
// skipped (no release engine: `just build release`, or SHAPELOGIC_ENGINE). With
// SHAPELOGIC_REQUIRE_ALL=1, as the gate sets it, a missing engine or build fails
// the run instead: a gate that skipped every spec must not pass. The apps
// run a copy of the engine, removed at the end (helpers/engine-path.ts):
// another session's build of the release preset cannot change it during
// the run.

import { useRunCopyOfEngine } from '../helpers/engine-path.ts';

import { APP_MAIN, BUILT, ENGINE, OUT_DIR } from './app.ts';

export default function setup(): () => void {
  if (process.env.SHAPELOGIC_REQUIRE_ALL === '1' && (ENGINE === null || !BUILT)) {
    throw new Error(
      `SHAPELOGIC_REQUIRE_ALL=1: the e2e tests cannot run: ${ENGINE === null ? 'no engine (build/release/engine/shapelogic_engine or SHAPELOGIC_ENGINE)' : `no app build (${APP_MAIN})`}`,
    );
  }
  if (ENGINE === null) {
    console.log(
      'e2e: no engine (build/release/engine/shapelogic_engine or SHAPELOGIC_ENGINE): the tests are skipped',
    );
  } else if (!BUILT) {
    console.log(`e2e: no app build (${APP_MAIN}): run \`npm run build\` first`);
  } else {
    console.log(`e2e: the app in ${OUT_DIR} with a copy of ${ENGINE}`);
  }
  return useRunCopyOfEngine('bt-e2e');
}
