// SPDX-License-Identifier: GPL-3.0-or-later
//
// The two lists the exports pick shapes in (not the original's, whose
// dialogs took one shape or every one): File ▸ Export ▸ STL's shapes and
// File ▸ Export ▸ Puzzlecad's pieces. On the left the shapes left out, on
// the right the ones to export, in shape order; arrows between them move
// the selected shapes across (→, ←) or all of them (⇒, ⇐), and a double
// click moves one.

import { esc } from '../shell/dom.ts';

/** A shape the lists show: its id, label and name. */
export interface PickerShape {
  readonly id: string;
  readonly label: string;
  readonly name: string;
}

/** The lists' tooltips, which say what the dialog does with the shapes. */
export interface PickerTips {
  /** The left list's. */
  readonly all: string;
  /** The right list's. */
  readonly chosen: string;
}

/** What the dialog hears from the lists. */
export interface PickerHandlers {
  /** The shapes to export changed (a dialog's button follows them). */
  onChange?: () => void;
  /** A shape was picked or moved: the one to show (a preview). */
  onPick?: (id: string) => void;
}

/** The lists' markup, under the caption "Shapes". */
export function pickerHtml(tips: PickerTips): string {
  return `<div class="pick-h">Shapes</div>
<div class="pick" data-f="picker"><div class="pick-cap">All shapes</div><div class="pick-cap pick-cap-r">To export</div>
<select multiple data-f="all" aria-label="Shapes" class="pick-list" title="${esc(tips.all)}"></select>
<div class="pick-arrows"><button class="btn sm" data-a="add" title="Export the selected shapes">→</button><button class="btn sm" data-a="addAll" title="Export every shape">⇒</button><button class="btn sm" data-a="remove" title="Do not export the selected shapes">←</button><button class="btn sm" data-a="removeAll" title="Export no shape">⇐</button></div>
<select multiple data-f="chosen" aria-label="Shapes to export" class="pick-list" title="${esc(tips.chosen)}"></select></div>`;
}

/** The two lists of a dialog, drawn into its `pickerHtml` markup. */
export class ShapePicker {
  private readonly shapes: readonly PickerShape[];
  private readonly chosen: Set<string>;
  private readonly handlers: PickerHandlers;
  private root: HTMLElement | null = null;

  /** The shapes to show, those to export first (unknown ids are dropped). */
  constructor(shapes: readonly PickerShape[], initial: Iterable<string>, handlers: PickerHandlers) {
    this.shapes = shapes;
    const known = new Set(shapes.map((s) => s.id));
    this.chosen = new Set([...initial].filter((id) => known.has(id)));
    this.handlers = handlers;
  }

  /** The shapes to export, in shape order. */
  ids(): string[] {
    return this.shapes.filter((s) => this.chosen.has(s.id)).map((s) => s.id);
  }

  /** Draws the lists into the dialog and listens to them; `selected` starts selected. */
  attach(dialog: HTMLElement, selected: Iterable<string> = []): void {
    const root = dialog.querySelector<HTMLElement>('[data-f="picker"]');
    if (root === null) {
      return;
    }
    this.root = root;
    this.render(new Set(selected));
    root.addEventListener('change', (e) => {
      // a shape picked in either list is the one shown
      const list = (e.target as Element).closest<HTMLSelectElement>('.pick-list');
      const picked = list === null ? undefined : [...list.selectedOptions].at(-1)?.value;
      if (picked !== undefined) {
        this.handlers.onPick?.(picked);
      }
    });
    root.addEventListener('dblclick', (e) => {
      const list = (e.target as Element).closest<HTMLSelectElement>('.pick-list');
      if (list !== null && !list.disabled) {
        this.move(list.dataset.f === 'all' ? 'all' : 'chosen', false);
      }
    });
    root.querySelector('.pick-arrows')?.addEventListener('click', (e) => {
      const a = (e.target as Element).closest<HTMLElement>('[data-a]')?.dataset.a;
      if (a === 'add' || a === 'addAll') {
        this.move('all', a === 'addAll');
      } else if (a === 'remove' || a === 'removeAll') {
        this.move('chosen', a === 'removeAll');
      }
    });
  }

  /** Greys the lists and the arrows (the dialog exports something else). */
  setDisabled(disabled: boolean): void {
    for (const x of this.root?.querySelectorAll<HTMLSelectElement | HTMLButtonElement>(
      'select, button',
    ) ?? []) {
      x.disabled = disabled;
    }
    this.root?.classList.toggle('off', disabled);
  }

  // the two lists, the shapes in `selected` selected where they are
  private render(selected: ReadonlySet<string>): void {
    const root = this.root;
    if (root === null) {
      return;
    }
    for (const [f, inList] of [
      ['all', false],
      ['chosen', true],
    ] as const) {
      const list = root.querySelector<HTMLSelectElement>(`[data-f="${f}"]`);
      if (list !== null) {
        list.innerHTML = this.shapes
          .filter((s) => this.chosen.has(s.id) === inList)
          .map(
            (s) =>
              `<option value="${esc(s.id)}">${esc(s.label)}${s.name === '' ? '' : ` — ${esc(s.name)}`}</option>`,
          )
          .join('');
        for (const o of list.options) {
          o.selected = selected.has(o.value);
        }
      }
    }
    root.dataset.chosen = String(this.chosen.size);
  }

  // the selected shapes of a list (or all of them) across, selected there, the first one shown
  private move(from: 'all' | 'chosen', every: boolean): void {
    const list = this.root?.querySelector<HTMLSelectElement>(`[data-f="${from}"]`);
    const ids = [...(list?.options ?? [])].filter((o) => every || o.selected).map((o) => o.value);
    if (ids.length === 0) {
      return;
    }
    for (const id of ids) {
      if (from === 'all') {
        this.chosen.add(id);
      } else {
        this.chosen.delete(id);
      }
    }
    this.render(new Set(ids));
    this.handlers.onChange?.();
    const first = ids[0];
    if (first !== undefined) {
      this.handlers.onPick?.(first);
    }
  }
}
