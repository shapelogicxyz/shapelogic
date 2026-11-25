// SPDX-License-Identifier: GPL-3.0-or-later
//
// The left card's Tilt & tolerance section (decision 0045): with a Tilted
// Square puzzle's board selected, the tray's settings above Transform:
// its width and height (linked while the Keep square switch is on, so a
// change of one changes both), the slope (down and over, the angle beside them), the
// allowance (a switch, and the amount while it is on), the placement
// (where the most cells are whole, or a shift of the user's own, and
// Placements… for every board the grid can give: placements-dialog.ts),
// what the tray gives. Below it (and below Repair & surface for a piece) the
// Tilted Square Tutorial, a section header that opens the research page
// that explains the geometry. Each
// change is one tray.set (one undo step: the engine makes the board again
// and moves the pieces); the section shows what the engine then says.

import type { Tray, TraySettings } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId, esc, forgetHtml, setHtml } from '../shell/dom.ts';
import { flash } from '../shell/status.ts';
import { stepperHtml, switchHtml, wireSteppers } from '../shell/widgets.ts';
import {
  ALLOWANCE_MAX,
  ALLOWANCE_ON,
  SHIFT_MAX,
  SLOPE_MAX,
  TRAY_MAX,
  degreesText,
  isTiltBoard,
  parseAllowance,
  sizeText,
  slopeText,
  unfitIds,
  unfitNote,
} from '../state/tilted.ts';

/** What the section asks of the workspace. */
export interface TiltHost {
  store: Store;
  /** Whether editing is refused now (a solve runs); says so when it is. */
  refuse(): boolean;
  /** Runs a mutating call and waits for its revision; reports a failure. */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null>;
  /** Opens the research page on tilted trays. */
  openPage(): void;
  /** Opens the Placements dialog: every board the grid can give in the tray. */
  openPlacements(): void;
}

/** Whether the section shows: a tilted puzzle with its board selected. */
export function tiltShown(store: Store): boolean {
  return store.snapshot?.tray != null && isTiltBoard(store.shape);
}

export class TiltCard {
  private readonly host: TiltHost;
  /** The allowance the switch gives when turned on: the last amount used, else 0.05. */
  private lastAllowance = ALLOWANCE_ON;
  /**
   * Keep square: the width and the height change together. On at first for
   * each document; off for a tray that is not square.
   */
  private linked = true;
  private linkedFor = -1;

  constructor(host: TiltHost) {
    this.host = host;
    this.install();
  }

  private get tray(): Tray | null {
    return this.host.store.snapshot?.tray ?? null;
  }

  /**
   * Draws the section (hidden unless the board of a tilted puzzle is
   * selected); its body only when its markup changed (setHtml).
   */
  render(): void {
    const sec = byId('secTilt');
    const t = this.tray;
    sec.hidden = !tiltShown(this.host.store) || t === null;
    if (sec.hidden || t === null) {
      return;
    }
    if (t.allowance > 0) {
      this.lastAllowance = t.allowance;
    }
    // another document: linked again; a tray that is not square is not linked
    if (this.linkedFor !== this.host.store.generation) {
      this.linkedFor = this.host.store.generation;
      this.linked = true;
    }
    if (t.width !== t.height) {
      this.linked = false;
    }
    const linked = this.linked;
    // the header's summary stays one line: the size is in the line under the steppers
    const sum = `${slopeText(t.down, t.over)} · ${degreesText(t.degrees)}`;
    if (byId('tiltSum').textContent !== sum) {
      byId('tiltSum').textContent = sum;
    }
    const step = (
      key: 'width' | 'height' | 'down' | 'over',
      value: number,
      min: number,
      max: number,
      tip: string,
    ): string =>
      // a value typed out of the range is refused and told, not held to it
      stepperHtml({
        key,
        value,
        min,
        max,
        less: `${tip} −1`,
        more: `${tip} +1`,
        label: tip,
        strict: true,
      });
    const row = (label: string, small: string, control: string, sub = false): string =>
      `<div class="row2${sub ? ' sub' : ''}"><div class="t">${label}<small>${small}</small></div>${control}</div>`;
    const on = t.allowance > 0;
    const custom = t.placement === 'custom';
    const shift = (key: 'shiftX' | 'shiftY', v: number): string =>
      `<input class="tsh" data-tf="${key}" value="${v.toFixed(3)}" aria-label="${key === 'shiftX' ? 'Shift along x' : 'Shift along y'}" title="Where grid point (0, 0) lies in the tray, in cells (${key === 'shiftX' ? 'x' : 'y'})">`;
    setHtml(
      byId('tiltBody'),
      '<div class="tilt">' +
        row(
          'Width',
          linked ? 'The tray’s sides, in cells' : 'The tray’s side across, in cells',
          step('width', t.width, 1, TRAY_MAX, 'Width'),
        ) +
        row(
          'Height',
          linked ? 'As the width: the tray is kept square' : 'The tray’s side up, in cells',
          step('height', t.height, 1, TRAY_MAX, 'Height'),
        ) +
        row(
          'Keep square',
          'The width and the height change together',
          switchHtml({
            on: linked,
            data: { tlink: '' },
            tip: linked
              ? 'Set the width and the height each on its own: a rectangular tray'
              : 'Make the tray square (the height takes the width) and keep it so',
          }),
          true,
        ) +
        row(
          'Down',
          'Cells down for the cells over (0: a straight tray)',
          step('down', t.down, 0, t.over, 'Down'),
        ) +
        row(
          'Over',
          'Cells over for the cells down',
          step('over', t.over, Math.max(1, t.down), SLOPE_MAX, 'Over'),
        ) +
        `<p class="tline">${esc(sizeText(t.width, t.height))} · ${esc(slopeText(t.down, t.over))} · turned ${esc(degreesText(t.degrees))}</p>` +
        row(
          'Allowance',
          'Let a cell reach a little past the tray’s edge or into a wall, as a real tray’s slack does',
          switchHtml({
            on,
            data: { tallow: '' },
            tip: on
              ? 'Strict: every cell wholly inside the tray'
              : `Allow ${String(this.lastAllowance)} of a cell past the edge`,
          }),
        ) +
        (on
          ? row(
              'Amount',
              `In cell widths, at most ${String(ALLOWANCE_MAX)}`,
              `<input class="tsh" data-tf="allowance" value="${String(t.allowance)}" aria-label="Allowance in cell widths" title="How far a cell may reach past the edge or into a wall, in cell widths (0 to ${String(ALLOWANCE_MAX)})">`,
              true,
            )
          : '') +
        row(
          'Placement',
          custom ? 'Where you put the grid' : 'Where the most cells are whole',
          `<select data-tf="placement" title="Where the turned grid sits in the tray"><option value="best" ${custom ? '' : 'selected'}>Most whole cells</option><option value="custom" ${custom ? 'selected' : ''}>Custom</option></select>`,
        ) +
        (custom
          ? row(
              'Shift',
              'Where grid point (0, 0) lies in the tray, in cells',
              `<span class="tshs">${shift('shiftX', t.shiftX)}${shift('shiftY', t.shiftY)}</span>`,
              true,
            )
          : '') +
        row(
          'Every placement',
          'The boards the grid gives wherever it lies, and which of them take the pieces',
          '<button class="btn sm" data-tplaces title="List every board the grid can give in this tray, count the assemblies of a problem’s pieces on each, and choose one">Placements…</button>',
          true,
        ) +
        '</div>',
    );
  }

  /** Draws the section again whatever it drew last: a field typed into gets the engine's value back. */
  private redraw(): void {
    forgetHtml(byId('tiltBody'));
    this.render();
  }

  /** Sets fields of the tray (one undo step). */
  private set(change: TraySettings, doing = 'Tilt & tolerance'): void {
    if (this.host.refuse()) {
      this.redraw();
      return;
    }
    const unfit = unfitIds(this.host.store.snapshot?.shapes ?? []);
    void this.host
      .mutate(doing, () => this.host.store.engine.call('tray.set', { tray: change }))
      .then((done) => {
        this.redraw();
        const note =
          done === null ? null : unfitNote(unfit, this.host.store.snapshot?.shapes ?? []);
        if (note !== null) {
          flash(note, 6000);
        }
      });
  }

  // a field typed in: a value in range is set, anything else puts the field back
  private typed(input: HTMLInputElement | HTMLSelectElement): void {
    const t = this.tray;
    const key = input.dataset.tf;
    if (t === null || key === undefined) {
      return;
    }
    const v = input.value;
    switch (key) {
      case 'allowance': {
        const a = parseAllowance(v);
        if (a === null) {
          flash(`The allowance is a number from 0 to ${String(ALLOWANCE_MAX)} of a cell`);
          this.redraw();
        } else if (a !== t.allowance) {
          this.set({ allowance: a });
        }
        return;
      }
      case 'placement':
        if (v === 'best' || v === 'custom') {
          // custom starts from the shift in use
          this.set(
            v === 'custom'
              ? { placement: v, shiftX: t.shiftX, shiftY: t.shiftY }
              : { placement: v },
          );
        }
        return;
      case 'shiftX':
      case 'shiftY': {
        const n = Number(v.trim().replace(',', '.'));
        if (v.trim() === '' || !Number.isFinite(n) || Math.abs(n) > SHIFT_MAX) {
          flash('The shift is a number of cells');
          this.redraw();
        } else if (n !== t[key]) {
          this.set({ placement: 'custom', [key]: n });
        }
        return;
      }
    }
  }

  // a new value of a stepper's field: a side of a linked tray sets both
  private change(key: 'width' | 'height' | 'down' | 'over', n: number): TraySettings {
    return this.linked && (key === 'width' || key === 'height') ? { size: n } : { [key]: n };
  }

  // Keep square: turned on, a tray that is not square takes its width for
  // both sides (refused when a piece is too large for it: the switch then
  // goes back with the next drawing); turned off, nothing changes yet
  private toggleLink(): void {
    const t = this.tray;
    if (t === null) {
      return;
    }
    this.linked = !this.linked;
    if (this.linked && t.width !== t.height) {
      this.set({ size: t.width });
    } else {
      this.redraw();
    }
  }

  private toggleAllowance(): void {
    const t = this.tray;
    if (t !== null) {
      this.set({ allowance: t.allowance > 0 ? 0 : this.lastAllowance });
    }
  }

  private install(): void {
    const body = byId('tiltBody');
    // the sides and the slope: stepped, or typed (a whole number in range, else told)
    wireSteppers(body, {
      set: (k, v) => {
        const key = k as 'width' | 'height' | 'over' | 'down';
        if (this.tray !== null && v !== this.tray[key]) {
          this.set(this.change(key, v));
        }
      },
      refused: (k, min, max) => {
        flash(
          `${k.charAt(0).toUpperCase()}${k.slice(1)}: a whole number from ${String(min)} to ${String(max)}`,
        );
      },
      restore: () => {
        this.redraw();
      },
    });
    body.addEventListener('click', (e) => {
      const el = e.target as Element;
      if (el.closest('[data-tplaces]') !== null) {
        this.host.openPlacements();
      } else if (el.closest('[data-tallow]') !== null) {
        this.toggleAllowance();
      } else if (el.closest('[data-tlink]') !== null) {
        this.toggleLink();
      }
    });
    // the Tilted Square Tutorial: a section header that opens the research page
    byId('secTut')
      .querySelector('button.sh')
      ?.addEventListener('click', () => {
        this.host.openPage();
      });
    body.addEventListener('change', (e) => {
      const f = (e.target as Element).closest<HTMLInputElement | HTMLSelectElement>('[data-tf]');
      if (f !== null) {
        this.typed(f);
      }
    });
    body.addEventListener('keydown', (e) => {
      const el = e.target as Element;
      if (e.key === 'Enter' && el.matches('input[data-tf]')) {
        (el as HTMLInputElement).blur();
      }
    });
  }
}
