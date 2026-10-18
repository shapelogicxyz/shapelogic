// SPDX-License-Identifier: GPL-3.0-or-later
//
// The website's demo of the app (site/public/demo/, served at /demo/):
// the renderer's page built by Vite with src/demo/main.ts as its entry
// (the recorded engine in place of Electron's), its CSP letting the page
// fetch the recording from the site. tools/record-demo.ts builds it and
// records session.json beside it.

import { resolve } from 'node:path';

import { defineConfig } from 'vite';

/** Where the demo is served on the site. */
export const DEMO_BASE = '/demo/';

/** Where it is built: the site's static files. */
export const DEMO_OUT = resolve(import.meta.dirname, '../site/public/demo');

export default defineConfig({
  root: resolve(import.meta.dirname, 'src/renderer'),
  base: DEMO_BASE,
  publicDir: false,
  define: { __BT_DEMO_BASE__: JSON.stringify(DEMO_BASE) },
  plugins: [
    {
      name: 'bt-demo-page',
      transformIndexHtml: {
        order: 'pre',
        handler(html) {
          const page = html
            .replace("connect-src 'none'", "connect-src 'self'")
            .replace('<title>ShapeLogic</title>', '<title>ShapeLogic demo</title>')
            .replace('src="./main.ts"', 'src="../demo/main.ts"');
          if (!page.includes('../demo/main.ts') || !page.includes("connect-src 'self'")) {
            throw new Error('src/renderer/index.html changed: update demo.vite.config.ts');
          }
          return page;
        },
      },
    },
  ],
  build: {
    outDir: DEMO_OUT,
    emptyOutDir: true,
    // the whole app is one module, as in Electron
    chunkSizeWarningLimit: 1500,
    rollupOptions: { input: { index: resolve(import.meta.dirname, 'src/renderer/index.html') } },
  },
});
