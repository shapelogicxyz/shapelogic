// SPDX-License-Identifier: GPL-3.0-or-later
//
// The left card's Tray border section (decision 0046): with a sliding
// puzzle's board selected, above Transform (which is folded for a board):
// a switch that shows the tray's border, the tray's body round its floor
// as the Play tab draws it, in every view of the board; and, while it is
// on, how the border looks: its outer corners (rounded or sharp), its
// color (the system's color picker; Default takes it off again) and its
// image (the Apply image dialog, as the board's own), and its material
// (smooth, textured, wood grain: wood is a light brown until the border
// has a color or an image of its own), and its thickness: thin (the rim
// the Play tab has always drawn) or thick, a rim as many voxels wide as
// chosen (1 when Thick is picked). Each change is one shape.setBorder
// (one undo step); the section shows what the engine then says.

import type { ShapeSummary, TrayBorder } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId, esc } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { chipColor, hex, isTray } from '../shell/piece-colors.ts';
import { BORDER_MATERIALS, MAX_BORDER_THICKNESS, borderColor } from '../shell/tray-border.ts';

/** What the section asks of the workspace. */
export interface BorderHost {
  store: Store;
  /** Whether editing is refused now (a solve runs); says so when it is. */
  refuse(): boolean;
  /** Runs a mutating call and waits for its revision; reports a failure. */
  mutate<T extends { revision: number }>(doing: string, call: () => Promise<T>): Promise<T | null>;
  /** Opens the Apply image dialog for the border of the board shown. */
  editImage(): void;
}

/** The board the section is for: a sliding puzzle's board, selected; null otherwise. */
export function borderBoard(store: Store): (ShapeSummary & { border: TrayBorder }) | null {
  const s = store.shape;
  return store.snapshot?.grid === 'sliding' && s !== null && isTray(s) && s.border !== null
    ? (s as ShapeSummary & { border: TrayBorder })
    : null;
}

/** What the section shows, as a key: it is drawn again only when this changes. */
export function borderKey(store: Store): string {
  const s = borderBoard(store);
  return s === null ? '-' : `${s.id}:${JSON.stringify(s.border)}:${String(s.color)}`;
}

export class BorderCard {
  private readonly host: BorderHost;

  constructor(host: BorderHost) {
    this.host = host;
    this.install();
  }

  /** Draws the section (hidden unless a sliding puzzle's board is selected). */
  render(): void {
    const sec = byId('secBorder');
    const s = borderBoard(this.host.store);
    sec.hidden = s === null;
    if (s === null) {
      return;
    }
    const b = s.border;
    const material = BORDER_MATERIALS.find(([m]) => m === b.material)?.[1] ?? '';
    byId('borderSum').textContent = b.enabled
      ? `${b.corners === 'round' ? 'Rounded' : 'Sharp'} · ${material}${b.thickness > 0 ? ` · ${String(b.thickness)} voxel${b.thickness === 1 ? '' : 's'}` : ''}`
      : 'Off';
    const row = (label: string, small: string, control: string): string =>
      `<div class="row2"><div class="t">${label}<small>${small}</small></div>${control}</div>`;
    let h =
      '<div class="tilt tborder">' +
      row(
        'Show tray border',
        'The tray’s edges round the floor, as on the Play tab',
        `<div class="switch ${b.enabled ? 'on' : ''}" data-bon role="switch" tabindex="0" aria-checked="${String(b.enabled)}" title="${b.enabled ? 'Hide the tray’s border (its look is kept)' : 'Show the tray’s border in every view of the board'}"><span class="tr"></span></div>`,
      );
    if (b.enabled) {
      const seg = (value: TrayBorder['corners'], text: string, tip: string): string =>
        `<button data-bcorners="${value}" class="${b.corners === value ? 'on' : ''}" aria-pressed="${String(b.corners === value)}" title="${tip}">${text}</button>`;
      const shown = hex(borderColor(b, chipColor(s, this.host.store.indexOf(s.id))));
      const own = b.color !== null;
      h +=
        row(
          'Corners',
          'The tray’s outside corners',
          `<div class="seg">${seg('round', 'Rounded', 'Round outside corners')}${seg('sharp', 'Sharp', 'Square outside corners, at 90 degrees')}</div>`,
        ) +
        row(
          'Color',
          b.images.length > 0
            ? 'An image covers the color'
            : own
              ? 'The border’s own color'
              : b.material === 'wood'
                ? 'Default: the wood’s light brown'
                : 'Default: the board’s color',
          '<span class="tbcolor">' +
            `<button class="tbsw" data-bcolor style="background:${shown}" title="Choose the border’s color" aria-label="Border color ${shown}"></button>` +
            `<button class="ib ${b.images.length === 0 ? '' : 'on'}" data-bimage title="${b.images.length === 0 ? 'Apply an image from this computer to the tray’s border' : 'Change or remove the border’s image'}" aria-pressed="${String(b.images.length > 0)}">${ic('image', 20)}</button>` +
            `<button class="btn sm" data-bdefault ${own ? '' : 'disabled'} title="Take the border’s own color off: the ${b.material === 'wood' ? 'wood’s light brown' : 'board’s color'} again">Default</button>` +
            '</span>',
        ) +
        row(
          'Material',
          'The border’s surface',
          `<select data-bmaterial aria-label="Material" title="Smooth: a plain surface. Textured: a fine grain. Wood grain: streaks of wood, light brown unless the border has a color or an image of its own">${BORDER_MATERIALS.map(([m, text]) => `<option value="${m}" ${m === b.material ? 'selected' : ''}>${esc(text)}</option>`).join('')}</select>`,
        );
      const thick = b.thickness > 0;
      const tseg = (on: boolean, text: string, tip: string): string =>
        `<button data-bthick="${on ? 'thick' : 'thin'}" class="${thick === on ? 'on' : ''}" aria-pressed="${String(thick === on)}" title="${tip}">${text}</button>`;
      h += row(
        'Thickness',
        thick ? 'The rim round the board, in voxels' : 'A thin rim round the board',
        `<div class="seg">${tseg(false, 'Thin', 'A thin rim round the board, as the Play tab has always drawn it')}${tseg(true, 'Thick', 'A rim one voxel wide or more round the board')}</div>`,
      );
      if (thick) {
        const counts = Array.from({ length: MAX_BORDER_THICKNESS }, (_, i) => i + 1);
        h += row(
          'Voxels',
          'How many voxels thick the border is',
          `<select data-bvoxels aria-label="Voxels thick" title="How many voxels wide the rim round the board is">${counts.map((n) => `<option value="${String(n)}" ${n === b.thickness ? 'selected' : ''}>${String(n)}</option>`).join('')}</select>`,
        ).replace('class="row2"', 'class="row2 sub"');
      }
    }
    byId('borderBody').innerHTML = h + '</div>';
  }

  // one change of the border shown: a shape.setBorder
  private set(
    change: Partial<Pick<TrayBorder, 'enabled' | 'corners' | 'material' | 'color' | 'thickness'>>,
  ): void {
    const s = borderBoard(this.host.store);
    if (s === null || this.host.refuse()) {
      this.render();
      return;
    }
    void this.host
      .mutate('Tray border', () =>
        this.host.store.engine.call('shape.setBorder', { shape: s.id, ...change }),
      )
      .then(() => {
        this.render();
      });
  }

  private toggle(): void {
    const s = borderBoard(this.host.store);
    if (s !== null) {
      this.set({ enabled: !s.border.enabled });
    }
  }

  private install(): void {
    const body = byId('borderBody');
    const pick = byId<HTMLInputElement>('borderColorPick');
    body.addEventListener('click', (e) => {
      const el = e.target as Element;
      const s = borderBoard(this.host.store);
      if (s === null) {
        return;
      }
      const corners = el.closest<HTMLElement>('[data-bcorners]');
      const thick = el.closest<HTMLElement>('[data-bthick]');
      if (el.closest('[data-bon]') !== null) {
        this.toggle();
      } else if (thick !== null) {
        // Thick starts at one voxel; Thin is the thin rim again
        const want = thick.dataset.bthick === 'thick';
        if (want !== s.border.thickness > 0) {
          this.set({ thickness: want ? 1 : 0 });
        }
      } else if (corners !== null) {
        this.set({ corners: corners.dataset.bcorners === 'sharp' ? 'sharp' : 'round' });
      } else if (el.closest('[data-bcolor]') !== null) {
        // the system's color picker, from the color shown
        pick.value = hex(borderColor(s.border, chipColor(s, this.host.store.indexOf(s.id))));
        pick.click();
      } else if (el.closest('[data-bimage]') !== null) {
        this.host.editImage();
      } else if (el.closest('[data-bdefault]') !== null) {
        this.set({ color: null });
      }
    });
    pick.addEventListener('change', () => {
      this.set({ color: pick.value.toLowerCase() });
    });
    body.addEventListener('change', (e) => {
      const v = (e.target as Element).closest<HTMLSelectElement>('select[data-bvoxels]');
      if (v !== null) {
        const n = Number(v.value);
        if (Number.isInteger(n) && n >= 1 && n <= MAX_BORDER_THICKNESS) {
          this.set({ thickness: n });
        }
        return;
      }
      const f = (e.target as Element).closest<HTMLSelectElement>('select[data-bmaterial]');
      if (f !== null) {
        const m = BORDER_MATERIALS.find(([v]) => v === f.value)?.[0];
        if (m !== undefined) {
          this.set({ material: m });
        }
      }
    });
    body.addEventListener('keydown', (e) => {
      if (
        (e.key === ' ' || e.key === 'Enter') &&
        (e.target as Element).closest('[data-bon]') !== null
      ) {
        e.preventDefault();
        this.toggle();
      }
    });
  }
}
