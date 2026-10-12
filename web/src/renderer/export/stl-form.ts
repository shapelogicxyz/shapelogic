// SPDX-License-Identifier: GPL-3.0-or-later
//
// The STL dialogs' words and fields (deferred part 5), DOM-free so the unit
// tests take them. From src/gui/stlexport.cpp and stlexportsolution.cpp:
// a parameter's field shows its value as the dialog's snprintf did (%2.2f
// for numbers, %i for whole numbers, a check box for a switch), the
// preview's status line ("Volume: %1.1f cubic-units"), the solution's
// pieces box ("Pieces placed in solution N:", a line a shape, "Total
// instances"), the overwrite question and the closing status line. The
// fields are read stricter than the original's atof and atoi (a field that
// is not a number is said to be one, not read as 0); the ranges are the
// engine's to check.

import type { StlParameter } from '../../engine/api.generated.ts';

/** A parameter's value as its field shows it. */
export function formatParameter(p: StlParameter, value: number): string {
  if (p.type === 'switch') {
    return value !== 0 ? '1' : '0';
  }
  if (p.type === 'positiveInteger') {
    return String(Math.trunc(value));
  }
  return value.toFixed(2);
}

/** What a field holds: its text, or a check box's state. */
export type FieldValue = string | boolean;

/** The values of the fields, in the parameters' order, or what is wrong with one. */
export function readParameters(
  params: readonly StlParameter[],
  fields: readonly FieldValue[],
): { values: number[] } | { error: string } {
  const values: number[] = [];
  for (const [i, p] of params.entries()) {
    const f = fields[i];
    if (p.type === 'switch') {
      values.push(f === true ? 1 : 0);
      continue;
    }
    const text = typeof f === 'string' ? f.trim().replace(',', '.') : '';
    const v = text === '' ? Number.NaN : Number(text);
    if (!Number.isFinite(v)) {
      return { error: `${p.name} must be a number` };
    }
    if (p.type === 'positiveInteger' && !Number.isInteger(v)) {
      return { error: `${p.name} must be a whole number` };
    }
    values.push(v);
  }
  return { values };
}

/** The preview's status line. */
export function volumeLine(volume: number): string {
  return `Volume: ${volume.toFixed(1)} cubic-units`;
}

/** A piece of a solution's export, with its shape's label and name. */
export interface PieceLine {
  label: string;
  name: string;
  count: number;
}

/** The pieces box: the heading, a line a shape, the total. */
export function piecesText(solutionNumber: number, pieces: readonly PieceLine[]): string[] {
  const lines = [`Pieces placed in solution ${String(solutionNumber)}:`];
  let total = 0;
  for (const p of pieces) {
    lines.push(
      p.name === ''
        ? `  ${p.label}  ×${String(p.count)}`
        : `  ${p.label}  ${p.name}  ×${String(p.count)}`,
    );
    total += p.count;
  }
  lines.push(`Total instances: ${String(total)}`);
  return lines;
}

/** The question before files are replaced. */
export function overwriteQuestion(existing: number): string {
  return `${String(existing)} output file(s) already exist. Overwrite?`;
}

/** The status line after an Export All. */
export function exportedLine(exported: number, errors: number): string {
  return errors === 0
    ? `Done — exported ${String(exported)} piece type(s).`
    : `Done — exported ${String(exported)} piece type(s), ${String(errors)} error(s).`;
}
