// SPDX-License-Identifier: GPL-3.0-or-later
//
// A question before an action that cannot be taken back. From
// docs/mockup/index.html: `askConfirm` (Cancel, and the action in red).
// A dialog of dialog.ts (cleanup item 6.4), so the keyboard map waits
// while it is open (Ctrl+Z, Del, the workspace keys), Tab stays inside it,
// Esc cancels, and the keyboard focus goes back to where it was (the
// Solver's list keeps its keys after a Del answered).

import { esc } from './dom.ts';
import { openDialog } from './dialog.ts';

/** The confirmation's element id; its buttons answer `cancel` and `ok`. */
export const CONFIRM_ID = 'confirm';

/** Asks; `onOk` runs when the action is chosen. */
export function askConfirm(title: string, text: string, ok: string, onOk: () => void): void {
  void openDialog({
    id: CONFIRM_ID,
    title,
    body: `<p>${esc(text)}</p>`,
    buttons: [
      { key: 'cancel', label: 'Cancel', tip: 'Close this question without doing anything' },
      { key: 'ok', label: ok, kind: 'danger' },
    ],
  }).then((k) => {
    if (k === 'ok') {
      onOk();
    }
  });
}
