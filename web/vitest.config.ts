// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vitest: the unit tests (tests/unit) and the engine tests (tests/engine,
// against build/release/engine/shapelogic_engine or BT_ENGINE; skipped with a
// message without one). Files run one at a time: the engine tests time
// pauses of real solves and must not share the machine with each other.

import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    include: ['tests/**/*.test.ts'],
    globalSetup: ['tests/global-setup.ts'],
    environment: 'node',
    fileParallelism: false,
    testTimeout: 20_000,
  },
});
