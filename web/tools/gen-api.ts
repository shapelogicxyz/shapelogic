// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TypeScript types of bt-engine's API, generated from the schema in
// api/*.json (decisions 0003 and 0012), into src/engine/api.generated.ts:
//
//   node tools/gen-api.ts           writes the file (`just web-gen`)
//   node tools/gen-api.ts --check   fails when the committed file is stale
//
// It reads the subset of JSON Schema that api/README.md lists and nothing
// more: a keyword outside it, a $ref that does not resolve or an errc it
// does not know stops the generator, as api/apischema.py's self-check does.

import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
type SchemaObject = Record<string, Json>;

/** The api/ directory beside web/. */
export const API_DIR = resolve(import.meta.dirname, '../../api');
/** Where the generated module goes. */
export const OUTPUT_PATH = resolve(import.meta.dirname, '../src/engine/api.generated.ts');

const ASSERTIONS = new Set([
  'type',
  'properties',
  'required',
  'additionalProperties',
  'items',
  'enum',
  'const',
  'pattern',
  'minimum',
  'maximum',
  'minItems',
  'maxItems',
  'minLength',
  'oneOf',
  '$ref',
]);
const ANNOTATIONS = new Set(['title', 'description', '$schema', '$id', '$defs']);
const FILE_KEYS = new Set([...ANNOTATIONS, 'methods', 'events']);
const ENTRY_KEYS = new Set(['summary', 'params', 'result', 'errors']);
/** The keywords that only narrow a value the type already names. */
const CONSTRAINTS = ['pattern', 'minimum', 'maximum', 'minItems', 'maxItems', 'minLength'] as const;

/**
 * The error codes of decision 0012, by errc: JSON-RPC's own for the
 * protocol, one each for the core's Errc. The schema names the errcs a
 * method may answer (its "errors") but not their codes, so the table lives
 * here; every errc the schema names must be in it.
 */
export const ERROR_CODES: Record<string, readonly number[]> = {
  parse_error: [-32700, -32003],
  invalid_request: [-32600],
  method_not_found: [-32601],
  invalid_params: [-32602],
  internal: [-32603],
  invalid_argument: [-32001],
  not_found: [-32002],
  io_error: [-32004],
  unsupported: [-32005],
  invalid_state: [-32006],
};
/** The errcs a method entry may declare (the core's, not the protocol's). */
const DECLARABLE_ERRCS = new Set([
  'invalid_argument',
  'not_found',
  'parse_error',
  'io_error',
  'unsupported',
  'invalid_state',
  'internal',
]);

/** A schema file of api/. */
interface ApiFile {
  name: string;
  doc: SchemaObject;
}

export class GenError extends Error {}

function isObject(v: Json | undefined): v is SchemaObject {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function pascal(stem: string): string {
  return stem.charAt(0).toUpperCase() + stem.slice(1);
}

function literal(v: Json): string {
  if (typeof v === 'string') {
    return `'${v.replace(/\\/g, '\\\\').replace(/'/g, "\\'")}'`;
  }
  if (v === null || typeof v === 'number' || typeof v === 'boolean') {
    return String(v);
  }
  throw new GenError(`a literal must be a string, number, boolean or null: ${JSON.stringify(v)}`);
}

function propertyKey(name: string): string {
  return /^[A-Za-z_$][A-Za-z0-9_$]*$/.test(name) ? name : literal(name);
}

/** The text of a doc comment, wrapped, at an indentation. */
export function docComment(text: string, indent: string): string {
  const clean = text.replace(/\*\//g, '*\\/').trim();
  if (clean === '') {
    return '';
  }
  const width = 96 - indent.length;
  const out: string[] = [];
  for (const paragraph of clean.split('\n')) {
    let line = '';
    for (const word of paragraph.split(/\s+/).filter((w) => w !== '')) {
      if (line !== '' && line.length + 1 + word.length > width) {
        out.push(line);
        line = word;
      } else {
        line = line === '' ? word : `${line} ${word}`;
      }
    }
    out.push(line);
  }
  if (out.length === 1 && (out[0] ?? '').length <= width - 4) {
    return `${indent}/** ${out[0] ?? ''} */\n`;
  }
  return `${indent}/**\n${out.map((l) => `${indent} *${l === '' ? '' : ` ${l}`}`).join('\n')}\n${indent} */\n`;
}

/** The description of a schema with its constraints, for a doc comment. */
function describe(s: SchemaObject, extra?: string): string {
  const parts: string[] = [];
  if (typeof s.title === 'string') {
    parts.push(s.title);
  }
  if (typeof s.description === 'string') {
    parts.push(s.description);
  }
  if (extra !== undefined) {
    parts.push(extra);
  }
  const constraints = CONSTRAINTS.filter((k) => k in s).map(
    (k) => `@${k} ${String(s[k] as string | number)}`,
  );
  return [...parts, ...constraints].join('\n');
}

/** The schema files and the names their $defs get. */
class Api {
  readonly files: ApiFile[];
  /** "file#Name" -> the TypeScript name. */
  readonly names = new Map<string, string>();

  constructor(apiDir: string) {
    this.files = readdirSync(apiDir)
      .filter((f) => f.endsWith('.json'))
      .sort()
      .map((name) => {
        const doc = JSON.parse(readFileSync(resolve(apiDir, name), 'utf8')) as Json;
        if (!isObject(doc)) {
          throw new GenError(`${name}: not a schema file`);
        }
        return { name, doc };
      });
    // types.json's definitions keep their names; an area file's keep theirs
    // unless another file defines the same name, then they take the area's
    // name in front (shape.json's Made is ShapeMade)
    const count = new Map<string, number>();
    for (const f of this.files) {
      for (const d of Object.keys(this.defs(f))) {
        count.set(d, (count.get(d) ?? 0) + 1);
      }
    }
    const taken = new Set<string>();
    for (const f of this.files) {
      for (const d of Object.keys(this.defs(f))) {
        const shared = f.name !== 'types.json' && (count.get(d) ?? 0) > 1;
        const name = shared ? pascal(f.name.replace(/\.json$/, '')) + d : d;
        if (taken.has(name)) {
          throw new GenError(`${f.name}: the type name ${name} is taken twice`);
        }
        taken.add(name);
        this.names.set(`${f.name}#${d}`, name);
      }
    }
  }

  defs(f: ApiFile): SchemaObject {
    const defs = f.doc.$defs ?? {};
    if (!isObject(defs)) {
      throw new GenError(`${f.name}: $defs is not an object`);
    }
    return defs;
  }

  /** The file and definition a $ref names. */
  target(ref: string, base: string): { file: ApiFile; def: string; schema: SchemaObject } {
    const m = /^([A-Za-z0-9_.-]*)#\/\$defs\/([A-Za-z0-9_]+)$/.exec(ref);
    if (m === null) {
      throw new GenError(`${base}: $ref ${ref} is not of the form [file]#/$defs/Name`);
    }
    const fileName = m[1] === '' ? base : (m[1] ?? base);
    const file = this.files.find((f) => f.name === fileName);
    const def = m[2] ?? '';
    const schema = file === undefined ? undefined : this.defs(file)[def];
    if (file === undefined || !isObject(schema)) {
      throw new GenError(`${base}: $ref ${ref} does not resolve`);
    }
    return { file, def, schema };
  }

  refName(ref: string, base: string): string {
    const t = this.target(ref, base);
    return this.names.get(`${t.file.name}#${t.def}`) ?? '';
  }
}

function checkKeywords(s: SchemaObject, where: string): void {
  for (const k of Object.keys(s)) {
    if (!ASSERTIONS.has(k) && !ANNOTATIONS.has(k)) {
      throw new GenError(`${where}: keyword ${k} is outside the supported subset`);
    }
  }
}

/** A long union of plain members, one member a line. */
function wrapUnion(t: string, indent: string, used: number): string {
  if (used + 2 + t.length <= 100 || /[{([\n]/.test(t) || !t.includes(' | ')) {
    return ` ${t}`;
  }
  return t
    .split(' | ')
    .map((m) => `\n${indent}  | ${m}`)
    .join('');
}

function union(types: string[]): string {
  const unique = [...new Set(types)];
  return unique.length === 0 ? 'never' : unique.join(' | ');
}

/** The TypeScript type of a schema, at an indentation (for objects). */
export function typeExpr(api: Api, s: Json, base: string, where: string, indent: string): string {
  if (s === true) {
    return 'unknown';
  }
  if (!isObject(s)) {
    throw new GenError(`${where}: a schema must be an object`);
  }
  checkKeywords(s, where);
  const assertions = Object.keys(s).filter((k) => ASSERTIONS.has(k));
  const shaping = assertions.filter((k) => !(CONSTRAINTS as readonly string[]).includes(k));
  if ('$ref' in s) {
    if (shaping.length !== 1) {
      throw new GenError(`${where}: $ref beside ${shaping.join(', ')} is not supported`);
    }
    if (typeof s.$ref !== 'string') {
      throw new GenError(`${where}: $ref must be a string`);
    }
    return api.refName(s.$ref, base);
  }
  if ('oneOf' in s) {
    const alts = s.oneOf;
    if (!Array.isArray(alts) || shaping.length !== 1) {
      throw new GenError(`${where}: oneOf must be a list and alone`);
    }
    return union(alts.map((a, i) => typeExpr(api, a, base, `${where}.oneOf[${i}]`, indent)));
  }
  if ('enum' in s || 'const' in s) {
    if (shaping.some((k) => k !== 'enum' && k !== 'const' && k !== 'type')) {
      throw new GenError(`${where}: enum and const go alone`);
    }
    const values = 'enum' in s ? s.enum : [s.const ?? null];
    if (!Array.isArray(values) || values.length === 0) {
      throw new GenError(`${where}: enum must be a list of values`);
    }
    return union(values.map(literal));
  }
  const declared = s.type;
  const types =
    declared === undefined
      ? ['properties', 'additionalProperties'].some((k) => k in s)
        ? ['object']
        : []
      : Array.isArray(declared)
        ? declared
        : [declared];
  if (types.length === 0) {
    return 'unknown';
  }
  return union(
    types.map((t) => {
      switch (t) {
        case 'null':
          return 'null';
        case 'boolean':
          return 'boolean';
        case 'integer':
        case 'number':
          return 'number';
        case 'string':
          return 'string';
        case 'array':
          return arrayExpr(api, s, base, where, indent);
        case 'object':
          return objectExpr(api, s, base, where, indent);
        default:
          throw new GenError(`${where}: unknown type ${JSON.stringify(t)}`);
      }
    }),
  );
}

function arrayExpr(api: Api, s: SchemaObject, base: string, where: string, indent: string): string {
  const item =
    'items' in s ? typeExpr(api, s.items ?? true, base, `${where}[]`, indent) : 'unknown';
  const min = s.minItems;
  const max = s.maxItems;
  if (typeof min === 'number' && min === max && min >= 1 && min <= 4) {
    return `[${Array<string>(min).fill(item).join(', ')}]`;
  }
  return item.includes(' ') ? `(${item})[]` : `${item}[]`;
}

function objectExpr(
  api: Api,
  s: SchemaObject,
  base: string,
  where: string,
  indent: string,
): string {
  const props = s.properties ?? {};
  if (!isObject(props)) {
    throw new GenError(`${where}: properties must be an object`);
  }
  const required = s.required ?? [];
  if (!Array.isArray(required) || !required.every((r) => typeof r === 'string' && r in props)) {
    throw new GenError(`${where}: required must name properties`);
  }
  const extra = s.additionalProperties;
  const names = Object.keys(props);
  if (names.length === 0) {
    if (extra === false) {
      return 'Record<string, never>';
    }
    if (extra === undefined || extra === true) {
      return 'Record<string, unknown>';
    }
  }
  const inner = `${indent}  `;
  const lines: string[] = [];
  const valueTypes: string[] = [];
  let optional = false;
  for (const name of names) {
    const p = props[name] ?? true;
    const t = typeExpr(api, p, base, `${where}.${name}`, inner);
    const isRequired = required.includes(name);
    optional ||= !isRequired;
    valueTypes.push(t);
    const doc = isObject(p) ? docComment(describe(p), inner) : '';
    const head = `${inner}${propertyKey(name)}${isRequired ? '' : '?'}:`;
    lines.push(`${doc}${head}${wrapUnion(t, inner, head.length)};\n`);
  }
  if (isObject(extra)) {
    // a named property must fit the index signature, so it takes theirs too
    const t = typeExpr(api, extra, base, `${where}.*`, inner);
    lines.push(
      `${inner}[key: string]: ${union([t, ...valueTypes, ...(optional && names.length > 0 ? ['undefined'] : [])])};\n`,
    );
  } else if (extra !== undefined && extra !== false && extra !== true) {
    throw new GenError(`${where}: additionalProperties must be false, true or a schema`);
  }
  return `{\n${lines.join('')}${indent}}`;
}

/** The property paths of a result schema that hold a Frame. */
function framePaths(
  api: Api,
  s: Json,
  base: string,
  path: string[],
  seen: Set<string>,
): string[][] {
  if (!isObject(s)) {
    return [];
  }
  if (typeof s.$ref === 'string') {
    const t = api.target(s.$ref, base);
    if (t.file.name === 'types.json' && t.def === 'Frame') {
      return [path];
    }
    const key = `${t.file.name}#${t.def}`;
    if (seen.has(key)) {
      return [];
    }
    return framePaths(api, t.schema, t.file.name, path, new Set([...seen, key]));
  }
  const out: string[][] = [];
  const props = s.properties;
  if (isObject(props)) {
    for (const [name, p] of Object.entries(props)) {
      out.push(...framePaths(api, p, base, [...path, name], seen));
    }
  }
  for (const k of ['items', 'additionalProperties', 'oneOf'] as const) {
    const sub = s[k];
    const subs = Array.isArray(sub) ? sub : [sub ?? null];
    if (subs.some((x) => framePaths(api, x, base, path, seen).length > 0)) {
      throw new GenError(
        `a Frame under ${k} at ${path.join('.') || 'the result'}: the client finds frames by property path only`,
      );
    }
  }
  return out;
}

/** The generated module's text. */
export function generate(apiDir: string = API_DIR): string {
  const api = new Api(apiDir);
  const out: string[] = [];
  out.push(
    '// SPDX-License-Identifier: GPL-3.0-or-later\n' +
      '//\n' +
      '// Generated from api/*.json by web/tools/gen-api.ts: do not edit. `just web-gen`\n' +
      '// writes it; a unit test fails when it is stale.\n' +
      '//\n' +
      "// Every $defs entry is a named type: types.json's under their own names, an area\n" +
      "// file's too unless another file defines the same name (then the area's name goes\n" +
      "// in front: shape.json's Made is ShapeMade). Methods and Events map each name to\n" +
      '// its params, result and errors. Constraints the types cannot say (patterns,\n' +
      '// ranges) are in the doc comments.\n',
  );

  const methods: string[] = [];
  const events: string[] = [];
  const methodNames: string[] = [];
  const eventNames: string[] = [];
  const frames: [string, string[][]][] = [];

  for (const f of api.files) {
    for (const k of Object.keys(f.doc)) {
      if (!FILE_KEYS.has(k)) {
        throw new GenError(`${f.name}: top-level key ${k} is not known`);
      }
    }
    const title = typeof f.doc.title === 'string' ? `: ${f.doc.title}` : '';
    const defs = Object.entries(api.defs(f));
    if (defs.length > 0) {
      out.push(`\n// ---- ${f.name}${title} ----\n`);
    }
    for (const [def, schema] of defs) {
      const name = api.names.get(`${f.name}#${def}`) ?? def;
      const t = typeExpr(api, schema, f.name, `${f.name} $defs.${def}`, '');
      out.push('\n');
      out.push(isObject(schema) ? docComment(describe(schema), '') : '');
      out.push(
        t.startsWith('{\n')
          ? `export interface ${name} ${t}\n`
          : `export type ${name} =${wrapUnion(t, '', 14 + name.length)};\n`,
      );
    }

    const fileMethods = f.doc.methods ?? {};
    const fileEvents = f.doc.events ?? {};
    if (!isObject(fileMethods) || !isObject(fileEvents)) {
      throw new GenError(`${f.name}: methods and events must be objects`);
    }
    for (const [m, entry] of Object.entries(fileMethods)) {
      if (
        !isObject(entry) ||
        !('params' in entry) ||
        !('result' in entry) ||
        typeof entry.summary !== 'string'
      ) {
        throw new GenError(`${f.name} ${m}: an entry needs summary, params and result`);
      }
      for (const k of Object.keys(entry)) {
        if (!ENTRY_KEYS.has(k)) {
          throw new GenError(`${f.name} ${m}: unknown key ${k}`);
        }
      }
      const errors = entry.errors ?? [];
      if (
        !Array.isArray(errors) ||
        !errors.every((e) => typeof e === 'string' && DECLARABLE_ERRCS.has(e))
      ) {
        throw new GenError(`${f.name} ${m}: errors must be errcs of the core`);
      }
      const params = typeExpr(api, entry.params ?? true, f.name, `${m}.params`, '    ');
      const result = typeExpr(api, entry.result ?? true, f.name, `${m}.result`, '    ');
      methods.push(
        docComment(`${entry.summary}\n(${f.name})`, '  ') +
          `  ${literal(m)}: {\n` +
          `    params: ${params};\n` +
          `    result: ${result};\n` +
          `    errors: ${union(errors.map(literal))};\n` +
          `  };\n`,
      );
      methodNames.push(m);
      const paths = framePaths(api, entry.result ?? null, f.name, [], new Set());
      if (paths.length > 0) {
        frames.push([m, paths]);
      }
    }
    for (const [e, entry] of Object.entries(fileEvents)) {
      if (!isObject(entry) || !('params' in entry) || typeof entry.summary !== 'string') {
        throw new GenError(`${f.name} ${e}: an event needs summary and params`);
      }
      const params = typeExpr(api, entry.params ?? true, f.name, `${e}.params`, '    ');
      events.push(
        docComment(`${entry.summary}\n(${f.name})`, '  ') +
          `  ${literal(e)}: {\n    params: ${params};\n  };\n`,
      );
      eventNames.push(e);
    }
  }

  for (const [m, paths] of frames) {
    if (paths.length !== 1 || paths[0]?.join('.') !== 'frame') {
      throw new GenError(
        `${m}: its frames are at ${paths.map((p) => p.join('.')).join(', ')}; the client's reply ` +
          `types (Reply in client-types.ts) expect one, at "frame": extend them`,
      );
    }
  }

  out.push('\n// ---- methods and events ----\n\n');
  out.push('/** Every method: its params, its result and the errcs it may answer. */\n');
  out.push(`export interface Methods {\n${methods.join('')}}\n\n`);
  out.push('/** Every event (notification) the engine sends. */\n');
  out.push(`export interface Events {\n${events.join('')}}\n\n`);
  const list = (names: string[]): string =>
    `[\n${[...names]
      .sort()
      .map((n) => `  ${literal(n)},\n`)
      .join('')}]`;
  out.push(`/** The methods of the schema (engine.info's methods must be these). */\n`);
  out.push(
    `export const METHOD_NAMES = ${list(methodNames)} as const satisfies readonly (keyof Methods)[];\n\n`,
  );
  out.push(`/** The events of the schema. */\n`);
  out.push(
    `export const EVENT_NAMES = ${list(eventNames)} as const satisfies readonly (keyof Events)[];\n\n`,
  );
  out.push(
    '/**\n' +
      ' * The error codes of decision 0012 by errc (data.errc of every error reply).\n' +
      ' * parse_error has two: -32700 for a line that is not JSON, -32003 for a file or\n' +
      ' * bytes that are no puzzle.\n' +
      ' */\n',
  );
  out.push(
    `export const ERROR_CODES = {\n${Object.entries(ERROR_CODES)
      .map(([errc, codes]) => `  ${errc}: [${codes.join(', ')}],\n`)
      .join('')}} as const;\n\n`,
  );
  out.push('/** The errcs the engine answers with. */\n');
  out.push('export type Errc = keyof typeof ERROR_CODES;\n\n');
  out.push(
    '/** The methods whose result carries a binary frame, and the property that holds it. */\n',
  );
  out.push(
    `export const FRAME_PATHS = {\n${frames
      .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0))
      .map(([m, paths]) => `  ${literal(m)}: ${literal(paths[0]?.join('.') ?? '')},\n`)
      .join('')}} as const satisfies Partial<Record<keyof Methods, string>>;\n\n`,
  );
  out.push('/** A method whose result carries a binary frame. */\n');
  out.push('export type FrameMethod = keyof typeof FRAME_PATHS;\n');
  return out.join('');
}

function main(): number {
  const check = process.argv.includes('--check');
  let text: string;
  try {
    text = generate();
  } catch (e) {
    if (e instanceof GenError) {
      console.error(`gen-api: ${e.message}`);
      return 1;
    }
    throw e;
  }
  let current = '';
  try {
    current = readFileSync(OUTPUT_PATH, 'utf8');
  } catch {
    // not written yet
  }
  if (check) {
    if (current !== text) {
      console.error('gen-api: src/engine/api.generated.ts is stale: run `just web-gen`');
      return 1;
    }
    return 0;
  }
  if (current !== text) {
    writeFileSync(OUTPUT_PATH, text);
    console.log('gen-api: wrote src/engine/api.generated.ts');
  } else {
    console.log('gen-api: src/engine/api.generated.ts is current');
  }
  return 0;
}

if (process.argv[1] !== undefined && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exitCode = main();
}
