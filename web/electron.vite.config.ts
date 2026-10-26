// SPDX-License-Identifier: GPL-3.0-or-later
//
// electron-vite: the main process and the preload script bundled for Node,
// the renderer by Vite. The preload is CommonJS: a sandboxed preload cannot
// be an ES module. The main process gets the project's one version,
// CMakeLists.txt's, read here at build (__BT_VERSION__): the version it
// reports and the one it requires of shapelogic_engine.

import { resolve } from 'node:path';

import { defineConfig } from 'electron-vite';

import { projectVersion } from './tools/project-version.ts';

export default defineConfig({
  main: {
    define: { __BT_VERSION__: JSON.stringify(projectVersion(resolve(import.meta.dirname, '..'))) },
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
    plugins: [
      {
        // dev server only: its client's websocket back to it (hot reload);
        // the built page keeps connect-src 'none'
        name: 'bt-dev-csp',
        apply: 'serve',
        transformIndexHtml: {
          order: 'pre',
          handler(html) {
            const page = html.replace("connect-src 'none'", "connect-src 'self' ws://localhost:*");
            if (!page.includes("connect-src 'self' ws://localhost:*")) {
              throw new Error('src/renderer/index.html changed: update electron.vite.config.ts');
            }
            return page;
          },
        },
      },
    ],
    build: {
      outDir: resolve(import.meta.dirname, 'out/renderer'),
      rollupOptions: { input: { index: resolve(import.meta.dirname, 'src/renderer/index.html') } },
    },
  },
});
