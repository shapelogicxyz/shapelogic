// SPDX-License-Identifier: GPL-3.0-or-later
//
// The colour picker in the popover (the Voxel editor's pencil): a
// saturation and brightness square, a hue slider and a #rrggbb field, with
// "Restore Default" below for a shape's own colour. The system's dialog
// could not hold that button. A colour is taken when a drag ends, the hue
// slider is let go or the field is confirmed: each is one step to undo.

import { esc } from './dom.ts';
import { openPop } from './popover.ts';

/** What the picker edits. */
export interface PickerOptions {
  /** The colour shown first, #rrggbb. */
  rgb: string;
  /** A colour was chosen, #rrggbb. */
  onPick(rgb: string): void;
  /** Restore Default (none: the button is left out). */
  onDefault?: () => void;
  /** Whether the colour is the default already (the button is off). */
  isDefault?: boolean;
}

type Hsv = [number, number, number];

const HEX = /^#?([0-9a-f]{6})$/i;

function toHsv(rgb: string): Hsv {
  const n = Number.parseInt(rgb.slice(1), 16);
  const [r, g, b] = [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
  const max = Math.max(r, g, b);
  const d = max - Math.min(r, g, b);
  let h = 0;
  if (d > 0) {
    h = max === r ? ((g - b) / d) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
  }
  return [(h * 60 + 360) % 360, max === 0 ? 0 : d / max, max];
}

function toRgb([h, s, v]: Hsv): string {
  const f = (n: number): number => {
    const k = (n + h / 60) % 6;
    return v - v * s * Math.max(0, Math.min(k, 4 - k, 1));
  };
  return (
    '#' +
    [f(5), f(3), f(1)]
      .map((c) =>
        Math.round(c * 255)
          .toString(16)
          .padStart(2, '0'),
      )
      .join('')
  );
}

/** Opens the picker above an element. */
export function openColourPicker(anchor: Element, o: PickerOptions): void {
  let hsv = toHsv(o.rgb);
  const def =
    o.onDefault === undefined
      ? ''
      : `<hr><button data-k="default" ${o.isDefault === true ? 'disabled' : ''} title="Back to the color ShapeLogic gives this shape">Restore Default</button>`;
  openPop(
    anchor,
    `<div class="cpk"><div class="cpk-sv" title="Saturation and brightness"><span class="cpk-knob"></span></div><input type="range" class="cpk-hue" min="0" max="359" step="1" aria-label="Hue" title="Hue"><div class="cpk-row"><span class="cpk-prev"></span><input class="cpk-hex" value="${esc(o.rgb)}" maxlength="7" spellcheck="false" aria-label="Hex color"></div></div>${def}`,
    (k) => {
      if (k === 'default') {
        o.onDefault?.();
      }
    },
    true,
  );
  const pop = document.getElementById('pop');
  const sv = pop?.querySelector<HTMLElement>('.cpk-sv');
  const knob = pop?.querySelector<HTMLElement>('.cpk-knob');
  const hue = pop?.querySelector<HTMLInputElement>('.cpk-hue');
  const prev = pop?.querySelector<HTMLElement>('.cpk-prev');
  const hex = pop?.querySelector<HTMLInputElement>('.cpk-hex');
  if (sv == null || knob == null || hue == null || prev == null || hex == null) {
    return;
  }
  let taken = o.rgb.toLowerCase();
  const show = (field = true): void => {
    const rgb = toRgb(hsv);
    sv.style.backgroundColor = `hsl(${String(hsv[0])},100%,50%)`;
    knob.style.left = `${String(hsv[1] * 100)}%`;
    knob.style.top = `${String((1 - hsv[2]) * 100)}%`;
    knob.style.background = rgb;
    hue.value = String(Math.round(hsv[0]));
    prev.style.background = rgb;
    if (field) {
      hex.value = rgb;
    }
  };
  const take = (): void => {
    const rgb = toRgb(hsv);
    if (rgb !== taken) {
      taken = rgb;
      o.onPick(rgb);
      // a colour of its own now: Default can be restored
      const d = pop?.querySelector<HTMLButtonElement>('[data-k="default"]');
      if (d != null) {
        d.disabled = false;
      }
    }
  };
  const at = (e: PointerEvent): void => {
    const r = sv.getBoundingClientRect();
    const x = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
    const y = Math.max(0, Math.min(1, (e.clientY - r.top) / r.height));
    hsv = [hsv[0], x, 1 - y];
    show();
  };
  sv.addEventListener('pointerdown', (e) => {
    sv.setPointerCapture(e.pointerId);
    at(e);
  });
  sv.addEventListener('pointermove', (e) => {
    if (sv.hasPointerCapture(e.pointerId)) {
      at(e);
    }
  });
  sv.addEventListener('pointerup', take);
  hue.addEventListener('input', () => {
    hsv = [Number(hue.value), hsv[1], hsv[2]];
    show();
  });
  hue.addEventListener('change', take);
  hex.addEventListener('input', () => {
    const m = HEX.exec(hex.value.trim());
    if (m?.[1] !== undefined) {
      hsv = toHsv(`#${m[1]}`);
      show(false);
    }
  });
  hex.addEventListener('change', () => {
    const m = HEX.exec(hex.value.trim());
    if (m?.[1] === undefined) {
      show();
      return;
    }
    take();
  });
  show();
}
