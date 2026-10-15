// SPDX-License-Identifier: GPL-3.0-or-later
//
// A modal dialog over the window (stage 6): the question about unsaved
// changes, the rescue copy's offer, the save point's offer, New's grid,
// the notes, Convert assemblies to pieces, the keyboard shortcuts (F1)
// and About. In the look of the mockup's confirmation (`askConfirm`, the
// `.cfm` card: a title, text, buttons on the right, the action in red or
// blue); one at a time, the newest on top. Esc and a click beside it
// cancel (unless it says otherwise), Tab stays inside it, Enter in a
// one-line field takes the default button, and the keyboard focus goes
// back to where it was when it closes. While one is open the keyboard
// map's keys do not act (keyboard.ts: `setModal`).

import { byId, esc } from './dom.ts';
import { addEscapeStep, ESC_ORDER } from './escape.ts';

/** A button of a dialog. */
export interface DialogButton {
  /** What the dialog answers when it is pressed. */
  key: string;
  label: string;
  /** `pri` (blue) or `danger` (red); plain otherwise. */
  kind?: 'pri' | 'danger';
  /** Enter in a field presses it, and it has the focus first unless a field takes it. */
  default?: boolean;
  /** Its tooltip; Cancel, Close and OK have one of their own when it is left out. */
  tip?: string;
}

/** The tooltips of the buttons most dialogs share. */
const SHARED_TIPS: Readonly<Record<string, string>> = {
  Cancel: 'Close this dialog without doing anything',
  Close: 'Close this window',
  OK: 'Accept and close this dialog',
  Ok: 'Close this message',
};

/** A dialog button's tooltip. */
function tipOf(b: DialogButton): string {
  return b.tip ?? SHARED_TIPS[b.label] ?? '';
}

export interface DialogOptions {
  /** The dialog's element id (the tests find it by it). */
  id: string;
  title: string;
  /** The body: HTML, already escaped. */
  body: string;
  buttons: readonly DialogButton[];
  /** A wider card that scrolls (the keyboard shortcuts, the convert dialog). */
  wide?: boolean;
  /**
   * A button was pressed: return false to keep the dialog open (a field
   * that is not right yet). Esc and a click beside the card answer null.
   */
  onAnswer?: (key: string | null, el: HTMLElement) => boolean | undefined;
  /** Esc and a click beside it do nothing (a question that must be answered). */
  modalOnly?: boolean;
}

interface Open {
  el: HTMLElement;
  options: DialogOptions;
  returnFocus: HTMLElement | null;
  done: (key: string | null) => void;
}

const stack: Open[] = [];

/** Whether a dialog is open (the keyboard map waits). */
export function dialogOpen(): boolean {
  return stack.length > 0;
}

/** The newest dialog's element, or null. */
export function topDialog(): HTMLElement | null {
  return stack.at(-1)?.el ?? null;
}

function close(o: Open, key: string | null): void {
  const i = stack.indexOf(o);
  if (i < 0) {
    return;
  }
  if (o.options.onAnswer?.(key, o.el) === false) {
    return;
  }
  stack.splice(i, 1);
  o.el.remove();
  const r = o.returnFocus;
  if (r?.isConnected === true) {
    r.focus({ preventScroll: true });
  }
  o.done(key);
}

/**
 * Opens a dialog; resolves with the key of the button pressed, or null
 * when it was cancelled (Esc, a click beside it) or closed by `closeDialog`.
 */
export function openDialog(options: DialogOptions): Promise<string | null> {
  return new Promise((done) => {
    // one of an id at a time
    const same = stack.find((o) => o.options.id === options.id);
    if (same !== undefined) {
      close(same, null);
    }
    const active = document.activeElement;
    const el = document.createElement('div');
    el.className = 'cfm-scrim dlg-scrim';
    el.id = options.id;
    el.innerHTML = `<div class="cfm${options.wide === true ? ' wide' : ''}" role="dialog" aria-modal="true" aria-label="${esc(options.title)}"><h3>${esc(options.title)}</h3><div class="cb">${options.body}</div><div class="bt">${options.buttons.map((b) => `<button class="btn${b.kind === undefined ? '' : ` ${b.kind}`}" data-x="${esc(b.key)}" title="${esc(tipOf(b))}">${esc(b.label)}</button>`).join('')}</div></div>`;
    byId('stage').appendChild(el);
    const o: Open = {
      el,
      options,
      returnFocus: active instanceof HTMLElement && active !== document.body ? active : null,
      done,
    };
    stack.push(o);
    el.addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-x]');
      if (b !== null && el.contains(b)) {
        close(o, b.dataset.x ?? null);
      } else if (e.target === el && options.modalOnly !== true) {
        close(o, null);
      }
    });
    el.addEventListener('keydown', (e) => {
      if (e.key === 'Tab') {
        trapTab(el, e);
      } else if (
        e.key === 'Enter' &&
        e.target instanceof HTMLInputElement &&
        e.target.type !== 'checkbox' &&
        e.target.type !== 'radio'
      ) {
        const def = options.buttons.find((b) => b.default === true);
        if (def !== undefined) {
          e.preventDefault();
          close(o, def.key);
        }
      }
    });
    const field = el.querySelector<HTMLElement>('[data-focus]');
    const def = options.buttons.find((b) => b.default === true) ?? options.buttons[0];
    (
      field ??
      (def === undefined
        ? null
        : el.querySelector<HTMLElement>(`[data-x="${CSS.escape(def.key)}"]`))
    )?.focus();
  });
}

/** Closes the dialog of an id, as if cancelled. */
export function closeDialog(id: string): void {
  const o = stack.find((x) => x.options.id === id);
  if (o !== undefined) {
    stack.splice(stack.indexOf(o), 1);
    o.el.remove();
    o.done(null);
  }
}

/** A question with buttons; resolves with the key of the one pressed, or null. */
export function askChoice(
  id: string,
  title: string,
  text: string,
  buttons: readonly DialogButton[],
  modalOnly = false,
): Promise<string | null> {
  return openDialog({
    id,
    title,
    body: `<p>${esc(text).replace(/\n/g, '<br>')}</p>`,
    buttons,
    modalOnly,
  });
}

function trapTab(root: HTMLElement, e: KeyboardEvent): void {
  const all = [
    ...root.querySelectorAll<HTMLElement>(
      'button:not(:disabled),input:not(:disabled),select:not(:disabled),textarea,[tabindex="0"]',
    ),
  ].filter((x) => x.offsetParent !== null);
  const first = all[0];
  const last = all.at(-1);
  if (first === undefined || last === undefined) {
    return;
  }
  if (e.shiftKey && document.activeElement === first) {
    e.preventDefault();
    last.focus();
  } else if (!e.shiftKey && document.activeElement === last) {
    e.preventDefault();
    first.focus();
  }
}

const told: string[] = [];

/**
 * A message box (the original's bt_message): the text and OK. Messages
 * come one at a time, in the order they were told.
 */
export function tell(text: string): void {
  told.push(text);
  if (told.length === 1) {
    showTold();
  }
}

function showTold(): void {
  const text = told[0];
  if (text === undefined) {
    return;
  }
  void openDialog({
    id: 'notice',
    title: 'ShapeLogic',
    body: `<p>${esc(text)}</p>`,
    buttons: [{ key: 'ok', label: 'OK', kind: 'pri', default: true }],
  }).then(() => {
    told.shift();
    showTold();
  });
}

/** Esc cancels the newest dialog (one that must be answered stays). */
export function installDialogs(): void {
  addEscapeStep(ESC_ORDER.dialog - 2, () => {
    const o = stack.at(-1);
    if (o === undefined) {
      return false;
    }
    if (o.options.modalOnly !== true) {
      close(o, null);
    }
    return true;
  });
}
