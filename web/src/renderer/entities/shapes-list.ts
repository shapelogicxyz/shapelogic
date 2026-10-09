// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shapes list of the Entities workspace's left card: the engine's
// shapes (labels S1, S2... by place, names, weights, colours), one of them
// selected; new, duplicate (added last), delete (never the last), move by
// drag or Alt+arrows (labels renumber), rename inline (F2, Enter or a
// double-click; Enter commits, Esc cancels; the engine refuses what a name
// cannot hold and its message shows), weight − and +, the shape's own
// colour, the right-click menu, and the collapsed card's chips. From
// docs/mockup/index.html: `renderShapes`, `selectShape`, `newShape`,
// `shapeAction`, the list's click, dblclick, keydown, contextmenu and
// drag handlers. Every action is a call; the list shows what the engine
// says after it (the store's snapshot).

import type { ShapeSummary } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { $$, byId, esc } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { ic } from '../shell/icons.ts';
import { onKey } from '../shell/keyboard.ts';
import { layout, applyLayout, tab } from '../shell/layout.ts';
import { contrast, hex, shapeColour } from '../shell/piece-colours.ts';
import { openPopAt } from '../shell/popover.ts';
import { flash } from '../shell/status.ts';

/** What the list asks of the workspace. */
export interface ListHost {
  store: Store;
  /** Whether editing is refused now (a solve runs); says so when it is. */
  refuse(): boolean;
  /** Whether a change of the list is refused now (also a paused BurrTools 2 solve); says so. */
  refuseList(): boolean;
  /** Runs a mutating call and waits for its revision; reports a failure. */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null>;
}

/** The colour of a shape's chip and the ink on it. */
export function chipColours(
  s: ShapeSummary | undefined,
  index: number,
): { bg: string; fg: string } {
  const c = shapeColour(s?.colour ?? null, index);
  return { bg: hex(c), fg: contrast(c) };
}

export class ShapesList {
  private readonly host: ListHost;
  private readonly list = byId('shapeList');
  private renaming: { id: string; input: HTMLInputElement; cancelled: boolean } | null = null;
  private dragFrom: string | null = null;

  constructor(host: ListHost) {
    this.host = host;
    this.install();
    this.render();
  }

  private get shapes(): ShapeSummary[] {
    return this.host.store.snapshot?.shapes ?? [];
  }

  /** Draws the list (the mockup's renderShapes) unless a rename is under way in it. */
  render(): void {
    const store = this.host.store;
    const shapes = this.shapes;
    byId('shapeCount').textContent = String(shapes.length);
    if (this.renaming === null) {
      this.list.innerHTML = shapes
        .map((s, i) => {
          const { bg, fg } = chipColours(s, i);
          const sel = s.id === store.selected;
          return `<div class="row ${sel ? 'sel' : ''}" draggable="true" role="option" aria-selected="${sel}" data-id="${s.id}" data-i="${i}">
  <span class="grip" title="Drag to reorder — ids follow the order">${ic('grip', 14)}</span>
  <span class="chip" style="background:${bg};color:${fg}">${esc(s.label)}</span>
  <span class="lbl">${esc(s.name)}</span>
  ${s.weight !== 1 ? `<span class="wbadge">W${s.weight}</span>` : ''}
  <span class="acts">
   <button class="ib" data-a="label" title="Rename">${ic('edit', 14)}</button>
   <button class="ib" data-a="copy" title="Duplicate (added as the last shape)">${ic('copy', 14)}</button>
   <button class="ib w" data-a="wm" title="Weight −1 — in the Solver, heavier shapes stay in place and lighter ones move" ${s.weight <= 0 ? 'disabled' : ''}><span class="wtxt">W−</span></button>
   <button class="ib w" data-a="wp" title="Weight +1 — in the Solver, heavier shapes stay in place and lighter ones move"><span class="wtxt">W+</span></button>
   <button class="ib del" data-a="del" title="Delete" ${shapes.length < 2 ? 'disabled' : ''}>${ic('trash', 14)}</button>
  </span></div>`;
        })
        .join('');
    }
    byId('lrail').innerHTML =
      `<button class="ib" id="btnLE" title="Expand Shapes">${ic('panelL')}</button>` +
      shapes
        .map((s, i) => {
          const { bg, fg } = chipColours(s, i);
          const ring =
            s.id === store.selected
              ? 'box-shadow:0 0 0 2px var(--panel),0 0 0 4px var(--accent);'
              : '';
          return `<button class="chip" data-id="${s.id}" title="${esc(s.label)} ${esc(s.name)}" style="background:${bg};color:${fg};border:0;cursor:pointer;min-width:34px;${ring}">${esc(s.label)}</button>`;
        })
        .join('') +
      `<button class="ib" id="btnLN" title="New shape">${ic('plus')}</button>`;
  }

  /** Selects a shape and shows its row. */
  select(id: string): void {
    this.host.store.select(id);
    this.list.querySelector(`.row[data-id="${id}"]`)?.scrollIntoView({ block: 'nearest' });
  }

  // ---- the actions (the mockup's shapeAction) ----

  /** A new empty shape of the grid's default size, selected. */
  async create(): Promise<void> {
    if (this.host.refuse()) {
      return;
    }
    const r = await this.host.mutate('New shape', () =>
      this.host.store.engine.call('shape.create', {}),
    );
    if (r !== null) {
      this.select(r.shape);
    }
  }

  async action(a: string, id: string): Promise<void> {
    const store = this.host.store;
    const shapes = this.shapes;
    const i = shapes.findIndex((s) => s.id === id);
    const s = shapes[i];
    if (s === undefined) {
      return;
    }
    if (a === 'label') {
      this.startRename(id);
      return;
    }
    if (this.host.refuse()) {
      return;
    }
    const e = store.engine;
    switch (a) {
      case 'copy': {
        const r = await this.host.mutate('Duplicate', () =>
          e.call('shape.duplicate', { shape: id }),
        );
        if (r !== null) {
          this.select(r.shape);
        }
        return;
      }
      case 'del': {
        if (shapes.length < 2 || this.host.refuseList()) {
          return;
        }
        // the mockup's choice: the shape now in the deleted one's place
        const next =
          s.id === store.selected ? (shapes[i + 1] ?? shapes[i - 1])?.id : store.selected;
        const r = await this.host.mutate('Delete', () => e.call('shape.delete', { shape: id }));
        if (r !== null && next !== undefined && next !== null) {
          this.select(next);
        }
        return;
      }
      case 'wp':
      case 'wm': {
        const weight = Math.max(0, s.weight + (a === 'wp' ? 1 : -1));
        if (weight !== s.weight) {
          await this.host.mutate('Weight', () => e.call('shape.setWeight', { shape: id, weight }));
        }
        return;
      }
      case 'up':
      case 'down': {
        const to = i + (a === 'up' ? -1 : 1);
        if (to >= 0 && to < shapes.length && !this.host.refuseList()) {
          await this.move(id, to);
        }
        return;
      }
      case 'colour':
        this.pickColour(s);
        return;
      case 'nocolour':
        await this.host.mutate('Colour', () => e.call('shape.setColour', { shape: id, rgb: null }));
        return;
    }
  }

  private async move(id: string, toIndex: number): Promise<void> {
    const r = await this.host.mutate('Move', () =>
      this.host.store.engine.call('shape.move', { shape: id, toIndex }),
    );
    if (r !== null) {
      this.select(id);
      flash(`Order changed — shapes renumbered S1…S${this.shapes.length}`);
    }
  }

  // the shape's own colour: the system's colour picker, for the shape it names
  private pickColour(s: ShapeSummary): void {
    const pick = byId<HTMLInputElement>('shapeColourPick');
    pick.value = hex(shapeColour(s.colour, this.shapes.indexOf(s)));
    pick.dataset.shape = s.id;
    pick.click();
  }

  // ---- rename in place ----

  private startRename(id: string): void {
    this.endRename(true);
    this.select(id);
    const s = this.shapes.find((x) => x.id === id);
    const lbl = this.list.querySelector(`.row[data-id="${id}"] .lbl`);
    if (s === undefined || lbl === null) {
      return;
    }
    if (this.host.refuse()) {
      return;
    }
    lbl.innerHTML = `<input value="${esc(s.name)}" placeholder="Label" aria-label="Shape name">`;
    const input = lbl.querySelector('input');
    if (input === null) {
      return;
    }
    this.renaming = { id, input, cancelled: false };
    input.focus();
    input.select();
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        input.blur();
      }
    });
    input.addEventListener('blur', () => {
      this.endRename(false);
    });
  }

  // ends a rename: commits unless cancelled
  private endRename(cancel: boolean): void {
    const r = this.renaming;
    if (r === null) {
      return;
    }
    this.renaming = null;
    const name = r.input.value;
    const s = this.shapes.find((x) => x.id === r.id);
    if (!cancel && !r.cancelled && s !== undefined && name !== s.name) {
      void this.host.mutate('Rename', () =>
        this.host.store.engine.call('shape.rename', { shape: r.id, name }),
      );
    }
    this.render();
    this.list.focus({ preventScroll: true });
  }

  // ---- events ----

  private inList(t: EventTarget | null): boolean {
    return t instanceof Node && this.list.contains(t) && !(t instanceof HTMLInputElement);
  }

  private install(): void {
    const list = this.list;
    byId('btnNew').addEventListener('click', () => void this.create());
    const pick = byId<HTMLInputElement>('shapeColourPick');
    pick.addEventListener('change', () => {
      const shape = pick.dataset.shape;
      const rgb = pick.value.toLowerCase();
      if (shape !== undefined && !this.host.refuse()) {
        void this.host.mutate('Colour', () =>
          this.host.store.engine.call('shape.setColour', { shape, rgb }),
        );
      }
    });
    list.addEventListener('click', (e) => {
      const t = e.target as Element;
      const row = t.closest<HTMLElement>('.row');
      const id = row?.dataset.id;
      if (id === undefined) {
        return;
      }
      const b = t.closest<HTMLElement>('[data-a]');
      if (b === null) {
        if (t.closest('input') === null) {
          this.select(id);
        }
        return;
      }
      e.stopPropagation();
      void this.action(b.dataset.a ?? '', id);
    });
    list.addEventListener('dblclick', (e) => {
      const t = e.target as Element;
      const id = t.closest<HTMLElement>('.row')?.dataset.id;
      if (id !== undefined && t.closest('[data-a],input') === null) {
        void this.action('label', id);
      }
    });
    // (the mockup focused the list a moment after a press; the list is
    // focusable, so a press in it focuses it, and a late focus would take
    // the focus from a rename begun meanwhile)
    list.addEventListener('contextmenu', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      const id = row?.dataset.id;
      if (id === undefined) {
        return;
      }
      e.preventDefault();
      this.select(id);
      const shapes = this.shapes;
      const i = shapes.findIndex((s) => s.id === id);
      const s = shapes[i];
      if (s === undefined) {
        return;
      }
      const n = shapes.length;
      openPopAt(
        e.clientX,
        e.clientY,
        `<div class="ph">Shape ${esc(s.label)}</div><button data-k="label">Rename…<span class="kb">F2</span></button><button data-k="copy">Duplicate<span class="kb">Ctrl D</span></button><hr><button data-k="up" ${i === 0 ? 'disabled' : ''}>Move earlier<span class="kb">Alt ↑</span></button><button data-k="down" ${i === n - 1 ? 'disabled' : ''}>Move later<span class="kb">Alt ↓</span></button><hr><button data-k="wp">Weight +1 <span class="kb">W+</span></button><button data-k="wm" ${s.weight === 0 ? 'disabled' : ''}>Weight −1 <span class="kb">W−</span></button><hr><button data-k="colour">Colour…</button><button data-k="nocolour" ${s.colour === null ? 'disabled' : ''}>Default colour</button><hr><button data-k="del" ${n < 2 ? 'disabled' : ''} style="color:var(--danger)">Delete<span class="kb">Del</span></button>`,
        (k) => {
          void this.action(k, id);
        },
      );
    });
    // reorder by drag (the mockup's dragstart, dragover, drop, dragend)
    list.addEventListener('dragstart', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      if (row?.dataset.id !== undefined && e.dataTransfer !== null) {
        this.dragFrom = row.dataset.id;
        e.dataTransfer.effectAllowed = 'move';
        e.dataTransfer.setData('text/plain', row.dataset.id);
      }
    });
    list.addEventListener('dragover', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      if (row !== null && this.dragFrom !== null) {
        e.preventDefault();
        for (const r of $$('.row', list)) {
          r.classList.toggle('dragover', r === row);
        }
      }
    });
    list.addEventListener('drop', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      const from = this.dragFrom;
      this.dragFrom = null;
      if (row === null || from === null) {
        return;
      }
      e.preventDefault();
      const to = Number(row.dataset.i);
      const i = this.shapes.findIndex((s) => s.id === from);
      if (i !== to && !this.host.refuseList()) {
        void this.move(from, to);
      }
    });
    list.addEventListener('dragend', () => {
      this.dragFrom = null;
      for (const r of $$('.row', list)) {
        r.classList.remove('dragover');
      }
    });
    // the collapsed card's rail
    byId('lrail').addEventListener('click', (e) => {
      const t = e.target as Element;
      const chip = t.closest<HTMLElement>('.chip[data-id]');
      if (chip?.dataset.id !== undefined) {
        this.select(chip.dataset.id);
      } else if (t.closest('#btnLE') !== null) {
        layout.focus = '';
        layout.lc = false;
        applyLayout();
      } else if (t.closest('#btnLN') !== null) {
        void this.create();
      }
    });

    // keys: the list's while it has the focus, F2, Del and Ctrl+D in the workspace
    const step = (d: number): boolean => {
      const shapes = this.shapes;
      const i = shapes.findIndex((s) => s.id === this.host.store.selected);
      const next = shapes[Math.max(0, Math.min(shapes.length - 1, i + d))];
      if (next !== undefined) {
        this.select(next.id);
      }
      list.focus({ preventScroll: true });
      return true;
    };
    const selected = (): string | null => this.host.store.selected;
    onKey('prev', (e) => (this.inList(e.target) ? step(-1) : false));
    onKey('next', (e) => (this.inList(e.target) ? step(1) : false));
    onKey('shape.earlier', (e) => {
      const id = selected();
      if (!this.inList(e.target) || id === null) {
        return false;
      }
      void this.action('up', id);
      return true;
    });
    onKey('shape.later', (e) => {
      const id = selected();
      if (!this.inList(e.target) || id === null) {
        return false;
      }
      void this.action('down', id);
      return true;
    });
    onKey('shape.rename', () => {
      const id = selected();
      if (tab !== 'entities' || id === null) {
        return false;
      }
      void this.action('label', id);
      return true;
    });
    // Enter renames while the list has the focus (Space is the Solver's)
    onKey('activate', (e) => {
      const id = selected();
      if (tab !== 'entities' || id === null || e.key !== 'Enter' || !this.inList(e.target)) {
        return false;
      }
      void this.action('label', id);
      return true;
    });
    onKey('delete', () => {
      const id = selected();
      if (tab !== 'entities' || id === null) {
        return false;
      }
      void this.action('del', id);
      return true;
    });
    onKey('shape.duplicate', () => {
      const id = selected();
      if (tab !== 'entities' || id === null) {
        return false;
      }
      void this.action('copy', id);
      return true;
    });
    addEscapeStep(ESC_ORDER.rename, () => {
      if (this.renaming === null) {
        return false;
      }
      this.renaming.cancelled = true;
      this.renaming.input.blur();
      return true;
    });
  }
}
