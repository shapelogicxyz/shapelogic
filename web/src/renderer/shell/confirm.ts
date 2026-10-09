// SPDX-License-Identifier: GPL-3.0-or-later
//
// A question before an action that cannot be taken back. From
// docs/mockup/index.html: `askConfirm` (Cancel, and the action in red);
// Esc cancels, as a dialog, and the keyboard focus goes back to where it
// was (the Solver's list keeps its keys after a Del answered).

import { byId, esc } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';

let open: HTMLElement | null = null;
let returnFocus: HTMLElement | null = null;

// the dialog goes; the focus back to where it was, if that is still there
function close(): void {
  open?.remove();
  open = null;
  const r = returnFocus;
  returnFocus = null;
  if (r?.isConnected === true) {
    r.focus({ preventScroll: true });
  }
}

/** Asks; `onOk` runs when the action is chosen. */
export function askConfirm(title: string, text: string, ok: string, onOk: () => void): void {
  open?.remove();
  const active = document.activeElement;
  returnFocus = active instanceof HTMLElement && active !== document.body ? active : returnFocus;
  const d = document.createElement('div');
  d.className = 'cfm-scrim';
  d.innerHTML = `<div class="cfm" role="alertdialog" aria-modal="true"><h3>${esc(title)}</h3><p>${esc(text)}</p><div class="bt"><button class="btn" data-x="0">Cancel</button><button class="btn danger" data-x="1">${esc(ok)}</button></div></div>`;
  byId('stage').appendChild(d);
  open = d;
  d.querySelector<HTMLButtonElement>('[data-x="0"]')?.focus();
  d.addEventListener('click', (e) => {
    const b = (e.target as Element).closest<HTMLElement>('[data-x]');
    if (e.target === d || b !== null) {
      close();
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
    close();
    return true;
  });
}
