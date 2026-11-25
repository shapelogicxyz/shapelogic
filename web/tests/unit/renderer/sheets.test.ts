// SPDX-License-Identifier: GPL-3.0-or-later
//
// The modules' own sheets (cleanup item 13.1): each `<module>.css` sits
// beside its module and is imported by it alone, as its first import, and
// the page links styles.css (the tokens, the shared controls, the layout)
// before the script that brings them in, so at equal specificity a
// module's rule wins over the shared sheet's, as it did when it sat at the
// shared sheet's end.

import { readdirSync, readFileSync } from 'node:fs';
import { basename, dirname, join, relative, resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

const RENDERER = join(import.meta.dirname, '../../../src/renderer');

function files(dir: string, ext: string): string[] {
  const out: string[] = [];
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isDirectory()) {
      out.push(...files(p, ext));
    } else if (e.name.endsWith(ext)) {
      out.push(p);
    }
  }
  return out;
}

describe("the modules' sheets", () => {
  const sheets = files(RENDERER, '.css').filter((f) => basename(f) !== 'styles.css');
  const sources = files(RENDERER, '.ts').map((f) => ({ f, text: readFileSync(f, 'utf8') }));

  it('are each imported first by the module of their name, and by no other', () => {
    expect(sheets.length).toBeGreaterThan(0);
    for (const sheet of sheets) {
      const name = basename(sheet);
      const owner = sheet.replace(/\.css$/, '.ts');
      const src = sources.find((s) => s.f === owner);
      expect(src, `${relative(RENDERER, sheet)} has no module beside it`).toBeDefined();
      const firstImport = /^import .*$/m.exec(src?.text ?? '')?.[0];
      expect(firstImport, relative(RENDERER, owner)).toBe(`import './${name}';`);
      const importers = sources
        .filter((s) =>
          [...s.text.matchAll(/^import '([^']+\.css)';$/gm)].some(
            (m) => resolve(dirname(s.f), m[1] ?? '') === sheet,
          ),
        )
        .map((s) => relative(RENDERER, s.f));
      expect(importers).toEqual([relative(RENDERER, owner)]);
    }
  });

  it('come after styles.css, which the page links before its script', () => {
    const html = readFileSync(join(RENDERER, 'index.html'), 'utf8');
    const link = html.indexOf('<link rel="stylesheet" href="./styles.css" />');
    const script = html.indexOf('<script type="module" src="./main.ts"></script>');
    expect(link).toBeGreaterThan(0);
    expect(script).toBeGreaterThan(link);
  });
});
