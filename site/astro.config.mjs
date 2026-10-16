// SPDX-License-Identifier: GPL-3.0-or-later
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightLinksValidator from 'starlight-links-validator';

// The site is served from the root of shapelogic.xyz. The app opens
// /docs/ and /docs/getting-started/ (web/src/main/security.ts HELP_URLS):
// renaming those pages breaks the Help menu, and site/tests/help-urls.test.mjs
// fails the build check if they go missing.
export default defineConfig({
  site: 'https://shapelogic.xyz',
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
      plugins: [starlightLinksValidator()],
      sidebar: [
        { label: 'User guide', link: '/docs/' },
        {
          label: 'Download',
          link: 'https://github.com/shapelogicxyz/shapelogic/releases/latest',
          attrs: { target: '_blank' },
        },
        {
          label: 'Start here',
          items: [
            { label: 'Install', slug: 'docs/install' },
            { label: 'Getting started', slug: 'docs/getting-started' },
          ],
        },
      ],
    }),
  ],
});
