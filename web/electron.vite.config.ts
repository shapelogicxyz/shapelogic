// SPDX-License-Identifier: GPL-3.0-or-later
//
// electron-vite: the main process and the preload script bundled for Node,
// the renderer by Vite. The preload is CommonJS: a sandboxed preload cannot
// be an ES module. The main process gets the project's one version, the
// git tag's (tools/project-version.ts), read here at build (__BT_VERSION__): the version it
// reports and the one it requires of shapelogic_engine. The build goes to
// out/, or to BT_OUT_DIR (a folder name under web/: the main process finds
// web/ as the folder above its own), so an e2e run can test a build of its
// own while another session builds out/ (cleanup item 8.4).

import { resolve } from 'node:path';

import { defineConfig } from 'electron-vite';

import { projectVersion } from './tools/project-version.ts';

const OUT = ((): string => {
  const dir = process.env.BT_OUT_DIR;
  if (dir === undefined || dir === '') {
    return 'out';
  }
  if (!/^[\w.-]+$/.test(dir) || dir === '.' || dir === '..') {
    throw new Error(`BT_OUT_DIR=${dir}: a folder name under web/, not a path`);
  }
  return dir;
})();

export default defineConfig({
  main: {
    define: { __BT_VERSION__: JSON.stringify(projectVersion(resolve(import.meta.dirname, '..'))) },
    build: {
      outDir: `${OUT}/main`,
      rollupOptions: { input: { index: resolve(import.meta.dirname, 'src/main/index.ts') } },
    },
  },
  preload: {
    build: {
      outDir: `${OUT}/preload`,
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
      outDir: resolve(import.meta.dirname, OUT, 'renderer'),
      rollupOptions: { input: { index: resolve(import.meta.dirname, 'src/renderer/index.html') } },
    },
  },
});
