// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edit ▸ Convert Assemblies to Pieces… (stage 6): the original's dialog
// (assmimportwindow.cpp: the source problem, the destination (just add the
// shapes, a new problem, an existing one), the range of copies, the filters
// with their defaults (drop disconnected and remove identical shapes on;
// millable and notchable on the brick grid only), the shapes' cell range
// 0 to 1,000,000) over solution.toShapes, which applies the filters as
// cb_AssembliesToShapes did. One undo step; refused while anything solves
// ("pause first", before any call), as every change of the lists.

import type { Edits } from '../state/edits.ts';
import type { Store } from '../state/store.ts';
import { openDialog } from '../shell/dialog.ts';
import { esc } from '../shell/dom.ts';
import { flash, reportError } from '../shell/status.ts';
import { FILTERS, toShapesParams } from './to-shapes-form.ts';

/** Opens the dialog; `shown` is the problem the Solver shows (chosen first). */
export function openToShapes(store: Store, edits: Edits, shown: string | null): void {
  const snap = store.snapshot;
  if (snap === null || edits.refuseList()) {
    return;
  }
  const sources = snap.problems.filter((p) => p.saved > 0);
  if (sources.length === 0) {
    flash('You need a problem with solutions to convert assemblies');
    return;
  }
  const bricks = snap.grid === 'bricks';
  const opt = (id: string, label: string, name: string, sel: boolean): string =>
    `<option value="${esc(id)}" ${sel ? 'selected' : ''}>${esc(label)}${name === '' ? '' : ` — ${esc(name)}`}</option>`;
  const first = sources.find((p) => p.id === shown) ?? sources[0];
  const gen = store.generation;
  void openDialog({
    id: 'toShapes',
    title: 'Convert assemblies to pieces',
    wide: true,
    body: `<div class="form">
<label class="fl">Source<select data-f="problem">${sources.map((p) => opt(p.id, `${p.label} (${p.saved} saved)`, p.name, p === first)).join('')}</select></label>
<fieldset><legend>Destination</legend>
<label><input type="radio" name="addTo" value="nothing" checked> Just add the shapes to the puzzle</label>
<label><input type="radio" name="addTo" value="newProblem"> Add the shapes to a new problem</label>
<label><input type="radio" name="addTo" value="problem"> Add the shapes to an existing problem</label>
<select data-f="target">${snap.problems.map((p) => opt(p.id, p.label, p.name, false)).join('')}</select></fieldset>
<fieldset><legend>Range of each piece</legend><label class="fl">Minimum<input data-f="min" value="0" inputmode="numeric"></label><label class="fl">Maximum<input data-f="max" value="1" inputmode="numeric"></label></fieldset>
<fieldset><legend>Filter</legend>${FILTERS.filter(([, , , b]) => !b || bricks)
      .map(
        ([k, label, on]) =>
          `<label><input type="checkbox" data-d="${k}" ${on ? 'checked' : ''}> ${esc(label)}</label>`,
      )
      .join('')}
<label class="fl">Shape size from<input data-f="minCells" value="0" inputmode="numeric"></label><label class="fl">to<input data-f="maxCells" value="1000000" inputmode="numeric"></label> cells</fieldset></div>`,
    buttons: [
      { key: 'cancel', label: 'Cancel' },
      {
        key: 'go',
        label: 'Convert',
        kind: 'pri',
        default: true,
        tip: 'Make the chosen assemblies into shapes',
      },
    ],
    onAnswer: (k, el) => {
      if (k !== 'go') {
        return true;
      }
      const v = (f: string): string =>
        el.querySelector<HTMLInputElement | HTMLSelectElement>(`[data-f="${f}"]`)?.value ?? '';
      const addTo = (el.querySelector<HTMLInputElement>('input[name="addTo"]:checked')?.value ??
        'nothing') as 'nothing' | 'newProblem' | 'problem';
      const drops = Object.fromEntries(
        [...el.querySelectorAll<HTMLInputElement>('[data-d]')].map((c) => [
          c.dataset.d ?? '',
          c.checked,
        ]),
      );
      const params = toShapesParams(
        {
          problem: v('problem'),
          addTo,
          target: v('target'),
          min: v('min'),
          max: v('max'),
          minCells: v('minCells'),
          maxCells: v('maxCells'),
          drops,
        },
        bricks,
      );
      if (typeof params === 'string') {
        flash(params, 5000);
        return false;
      }
      if (store.generation !== gen) {
        flash('Another puzzle was opened: nothing converted', 5000);
        return true;
      }
      if (edits.refuseList()) {
        return false;
      }
      void edits
        .mutate('Convert assemblies to pieces', () =>
          store.engine.call('solution.toShapes', params),
        )
        .then((r) => {
          if (r !== null) {
            flash(
              r.shapes.length === 0
                ? 'No assembly passed the filters: no shapes added'
                : `Added ${r.shapes.length} shape${r.shapes.length === 1 ? '' : 's'}`,
              4000,
            );
          }
        }, reportError);
      return true;
    },
  });
}
