// SPDX-License-Identifier: GPL-3.0-or-later
//
// The export dialogs' fields read as numbers (stage 7), DOM-free so the
// unit tests take them.

/** A number typed in a field: finite and above 0 (a comma taken as the point), else null. */
export function positive(text: string): number | null {
  const v = Number(text.trim().replace(',', '.'));
  return Number.isFinite(v) && v > 0 ? v : null;
}
