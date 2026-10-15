// SPDX-License-Identifier: GPL-3.0-or-later
//
// Says which engine and which build the e2e tests run, or why they are
// skipped (no release engine: `just build release`, or BT_ENGINE).

import { BUILT, ENGINE } from './app.ts';

export default function setup(): void {
  if (ENGINE === null) {
    console.log(
      'e2e: no engine (build/release/engine/shapelogic_engine or BT_ENGINE): the tests are skipped',
    );
  } else if (!BUILT) {
    console.log('e2e: no app build (out/main/index.js): run `npm run build` first');
  } else {
    console.log(`e2e: the app in out/ with ${ENGINE}`);
  }
}
