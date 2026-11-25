// SPDX-License-Identifier: GPL-3.0-or-later
//
// The page and the code kept in step (cleanup item 14.2): every id the
// renderer asks for by name (`byId('x')`) is on the page (index.html) or in
// markup a module writes, and every class a sheet styles (styles.css and
// the modules' own sheets) is written somewhere: on the page, or in a
// module's string or template. A rule for a class nobody writes is dead
// (stage 14 removed 18 of them), and a byId of an id nobody writes throws
// only when that code runs. The modules are read with TypeScript's parser,
// so only their strings count, not their comments or names.

import { readdirSync, readFileSync } from 'node:fs';
import { join, relative, resolve } from 'node:path';

import ts from 'typescript';
import { describe, expect, it } from 'vitest';

const SRC = resolve(import.meta.dirname, '../../../src');
const RENDERER = join(SRC, 'renderer');

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

/** A module's string literals and template pieces, and its byId arguments. */
interface Module {
  file: string;
  strings: string[];
  /** The literal ids passed to byId, each with its line. */
  byId: { id: string; line: number }[];
  /** byId calls whose id is worked out at run time (a variable, a template). */
  computed: number;
  /** The ids it gives elements by code: `el.id = 'x'`, openDialog({id: 'x'}). */
  setIds: string[];
}

function read(file: string): Module {
  const text = readFileSync(file, 'utf8');
  const sf = ts.createSourceFile(file, text, ts.ScriptTarget.Latest, true);
  const m: Module = { file: relative(SRC, file), strings: [], byId: [], computed: 0, setIds: [] };
  const line = (n: ts.Node): number => sf.getLineAndCharacterOfPosition(n.getStart()).line + 1;
  // the literal ids an argument can be: a string, or either branch of a ?:
  const literals = (n: ts.Expression): ts.StringLiteral[] | null => {
    if (ts.isStringLiteral(n)) {
      return [n];
    }
    if (ts.isParenthesizedExpression(n)) {
      return literals(n.expression);
    }
    if (ts.isConditionalExpression(n)) {
      const a = literals(n.whenTrue);
      const b = literals(n.whenFalse);
      return a && b ? [...a, ...b] : null;
    }
    return null;
  };
  const visit = (n: ts.Node): void => {
    if (
      ts.isStringLiteral(n) ||
      ts.isNoSubstitutionTemplateLiteral(n) ||
      ts.isTemplateHead(n) ||
      ts.isTemplateMiddle(n) ||
      ts.isTemplateTail(n)
    ) {
      m.strings.push(n.text);
    }
    if (ts.isCallExpression(n) && ts.isIdentifier(n.expression) && n.expression.text === 'byId') {
      const arg = n.arguments[0];
      const ids = arg === undefined ? null : literals(arg);
      if (ids === null) {
        m.computed++;
      } else {
        m.byId.push(...ids.map((s) => ({ id: s.text, line: line(s) })));
      }
    }
    if (
      ts.isBinaryExpression(n) &&
      n.operatorToken.kind === ts.SyntaxKind.EqualsToken &&
      ts.isPropertyAccessExpression(n.left) &&
      n.left.name.text === 'id' &&
      ts.isStringLiteral(n.right)
    ) {
      m.setIds.push(n.right.text);
    }
    if (
      ts.isCallExpression(n) &&
      ts.isIdentifier(n.expression) &&
      n.expression.text === 'openDialog'
    ) {
      const opts = n.arguments[0];
      for (const p of opts !== undefined && ts.isObjectLiteralExpression(opts)
        ? opts.properties
        : []) {
        if (
          ts.isPropertyAssignment(p) &&
          ts.isIdentifier(p.name) &&
          p.name.text === 'id' &&
          ts.isStringLiteral(p.initializer)
        ) {
          m.setIds.push(p.initializer.text);
        }
      }
    }
    ts.forEachChild(n, visit);
  };
  visit(sf);
  return m;
}

// the renderer's modules, and the demo's, which builds on the same page
const modules = [...files(RENDERER, '.ts'), ...files(join(SRC, 'demo'), '.ts')].map(read);
const page = readFileSync(join(RENDERER, 'index.html'), 'utf8');
const sheets = files(RENDERER, '.css').map((f) => ({
  file: relative(SRC, f),
  text: readFileSync(f, 'utf8'),
}));

/** The ids on the page, in the modules' markup and given by their code. */
function ids(): Set<string> {
  const out = new Set<string>();
  const markup = (s: string): void => {
    for (const m of s.matchAll(/\bid=["']([^"'$]+)["']/g)) {
      out.add(m[1] ?? '');
    }
  };
  markup(page);
  for (const m of modules) {
    m.strings.forEach(markup);
    m.setIds.forEach((id) => out.add(id));
  }
  return out;
}

/** The classes a sheet's selectors name, each with the first sheet that does. */
function styledClasses(): Map<string, string> {
  const out = new Map<string, string>();
  for (const s of sheets) {
    const css = s.text.replace(/\/\*[\s\S]*?\*\//g, '');
    // a rule's prelude is the text before its `{` since the last `{`, `}`
    // or `;`; an at-rule's (@media, @keyframes) names no class
    for (const m of css.matchAll(/([^{};]*)\{/g)) {
      const prelude = (m[1] ?? '').trim();
      if (prelude.startsWith('@')) {
        continue;
      }
      for (const c of prelude.matchAll(/\.(-?[_a-zA-Z][\w-]*)/g)) {
        const name = c[1] ?? '';
        if (!out.has(name)) {
          out.set(name, s.file);
        }
      }
    }
  }
  return out;
}

/** The words a module's strings and the page's class attributes hold. */
function writtenWords(): Set<string> {
  const out = new Set<string>();
  const words = (s: string): void => {
    for (const w of s.split(/[^\w-]+/)) {
      if (w !== '') {
        out.add(w);
      }
    }
  };
  for (const m of page.matchAll(/\bclass=["']([^"']*)["']/g)) {
    words(m[1] ?? '');
  }
  for (const m of modules) {
    m.strings.forEach(words);
  }
  return out;
}

/**
 * Classes the modules build from a start and a value (`lf-${kind}`), by
 * that start, with where they are built: the whole name is in no string.
 */
const BUILT: Record<string, string> = {
  'lf-': "a log row's level (shell/log-rows.ts: lf-${level})",
  'ax-': "an axis's color (entities/inspector.ts, shell/widgets.ts: ax-${axis})",
  'tp-': "a tray picture's view (state/tilted.ts: tp-${view})",
};

/**
 * The modules whose byId calls take an id worked out at run time, and
 * where the ids come from: the walk cannot check those calls.
 */
const COMPUTED: Record<string, string> = {
  'renderer/play/board-shell.ts': "toggleButton's id, a literal at each caller",
  'renderer/play/sliding.ts': 'a loop over a list of literal ids (the view toggles)',
  'renderer/play/workspace.ts': 'dis(id, off), called with literals',
  'renderer/shell/topbar.ts': 'a loop over a list of literal ids (the boxes the name fits in)',
};

describe('the page and the code', () => {
  it('every id asked for by name is on the page, in a module’s markup or given by code', () => {
    const known = ids();
    const asked = modules.flatMap((m) => m.byId.map((b) => ({ ...b, file: m.file })));
    // the walk found the calls (410 literals, 190 ids, when this was written)
    expect(asked.length).toBeGreaterThan(300);
    const missing = asked
      .filter((b) => !known.has(b.id))
      .map((b) => `${b.file}:${String(b.line)} ${b.id}`);
    expect(missing).toEqual([]);
    // what the walk leaves out is listed, with its reason
    expect(
      modules
        .filter((m) => m.computed > 0)
        .map((m) => m.file)
        .sort(),
    ).toEqual(Object.keys(COMPUTED).sort());
  });

  it('every class a sheet styles is written on the page or by a module', () => {
    const styled = styledClasses();
    const written = writtenWords();
    // (585 classes when this was written)
    expect(styled.size).toBeGreaterThan(300);
    const unused = [...styled]
      .filter(([c]) => !written.has(c) && !Object.keys(BUILT).some((p) => c.startsWith(p)))
      .map(([c, file]) => `${file}: .${c}`);
    expect(unused).toEqual([]);
  });

  it('each start of a built class still names a styled class', () => {
    const styled = [...styledClasses().keys()];
    for (const p of Object.keys(BUILT)) {
      expect(
        styled.some((c) => c.startsWith(p)),
        p,
      ).toBe(true);
    }
  });
});
