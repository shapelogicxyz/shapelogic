// SPDX-License-Identifier: GPL-3.0-or-later
//
// electron-vite: the main process and the preload script bundled for Node,
// the renderer by Vite. The preload is CommonJS: a sandboxed preload cannot
// be an ES module.

import { resolve } from 'node:path';

import { defineConfig } from 'electron-vite';

export default defineConfig({
  main: {
    build: {
      outDir: 'out/main',
      rollupOptions: { input: { index: resolve(import.meta.dirname, 'src/main/index.ts') } },
    },
  },
  preload: {
    build: {
      outDir: 'out/preload',
      rollupOptions: {
        input: { index: resolve(import.meta.dirname, 'src/preload/index.ts') },
        output: { format: 'cjs', entryFileNames: '[name].cjs' },
      },
    },
  },
  renderer: {
    root: resolve(import.meta.dirname, 'src/renderer'),
    build: {
      outDir: resolve(import.meta.dirname, 'out/renderer'),
      rollupOptions: { input: { index: resolve(import.meta.dirname, 'src/renderer/index.html') } },
    },
  },
});
