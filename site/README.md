# site/: shapelogic.xyz

The landing page and the user guide, built with [Astro Starlight](https://starlight.astro.build).
Pages are Markdown in `src/content/docs/` (`docs/` there is the `/docs/` URL
path); the landing page is `src/content/docs/index.mdx`.

```bash
cd site && npm ci
npm run dev       # http://localhost:4321, live reload
npm run build     # to dist/; fails on a broken internal link
npm test          # the app's Help URLs are pages of dist/ (build first)
```

## Rules

- The app's Help menu opens `/docs/` and `/docs/getting-started/`
  (`HELP_URLS` in `web/src/main/security.ts`). Do not rename those pages;
  `npm test` fails if they go missing.
- Screenshots are made by the app itself, not by hand: `just docs-screenshots`
  (needs `just build release`) drives the built app and writes
  `src/assets/screens/*.png`. Run it when the UI changes and commit the PNGs.
  They are not regenerated in CI: font rendering differs between systems.
- Installers are not stored here. Download links point at the GitHub
  Release (`/releases/latest`).

## Deploy

Cloudflare (Workers Builds) builds and deploys from this repository's `main`;
nothing in GitHub Actions deploys. Settings: root directory `site`, build
command `npm ci && npm run build`, deploy command `npx wrangler deploy`
(reads `wrangler.jsonc`: serves `dist/`, Starlight's 404 page for unknown
paths), Node 24 (`.node-version`), build watch paths `site/*`. The Worker's
name in the dashboard must match `name` in `wrangler.jsonc`. Non-production
branches and pull requests get preview URLs.
