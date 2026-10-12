// SPDX-License-Identifier: GPL-3.0-or-later
//
// File ▸ Import ▸ PuzzleSolver3D… and Puzzlecad (OpenSCAD)…, File ▸
// Export ▸ Puzzlecad (OpenSCAD)… and Edit ▸ Convert Brick Grid Type to
// Other… (deferred part 6; docs/webui-spec.md section 11), from the
// original's mainwindow_file.cpp (cb_Load_Ps3d, cb_Load_Scad,
// cb_Export_Scad, cb_Convert) and convertwindow.cpp. The engine reads the
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
  return askChoice(id, 'BurrTools', reason === undefined ? text : `${text}\n${reason}`, [
    { key: 'ok', label: 'OK', kind: 'pri', default: true },
  ]);
}

/** Whether an engine error is a refusal to tell in a box (not "pause first", not superseded). */
function told(e: unknown): e is EngineError {
  return e instanceof EngineError && e.errc !== 'invalid_state' && e.errc !== 'superseded';
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

  /** File ▸ Export ▸ Puzzlecad: the pieces of the Puzzle workspace's problem, to a file chosen. */
  async exportScad(): Promise<void> {
    const snap = this.store.snapshot;
    if (!this.ready() || snap === null) {
      return;
    }
    const gen = this.store.generation;
    const problem = this.host.problem ?? snap.problems[0]?.id ?? null;
    const params = problem === null ? {} : { problem };
    // the engine first: what it refuses is told before a file is asked for,
    // as the original checked the grid and the shapes first
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
            `<label><input type="radio" name="target" value="${esc(g)}" ${i === 0 ? 'checked' : ''}> ${esc(label(g))}</label>`,
        )
        .join('')}</div><p>${esc(CONVERT_NOTE)}</p>`,
      buttons: [
        { key: 'abort', label: 'Abort' },
        { key: 'go', label: 'Continue', kind: 'pri', default: true },
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
