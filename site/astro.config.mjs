// SPDX-License-Identifier: GPL-3.0-or-later
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightLinksValidator from 'starlight-links-validator';

// The site is served from the root of shapelogic.xyz. The app opens
// /docs/ and /docs/getting-started/ (web/src/main/security.ts HELP_URLS):
// renaming those pages breaks the Help menu, and site/tests/help-urls.test.mjs
// fails the build check if they go missing.
// public/demo/ is the app's demo (web/tools/record-demo.ts), a static page
// of its own: the built site serves /demo/ from its index.html, the dev
// server serves public files only by their full name, so it is told.
const demoIndex = {
  name: 'demo-index',
  configureServer(server) {
    server.middlewares.use((req, _res, next) => {
      if (req.url === '/demo' || req.url === '/demo/') {
        req.url = '/demo/index.html';
      }
      next();
    });
  },
};

export default defineConfig({
  site: 'https://shapelogic.xyz',
  vite: { plugins: [demoIndex] },
  // the landing page's inline links need the spaces around them kept
  compressHTML: false,
  integrations: [
    starlight({
      title: 'ShapeLogic',
      description: 'An open source puzzle solver: design and solve interlocking and packing puzzles.',
      logo: { src: './src/assets/puzzle_icon.jpg', alt: '' },
      favicon: '/favicon.jpg',
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/shapelogicxyz/shapelogic' },
      ],
      editLink: {
        baseUrl: 'https://github.com/shapelogicxyz/shapelogic/edit/main/site/',
      },
      // /demo/ is a static page of public/ (the app's demo), not one of Starlight's
      plugins: [starlightLinksValidator({ exclude: ['/demo/'] })],
      sidebar: [
        { label: 'User Guide', link: '/docs/' },
        {
          label: 'Download',
          link: 'https://github.com/shapelogicxyz/shapelogic/releases/latest',
          attrs: { target: '_blank' },
        },
        { label: 'Demo App', slug: 'docs/demo-app' },
        {
          label: 'Start here',
          items: [
            {
              label: 'Install',
              items: [
                { label: 'Overview', slug: 'docs/install' },
                { label: 'Windows', slug: 'docs/windows' },
                { label: 'macOS', slug: 'docs/macos' },
                { label: 'Linux', slug: 'docs/linux' },
              ],
            },
            { label: 'Getting started', slug: 'docs/getting-started' },
          ],
        },
        {
          label: 'Using ShapeLogic',
          items: [
            { label: '3D Model Viewer', slug: 'docs/viewer' },
            { label: 'Shapes Tab Options', slug: 'docs/shapes-tab' },
            { label: 'Puzzle Tab Options', slug: 'docs/puzzle-tab' },
            { label: 'Solver Tab Options', slug: 'docs/solver-tab' },
            { label: 'Keyboard Shortcuts', slug: 'docs/keyboard-shortcuts' },
            { label: 'Imports', slug: 'docs/imports' },
            { label: 'Exports', slug: 'docs/exports' },
          ],
        },
        {
          label: 'Puzzle Types',
          items: [
            { label: 'Overview', slug: 'docs/puzzle-types' },
            { label: 'Brick', slug: 'docs/puzzle-types/brick' },
            { label: 'Triangular Prism', slug: 'docs/puzzle-types/triangular-prism' },
            { label: 'Spheres', slug: 'docs/puzzle-types/spheres' },
            { label: 'Rhombic', slug: 'docs/puzzle-types/rhombic' },
            { label: 'Tetrahedra-Octahedra', slug: 'docs/puzzle-types/tetra-octa' },
            { label: 'Sliding', slug: 'docs/puzzle-types/sliding' },
            { label: 'Stacking', slug: 'docs/puzzle-types/stacking' },
          ],
        },
        {
          label: 'Advanced Topics',
          items: [
            { label: 'Color Restrictions', slug: 'docs/color-restrictions' },
            { label: 'Piece Weights', slug: 'docs/piece-weights' },
            { label: 'Piece Rotations (TIC)', slug: 'docs/piece-rotations' },
            { label: 'Solve Notation', slug: 'docs/solve-notation' },
            { label: 'Command Line Tool', slug: 'docs/command-line' },
          ],
        },
        {
          label: 'Code Base',
          items: [
            { label: 'Overview', slug: 'docs/code-base' },
            { label: 'Languages and Tools', slug: 'docs/code-base/stack' },
            { label: 'Repository Layout', slug: 'docs/code-base/file-structure' },
            { label: 'Build, Test and Tool Commands', slug: 'docs/code-base/commands' },
            { label: 'Where the Code Lives', slug: 'docs/code-base/core-functions' },
            { label: 'Engine Protocol', slug: 'docs/code-base/engine-protocol' },
            { label: 'Example Solve Flow', slug: 'docs/code-base/example-solve-flow' },
            { label: 'Conventions and Further Reading', slug: 'docs/code-base/conventions' },
          ],
        },
      ],
    }),
  ],
});
