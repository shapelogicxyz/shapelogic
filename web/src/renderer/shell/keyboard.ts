// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell's keydown handler: the action of a press comes from the one
// table (keys.ts); views register what an action does. From
// docs/mockup/index.html: the document's keydown handler (keys go to a
// text field when one has the focus, except Esc; with the settings dialog
// open only Esc, Ctrl+, and F1 act; Ctrl or Cmd for the "Ctrl" keys).
// An action may have several handlers (one per workspace or view): they
// are tried in the order they were registered until one takes the press.

import { isTyping } from './dom.ts';
import { actionOf } from './keys.ts';
import type { KeyAction } from './keys.ts';

/** What an action does; false: it did nothing (the press goes on to the next handler). */
export type KeyHandler = (e: KeyboardEvent) => boolean | undefined;

const handlers = new Map<KeyAction, KeyHandler[]>();
let modal: () => boolean = () => false;

/**
 * Says what an action does; a handler returning false passes the press on.
 * `first`: tried before the handlers already there (a focused control's
 * meaning of a key before the view's: Home in the Solver's list).
 */
export function onKey(action: KeyAction, handler: KeyHandler, first = false): void {
  const list = handlers.get(action) ?? [];
  if (first) {
    list.unshift(handler);
  } else {
    list.push(handler);
  }
  handlers.set(action, list);
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
  for (const h of handlers.get(action) ?? []) {
    if (h(e) !== false) {
      e.preventDefault();
      return true;
    }
  }
  return false;
}

/** Listens to the document's key presses. */
export function installKeyboard(): void {
  document.addEventListener('keydown', (e) => {
    if (!e.defaultPrevented) {
      dispatchKey(e);
    }
  });
}
