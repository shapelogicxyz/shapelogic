// SPDX-License-Identifier: GPL-3.0-or-later
//
// File ▸ Export (stage 7): Image (the view as a PNG at its size or twice
// it), Vector Image (the view as an SVG over the flat meshes,
// view3d/svg.ts), STL (the engine's STL meshes and files, deferred part 5:
// stl-dialogs.ts, also Solutions ▸ Export to STL) and Solution Animation (the solution the Solver shows as a .glb: glb.ts,
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
import { byId } from '../shell/dom.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { flash, reportError } from '../shell/status.ts';
import type { View3D } from '../view3d/view.ts';
import { positive } from './form.ts';
import { ANIMATION_DEFAULTS, ANIMATION_SAMPLES, animationGlb } from './glb.ts';
import type { AnimationOptions, AnimationPiece } from './glb.ts';
import { shapeStlDialog, solutionStlDialog } from './stl-dialogs.ts';
import type { StlContext } from './stl-dialogs.ts';

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
      `<label><input type="radio" name="scale" value="${String(scale)}" ${scale === this.scale ? 'checked' : ''} title="Export the image at this size"> ${label} (${String(w * scale)} × ${String(h * scale)} pixels)</label>`;
    let scale = this.scale;
    const go = await openDialog({
      id: 'exportImage',
      title: 'Export image',
      body: `<div class="form"><p>The 3D view as it is now, as a PNG image.</p><fieldset><legend>Size</legend>${radio(1, 'The view’s size')}${radio(2, 'Twice the view’s size')}</fieldset></div>`,
      buttons: [
        { key: 'cancel', label: 'Cancel' },
        {
          key: 'go',
          label: 'Export…',
          kind: 'pri',
          default: true,
          tip: 'Choose a file and save the view to it as a PNG image',
        },
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

  // what the STL dialogs need
  private get stlContext(): StlContext {
    return {
      store: this.store,
      app: this.app,
      base: this.base,
      write: (path, data) => this.write(path, data),
    };
  }

  /** File ▸ Export ▸ STL: a shape's STL file, with the preview (stl-dialogs.ts). */
  async stlFile(): Promise<void> {
    if (this.ready()) {
      await shapeStlDialog(this.stlContext);
    }
  }

  /** Solutions ▸ Export to STL: the pieces of a solution, a file each, into a folder. */
  async solutionStl(problem: string, solution: { id: string; index: number }): Promise<void> {
    if (this.ready()) {
      await solutionStlDialog(this.stlContext, problem, solution);
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
<p>${String(pb.steps)} steps. Pieces keep their solution colors.</p></div>`,
        buttons: [
          { key: 'cancel', label: 'Cancel' },
          {
            key: 'go',
            label: 'Export…',
            kind: 'pri',
            default: true,
            tip: 'Choose a file and save the animation to it as GLB',
          },
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
