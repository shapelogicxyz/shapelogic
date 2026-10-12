// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stacking puzzle's Rods card, in the Entities workspace's right card
// where the voxel editor is on the other grids (docs/webui-spec.md,
// section 9.1): the rod sets ("R1 - name") with New, Delete, Copy, Label
// and the arrows, and the selected rod set's rules. From the original's
// mainwindow_layout.cpp (the Rods panel: its labels, tooltips and bounds)
// and mainwindow_rods.cpp (`cb_NewRod`, `cb_DeleteRod`, `cb_CopyRod`,
// `cb_NameRod`, `cb_RodExchange`, `cb_RodSel`, `cb_RodField`,
// `loadRodFields`, `syncPanexFields`, the buttons' states of
// `syncStackingChrome`). Every change is a `stacking.*` call (the engine
// writes the rules as cb_RodField did and drops the solutions of the
// problems on the set); the fields show the rod set as the snapshot has it
// (Snapshot.rodSets). The rod history of the original is the document's
// undo. Frozen while anything solves (the original greyed the fields).

import type { RodSet } from '../../engine/api.generated.ts';
import { rodFieldsOff } from '../state/stacking.ts';
import type { RodRules } from '../state/stacking.ts';
import type { Store } from '../state/store.ts';
import { openDialog } from '../shell/dialog.ts';
import { byId, esc, setHtml } from '../shell/dom.ts';
import { ROD_CHIP } from '../shell/piece-colours.ts';

/** What the card asks of the workspace. */
export interface RodsHost {
  store: Store;
  /** Whether a change is refused now (anything solving); says so. */
  refuseList(): boolean;
  /** Runs a mutating call and waits for its revision; null when it failed (said). */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null>;
  /** The rod set chosen or edited: the view shows it alone (showSelectedRods). */
  showRodSet(id: string): void;
}

const TIP = {
  rodCount: 'Pegs on this board',
  grow: 'A rod can hold every disc in the problem',
  defined: 'A rod is full at this disc count',
  sizeMatters:
    'A disc may not be placed on a disc with a smaller size number. Equal numbers may stack.',
  distanceMatters:
    'When checked, a disc may move only to a neighboring rod. When unchecked, a disc may move to any rod.',
  panexColumns:
    'Rods are Panex columns: a disc of size n can sit at most n places below the top, and discs stack in any order. One more disc may wait at the top, raised into the bridge between the columns, where it blocks every move that passes over it.',
  pocketColumn:
    'An extra column beside rod 1 that holds any disc, in any order, up to the height given here. Needs Panex Style Columns.',
  pocketHeight: 'How many discs the pocket column holds',
  outsideChannel:
    'A channel outside the bridge joins the first and last rods, so a disc can move between them even while a disc is raised on a rod in between. Needs Panex Style Columns and three rods or more.',
} as const;

export class RodsCard {
  /** The rod set selected (its id), or null. */
  selected: string | null = null;
  private readonly host: RodsHost;
  private readonly el = byId('rodsPanel');
  private generation = -1;

  constructor(host: RodsHost) {
    this.host = host;
    this.install();
  }

  private get sets(): RodSet[] {
    return this.host.store.snapshot?.rodSets ?? [];
  }

  /** The rod set selected, as the snapshot has it. */
  get rodSet(): RodSet | null {
    return this.sets.find((r) => r.id === this.selected) ?? null;
  }

  /** Draws the card (only on a stacking puzzle). */
  render(): void {
    const store = this.host.store;
    const sets = this.sets;
    if (store.generation !== this.generation) {
      // another document: its first rod set (RodSelector::setPuzzle)
      this.generation = store.generation;
      this.selected = null;
    }
    if (!sets.some((r) => r.id === this.selected)) {
      this.selected = sets[0]?.id ?? null;
    }
    if (store.snapshot?.grid !== 'stacking') {
      setHtml(this.el, '');
      return;
    }
    const frozen = store.solving;
    const i = sets.findIndex((r) => r.id === this.selected);
    const r = sets[i] ?? null;
    const on = (b: boolean): string => (b ? '' : 'disabled');
    const btn = (k: string, label: string, tip: string, ok: boolean): string =>
      `<button class="btn sm" data-r="${k}" title="${tip}" ${on(ok && !frozen)}>${label}</button>`;
    const buttons =
      btn('new', 'New', 'Add another rod set', true) +
      btn('del', 'Delete', 'Delete the selected rod set', r !== null && sets.length > 1) +
      btn('copy', 'Copy', 'Copy the selected rod set', r !== null) +
      btn('label', 'Label', 'Name the selected rod set', r !== null) +
      btn('left', '←', 'Exchange with the previous rod set', r !== null && i > 0) +
      btn('right', '→', 'Exchange with the next rod set', r !== null && i + 1 < sets.length);
    const list = sets
      .map(
        (x) =>
          `<div class="rodrow ${x.id === this.selected ? 'sel' : ''}" data-rod="${x.id}" role="option" aria-selected="${String(x.id === this.selected)}" title="Select the rod set to edit"><span class="chip" style="background:${ROD_CHIP};color:#fff">${esc(x.label)}</span><span class="lbl">${esc(x.name)}</span><span class="mute">${String(x.rodCount)} rod${x.rodCount === 1 ? '' : 's'}${x.panexColumns ? ' · Panex' : ''}</span></div>`,
      )
      .join('');
    let fields = '';
    if (r !== null) {
      const off = rodFieldsOff(r);
      const dis = (k: keyof RodRules): string => (frozen || off.has(k) ? 'disabled' : '');
      const check = (k: keyof RodRules, label: string, tip: string, value: boolean): string =>
        `<label class="rodck" title="${esc(tip)}"><input type="checkbox" data-f="${k}" ${value ? 'checked' : ''} ${dis(k)}><span>${label}</span></label>`;
      const num = (
        k: keyof RodRules,
        value: number,
        min: number,
        max: number,
        tip: string,
      ): string =>
        `<input class="v rodnum" type="number" data-f="${k}" min="${String(min)}" max="${String(max)}" value="${String(value)}" title="${esc(tip)}" aria-label="${esc(tip)}" ${dis(k)}>`;
      fields =
        `<div class="rodf"><div class="rodline"><span title="${TIP.rodCount}">How many rods</span>${num('rodCount', r.rodCount, 1, 16, TIP.rodCount)}</div>` +
        `<label class="rodck" title="${TIP.grow}"><input type="radio" name="rodGrow" data-f="growHeight" value="1" ${r.growHeight ? 'checked' : ''} ${frozen ? 'disabled' : ''}><span>Grow to the height of all pieces</span></label>` +
        `<div class="rodline"><label class="rodck" title="${TIP.defined}"><input type="radio" name="rodGrow" data-f="growHeight" value="0" ${r.growHeight ? '' : 'checked'} ${frozen ? 'disabled' : ''}><span>Defined height</span></label>${num('definedHeight', r.definedHeight, 1, 64, TIP.defined)}</div>` +
        check(
          'sizeMatters',
          'Disc size matters - no large on small',
          TIP.sizeMatters,
          r.sizeMatters,
        ) +
        check(
          'distanceMatters',
          'Disc can only move over 1 rod',
          TIP.distanceMatters,
          r.distanceMatters,
        ) +
        check('panexColumns', 'Panex Style Columns', TIP.panexColumns, r.panexColumns) +
        `<div class="rodline">${check('pocketColumn', 'Add pocket column', TIP.pocketColumn, r.pocketColumn)}${num('pocketHeight', r.pocketHeight, 1, 64, TIP.pocketHeight)}</div>` +
        check('outsideChannel', 'Extra outside channel', TIP.outsideChannel, r.outsideChannel) +
        '</div>';
    }
    setHtml(
      this.el,
      `<div class="rodbtns">${buttons}</div><div class="rodlist" role="listbox" aria-label="Rod sets">${list}</div>${fields}<div class="rodnote mute">Undo and Redo (Ctrl+Z) take rod changes back too. The 3D view shows the rod set chosen, or the disc chosen in the list.</div>`,
    );
    this.el.dataset.rodSet = this.selected ?? '';
  }

  /** Selects a rod set and shows it (cb_RodSel). */
  select(id: string): void {
    this.selected = id;
    this.render();
    this.host.showRodSet(id);
  }

  private async rules(change: RodRules): Promise<void> {
    const r = this.rodSet;
    if (r === null || this.host.refuseList()) {
      this.render();
      return;
    }
    const rodSet = r.id;
    this.host.showRodSet(rodSet);
    await this.host.mutate('Rods', () =>
      this.host.store.engine.call('stacking.setRules', { rodSet, rules: change }),
    );
    this.render();
  }

  private async action(k: string): Promise<void> {
    const store = this.host.store;
    const e = store.engine;
    const sets = this.sets;
    const i = sets.findIndex((x) => x.id === this.selected);
    const r = sets[i];
    if (this.host.refuseList()) {
      return;
    }
    switch (k) {
      case 'new': {
        const got = await this.host.mutate('New rod set', () => e.call('stacking.addRodSet'));
        if (got !== null) {
          this.select(got.rodSet);
        }
        return;
      }
      case 'copy': {
        if (r === undefined) {
          return;
        }
        const got = await this.host.mutate('Copy rod set', () =>
          e.call('stacking.duplicateRodSet', { rodSet: r.id }),
        );
        if (got !== null) {
          this.select(got.rodSet);
        }
        return;
      }
      case 'del': {
        if (r === undefined) {
          return;
        }
        const next = (sets[i + 1] ?? sets[i - 1])?.id ?? null;
        const got = await this.host.mutate('Delete rod set', () =>
          e.call('stacking.deleteRodSet', { rodSet: r.id }),
        );
        if (got !== null && next !== null) {
          this.select(next);
        }
        return;
      }
      case 'left':
      case 'right': {
        const to = i + (k === 'left' ? -1 : 1);
        if (r === undefined || to < 0 || to >= sets.length) {
          return;
        }
        const got = await this.host.mutate('Move rod set', () =>
          e.call('stacking.moveRodSet', { rodSet: r.id, toIndex: to }),
        );
        if (got !== null) {
          this.select(r.id);
        }
        return;
      }
      case 'label': {
        if (r !== undefined) {
          await this.rename(r);
        }
        return;
      }
    }
  }

  // Label: the name asked in a dialog (cb_NameRod's fl_input)
  private async rename(r: RodSet): Promise<void> {
    let name = r.name;
    const k = await openDialog({
      id: 'rodName',
      title: 'Label',
      body: `<p>Enter name for the rod set</p><input class="v" id="rodNameIn" value="${esc(r.name)}" aria-label="Rod set name" style="width:100%">`,
      buttons: [
        { key: 'cancel', label: 'Cancel' },
        { key: 'ok', label: 'OK', kind: 'pri', default: true },
      ],
      onAnswer: (_key, el) => {
        name = el.querySelector<HTMLInputElement>('#rodNameIn')?.value ?? name;
        return undefined;
      },
    });
    if (k !== 'ok' || name === r.name || this.host.refuseList()) {
      return;
    }
    // the rod set was chosen to be labelled: the view shows it already
    await this.host.mutate('Label rod set', () =>
      this.host.store.engine.call('stacking.renameRodSet', { rodSet: r.id, name }),
    );
  }

  private install(): void {
    this.el.addEventListener('click', (e) => {
      const t = e.target as Element;
      const b = t.closest<HTMLButtonElement>('button[data-r]');
      if (b !== null) {
        if (!b.disabled) {
          void this.action(b.dataset.r ?? '');
        }
        return;
      }
      const row = t.closest<HTMLElement>('[data-rod]');
      if (row?.dataset.rod !== undefined) {
        this.select(row.dataset.rod);
      }
    });
    this.el.addEventListener('change', (e) => {
      const t = e.target as HTMLInputElement;
      const f = t.dataset.f as keyof RodRules | undefined;
      if (f === undefined) {
        return;
      }
      if (t.type === 'checkbox') {
        void this.rules({ [f]: t.checked });
      } else if (t.type === 'radio') {
        void this.rules({ growHeight: t.value === '1' });
      } else {
        const v = Math.round(Number(t.value));
        if (!Number.isFinite(v) || t.value.trim() === '') {
          this.render();
          return;
        }
        void this.rules({ [f]: v });
      }
    });
  }
}
