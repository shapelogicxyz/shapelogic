// SPDX-License-Identifier: GPL-3.0-or-later
//
// The status window (FRONTEND-PLAN.md, stage 5; webui-spec.md section 7):
// a row per shape from the engine's shape.table: its cells (normal,
// variable, total), the earlier shape it is identical to (mirrored, as a
// shape, with its colours), its connectivity by faces, edges and corners,
// its holes, on the cube grid whether it can be notched or milled, its
// symmetry group; the shape's status line as the row's tooltip. Refresh,
// Close, Remove selected (one undo step; refused while solving, and never
// every shape), and the selections of the original (holes, identical
// shapes, identical with colours, identical mirrored). Read again when a
// change names the shapes. Ported from src/gui/statuswindow.cpp (the
// layout of its columns and its buttons); the mockup has no status window.

import type { ShapeInfo } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Edits } from '../state/edits.ts';
import type { Store } from '../state/store.ts';
import { esc } from '../shell/dom.ts';
import { openPanel } from '../shell/panel.ts';
import type { Panel } from '../shell/panel.ts';
import { contrast, hex, shapeColour } from '../shell/piece-colours.ts';
import { flash, reportError } from '../shell/status.ts';

export class StatusWindow {
  private readonly store: Store;
  private readonly edits: Edits;
  private panel: Panel | null = null;
  private rows: ShapeInfo[] = [];
  private readonly selected = new Set<string>();
  private loads = 0;

  constructor(store: Store, edits: Edits) {
    this.store = store;
    this.edits = edits;
    store.subscribe((c) => {
      if (this.panel === null) {
        return;
      }
      if (
        c.kind === 'changed' &&
        (c.change.what.includes('shapes') || c.change.what.includes('colours'))
      ) {
        this.load();
      } else if (c.kind === 'document') {
        this.load();
      }
    });
  }

  /** Whether the window is open. */
  get isOpen(): boolean {
    return this.panel !== null;
  }

  /** Opens the window (or reads it again when it is open). */
  open(): void {
    if (this.panel === null) {
      const p = openPanel('statusWin', 'Status', () => {
        this.panel = null;
      });
      p.el.classList.add('stw');
      p.foot.innerHTML =
        '<button class="btn sm" data-a="refresh" title="Reload piece information">Refresh</button>' +
        '<button class="btn sm" data-a="close" title="Close the window">Close</button>' +
        '<button class="btn sm danger" data-a="remove" title="Delete the ticked shapes from the puzzle">Remove selected</button>' +
        '<span class="sp"></span>' +
        '<button class="btn sm" data-a="holes" title="Select all shapes that have 2D or 3D holes">Select holes</button>' +
        '<button class="btn sm" data-a="shape" title="Select shapes that match another shape ignoring color">Select Identical Shapes</button>' +
        '<button class="btn sm" data-a="complete" title="Select shapes that match another shape including color">Select Identical Complete</button>' +
        '<button class="btn sm" data-a="mirror" title="Select shapes that match another shape as a mirror image">Select Identical Mirror</button>';
      p.foot.addEventListener('click', (e) => {
        const b = (e.target as Element).closest<HTMLElement>('[data-a]');
        if (b !== null) {
          void this.action(b.dataset.a ?? '');
        }
      });
      p.body.addEventListener('change', (e) => {
        const box = e.target as HTMLInputElement;
        const id = box.closest<HTMLElement>('[data-shape]')?.dataset.shape;
        if (id !== undefined) {
          if (box.checked) {
            this.selected.add(id);
          } else {
            this.selected.delete(id);
          }
        }
      });
      this.panel = p;
    }
    this.load();
  }

  /** Closes the window. */
  close(): void {
    this.panel?.close();
  }

  private load(): void {
    const panel = this.panel;
    if (panel === null || !this.store.running) {
      return;
    }
    const n = ++this.loads;
    panel.el.dataset.loading = '1';
    this.store.engine.call('shape.table', {}, { coalesce: 'status:shape.table' }).then(
      (r) => {
        if (n !== this.loads || this.panel !== panel) {
          return;
        }
        this.rows = r.rows;
        const ids = new Set(r.rows.map((x) => x.shape));
        for (const s of [...this.selected]) {
          if (!ids.has(s)) {
            this.selected.delete(s);
          }
        }
        this.render();
        panel.el.dataset.loading = '0';
        panel.el.dataset.revision = String(this.store.snapshot?.revision ?? '');
      },
      (e: unknown) => {
        if (e instanceof EngineError && (e.errc === 'superseded' || e.errc === 'disconnected')) {
          return;
        }
        reportError(e, 'The status could not be read');
      },
    );
  }

  // the original's table: two header rows, then a row per shape
  private render(): void {
    const panel = this.panel;
    if (panel === null) {
      return;
    }
    const snap = this.store.snapshot;
    const cube = snap?.grid === 'bricks';
    const chip = (id: string | null): string => {
      if (id === null) {
        return '';
      }
      const i = this.store.indexOf(id);
      const c = shapeColour(snap?.shapes[i]?.colour ?? null, i);
      return `<span class="chip" style="background:${hex(c)};color:${contrast(c)}">${esc(snap?.shapes[i]?.label ?? id)}</span>`;
    };
    // a mark: X, or the shape's colour where the original coloured the box
    const mark = (x: boolean, colourIt: boolean, i: number): string =>
      x
        ? 'X'
        : colourIt
          ? `<i style="background:${hex(shapeColour(snap?.shapes[i]?.colour ?? null, i))}"></i>`
          : '';
    const head =
      `<tr><th rowspan="2"></th><th rowspan="2">Shape</th><th colspan="3">Units</th><th colspan="3">Identical</th><th colspan="3">Connectivity</th><th colspan="2">Holes</th>${cube ? '<th colspan="2">Tools</th>' : ''}<th rowspan="2">Sym</th></tr>` +
      `<tr><th>Normal</th><th>Variable</th><th>Total</th><th>Mirror</th><th>Shape</th><th>Complete</th><th>Face</th><th>Edge</th><th>Corner</th><th>2D</th><th>3D</th>${cube ? '<th>Notch</th><th>Mill</th>' : ''}</tr>`;
    const body = this.rows
      .map((r) => {
        const i = this.store.indexOf(r.shape);
        const s = snap?.shapes[i];
        const name = s === undefined ? r.shape : s.name === '' ? s.label : `${s.label} - ${s.name}`;
        const c = shapeColour(s?.colour ?? null, i);
        const holeMark = (x: boolean): string =>
          x ? `<span class="chip" style="background:${hex(c)};color:${contrast(c)}">X</span>` : '';
        return (
          `<tr data-shape="${esc(r.shape)}" title="${esc(r.statusLine)}">` +
          `<td><input type="checkbox"${this.selected.has(r.shape) ? ' checked' : ''} aria-label="Select ${esc(name)}" title="Tick to select this shape"></td>` +
          `<td><span class="chip" style="background:${hex(c)};color:${contrast(c)}">${esc(name)}</span></td>` +
          `<td data-k="fixed">${String(r.fixed)}</td><td data-k="variable">${String(r.variable)}</td><td data-k="total">${String(r.fixed + r.variable)}</td>` +
          `<td data-k="identicalMirror">${chip(r.identicalMirror)}</td><td data-k="identicalShape">${chip(r.identicalShape)}</td><td data-k="identicalComplete">${chip(r.identicalComplete)}</td>` +
          `<td data-k="connectedFace">${mark(r.connectedFace, true, i)}</td><td data-k="connectedEdge">${mark(r.connectedEdge, true, i)}</td><td data-k="connectedCorner">${mark(r.connectedCorner, true, i)}</td>` +
          `<td data-k="holes2D">${holeMark(r.holes2D)}</td><td data-k="holes3D">${holeMark(r.holes3D)}</td>` +
          (cube
            ? `<td data-k="notchable">${r.notchable === true ? 'X' : ''}</td><td data-k="millable">${r.millable === true ? 'X' : ''}</td>`
            : '') +
          `<td data-k="symmetry">${r.symmetry === null ? `<span class="chip" style="background:${hex(c)};color:${contrast(c)}">---</span>` : String(r.symmetry)}</td>` +
          '</tr>'
        );
      })
      .join('');
    panel.body.innerHTML = `<table class="stt"><thead>${head}</thead><tbody>${body}</tbody></table>`;
  }

  private async action(a: string): Promise<void> {
    switch (a) {
      case 'refresh':
        this.load();
        return;
      case 'close':
        this.close();
        return;
      case 'remove':
        await this.removeSelected();
        return;
      case 'holes':
        this.select((r) => r.holes2D || r.holes3D);
        return;
      case 'shape':
        this.select((r) => r.identicalShape !== null);
        return;
      case 'complete':
        this.select((r) => r.identicalComplete !== null);
        return;
      case 'mirror':
        this.select((r) => r.identicalMirror !== null);
        return;
    }
  }

  // the original's selections add to what is selected
  private select(which: (r: ShapeInfo) => boolean): void {
    for (const r of this.rows) {
      if (which(r)) {
        this.selected.add(r.shape);
      }
    }
    this.render();
  }

  // the shapes ticked deleted as one undo step (never all: one shape stays)
  private async removeSelected(): Promise<void> {
    const ids = this.rows.map((r) => r.shape).filter((s) => this.selected.has(s));
    if (ids.length === 0) {
      flash('No shapes selected');
      return;
    }
    if (ids.length >= (this.store.snapshot?.shapes.length ?? 0)) {
      flash('At least one shape must stay');
      return;
    }
    if (this.edits.refuseList()) {
      return;
    }
    const e = this.store.engine;
    const r = await this.edits.mutate('Remove selected', async () => {
      await e.call('edit.begin', { label: 'Remove shapes' });
      let failed: Error | null = null;
      try {
        for (const shape of ids) {
          await e.call('shape.delete', { shape });
        }
      } catch (x) {
        failed = x instanceof Error ? x : new Error(String(x));
      }
      const end = await e.call('edit.end');
      if (failed !== null) {
        throw failed;
      }
      return end;
    });
    if (r !== null) {
      for (const s of ids) {
        this.selected.delete(s);
      }
      flash(`${String(ids.length)} shape${ids.length === 1 ? '' : 's'} removed`);
    }
  }
}
