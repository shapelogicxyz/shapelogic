// SPDX-License-Identifier: GPL-3.0-or-later
//
// File ▸ Export (stage 7): Image (the view as a PNG at its size or twice
// it), Vector Image (the view as an SVG over the flat meshes,
// view3d/svg.ts), STL (a shape's flat mesh, an interim: stl.ts) and
// Solution Animation (the solution the Solver shows as a .glb: glb.ts,
// with the original's dialog, gltfexportwindow.cpp). Each asks its
// options, then the file (the main process's save dialog, which alone
// gives a path the main process will write an export to), makes the
// bytes here and has the main process write them (beside the file and
// renamed over it, as the core saves). The paused solver state's export
// is File's (stage 6).

import type { BtApp } from '../../engine/app-bridge.ts';
import type { SolutionRow } from '../../engine/api.generated.ts';
import { meshArrays, poseArrays } from '../../engine/frames.ts';
import type { Store } from '../state/store.ts';
import { openDialog } from '../shell/dialog.ts';
import { byId, esc } from '../shell/dom.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { flash, reportError } from '../shell/status.ts';
import type { View3D } from '../view3d/view.ts';
import { positive } from './form.ts';
import { ANIMATION_DEFAULTS, ANIMATION_SAMPLES, animationGlb } from './glb.ts';
import type { AnimationOptions, AnimationPiece } from './glb.ts';
import { STL_DEFAULTS, stlBytes } from './stl.ts';
import type { StlOptions } from './stl.ts';

/** The solution the Solver shows with its moves stored, as the animation needs it. */
export interface AnimationSource {
  problem: string;
  row: SolutionRow;
  /** The pieces' colours as the player draws them, by piece number. */
  colours: ReadonlyMap<number, Rgb3>;
}

/** What the exports need besides the store, the bridge and the view. */
export interface ExportsHost {
  /** The document's name (its file's, else the one shown). */
  readonly name: string;
  /** The solution to animate, or null when the Solver shows none with its moves. */
  animation(): AnimationSource | null;
}

function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

/** The bytes of a data URL (the canvas's PNG). */
export function dataUrlBytes(url: string): Uint8Array {
  const b64 = url.slice(url.indexOf(',') + 1);
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) {
    out[i] = bin.charCodeAt(i);
  }
  return out;
}

export class Exports {
  private readonly store: Store;
  private readonly app: BtApp;
  private readonly view: View3D;
  private readonly host: ExportsHost;
  /** The choices of the last export of each kind, for the next. */
  private scale = 1;
  private stl: StlOptions = { ...STL_DEFAULTS };
  private animationOptions: AnimationOptions = { ...ANIMATION_DEFAULTS };
  private written = 0;

  constructor(store: Store, app: BtApp, view: View3D, host: ExportsHost) {
    this.store = store;
    this.app = app;
    this.view = view;
    this.host = host;
  }

  private get base(): string {
    return this.host.name.replace(/\.xmpuzzle$/i, '');
  }

  private ready(): boolean {
    return this.store.running && this.store.snapshot !== null;
  }

  // writes the bytes to the path, says so, and counts it (#stage[data-exported], for the tests)
  private async write(path: string, data: Uint8Array): Promise<void> {
    await this.app.writeExport(path, data);
    this.written++;
    byId('stage').dataset.exported = String(this.written);
    flash(`Exported ${baseName(path)}`);
  }

  /** File ▸ Export ▸ Image: the view as a PNG, at its size or twice it. */
  async image(): Promise<void> {
    if (!this.ready()) {
      return;
    }
    const w = this.view.gl.width;
    const h = this.view.gl.height;
    const radio = (scale: number, label: string): string =>
      `<label><input type="radio" name="scale" value="${String(scale)}" ${scale === this.scale ? 'checked' : ''}> ${label} (${String(w * scale)} × ${String(h * scale)} pixels)</label>`;
    let scale = this.scale;
    const go = await openDialog({
      id: 'exportImage',
      title: 'Export image',
      body: `<div class="form"><p>The 3D view as it is now, as a PNG image.</p><fieldset><legend>Size</legend>${radio(1, 'The view’s size')}${radio(2, 'Twice the view’s size')}</fieldset></div>`,
      buttons: [
        { key: 'cancel', label: 'Cancel' },
        { key: 'go', label: 'Export…', kind: 'pri', default: true },
      ],
      onAnswer: (k, el) => {
        if (k === 'go') {
          scale =
            el.querySelector<HTMLInputElement>('input[name="scale"]:checked')?.value === '2'
              ? 2
              : 1;
        }
        return true;
      },
    });
    if (go !== 'go') {
      return;
    }
    this.scale = scale;
    const path = await this.app.chooseSave('image', `${this.base}.png`);
    if (path === null) {
      return;
    }
    try {
      await this.write(path, dataUrlBytes(this.view.image(scale).data));
    } catch (e) {
      reportError(e, 'Export the image');
    }
  }

  /** File ▸ Export ▸ Vector Image: the view as an SVG over the flat meshes. */
  async vector(): Promise<void> {
    if (!this.ready()) {
      return;
    }
    const path = await this.app.chooseSave('vector', `${this.base}.svg`);
    if (path === null) {
      return;
    }
    try {
      const text = await this.view.vectorImage();
      await this.write(path, new TextEncoder().encode(text));
    } catch (e) {
      reportError(e, 'Export the vector image');
    }
  }

  /** File ▸ Export ▸ STL: a shape's flat mesh (an interim until the STL meshes are ported). */
  async stlFile(): Promise<void> {
    const snap = this.store.snapshot;
    if (!this.ready() || snap === null) {
      return;
    }
    const shapes = snap.shapes.filter((s) => s.fixed + s.variable > 0);
    if (shapes.length === 0) {
      flash('There is no shape with cells to export');
      return;
    }
    const first = shapes.find((s) => s.id === this.store.shape?.id) ?? shapes[0];
    let chosen = first?.id ?? '';
    let options = this.stl;
    const go = await openDialog({
      id: 'exportStl',
      title: 'Export STL',
      body: `<div class="form">
<p>The shape’s flat faces, as the 3D view’s “flat faces with edges” style draws them: <b>an interim</b>. The STL meshes of the original, with bevelled edges and an offset for printing, come in a later version.</p>
<label class="fl">Shape<select data-f="shape">${shapes.map((s) => `<option value="${esc(s.id)}" ${s === first ? 'selected' : ''}>${esc(s.label)}${s.name === '' ? '' : ` — ${esc(s.name)}`}</option>`).join('')}</select></label>
<label class="fl" title="The size of one cell's unit (millimetres in most programs)">Unit Size<input data-f="unit" value="${String(this.stl.unit)}" inputmode="decimal"></label>
<label><input type="checkbox" data-f="binary" ${this.stl.binary ? 'checked' : ''}> Binary STL</label></div>`,
      buttons: [
        { key: 'cancel', label: 'Cancel' },
        { key: 'go', label: 'Export…', kind: 'pri', default: true },
      ],
      onAnswer: (k, el) => {
        if (k !== 'go') {
          return true;
        }
        const unit = positive(el.querySelector<HTMLInputElement>('[data-f="unit"]')?.value ?? '');
        if (unit === null) {
          flash('The unit size must be a number above 0', 4000);
          return false;
        }
        chosen = el.querySelector<HTMLSelectElement>('[data-f="shape"]')?.value ?? chosen;
        options = {
          unit,
          binary: el.querySelector<HTMLInputElement>('[data-f="binary"]')?.checked ?? true,
        };
        return true;
      },
    });
    if (go !== 'go') {
      return;
    }
    this.stl = options;
    const shape = this.store.snapshot?.shapes.find((s) => s.id === chosen);
    if (shape === undefined) {
      return;
    }
    const proposed = shape.name.trim() === '' ? `${this.base} ${shape.label}` : shape.name.trim();
    const path = await this.app.chooseSave('stl', `${proposed}.stl`);
    if (path === null) {
      return;
    }
    try {
      const revision = this.store.snapshot?.revision ?? 0;
      const reply = await this.view.meshCache.get(shape.id, 'flat', revision);
      const name = shape.name.trim() === '' ? shape.label : shape.name.trim();
      await this.write(path, stlBytes(meshArrays(reply), name, options));
    } catch (e) {
      reportError(e, 'Export the STL file');
    }
  }

  /** File ▸ Export ▸ Solution Animation: the solution the Solver shows, as a .glb. */
  async animation(): Promise<void> {
    const source = this.host.animation();
    if (!this.ready() || source === null) {
      flash('Choose a solution with its disassembly in the Solver first', 4000);
      return;
    }
    const engine = this.store.engine;
    let handle: string | null = null;
    try {
      const pb = await engine.call('playback.open', {
        problem: source.problem,
        solution: source.row.id,
      });
      handle = pb.handle;
      if (pb.steps === 0) {
        flash('This solution has no disassembly to animate', 4000);
        return;
      }
      const o = this.animationOptions;
      let options = o;
      const go = await openDialog({
        id: 'exportGlb',
        title: 'Export solution animation',
        body: `<div class="form">
<label class="fl" title="Time for one move or one 90 degree rotation">Seconds per move<input data-f="speed" value="${o.secondsPerStep.toFixed(2)}" inputmode="decimal"></label>
<label class="fl" title="Edge length of one cell. AR viewers show the puzzle at this size">Cell size (mm)<input data-f="cell" value="${(o.cellSize * 1000).toFixed(1)}" inputmode="decimal"></label>
<label title="Play the moves backwards at the end, so the looping animation goes apart and back together"><input type="checkbox" data-f="reassemble" ${o.reassemble ? 'checked' : ''}> Reassemble after the disassembly</label>
<label title="Off gives plain blocks and a much smaller file"><input type="checkbox" data-f="bevel" ${o.bevel ? 'checked' : ''}> Bevelled edges, as in the 3D view</label>
<p>${String(pb.steps)} steps. Pieces keep their solution colours.</p></div>`,
        buttons: [
          { key: 'cancel', label: 'Cancel' },
          { key: 'go', label: 'Export…', kind: 'pri', default: true },
        ],
        onAnswer: (k, el) => {
          if (k !== 'go') {
            return true;
          }
          const field = (f: string): string =>
            el.querySelector<HTMLInputElement>(`[data-f="${f}"]`)?.value ?? '';
          const check = (f: string): boolean =>
            el.querySelector<HTMLInputElement>(`[data-f="${f}"]`)?.checked ?? false;
          const speed = positive(field('speed'));
          const cell = positive(field('cell'));
          if (speed === null || speed > 3600) {
            flash('The seconds per move must be a number above 0 (at most 3600)', 4000);
            return false;
          }
          if (cell === null) {
            flash('The cell size must be a number above 0', 4000);
            return false;
          }
          options = {
            secondsPerStep: speed,
            cellSize: cell / 1000,
            reassemble: check('reassemble'),
            bevel: check('bevel'),
          };
          return true;
        },
      });
      if (go !== 'go') {
        return;
      }
      this.animationOptions = options;
      const number = source.row.index + 1;
      const path = await this.app.chooseSave('glb', `${this.base} solution ${String(number)}.glb`);
      if (path === null) {
        return;
      }
      const style = options.bevel ? 'drawing' : 'flat';
      const pieces: AnimationPiece[] = [];
      for (const [i, p] of pb.pieces.entries()) {
        pieces.push({
          name: p.name,
          colour: source.colours.get(p.piece) ?? [0.7, 0.7, 0.7],
          mesh: meshArrays(await engine.call('playback.mesh', { handle, piece: i, style })),
        });
      }
      const track = poseArrays(
        await engine.call('playback.track', {
          handle,
          samplesPerStep: ANIMATION_SAMPLES,
          secondsPerStep: options.secondsPerStep,
          reassemble: options.reassemble,
        }),
        'time',
      );
      const glb = await animationGlb({
        name: `Solution ${String(number)}`,
        pieces,
        track,
        cellSize: options.cellSize,
      });
      await this.write(path, glb);
    } catch (e) {
      reportError(e, 'Export the solution animation');
    } finally {
      if (handle !== null) {
        engine.call('playback.close', { handle }).catch(() => undefined);
      }
    }
  }
}
