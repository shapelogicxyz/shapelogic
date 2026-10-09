// SPDX-License-Identifier: GPL-3.0-or-later
//
// The type generator: the committed api.generated.ts is current (as ctest
// api-docs checks docs/API.md), and the schema subset maps as the plan says.

import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterEach, describe, expect, it } from 'vitest';

import { EVENT_NAMES, METHOD_NAMES } from '../../src/engine/api.generated.ts';
import type { Events, Methods } from '../../src/engine/api.generated.ts';
import { API_DIR, generate, GenError, OUTPUT_PATH } from '../../tools/gen-api.ts';

describe('the generated types', () => {
  it('are current: run `just web-gen` after changing api/', () => {
    expect(readFileSync(OUTPUT_PATH, 'utf8')).toBe(generate(API_DIR));
  });

  it('name every method and event in METHOD_NAMES and EVENT_NAMES', () => {
    type MissingMethod = Exclude<keyof Methods, (typeof METHOD_NAMES)[number]>;
    type MissingEvent = Exclude<keyof Events, (typeof EVENT_NAMES)[number]>;
    const none: [MissingMethod, MissingEvent] extends [never, never] ? true : false = true;
    expect(none).toBe(true);
    expect([...METHOD_NAMES]).toEqual([...METHOD_NAMES].sort());
    expect(new Set(METHOD_NAMES).size).toBe(METHOD_NAMES.length);
  });
});

describe('the schema subset', () => {
  let dir = '';
  afterEach(() => {
    if (dir !== '') {
      rmSync(dir, { recursive: true });
      dir = '';
    }
  });

  function gen(files: Record<string, unknown>): string {
    dir = mkdtempSync(join(tmpdir(), 'gen-api-'));
    for (const [name, doc] of Object.entries(files)) {
      writeFileSync(join(dir, name), JSON.stringify(doc));
    }
    return generate(dir);
  }

  const frame = {
    type: 'object',
    properties: {
      frame: { type: 'integer', minimum: 1 },
      bytes: { type: 'integer' },
      arrays: { type: 'object', additionalProperties: { type: 'string' } },
    },
    required: ['frame', 'bytes', 'arrays'],
    additionalProperties: false,
  };

  const types = {
    $id: 'types.json',
    title: 'Shared',
    $defs: {
      Id: { type: 'string', pattern: '^s[0-9]+$', description: 'An id.' },
      Kind: { enum: ['a', 'b', null] },
      One: { const: 'only' },
      MaybeId: { oneOf: [{ $ref: '#/$defs/Id' }, { type: 'null' }] },
      Count: { type: ['integer', 'null'], minimum: 0 },
      Triple: { type: 'array', items: { type: 'integer' }, minItems: 3, maxItems: 3 },
      Axes: { type: 'array', items: { enum: ['x', 'y'] }, maxItems: 2 },
      Thing: {
        type: 'object',
        properties: {
          id: { $ref: '#/$defs/Id' },
          size: { type: 'number', description: 'How big.' },
        },
        required: ['id'],
        additionalProperties: false,
      },
      Map: { type: 'object', additionalProperties: { $ref: '#/$defs/Thing' } },
      Settings: {
        type: 'object',
        properties: { on: { type: 'boolean' } },
        additionalProperties: { type: 'string' },
      },
      Empty: { type: 'object', properties: {}, additionalProperties: false },
      Untyped: { items: { type: 'integer' }, minItems: 1 },
      Frame: frame,
      Mesh: {
        type: 'object',
        properties: { frame: { $ref: '#/$defs/Frame' }, triangles: { type: 'integer' } },
        required: ['frame', 'triangles'],
        additionalProperties: false,
      },
    },
  };

  const area = (
    methods: Record<string, unknown>,
    extra: Record<string, unknown> = {},
  ): unknown => ({
    $id: 'a.json',
    title: 'Area',
    methods,
    events: {
      'a.happened': { summary: 'It happened.', params: { $ref: 'types.json#/$defs/Thing' } },
    },
    $defs: { Made: { type: 'object', properties: { id: { $ref: 'types.json#/$defs/Id' } } } },
    ...extra,
  });

  const method = {
    summary: 'Gets a mesh.',
    params: {
      type: 'object',
      properties: {
        id: { $ref: 'types.json#/$defs/Id' },
        kind: { $ref: 'types.json#/$defs/Kind' },
      },
      required: ['id'],
      additionalProperties: false,
    },
    result: { $ref: 'types.json#/$defs/Mesh' },
    errors: ['not_found', 'invalid_state'],
  };

  it('maps types, enums, consts, oneOf, nullable types, tuples and objects', () => {
    const out = gen({ 'types.json': types, 'a.json': area({ 'a.mesh': method }) });
    expect(out).toContain('export type Id = string;');
    expect(out).toContain('@pattern ^s[0-9]+$');
    expect(out).toContain("export type Kind = 'a' | 'b' | null;");
    expect(out).toContain("export type One = 'only';");
    expect(out).toContain('export type MaybeId = Id | null;');
    expect(out).toContain('export type Count = number | null;');
    expect(out).toContain('export type Triple = [number, number, number];');
    expect(out).toContain("export type Axes = ('x' | 'y')[];");
    expect(out).toMatch(
      /export interface Thing \{\n {2}id: Id;\n {2}\/\*\* How big\. \*\/\n {2}size\?: number;\n\}/,
    );
    expect(out).toMatch(/export interface Map \{\n {2}\[key: string\]: Thing;\n\}/);
    // a named property must fit the index signature
    expect(out).toContain('[key: string]: string | boolean | undefined;');
    expect(out).toContain('export type Empty = Record<string, never>;');
    // the keywords say what an untyped schema is (never `unknown` in silence)
    expect(out).toContain('export type Untyped = number[];');
  });

  it('maps methods, events, errors and the frame marker', () => {
    const out = gen({ 'types.json': types, 'a.json': area({ 'a.mesh': method }) });
    expect(out).toMatch(
      /'a\.mesh': \{\n {4}params: \{\n {6}id: Id;\n {6}kind\?: Kind;\n {4}\};\n {4}result: Mesh;\n {4}errors: 'not_found' \| 'invalid_state';/,
    );
    expect(out).toMatch(/'a\.happened': \{\n {4}params: Thing;/);
    expect(out).toContain("export const METHOD_NAMES = [\n  'a.mesh',\n]");
    expect(out).toContain("export const EVENT_NAMES = [\n  'a.happened',\n]");
    expect(out).toContain("'a.mesh': 'frame',");
    expect(out).toContain('parse_error: [-32700, -32003],');
  });

  it('names a shared $defs name after its area file', () => {
    const b = { $id: 'b.json', $defs: { Made: { type: 'object', properties: {} } } };
    const out = gen({ 'types.json': types, 'a.json': area({}), 'b.json': b });
    expect(out).toContain('export interface AMade');
    expect(out).toContain('export type BMade = Record<string, unknown>;');
    expect(out).not.toMatch(/export (interface|type) Made\b/);
  });

  it('refuses what is outside the subset', () => {
    const bad = (s: unknown): unknown => ({ ...types, $defs: { ...types.$defs, Bad: s } });
    expect(() => gen({ 'types.json': bad({ type: 'string', format: 'date' }) })).toThrow(GenError);
    expect(() => gen({ 'types.json': bad({ $ref: '#/$defs/Nowhere' }) })).toThrow(
      /does not resolve/,
    );
    expect(() => gen({ 'types.json': bad({ $ref: 'other.json#/$defs/Id' }) })).toThrow(
      /does not resolve/,
    );
    expect(() => gen({ 'types.json': bad({ $ref: '#/properties/x' }) })).toThrow(/not of the form/);
    expect(() => gen({ 'types.json': bad({ type: 'date' }) })).toThrow(/unknown type/);
    expect(() => gen({ 'types.json': bad({ $ref: '#/$defs/Id', type: 'string' }) })).toThrow(
      /beside/,
    );
    expect(() => gen({ 'types.json': bad({ allOf: [] }) })).toThrow(/outside the supported subset/);
  });

  it('refuses entries that are not well formed', () => {
    expect(() =>
      gen({ 'types.json': types, 'a.json': area({ 'a.x': { ...method, errors: ['oops'] } }) }),
    ).toThrow(/errcs of the core/);
    expect(() =>
      gen({ 'types.json': types, 'a.json': area({ 'a.x': { params: {}, result: {} } }) }),
    ).toThrow(/needs summary/);
    expect(() =>
      gen({ 'types.json': types, 'a.json': area({ 'a.x': { ...method, deprecated: true } }) }),
    ).toThrow(/unknown key/);
    expect(() => gen({ 'types.json': types, 'a.json': area({}, { extra: 1 }) })).toThrow(
      /top-level key/,
    );
    const event = { summary: 'It happened.', params: { type: 'object' }, result: {} };
    expect(() =>
      gen({ 'types.json': types, 'a.json': area({}, { events: { 'a.e': event } }) }),
    ).toThrow(/unknown key result/);
  });

  it('refuses a frame the client cannot find', () => {
    const inArray = {
      ...method,
      result: { type: 'array', items: { $ref: 'types.json#/$defs/Mesh' } },
    };
    expect(() => gen({ 'types.json': types, 'a.json': area({ 'a.x': inArray }) })).toThrow(
      /under items/,
    );
    const nested = {
      ...method,
      result: {
        type: 'object',
        properties: { mesh: { $ref: 'types.json#/$defs/Mesh' } },
        additionalProperties: false,
      },
    };
    expect(() => gen({ 'types.json': types, 'a.json': area({ 'a.x': nested }) })).toThrow(
      /mesh\.frame/,
    );
  });
});
