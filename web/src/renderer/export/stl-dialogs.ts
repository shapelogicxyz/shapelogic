// SPDX-License-Identifier: GPL-3.0-or-later
//
// The STL dialogs (deferred part 5; docs/webui-spec.md section 10). From
// src/gui/stlexport.cpp (stlExport_c: File ▸ Export ▸ STL, the grid's
// parameters, Binary STL, the shape (here the shapes, picked into a second
// list, each in a file of its own), a preview of the mesh with its
// volume, normally or its insides; Export STL keeps the dialog open) and
// src/gui/stlexportsolution.cpp (stlExportSolution_c: the parameters, the
// output folder, the pieces placed in the solution, Export All with the
// overwrite question and its closing line). The parameters, meshes, files
// and the pieces' names are the engine's (export.*); the files are written
// by the main process, to the path its save dialog gave, or into the
// folder its folder dialog gave.

import type { StlParameter } from '../../engine/api.generated.ts';
import type { BtApp } from '../../engine/app-bridge.ts';
import { EngineError } from '../../engine/errors.ts';
import { fileBytes, meshArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { askChoice, openDialog, tell } from '../shell/dialog.ts';
import { css, esc } from '../shell/dom.ts';
import { viewColour } from '../shell/piece-colours.ts';
import { flash, reportError } from '../shell/status.ts';
import {
  exportedLine,
  formatParameter,
  overwriteQuestion,
  piecesText,
  readParameters,
  volumeLine,
} from './stl-form.ts';
import type { FieldValue } from './stl-form.ts';
import { pickerHtml, ShapePicker } from './shape-picker.ts';
import { StlPreview } from './stl-preview.ts';

/** What the dialogs need of the app. */
export interface StlContext {
  readonly store: Store;
  readonly app: BtApp;
  /** The document's name without its extension (a file's proposed name). */
  readonly base: string;
  /** Writes an export's bytes to a path the save dialog gave, and says so. */
  write(path: string, data: Uint8Array): Promise<void>;
}

/** The values typed last, by grid, and Binary STL: the next dialog starts with them. */
const remembered = new Map<string, FieldValue[]>();
let binaryChosen = true;

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

// the engine's words (the original's), without the method in front
function message(e: unknown): string {
  return e instanceof EngineError ? e.detail : e instanceof Error ? e.message : String(e);
}

/** The parameters' rows: a field per number, a check box per switch, the original's tooltips. */
function parameterRows(params: readonly StlParameter[], fields: readonly FieldValue[]): string {
  return params
    .map((p, i) => {
      const f = fields[i];
      const tip = esc(p.tooltip);
      if (p.type === 'switch') {
        return `<label title="${tip}"><input type="checkbox" data-p="${String(i)}" ${f === true ? 'checked' : ''}> ${esc(p.name)}</label>`;
      }
      const mode = p.type === 'positiveInteger' ? 'numeric' : 'decimal';
      return `<label class="fl" title="${tip}">${esc(p.name)}<input data-p="${String(i)}" value="${esc(typeof f === 'string' ? f : '')}" inputmode="${mode}"></label>`;
    })
    .join('');
}

/** The fields as they stand. */
function fieldsOf(el: HTMLElement, params: readonly StlParameter[]): FieldValue[] {
  return params.map((p, i) => {
    const input = el.querySelector<HTMLInputElement>(`[data-p="${String(i)}"]`);
    return p.type === 'switch' ? (input?.checked ?? false) : (input?.value ?? '');
  });
}

/** The grid's parameters and the fields to start with (the last typed for that grid). */
async function parameters(
  store: Store,
): Promise<{ grid: string; params: StlParameter[]; fields: FieldValue[] }> {
  const grid = store.snapshot?.grid ?? 'bricks';
  const { parameters: params } = await store.engine.call('export.stlParameters', {});
  const last = remembered.get(grid);
  const fields =
    last?.length === params.length
      ? last
      : params.map((p) => (p.type === 'switch' ? p.default !== 0 : formatParameter(p, p.default)));
  return { grid, params, fields };
}

function setStatus(el: HTMLElement, text: string, error = false): void {
  const s = el.querySelector<HTMLElement>('[data-f="status"]');
  if (s !== null) {
    s.textContent = text;
    s.classList.toggle('err', error);
  }
}

/** The shapes a dialog last exported, by document: the next one starts with them. */
let rememberedShapes: { path: string | null; shapes: string[] } | null = null;

/**
 * File ▸ Export ▸ STL: the dialog with the preview (stlExport_c), the shapes
 * to export picked from every shape into a second list (the original
 * exported one shape at a time). One shape is written to the file the save
 * dialog gives; several, each to its own file in the folder the folder
 * dialog gives, named as a solution's pieces are (export.stlFileNames).
 */
export async function shapeStlDialog(ctx: StlContext): Promise<void> {
  const { store, app } = ctx;
  const snap = store.snapshot;
  if (snap === null || !store.running) {
    return;
  }
  const shapes = snap.shapes;
  if (shapes.length === 0) {
    flash('There is no shape to export');
    return;
  }
  const first = shapes.find((s) => s.id === store.shape?.id) ?? shapes[0];
  let params: StlParameter[];
  let fields: FieldValue[];
  let grid: string;
  try {
    ({ grid, params, fields } = await parameters(store));
  } catch (e) {
    reportError(e, 'Export STL');
    return;
  }

  // the shapes to export: the last ones for this document, else the shape shown
  const kept = rememberedShapes?.path === snap.path ? rememberedShapes.shapes : [];
  let previewed = first?.id ?? '';
  let preview: StlPreview | null = null;
  let generation = 0;
  let exporting = false;
  let dialogEl: HTMLElement | null = null;
  // Export STL… is greyed with nothing to export
  const syncGo = (): void => {
    const go = dialogEl?.querySelector<HTMLButtonElement>('[data-x="go"]');
    if (go != null) {
      go.disabled = picker.ids().length === 0;
    }
  };
  const picker = new ShapePicker(
    shapes,
    kept.some((id) => shapes.some((x) => x.id === id))
      ? kept
      : first === undefined
        ? []
        : [first.id],
    {
      onChange: syncGo,
      onPick: (id) => {
        if (id !== previewed && dialogEl !== null) {
          previewed = id;
          void update(dialogEl);
        }
      },
    },
  );

  // the preview of the shape at the values as they are (cb_Update3DView)
  const update = async (el: HTMLElement): Promise<void> => {
    const g = ++generation;
    const read = readParameters(params, fieldsOf(el, params));
    if ('error' in read) {
      setStatus(el, read.error, true);
      return;
    }
    const shape = previewed;
    const index = shapes.findIndex((s) => s.id === shape);
    if (index < 0) {
      return;
    }
    el.dataset.busy = '1';
    el.dataset.preview = shape;
    setStatus(el, 'Building the mesh…');
    try {
      const r = await store.engine.call('export.stlMesh', { shape, parameters: read.values });
      if (g !== generation) {
        return;
      }
      preview?.setMesh(meshArrays(r), viewColour(shapes[index], index));
      setStatus(el, volumeLine(r.volume));
      el.dataset.volume = r.volume.toFixed(1);
      el.dataset.triangles = String(r.triangles);
    } catch (e) {
      if (g !== generation) {
        return;
      }
      // nothing to show: the preview must not keep the last mesh
      preview?.setMesh(null, [1, 1, 1]);
      setStatus(el, message(e), true);
      delete el.dataset.volume;
    } finally {
      if (g === generation) {
        el.dataset.busy = '0';
      }
    }
  };

  // Export STL… with one shape (exportSTL): the file from the save dialog
  const exportOne = async (shapeId: string, values: number[], binary: boolean): Promise<void> => {
    const shape = shapes.find((s) => s.id === shapeId);
    if (shape === undefined) {
      return;
    }
    const proposed = shape.name.trim() === '' ? `${ctx.base} ${shape.label}` : shape.name.trim();
    const path = await app.chooseSave('stl', `${proposed}.stl`);
    if (path === null) {
      return;
    }
    const r = await store.engine.call('export.stl', {
      shape: shape.id,
      parameters: values,
      binary,
      name: baseName(path),
    });
    await ctx.write(path, fileBytes(r));
  };

  // Export STL… with several shapes: a file each into the folder chosen, as Export All writes them
  const exportMany = async (
    el: HTMLElement,
    ids: string[],
    values: number[],
    binary: boolean,
  ): Promise<void> => {
    const to = await app.chooseFolder(true);
    if (to === null) {
      return;
    }
    const { fileNames } = await store.engine.call('export.stlFileNames', { shapes: ids });
    const existing = await app.existingExports(to, fileNames);
    if (existing > 0) {
      const answer = await askChoice('stlOverwrite', 'Export STL', overwriteQuestion(existing), [
        { key: 'cancel', label: 'Cancel' },
        {
          key: 'overwrite',
          label: 'Overwrite',
          kind: 'danger',
          default: true,
          tip: 'Replace the files that are already there',
        },
      ]);
      if (answer !== 'overwrite') {
        return;
      }
    }
    let exported = 0;
    let errors = 0;
    setStatus(el, 'Exporting…');
    for (const [i, shape] of ids.entries()) {
      const fileName = fileNames[i] ?? `${shape}.stl`;
      try {
        const r = await store.engine.call('export.stl', {
          shape,
          parameters: values,
          binary,
          name: fileName,
        });
        await app.writeFolderExport(to, fileName, fileBytes(r));
        exported++;
      } catch (e) {
        tell(`Error exporting ${fileName}:\n${message(e)}`);
        errors++;
      }
    }
    setStatus(el, exportedLine(exported, errors, 'shape(s)'), errors > 0);
    if (exported > 0) {
      flash(`Exported ${String(exported)} STL file(s) to ${baseName(to)}`);
    }
    el.dataset.exported = String(Number(el.dataset.exported ?? '0') + 1);
  };

  // Export STL…: the dialog stays open, as the original's
  const exportFiles = async (el: HTMLElement): Promise<void> => {
    const read = readParameters(params, fieldsOf(el, params));
    if ('error' in read) {
      setStatus(el, read.error, true);
      return;
    }
    const ids = picker.ids();
    if (ids.length === 0 || exporting) {
      return;
    }
    remembered.set(grid, fieldsOf(el, params));
    rememberedShapes = { path: snap.path, shapes: ids };
    const binary = el.querySelector<HTMLInputElement>('[data-f="binary"]')?.checked ?? true;
    binaryChosen = binary;
    exporting = true;
    try {
      if (ids.length === 1 && ids[0] !== undefined) {
        await exportOne(ids[0], read.values, binary);
      } else {
        await exportMany(el, ids, read.values, binary);
      }
    } catch (e) {
      if (e instanceof EngineError && e.errc === 'invalid_argument') {
        tell(e.detail);
      } else {
        reportError(e, 'Export the STL file');
      }
    } finally {
      exporting = false;
    }
  };

  const shown = openDialog({
    id: 'exportStl',
    title: 'Export STL',
    wide: true,
    body: `<div class="stl-dlg"><div class="form stl-side">
<div class="stl-params">${parameterRows(params, fields)}</div>
<label title="A binary STL file is smaller; a text STL file can be read in an editor"><input type="checkbox" data-f="binary" ${binaryChosen ? 'checked' : ''}> Binary STL</label>
${pickerHtml({
  all: 'Every shape not chosen; select shapes and press → to export them',
  chosen: 'The shapes to export: one to a file, several to a file each in a folder',
})}</div>
<div class="stl-look"><canvas class="stl-view" data-f="view"></canvas>
<div class="stl-bar"><div class="stl-status" data-f="status"></div><div class="seg"><button data-mode="normal" class="on" title="Display STL object normally">Normal</button><button data-mode="insides" title="Display the insides of the STL object">Insides</button></div></div></div></div>`,
    buttons: [
      { key: 'close', label: 'Close' },
      {
        key: 'go',
        label: 'Export STL…',
        kind: 'pri',
        tip: 'Write the shapes to export as STL: one shape to a file you choose, several to a file each in a folder you choose',
      },
    ],
    onAnswer: (k, el) => {
      if (k === 'go') {
        void exportFiles(el);
        return false;
      }
      remembered.set(grid, fieldsOf(el, params));
      rememberedShapes = { path: snap.path, shapes: picker.ids() };
      return true;
    },
  });
  const el = document.getElementById('exportStl');
  const canvas = el?.querySelector<HTMLCanvasElement>('[data-f="view"]');
  if (el !== null && canvas != null) {
    preview = new StlPreview(canvas, css('--canvas') || '#e4e8ef');
    dialogEl = el;
    picker.attach(el, [previewed]);
    syncGo();
    el.addEventListener('change', (e) => {
      if ((e.target as Element).closest('[data-p]') !== null) {
        void update(el);
      }
    });
    el.querySelector('.seg')?.addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-mode]');
      if (b === null) {
        return;
      }
      for (const x of el.querySelectorAll<HTMLElement>('[data-mode]')) {
        x.classList.toggle('on', x === b);
      }
      preview?.setInsides(b.dataset.mode === 'insides');
    });
    void update(el);
  }
  await shown;
  generation++;
  preview?.dispose();
}

/** Solutions ▸ Export to STL: the pieces of a solution into a folder (stlExportSolution_c). */
export async function solutionStlDialog(
  ctx: StlContext,
  problem: string,
  solution: { id: string; index: number },
): Promise<void> {
  const { store, app } = ctx;
  const snap = store.snapshot;
  if (snap === null || !store.running) {
    return;
  }
  let params: StlParameter[];
  let fields: FieldValue[];
  let grid: string;
  let pieces: { shape: string; count: number; fileName: string }[];
  try {
    ({ grid, params, fields } = await parameters(store));
    ({ pieces } = await store.engine.call('export.solutionPieces', {
      problem,
      solution: solution.id,
    }));
  } catch (e) {
    reportError(e, 'Export the solution to STL');
    return;
  }
  const labelOf = (shape: string): { label: string; name: string } => {
    const s = store.snapshot?.shapes.find((x) => x.id === shape);
    return { label: s?.label ?? shape, name: s?.name ?? '' };
  };
  const lines = piecesText(
    solution.index + 1,
    pieces.map((p) => ({ ...labelOf(p.shape), count: p.count })),
  );
  let folder: string | null = null;
  let exporting = false;

  const setFolder = (el: HTMLElement): void => {
    const f = el.querySelector<HTMLElement>('[data-f="folder"]');
    if (f !== null) {
      f.textContent = folder ?? 'none chosen';
      f.title = folder ?? '';
    }
    const go = el.querySelector<HTMLButtonElement>('[data-x="go"]');
    if (go !== null) {
      go.disabled = folder === null || pieces.length === 0;
    }
  };

  // Export All (cb_Export): every piece's file into the folder
  const exportAll = async (el: HTMLElement): Promise<void> => {
    const read = readParameters(params, fieldsOf(el, params));
    if ('error' in read) {
      setStatus(el, read.error, true);
      return;
    }
    const to = folder;
    if (to === null || exporting) {
      return;
    }
    if (pieces.length === 0) {
      tell('No piece shapes to export for this solution.');
      return;
    }
    remembered.set(grid, fieldsOf(el, params));
    const binary = el.querySelector<HTMLInputElement>('[data-f="binary"]')?.checked ?? true;
    binaryChosen = binary;
    exporting = true;
    try {
      const existing = await app.existingExports(
        to,
        pieces.map((p) => p.fileName),
      );
      if (existing > 0) {
        const answer = await askChoice(
          'stlOverwrite',
          'Export to STL',
          overwriteQuestion(existing),
          [
            { key: 'cancel', label: 'Cancel' },
            {
              key: 'overwrite',
              label: 'Overwrite',
              kind: 'danger',
              default: true,
              tip: 'Replace the files that are already there',
            },
          ],
        );
        if (answer !== 'overwrite') {
          return;
        }
      }
      let exported = 0;
      let errors = 0;
      setStatus(el, 'Exporting…');
      for (const p of pieces) {
        try {
          const r = await store.engine.call('export.stl', {
            shape: p.shape,
            parameters: read.values,
            binary,
            name: p.fileName,
          });
          await app.writeFolderExport(to, p.fileName, fileBytes(r));
          exported++;
        } catch (e) {
          tell(`Error exporting ${p.fileName}:\n${message(e)}`);
          errors++;
        }
      }
      setStatus(el, exportedLine(exported, errors), errors > 0);
      el.dataset.exported = String(Number(el.dataset.exported ?? '0') + 1);
    } catch (e) {
      reportError(e, 'Export the solution to STL');
    } finally {
      exporting = false;
    }
  };

  const shown = openDialog({
    id: 'exportStlSolution',
    title: 'Export Solution Pieces to STL',
    body: `<div class="form">
<div class="stl-params">${parameterRows(params, fields)}</div>
<div class="fl">Output folder<span class="stl-folder" data-f="folder"></span><button class="btn sm" data-a="folder" title="Choose the folder the STL files are written to">Choose…</button></div>
<label title="A binary STL file is smaller; a text STL file can be read in an editor"><input type="checkbox" data-f="binary" ${binaryChosen ? 'checked' : ''}> Binary STL</label>
<pre class="stl-pieces" data-f="pieces">${esc(lines.join('\n'))}</pre>
<div class="stl-status" data-f="status"></div></div>`,
    buttons: [
      { key: 'close', label: 'Close' },
      {
        key: 'go',
        label: 'Export All',
        kind: 'pri',
        tip: 'Write each piece of the solution to its own STL file',
      },
    ],
    onAnswer: (k, el) => {
      if (k === 'go') {
        void exportAll(el);
        return false;
      }
      remembered.set(grid, fieldsOf(el, params));
      return true;
    },
  });
  const el = document.getElementById('exportStlSolution');
  if (el !== null) {
    setFolder(el);
    el.querySelector('[data-a="folder"]')?.addEventListener('click', () => {
      void app.chooseFolder().then((f) => {
        if (f !== null) {
          folder = f;
          setFolder(el);
        }
      });
    });
  }
  await shown;
}
