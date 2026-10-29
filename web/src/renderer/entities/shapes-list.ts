// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shapes list of the Entities workspace's left card: the engine's
// shapes (labels S1, S2... by place, names, colours), one of them
// selected; new, duplicate (added last), delete (never the last), move by
// drag or Alt+arrows (labels renumber), rename inline (F2, Enter or a
// double-click; Enter commits, Esc cancels; the engine refuses what a name
// cannot hold and its message shows), the shape's own colour, the
// right-click menu (weight − and + too; the Puzzle workspace's Groups
// table has them beside each piece), and the collapsed card's chips. From
// docs/mockup/index.html: `renderShapes`, `selectShape`, `newShape`,
// `shapeAction`, the list's click, dblclick, keydown, contextmenu and
// drag handlers. Every action is a call; the list shows what the engine
// says after it (the store's snapshot). On a stacking puzzle (docs/webui-
// spec.md, section 9.1) it is the original's disc list (BlockList.cpp's
// DiskSelector): each disc's "size" field with − and + (1 to 100,
// stacking.setDiscSize), no weight; New makes a disc of size 1. On a
// sliding puzzle the boards have a list of their own above the pieces,
// with + New Board in its header.

import type { ShapeSummary } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { $$, byId, esc } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { ic } from '../shell/icons.ts';
import { onKey } from '../shell/keyboard.ts';
import { layout, applyLayout, tab } from '../shell/layout.ts';
import { onTheme } from '../shell/theme.ts';
import { chipColour, contrast, hex, isTray, shownName } from '../shell/piece-colours.ts';
import { openPopAt } from '../shell/popover.ts';
import { prefs } from '../shell/prefs.ts';
import { DISC_MAX, DISC_MIN, DISC_SIZE_REFUSED, discSizeOf } from '../state/stacking.ts';
import { tell } from '../shell/dialog.ts';
import { flash } from '../shell/status.ts';

/** What the list asks of the workspace. */
export interface ListHost {
  store: Store;
  /** Whether editing is refused now (a solve runs); says so when it is. */
  refuse(): boolean;
  /** Whether a change of the list is refused now; says so. */
  refuseList(): boolean;
  /** Runs a mutating call and waits for its revision; reports a failure. */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null>;
}

/** The colour of a shape's chip and the ink on it. */
export function chipColours(
  s: ShapeSummary | undefined,
  index: number,
): { bg: string; fg: string } {
  const c = chipColour(s, index);
  return { bg: hex(c), fg: contrast(c) };
}

export class ShapesList {
  private readonly host: ListHost;
  private readonly list = byId('shapeList');
  /** A sliding puzzle's boards (empty on other grids). */
  private readonly boards = byId('boardList');
  private renaming: { id: string; input: HTMLInputElement; cancelled: boolean } | null = null;
  /** A disc's size field being typed in (the list is not drawn over it meanwhile). */
  private sizing: string | null = null;
  private dragFrom: string | null = null;

  constructor(host: ListHost) {
    this.host = host;
    this.install();
    this.render();
    // a board's swatch follows the theme's board color
    onTheme(() => {
      this.render();
    });
  }

  private get shapes(): ShapeSummary[] {
    return this.host.store.snapshot?.shapes ?? [];
  }

  /** The list a shape's row is in. */
  private listOf(id: string | null): HTMLElement {
    const s = this.shapes.find((x) => x.id === id);
    return this.host.store.snapshot?.grid === 'sliding' && isTray(s) ? this.boards : this.list;
  }

  /** A shape's row, in either list. */
  private row(id: string): HTMLElement | null {
    return (
      this.list.querySelector<HTMLElement>(`.row[data-id="${id}"]`) ??
      this.boards.querySelector<HTMLElement>(`.row[data-id="${id}"]`)
    );
  }

  /** Draws the lists (the mockup's renderShapes) unless a rename is under way in them. */
  render(): void {
    const store = this.host.store;
    const shapes = this.shapes;
    const discs = store.snapshot?.grid === 'stacking';
    const sliding = store.snapshot?.grid === 'sliding';
    // a sliding puzzle's boards (the trays that hold start and goal
    // positions) in their own list, its pieces in Shapes; the rows keep
    // their places in the whole list (data-i), boards first
    const boards = sliding ? shapes.filter(isTray).length : 0;
    byId('shapeCount').textContent = String(shapes.length - boards);
    byId('boardCount').textContent = String(boards);
    byId('btnNew').title = discs ? 'New disc (size 1)' : sliding ? 'New piece' : 'New shape';
    byId('btnNewWhat').hidden = !sliding;
    byId('boardsSec').hidden = !sliding;
    if (this.renaming === null && this.sizing === null) {
      const rows = shapes.map((s, i) => {
        const { bg, fg } = chipColours(s, i);
        const sel = s.id === store.selected;
        if (discs) {
          // the disc list's row: the name, then "size:" with its field, + and −
          const size = s.disc ?? 1;
          return `<div class="row disc ${sel ? 'sel' : ''}" draggable="true" role="option" aria-selected="${sel}" data-id="${s.id}" data-i="${i}">
  <span class="grip" title="Drag to reorder — ids follow the order">${ic('grip', 14)}</span>
  <span class="chip" style="background:${bg};color:${fg}">${esc(s.label)}</span>
  <span class="lbl">${esc(s.name)}</span>
  <span class="dsz" title="Rank compared by the size rule, and the radius of the disc"><span class="mute">size</span><input class="v" data-dsize value="${String(size)}" aria-label="Disc size" inputmode="numeric"><button class="ib" data-a="dp" title="Size +1" ${size >= DISC_MAX ? 'disabled' : ''}>+</button><button class="ib" data-a="dm" title="Size −1" ${size <= DISC_MIN ? 'disabled' : ''}>−</button></span>
  <span class="acts">
   <button class="ib" data-a="label" title="Rename">${ic('edit', 17)}</button>
   <button class="ib" data-a="copy" title="Duplicate (added as the last disc)">${ic('copy', 17)}</button>
   <button class="ib del" data-a="del" title="Delete">${ic('trash', 17)}</button>
  </span></div>`;
        }
        return `<div class="row ${sel ? 'sel' : ''}" draggable="true" role="option" aria-selected="${sel}" data-id="${s.id}" data-i="${i}">
  <span class="grip" title="Drag to reorder — ids follow the order">${ic('grip', 14)}</span>
  <span class="chip" style="background:${bg};color:${fg}">${esc(s.label)}</span>
  <span class="lbl">${isTray(s) && s.name === '' ? `<b class="sgtag">${esc(shownName(s))}</b>` : esc(s.name)}</span>
  <span class="acts">
   <button class="ib" data-a="label" title="Rename">${ic('edit', 17)}</button>
   <button class="ib" data-a="copy" title="Duplicate (added as the last shape)">${ic('copy', 17)}</button>
   <button class="ib del" data-a="del" title="${isTray(s) ? 'Delete this board and its start/goal problem' : 'Delete'}">${ic('trash', 17)}</button>
  </span></div>`;
      });
      this.boards.innerHTML = rows.slice(0, boards).join('');
      this.list.innerHTML = rows.slice(boards).join('');
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
          return `<button class="chip" data-id="${s.id}" title="${esc(s.label)} ${esc(shownName(s))}" style="background:${bg};color:${fg};border:0;cursor:pointer;min-width:34px;${ring}">${esc(s.label)}</button>`;
        })
        .join('') +
      `<button class="ib" id="btnLN" title="New shape">${ic('plus')}</button>`;
  }

  /**
   * New Board (the original's New Start/Goal Positions): a tray of the
   * last board's size, else of the sliding size setting, selected.
   */
  async newBoard(): Promise<void> {
    if (this.host.refuseList()) {
      return;
    }
    const store = this.host.store;
    const last = this.shapes.filter(isTray).at(-1);
    const u = store.info?.grids.find((g) => g.grid === store.snapshot?.grid)?.sizeUnit ?? 1;
    const size: [number, number, number] =
      last === undefined
        ? [prefs.slideX * u, prefs.slideY * u, prefs.slideZ * u]
        : [last.size[0], last.size[1], last.size[2]];
    // the board has no colour of its own: it takes the theme's Sliding board color
    const r = await this.host.mutate('New board', () =>
      this.host.store.engine.call('sliding.addStartGoal', { size }),
    );
    if (r !== null) {
      this.select(r.shape);
    }
  }

  /** Selects a shape and shows its row. */
  select(id: string): void {
    this.host.store.select(id);
    this.row(id)?.scrollIntoView({ block: 'nearest' });
  }

  // ---- the actions (the mockup's shapeAction) ----

  /**
   * A new empty shape of the selected shape's size, else of the New shape
   * size setting (in the size steppers' blocks), selected. A sliding
   * puzzle's piece takes the last piece's size, else the sliding size
   * setting (boards do not count). A stacking puzzle's disc takes no size.
   */
  async create(): Promise<void> {
    if (this.host.refuse()) {
      return;
    }
    const store = this.host.store;
    const grid = store.snapshot?.grid;
    const sel = store.shape;
    const u = store.info?.grids.find((g) => g.grid === grid)?.sizeUnit ?? 1;
    const fresh: [number, number, number] =
      grid === 'sliding'
        ? [prefs.slideX * u, prefs.slideY * u, prefs.slideZ * u]
        : [prefs.newX * u, prefs.newY * u, prefs.newZ * u];
    const lastPiece = this.shapes.filter((x) => !isTray(x)).at(-1);
    const from = grid === 'sliding' ? lastPiece : sel;
    const params: { size?: [number, number, number] } =
      grid === 'stacking'
        ? {}
        : { size: from == null ? fresh : [from.size[0], from.size[1], from.size[2]] };
    const r = await this.host.mutate('New shape', () => store.engine.call('shape.create', params));
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
        // a tray goes with its problem (the original's Delete Start/Goal Positions)
        if (this.host.refuseList()) {
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
      case 'dp':
      case 'dm': {
        // a size step: nothing past the bounds (DiskSelector::onDelta)
        const now = s.disc ?? 1;
        const size = now + (a === 'dp' ? 1 : -1);
        if (size >= DISC_MIN && size <= DISC_MAX) {
          await this.setDiscSize(id, size);
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
        if (this.canMove(i, to) && !this.host.refuseList()) {
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

  /** A disc's size (stacking.setDiscSize); the disc selected, as the original's list did. */
  async setDiscSize(id: string, size: number): Promise<void> {
    this.select(id);
    await this.host.mutate('Disc size', () =>
      this.host.store.engine.call('stacking.setDiscSize', { shape: id, size }),
    );
  }

  // a size typed in a disc's field: refused with the original's message, the size then 1
  private sizeTyped(input: HTMLInputElement): void {
    const id = input.closest<HTMLElement>('.row')?.dataset.id;
    this.sizing = null;
    const s = this.shapes.find((x) => x.id === id);
    if (id === undefined || s === undefined) {
      this.render();
      return;
    }
    if (this.host.refuse()) {
      this.render();
      return;
    }
    const size = discSizeOf(input.value);
    if (size === null) {
      tell(DISC_SIZE_REFUSED);
      // the field shows the size again (a disc of size 1 already is not changed)
      this.render();
      void this.setDiscSize(id, 1);
      return;
    }
    if (size !== (s.disc ?? 1)) {
      void this.setDiscSize(id, size);
    } else {
      this.render();
    }
  }

  /** Whether the shape at a place may go to another: a sliding puzzle's boards stay above its pieces. */
  private canMove(from: number, to: number): boolean {
    const shapes = this.shapes;
    if (to < 0 || to >= shapes.length || to === from) {
      return false;
    }
    if (this.host.store.snapshot?.grid !== 'sliding') {
      return true;
    }
    const boards = shapes.filter(isTray).length;
    return isTray(shapes[from]) ? to < boards : to >= boards;
  }

  private async move(id: string, toIndex: number): Promise<void> {
    const r = await this.host.mutate('Move', () =>
      this.host.store.engine.call('shape.move', { shape: id, toIndex }),
    );
    if (r !== null) {
      this.select(id);
      flash(
        this.host.store.snapshot?.grid === 'sliding'
          ? 'Order changed — boards and pieces renumbered'
          : `Order changed — shapes renumbered S1…S${this.shapes.length}`,
      );
    }
  }

  // the shape's own colour: the system's colour picker, for the shape it names
  private pickColour(s: ShapeSummary): void {
    const pick = byId<HTMLInputElement>('shapeColourPick');
    pick.value = hex(chipColour(s, this.shapes.indexOf(s)));
    pick.dataset.shape = s.id;
    pick.click();
  }

  // ---- rename in place ----

  private startRename(id: string): void {
    this.endRename(true);
    this.select(id);
    const s = this.shapes.find((x) => x.id === id);
    const lbl = this.row(id)?.querySelector('.lbl') ?? null;
    if (s === undefined || lbl === null) {
      return;
    }
    if (this.host.refuse()) {
      return;
    }
    // a board without a name starts from the one it shows, to change or delete
    lbl.innerHTML = `<input value="${esc(shownName(s))}" placeholder="Label" aria-label="Shape name">`;
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
    if (!cancel && !r.cancelled && s !== undefined && name !== shownName(s)) {
      void this.host.mutate('Rename', () =>
        this.host.store.engine.call('shape.rename', { shape: r.id, name }),
      );
    }
    this.render();
    this.listOf(r.id).focus({ preventScroll: true });
  }

  // ---- events ----

  private inList(t: EventTarget | null): boolean {
    return (
      t instanceof Node &&
      (this.list.contains(t) || this.boards.contains(t)) &&
      !(t instanceof HTMLInputElement)
    );
  }

  private install(): void {
    byId('btnNew').addEventListener('click', () => void this.create());
    byId('btnNewBoard').addEventListener('click', () => void this.newBoard());
    for (const list of [this.boards, this.list]) {
      this.installList(list);
    }
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
      this.listOf(this.host.store.selected).focus({ preventScroll: true });
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

  // a list's own events, the boards' and the shapes' alike (a row's place,
  // data-i, is in the whole list: a board never goes among the pieces)
  private installList(list: HTMLElement): void {
    // a disc's size field: typed, then Enter or leaving it
    list.addEventListener('focusin', (e) => {
      const t = e.target as Element;
      if (t instanceof HTMLInputElement && t.dataset.dsize !== undefined) {
        this.sizing = t.closest<HTMLElement>('.row')?.dataset.id ?? null;
      }
    });
    list.addEventListener('keydown', (e) => {
      const t = e.target as Element;
      if (t instanceof HTMLInputElement && t.dataset.dsize !== undefined && e.key === 'Enter') {
        e.preventDefault();
        t.blur();
      }
    });
    list.addEventListener('focusout', (e) => {
      const t = e.target as Element;
      if (t instanceof HTMLInputElement && t.dataset.dsize !== undefined) {
        this.sizeTyped(t);
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
        // a disc's size field chooses its disc too (the list is not drawn over the field)
        if (t.closest('input') === null || t.closest('[data-dsize]') !== null) {
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
      openPopAt(
        e.clientX,
        e.clientY,
        `<div class="ph">Shape ${esc(s.label)}</div><button data-k="label" title="Give this shape a name">Rename…<span class="kb">F2</span></button><button data-k="copy" title="Make a copy of this shape">Duplicate<span class="kb">Ctrl D</span></button><hr><button data-k="up" ${this.canMove(i, i - 1) ? '' : 'disabled'} title="Exchange this shape with the previous shape">Move earlier<span class="kb">Alt ↑</span></button><button data-k="down" ${this.canMove(i, i + 1) ? '' : 'disabled'} title="Exchange this shape with the next shape">Move later<span class="kb">Alt ↓</span></button>${this.host.store.snapshot?.grid === 'stacking' ? '' : `<hr><button data-k="wp" title="Make this shape heavier: when the disassembly separates pieces, the heavier group stays in place">Weight +1</button><button data-k="wm" ${s.weight === 0 ? 'disabled' : ''} title="Make this shape lighter: when the disassembly separates pieces, the lighter group moves">Weight −1</button>`}<hr><button data-k="colour" title="Choose this shape’s own color">Color…</button><button data-k="nocolour" ${s.colour === null ? 'disabled' : ''} title="Back to the color ShapeLogic gives this shape">Default color</button><hr><button data-k="del" title="${isTray(s) ? 'Delete this board and its start/goal problem' : 'Delete this shape from the puzzle'}" style="color:var(--danger)">Delete<span class="kb">Del</span></button>`,
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
      const from = this.shapes.findIndex((s) => s.id === this.dragFrom);
      // no drop where the shape may not go (a board below a piece)
      if (row !== null && this.dragFrom !== null && this.canMove(from, Number(row.dataset.i))) {
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
      if (this.canMove(i, to) && !this.host.refuseList()) {
        void this.move(from, to);
      }
    });
    list.addEventListener('dragend', () => {
      this.dragFrom = null;
      for (const r of $$('.row', list)) {
        r.classList.remove('dragover');
      }
    });
  }
}
