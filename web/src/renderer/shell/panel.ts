// SPDX-License-Identifier: GPL-3.0-or-later
//
// A window of its own over the workspace, as the original's status window
// and debug statistics were (FRONTEND-PLAN.md, stage 5): a title, a close
// button, a body and a row of buttons, not modal (the solve it watches can
// be paused and carried on beside it). Esc closes the newest one after a
// dialog and a menu. In the look of the mockup's cards and dialogs (the
// `.cfm` question, the `.sdrawer`'s header).

import { byId, esc } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';
import { ic } from './icons.ts';

/** An open panel. */
export interface Panel {
  readonly el: HTMLElement;
  readonly body: HTMLElement;
  readonly foot: HTMLElement;
  close(): void;
}

const open: { panel: Panel; onClose: () => void }[] = [];

/**
 * Opens a panel (`id` on its element; one of an id at a time: the one
 * open is closed first). `onClose` runs when it closes, by its button, Esc
 * or `close()`.
 */
export function openPanel(id: string, title: string, onClose: () => void): Panel {
  open.find((o) => o.panel.el.id === id)?.panel.close();
  const el = document.createElement('div');
  el.className = 'fpanel';
  el.id = id;
  el.setAttribute('role', 'dialog');
  el.setAttribute('aria-label', title);
  el.innerHTML = `<div class="fh"><span>${esc(title)}</span><span class="sp"></span><button class="ib" data-close title="Close  (Esc)">${ic('close', 16)}</button></div><div class="fb"></div><div class="ff"></div>`;
  byId('stage').appendChild(el);
  const panel: Panel = {
    el,
    body: el.querySelector<HTMLElement>('.fb') ?? el,
    foot: el.querySelector<HTMLElement>('.ff') ?? el,
    close: () => {
      const i = open.findIndex((o) => o.panel === panel);
      if (i < 0) {
        return;
      }
      const [o] = open.splice(i, 1);
      el.remove();
      o?.onClose();
    },
  };
  el.querySelector('[data-close]')?.addEventListener('click', () => {
    panel.close();
  });
  open.push({ panel, onClose });
  return panel;
}

/** Esc closes the newest panel (after a dialog and a menu). */
export function installPanels(): void {
  addEscapeStep(ESC_ORDER.menu + 5, () => {
    const last = open.at(-1);
    if (last === undefined) {
      return false;
    }
    last.panel.close();
    return true;
  });
}
