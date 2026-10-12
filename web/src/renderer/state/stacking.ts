// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a stacking puzzle's screens work out for themselves (docs/webui-
// spec.md, section 9), DOM-free for the unit tests: the disc list's bounds
// and message (BlockList.cpp's DiskSelector), a rod set's name in the lists
// and the fields its rules grey out (BlockList.cpp's RodSelector,
// mainwindow_rods.cpp's loadRodFields and syncPanexFields), the rod
// slider's places and caption (rodbars.h's RodIndexBar_c,
// mainwindow_rods.cpp's selectedStackRod), the validity bar's words
// (StackValidBar_c::setState), a disc's place from the stack list's row
// (the list shows the top first), and the paused export's size
// (mainwindow_file.cpp's sizeText).

import type { RodSet } from '../../engine/api.generated.ts';

/** The disc list's bounds and its message (DiskSelector::onDelta, onSize). */
export const DISC_MIN = 1;
export const DISC_MAX = 100;
export const DISC_SIZE_REFUSED = 'An invalid size was entered, please pick a value between 1-100.';

/** A disc size typed in the field: the number, or null when it is not one from 1 to 100. */
export function discSizeOf(text: string): number | null {
  const t = text.trim();
  if (!/^[+-]?\d+$/.test(t)) {
    return null;
  }
  const v = Number(t);
  return v >= DISC_MIN && v <= DISC_MAX ? v : null;
}

/** A rod set's rules as stacking.setRules takes them. */
export type RodRules = Partial<
  Pick<
    RodSet,
    | 'rodCount'
    | 'growHeight'
    | 'definedHeight'
    | 'sizeMatters'
    | 'distanceMatters'
    | 'panexColumns'
    | 'pocketColumn'
    | 'pocketHeight'
    | 'outsideChannel'
  >
>;

/** The fields the original greyed out for a rod set's rules (syncPanexFields, loadRodFields). */
export function rodFieldsOff(r: RodSet): Set<keyof RodRules> {
  const off = new Set<keyof RodRules>();
  if (r.growHeight) {
    off.add('definedHeight');
  }
  if (r.panexColumns) {
    off.add('sizeMatters');
  } else {
    off.add('pocketColumn');
  }
  if (!(r.panexColumns && r.pocketColumn)) {
    off.add('pocketHeight');
  }
  if (!(r.panexColumns && r.rodCount >= 3)) {
    off.add('outsideChannel');
  }
  return off;
}

/** A rod set's name in the lists (RodSelector::getText): "R1", or "R1 - name". */
export function rodSetText(r: Pick<RodSet, 'label' | 'name'>): string {
  return r.name === '' ? r.label : `${r.label} - ${r.name}`;
}

/** The rod slider's places: the pocket first when there is one (RodIndexBar_c's pocketFirst). */
export interface RodPlaces {
  /** How many places the slider has. */
  count: number;
  /** Whether place 0 is the pocket column (the stack map's last rod). */
  pocketFirst: boolean;
}

/** The slider's places of a board with `rods` rods, the pocket among them or not. */
export function rodPlaces(rods: number, pocket: boolean): RodPlaces {
  return { count: Math.max(1, rods), pocketFirst: pocket };
}

/** The stack map's rod at a place of the slider (selectedStackRod). */
export function rodAt(p: RodPlaces, place: number): number {
  const v = Math.max(0, Math.min(p.count - 1, place));
  if (!p.pocketFirst) {
    return v;
  }
  return v === 0 ? p.count - 1 : v - 1;
}

/** The slider's caption at a place (RodIndexBar_c::syncCaption): "Rod: 2", or "Rod: P". */
export function rodCaption(p: RodPlaces, place: number): string {
  const v = Math.max(0, Math.min(p.count - 1, place));
  return p.pocketFirst && v === 0 ? 'Rod: P' : `Rod: ${String(v + (p.pocketFirst ? 0 : 1))}`;
}

/** The validity bar's text (StackValidBar_c::setState). */
export function stackValidity(error: string | null): { valid: boolean; text: string } {
  return error === null
    ? { valid: true, text: 'Valid' }
    : { valid: false, text: `Invalid - ${error}` };
}

/** A disc's place on its rod from the list's row (the list shows the top first). */
export function stackIndex(depth: number, row: number): number {
  return depth - 1 - row;
}

/** A size as the original's export question gave it (mainwindow_file.cpp's sizeText). */
export function sizeText(bytes: number): string {
  return bytes >= 1e9 ? `${(bytes / 1e9).toFixed(1)} GB` : `${(bytes / 1e6).toFixed(0)} MB`;
}
