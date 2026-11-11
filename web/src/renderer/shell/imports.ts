// SPDX-License-Identifier: GPL-3.0-or-later
//
// File ▸ Import ▸ PuzzleSolver3D… and Puzzlecad (OpenSCAD)…, File ▸
// Export ▸ Puzzlecad (OpenSCAD)… and Edit ▸ Convert Brick Grid Type to
// Other… (deferred part 6; docs/webui-spec.md section 11), from the
// original's mainwindow_file.cpp (cb_Load_Ps3d, cb_Load_Scad,
// cb_Export_Scad, cb_Convert) and convertwindow.cpp; the export's options
// (pieces or a problem's, the trays and their lip) are not the original's.
// The engine reads the
// files, writes the Puzzlecad text and converts (document.import,
// document.exportPuzzlecad, document.convertGrid); the grids a puzzle
// converts to are engine.info's. An imported puzzle has no file (Save asks
// where, never over the source); a converted one keeps the document's, as
// the original kept its file name.

import type { BtApp } from '../../engine/app-bridge.ts';
import type { Grid } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { ImportFormat, Store } from '../state/store.ts';
import { askChoice, openDialog } from './dialog.ts';
import { pickerHtml, ShapePicker } from '../export/shape-picker.ts';
import type { PickerShape } from '../export/shape-picker.ts';
import { esc } from './dom.ts';
import { flash, reportError } from './status.ts';

/** What the imports need besides the store and the bridge. */
export interface ImportsHost {
  /** The document's name (its file's, else the one shown). */
  readonly name: string;
  /** The problem the Puzzle workspace shows (the original's problem selector), or null. */
  readonly problem: string | null;
  /** Asks about unsaved changes before the puzzle is replaced; true when it may go. */
  confirmDiscard(action: string): Promise<boolean>;
  /** Brings the Entities workspace forward (the original's selectEntitiesTab). */
  showEntities(): void;
}

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

/** What an imported puzzle is shown as: its file's name without the extension. */
export function importedName(path: string): string {
  const b = baseName(path);
  const dot = b.lastIndexOf('.');
  return dot > 0 ? b.slice(0, dot) : b;
}

/** The open dialog's kind of each format. */
const OPEN_KIND = { puzzleSolver3D: 'ps3d', puzzlecad: 'scad' } as const;

/** The convert dialog's line saying what Continue does (not the original's: it said nothing). */
export const CONVERT_NOTE =
  'The puzzle is replaced: its undo history starts afresh and its solutions are dropped; Save writes it over its file.';

/** A message box of the original's words and, under them, the reason. */
function alertBox(id: string, text: string, reason?: string): Promise<string | null> {
  return askChoice(id, 'ShapeLogic', reason === undefined ? text : `${text}\n${reason}`, [
    { key: 'ok', label: 'OK', kind: 'pri', default: true },
  ]);
}

/** Whether an engine error is a refusal to tell in a box (not "pause first", not superseded). */
function told(e: unknown): e is EngineError {
  return e instanceof EngineError && e.errc !== 'invalid_state' && e.errc !== 'superseded';
}

/** What the Puzzlecad export writes: a problem's pieces (null: every piece shape) and the trays. */
export interface ScadChoice {
  problem: string | null;
  /** With no problem, the shapes chosen as the pieces, in shape order. */
  shapes: string[] | null;
  tray: boolean;
  /** The lip's height in voxels; 0 for none. */
  lip: number;
}

/** The highest lip the engine writes (document.exportPuzzlecad). */
const MAX_LIP = 16;

/** The pieces chosen last, by document: the next dialog starts with them. */
let scadPieces: { path: string | null; shapes: string[] } | null = null;

/** The choices made last: the next dialog starts with them. */
let scadChosen: { problems: boolean; tray: boolean | null; lip: boolean; height: number } = {
  problems: true,
  tray: null,
  lip: true,
  height: 1,
};

/**
 * The Puzzlecad export's options (not in the original, which wrote the
 * problem's distinct pieces): Pieces, the shapes picked in two lists as in
 * Export STL (all on the left to start with, then the last ones picked for
 * the document on the right), each once, or Problems,
 * the chosen problem's pieces with every copy; the trays the pieces go in
 * (Export Tray; on to start with), and the wall around
 * them (Add Tray Lip) of a height in voxels, both for sliding puzzles only.
 * Null when cancelled.
 */
export async function scadOptionsDialog(
  problems: readonly { id: string; label: string }[],
  current: string | null,
  sliding: boolean,
  pieces: readonly PickerShape[],
  path: string | null,
): Promise<ScadChoice | null> {
  const asProblems = scadChosen.problems && problems.length > 0;
  const tray = scadChosen.tray ?? sliding;
  let choice: ScadChoice | null = null;
  const kept = scadPieces?.path === path ? scadPieces.shapes : [];
  const picker = new ShapePicker(
    pieces,
    // none to start with, as File ▸ Export ▸ STL's left list: the user moves them across
    kept,
    {},
  );
  const sync = (el: HTMLElement): void => {
    const q = (f: string): HTMLInputElement | null =>
      el.querySelector<HTMLInputElement>(`[data-f="${f}"]`);
    picker.setDisabled(q('pieces')?.checked !== true);
    const problem = el.querySelector<HTMLSelectElement>('[data-f="problem"]');
    if (problem !== null) {
      problem.disabled = q('problems')?.checked !== true;
    }
    const lip = q('lip');
    if (lip !== null) {
      lip.disabled = q('tray')?.checked !== true;
    }
    const height = q('height');
    if (height !== null) {
      height.disabled = lip === null || lip.disabled || !lip.checked;
    }
  };
  const shown = openDialog({
    id: 'exportScad',
    title: 'Export Puzzlecad (OpenSCAD)',
    body: `<div class="form scad-opts">
<label title="The shapes you choose, once each"><input type="radio" name="scadWhat" data-f="pieces" ${asProblems ? '' : 'checked'}> Pieces</label>
<div class="scad-pick">${pickerHtml({
      all: 'The shapes left out; select shapes and press → to export them',
      chosen: 'The shapes to export as pieces, once each',
    })}</div>
<label title="${problems.length === 0 ? 'The puzzle has no problem' : 'The pieces of a problem, as many of each as the problem has'}"><input type="radio" name="scadWhat" data-f="problems" ${asProblems ? 'checked' : ''} ${problems.length === 0 ? 'disabled' : ''}> Problems</label>
<select data-f="problem" class="scad-problem" aria-label="Problem" title="The problem whose pieces are exported">${problems.map((p) => `<option value="${esc(p.id)}" ${p.id === current ? 'selected' : ''}>${esc(p.label)}</option>`).join('')}</select>
${
  sliding
    ? `<label title="Also export the board the pieces slide in, as a packing box"><input type="checkbox" data-f="tray" ${tray ? 'checked' : ''}> Export Tray</label>
<div class="scad-lip"><label title="A wall around the tray, the tray made a voxel bigger on each side to take it"><input type="checkbox" data-f="lip" ${scadChosen.lip ? 'checked' : ''}> Add Tray Lip</label>
<label class="fl" title="The height of the lip, in voxels">Height (voxels)<input data-f="height" value="${String(scadChosen.height)}" inputmode="numeric"></label></div>`
    : ''
}
<div class="scad-err" data-f="err"></div></div>`,
    buttons: [
      { key: 'cancel', label: 'Cancel' },
      {
        key: 'go',
        label: 'Export…',
        kind: 'pri',
        default: true,
        tip: 'Choose a file and write the Puzzlecad file to it',
      },
    ],
    onAnswer: (k, el) => {
      if (k !== 'go') {
        return true;
      }
      const q = (f: string): HTMLInputElement | null =>
        el.querySelector<HTMLInputElement>(`[data-f="${f}"]`);
      const byProblem = q('problems')?.checked === true;
      const withTray = q('tray')?.checked === true;
      const withLip = q('lip')?.checked === true;
      const err = el.querySelector<HTMLElement>('[data-f="err"]');
      const shapes = picker.ids();
      if (!byProblem && shapes.length === 0) {
        if (err !== null) {
          err.textContent = 'Choose the shapes to export: select them and press →.';
        }
        return false;
      }
      const text = (q('height')?.value ?? '').trim();
      const height = /^\d+$/.test(text) ? Number(text) : Number.NaN;
      if (withTray && withLip && !(height >= 1 && height <= MAX_LIP)) {
        if (err !== null) {
          err.textContent = `The lip's height is a whole number of voxels from 1 to ${String(MAX_LIP)}.`;
        }
        return false;
      }
      const problem = el.querySelector<HTMLSelectElement>('[data-f="problem"]')?.value ?? null;
      // a brick puzzle's dialog has no tray choices: the sliding ones are kept
      scadChosen = sliding
        ? {
            problems: byProblem,
            tray: withTray,
            lip: withLip,
            height: Number.isNaN(height) ? scadChosen.height : height,
          }
        : { ...scadChosen, problems: byProblem };
      scadPieces = { path, shapes };
      choice = {
        problem: byProblem ? problem : null,
        shapes: byProblem ? null : shapes,
        tray: withTray,
        lip: withTray && withLip ? height : 0,
      };
      return true;
    },
  });
  const el = document.getElementById('exportScad');
  if (el !== null) {
    picker.attach(el);
    sync(el);
    el.addEventListener('change', () => {
      sync(el);
    });
  }
  await shown;
  return choice;
}

export class Imports {
  private readonly store: Store;
  private readonly app: BtApp;
  private readonly host: ImportsHost;

  constructor(store: Store, app: BtApp, host: ImportsHost) {
    this.store = store;
    this.app = app;
    this.host = host;
  }

  private ready(): boolean {
    if (!this.store.running || this.store.snapshot === null) {
      flash('The engine is not running');
      return false;
    }
    return true;
  }

  /** File ▸ Import ▸ PuzzleSolver3D or Puzzlecad: a file chosen, the puzzle replaced. */
  async importFile(format: ImportFormat): Promise<void> {
    if (!this.ready() || !(await this.host.confirmDiscard('import another puzzle'))) {
      return;
    }
    const path = await this.app.chooseOpen(OPEN_KIND[format]);
    if (path === null) {
      return;
    }
    try {
      await this.store.importFile(format, path, importedName(path));
      this.host.showEntities();
      flash(`Imported ${baseName(path)}`);
    } catch (e) {
      if (told(e)) {
        await alertBox('importFailed', 'Could not load puzzle, sorry!', e.detail);
      } else {
        reportError(e, 'Import');
      }
    }
  }

  /**
   * File ▸ Export ▸ Puzzlecad: the pieces (every piece shape once) or a
   * problem's (every copy it has), with the trays if asked for, to a file
   * chosen.
   */
  async exportScad(): Promise<void> {
    const snap = this.store.snapshot;
    if (!this.ready() || snap === null) {
      return;
    }
    const gen = this.store.generation;
    // the engine first: what it refuses is told before anything is asked,
    // as the original checked the grid and the shapes first
    try {
      await this.store.engine.call('document.exportPuzzlecad', {});
    } catch (e) {
      if (told(e)) {
        await alertBox('exportScadFailed', e.detail);
      } else {
        reportError(e, 'Export Puzzlecad');
      }
      return;
    }
    const choice = await scadOptionsDialog(
      snap.problems.map((p) => ({
        id: p.id,
        label: p.name === '' ? p.label : `${p.label} — ${p.name}`,
      })),
      this.host.problem ?? snap.problems[0]?.id ?? null,
      snap.grid === 'sliding',
      // a sliding puzzle's boards are never pieces
      snap.shapes.filter((x) => x.startGoal === null),
      snap.path,
    );
    if (choice === null) {
      return;
    }
    if (this.store.generation !== gen) {
      flash('Another puzzle was opened: nothing exported', 5000);
      return;
    }
    const params = {
      ...(choice.problem === null ? {} : { problem: choice.problem, copies: true }),
      ...(choice.shapes === null ? {} : { shapes: choice.shapes }),
      tray: choice.tray,
      lip: choice.lip,
    };
    let text: string;
    try {
      text = (await this.store.engine.call('document.exportPuzzlecad', params)).text;
    } catch (e) {
      if (told(e)) {
        await alertBox('exportScadFailed', e.detail);
      } else {
        reportError(e, 'Export Puzzlecad');
      }
      return;
    }
    const path = await this.app.chooseSave(
      'scad',
      `${this.host.name.replace(/\.xmpuzzle$/i, '')}.scad`,
    );
    if (path === null) {
      return;
    }
    if (this.store.generation !== gen) {
      flash('Another puzzle was opened: nothing exported', 5000);
      return;
    }
    try {
      if (this.store.snapshot?.path == null) {
        // a puzzle without a file: the header names the export's
        text = (
          await this.store.engine.call('document.exportPuzzlecad', {
            ...params,
            name: baseName(path),
          })
        ).text;
      }
      await this.app.writeExport(path, new TextEncoder().encode(text));
      flash(`Exported ${baseName(path)}`);
    } catch (e) {
      await alertBox(
        'exportScadFailed',
        'Could not export puzzlecad file.',
        e instanceof EngineError ? e.detail : e instanceof Error ? e.message : String(e),
      );
    }
  }

  /** Edit ▸ Convert Brick Grid Type to Other: the target chosen, the puzzle replaced. */
  async convertGrid(): Promise<void> {
    const snap = this.store.snapshot;
    if (!this.ready() || snap === null) {
      return;
    }
    if (this.store.solving) {
      flash('Pause the solve first', 4000);
      return;
    }
    let info = this.store.info;
    if (info === null) {
      try {
        info = await this.store.engine.call('engine.info');
      } catch (e) {
        reportError(e, 'Convert grid type');
        return;
      }
    }
    const grids = info.grids;
    const label = (g: Grid): string => grids.find((x) => x.grid === g)?.label ?? g;
    const targets = grids.find((g) => g.grid === snap.grid)?.convertsTo ?? [];
    if (targets.length === 0) {
      await openDialog({
        id: 'convertGrid',
        title: 'Convert Grid Type',
        body: '<p>There are no grids that this puzzle can be converted to, sorry</p>',
        buttons: [{ key: 'ok', label: 'Ok', kind: 'pri', default: true }],
      });
      return;
    }
    const gen = this.store.generation;
    // set by the dialog's answer (a holder: the checker keeps a local's narrowing)
    const chosen: { grid: Grid | null } = { grid: null };
    const k = await openDialog({
      id: 'convertGrid',
      title: 'Convert Grid Type',
      body: `<p>Please Select Target grid</p><div class="form">${targets
        .map(
          (g, i) =>
            `<label title="Convert the puzzle to this grid"><input type="radio" name="target" value="${esc(g)}" ${i === 0 ? 'checked' : ''}> ${esc(label(g))}</label>`,
        )
        .join('')}</div><p>${esc(CONVERT_NOTE)}</p>`,
      buttons: [
        { key: 'abort', label: 'Abort', tip: 'Keep the puzzle as it is' },
        {
          key: 'go',
          label: 'Continue',
          kind: 'pri',
          default: true,
          tip: 'Convert the puzzle to the grid chosen',
        },
      ],
      onAnswer: (key, el) => {
        if (key === 'go') {
          const v = el.querySelector<HTMLInputElement>('input[name="target"]:checked')?.value;
          chosen.grid = targets.find((g) => g === v) ?? null;
        }
        return true;
      },
    });
    const target = chosen.grid;
    if (k !== 'go' || target === null) {
      return;
    }
    if (this.store.generation !== gen) {
      flash('Another puzzle was opened: nothing converted', 5000);
      return;
    }
    try {
      await this.store.convertGrid(target);
      this.host.showEntities();
      flash(`Converted the puzzle to the ${label(target)} grid`);
    } catch (e) {
      reportError(e, 'Convert grid type');
    }
  }
}
