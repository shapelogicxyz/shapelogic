// SPDX-License-Identifier: GPL-3.0-or-later
//
// The popover menu (Display options, right-click menus). From
// docs/mockup/index.html: `openPop`, `openPopAt`, `closePop`, the click
// handler (a button with data-keep keeps it open) and the mousedown that
// closes it from outside. The stage is the window here (the mockup scaled
// a 1600 x 1000 board), so positions are the window's.

import { byId } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';

type Pick = (key: string, button: HTMLButtonElement) => void;

let cb: Pick | null = null;
let owner: Element | null = null;

function pop(): HTMLElement {
  return byId('pop');
}

/** Whether the popover is open. */
export function popOpen(): boolean {
  return pop().style.display === 'block';
}

function place(x: number, y: number, above: number | null): void {
  const p = pop();
  const stage = byId('stage').getBoundingClientRect();
  p.style.left = '0px';
  p.style.top = '0px';
  p.style.maxHeight = `${String(Math.max(120, stage.height - 16))}px`;
  const pw = p.offsetWidth;
  const ph = p.offsetHeight;
  let left = Math.max(8, Math.min(x - stage.left, stage.width - pw - 8));
  let top = y - stage.top;
  if (top + ph > stage.height - 10 || above !== null) {
    top = (above ?? y) - stage.top - ph - 6;
  }
  top = Math.max(8, top);
  left = Math.max(8, left);
  p.style.left = `${left}px`;
  p.style.top = `${top}px`;
}

/** Opens the popover below an element (above it when there is no room). */
export function openPop(anchor: Element, html: string, onPick: Pick, above = false): void {
  const p = pop();
  p.innerHTML = html;
  cb = onPick;
  owner = anchor;
  p.style.display = 'block';
  const r = anchor.getBoundingClientRect();
  place(r.left, r.bottom + 6, above ? r.top : null);
}

/** Opens the popover at a point (a right-click). */
export function openPopAt(x: number, y: number, html: string, onPick: Pick): void {
  const p = pop();
  p.innerHTML = html;
  cb = onPick;
  owner = null;
  p.style.display = 'block';
  place(x, y, null);
}

/** Re-renders the open popover's contents in place. */
export function refreshPop(html: string): void {
  if (popOpen()) {
    pop().innerHTML = html;
  }
}

/** Closes the popover. */
export function closePop(): void {
  pop().style.display = 'none';
  cb = null;
  owner = null;
}

/** Wires the popover's clicks, the closing click outside, and Esc. */
export function installPopover(): void {
  const p = pop();
  p.addEventListener('click', (e) => {
    const b = (e.target as Element).closest('button');
    if (b === null || b.disabled) {
      return;
    }
    const keep = b.dataset.keep !== undefined;
    cb?.(b.dataset.k ?? '', b);
    if (!keep) {
      closePop();
    }
  });
  document.addEventListener('mousedown', (e) => {
    const t = e.target as Node;
    if (popOpen() && !p.contains(t) && !(owner?.contains(t) ?? false)) {
      closePop();
    }
  });
  addEscapeStep(ESC_ORDER.menu, () => {
    if (!popOpen()) {
      return false;
    }
    closePop();
    return true;
  });
}
