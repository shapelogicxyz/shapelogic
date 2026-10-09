// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell's keydown handler: the action of a press comes from the one
// table (keys.ts); views register what an action does. From
// docs/mockup/index.html: the document's keydown handler (keys go to a
// text field when one has the focus, except Esc; with the settings dialog
// open only Esc, Ctrl+, and F1 act; Ctrl or Cmd for the "Ctrl" keys).

import { isTyping } from './dom.ts';
import { actionOf } from './keys.ts';
import type { KeyAction } from './keys.ts';

const handlers = new Map<KeyAction, () => boolean | undefined>();
let modal: () => boolean = () => false;

/**
 * Says what an action does; a handler returning false did nothing (the
 * key then goes on as if unbound).
 */
export function onKey(action: KeyAction, handler: () => boolean | undefined): void {
  handlers.set(action, handler);
}

/** Says when a modal dialog holds the keyboard (only Esc, Ctrl+, and F1 act then). */
export function setModal(isModal: () => boolean): void {
  modal = isModal;
}

/** Handles a key press as the shell does; true when it was taken. */
export function dispatchKey(e: KeyboardEvent): boolean {
  const action = actionOf(e);
  if (action === null) {
    return false;
  }
  if (isTyping(e.target) && action !== 'escape') {
    return false;
  }
  if (modal() && action !== 'escape' && action !== 'settings' && action !== 'help.keys') {
    return false;
  }
  const h = handlers.get(action);
  if (h === undefined || h() === false) {
    return false;
  }
  e.preventDefault();
  return true;
}

/** Listens to the document's key presses. */
export function installKeyboard(): void {
  document.addEventListener('keydown', (e) => {
    if (!e.defaultPrevented) {
      dispatchKey(e);
    }
  });
}
