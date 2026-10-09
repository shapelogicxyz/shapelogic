// SPDX-License-Identifier: GPL-3.0-or-later
//
// A question before an action that cannot be taken back. From
// docs/mockup/index.html: `askConfirm` (Cancel, and the action in red);
// Esc cancels, as a dialog.

import { byId, esc } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';

let open: HTMLElement | null = null;

/** Asks; `onOk` runs when the action is chosen. */
export function askConfirm(title: string, text: string, ok: string, onOk: () => void): void {
  open?.remove();
  const d = document.createElement('div');
  d.className = 'cfm-scrim';
  d.innerHTML = `<div class="cfm" role="alertdialog" aria-modal="true"><h3>${esc(title)}</h3><p>${esc(text)}</p><div class="bt"><button class="btn" data-x="0">Cancel</button><button class="btn danger" data-x="1">${esc(ok)}</button></div></div>`;
  byId('stage').appendChild(d);
  open = d;
  d.querySelector<HTMLButtonElement>('[data-x="0"]')?.focus();
  d.addEventListener('click', (e) => {
    const b = (e.target as Element).closest<HTMLElement>('[data-x]');
    if (e.target === d || b !== null) {
      d.remove();
      open = null;
      if (b?.dataset.x === '1') {
        onOk();
      }
    }
  });
}

/** Esc closes the question (as Cancel). */
export function installConfirm(): void {
  addEscapeStep(ESC_ORDER.dialog - 1, () => {
    if (open === null) {
      return false;
    }
    open.remove();
    open = null;
    return true;
  });
}
