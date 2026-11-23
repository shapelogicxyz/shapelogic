// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keeps the project in American spelling: fails on any British spelling in a file we write.
// The BurrTools reference files, the read-only mockup, built output and licence text are left out.
// To read a name an older version saved under its British spelling, build the word from parts
// ('Col' + 'our'), as prefs.ts does.

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

const ROOT = resolve(import.meta.dirname, '../../..');
const WORDS = [
  'ageing',
  'aluminium',
  'amongst',
  'analogue',
  'artefact',
  'behaviour',
  'calibre',
  'cancelled',
  'cancelling',
  'catalogue',
  'centre',
  'colour',
  'customise',
  'defence',
  'deserialise',
  'dialogue',
  'draught',
  'encyclopaedia',
  'endeavour',
  'favourite',
  'fibre',
  'flavour',
  'fuelled',
  'glamour',
  'grey',
  'grey-scale',
  'harbour',
  'honour',
  'humour',
  'initialise',
  'jewellery',
  'kerb',
  'labelled',
  'labelling',
  'labour',
  'learnt',
  'licence (noun)',
  'litre',
  'manoeuvre',
  'maximise',
  'metre',
  'minimise',
  'modelled',
  'modelling',
  'monologue',
  'mould',
  'moulding',
  'neighbour',
  'normalise',
  'offence',
  'optimise',
  'organise',
  'paediatric',
  'programme',
  'recognise',
  'rumour',
  'sabre',
  'sceptical',
  'serialise',
  'specialise',
  'spectre',
  'spelt',
  'standardise',
  'sulphur',
  'theatre',
  'towards',
  'travelled',
  'traveller',
  'travelling',
  'tyre',
  'visualise',
  'whilst',
];
const SKIP = [
  'docs/mockup/',
  'tests/data/',
  'tests/oracle/',
  'tests/unit/grid/grid_dump.h',
  'tests/unit/grid/test_grid_dump.cpp',
  'tests/unit/spelling',
  'web/tests/unit/spelling',
  'site/package-lock.json',
  'site/public/demo/',
  'COPYING',
];
// a lowercase word may follow these without a break (decolour, uninitialised, slategrey)
const PREFIXES = [
  'un',
  'de',
  're',
  'no',
  'non',
  'mis',
  'pre',
  'nano',
  'slate',
  'dim',
  'dark',
  'light',
];

const rx = new RegExp(`(${[...WORDS].sort((a, b) => b.length - a.length).join('|')})`, 'gi');

function isBreak(text: string, at: number): boolean {
  const prev = text[at - 1];
  if (prev === undefined || !/[A-Za-z]/.test(prev)) {
    return true;
  }
  if (/[A-Z]/.test(text[at] ?? '')) {
    return true; // camelCase: setColour
  }
  const before = text.slice(Math.max(0, at - 5), at).toLowerCase();
  return PREFIXES.some((p) => before.endsWith(p));
}

describe('spelling', () => {
  it('uses American spelling in every file we write', () => {
    const files = execFileSync(
      'git',
      ['ls-files', '-z', '--cached', '--others', '--exclude-standard'],
      {
        cwd: ROOT,
        encoding: 'utf8',
      },
    )
      .split('\0')
      .filter(
        (f) => f !== '' && !SKIP.some((s) => f.startsWith(s)) && existsSync(resolve(ROOT, f)),
      );
    const found: string[] = [];
    for (const f of files) {
      let text: string;
      try {
        text = readFileSync(resolve(ROOT, f), 'utf8');
      } catch {
        continue;
      }
      if (text.includes('\0')) {
        continue;
      }
      for (const m of [...text.matchAll(rx)]) {
        if (isBreak(text, m.index)) {
          found.push(`${f}: ${m[0]}`);
        }
      }
      for (const m of [...f.matchAll(rx)]) {
        if (isBreak(f, m.index)) {
          found.push(`${f}: file name has ${m[0]}`);
        }
      }
    }
    expect(found).toEqual([]);
  });
});
