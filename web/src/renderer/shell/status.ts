// SPDX-License-Identifier: GPL-3.0-or-later
//
// The status bar: what the workspace says on the left, a flash after an
// action for 2.4 s, the cursor on the right. From docs/mockup/index.html:
// `flash`, `updateStatus`, `#stCursor`.

import { EngineError } from '../../engine/errors.ts';
import { byId } from './dom.ts';

let provider: () => string = () => '';
let timer = 0;
let flashing = false;

/** Sets what the bar says when nothing flashes (HTML: the provider escapes what it shows). */
export function setStatusProvider(p: () => string): void {
  provider = p;
  updateStatus();
}

/** Shows the workspace's status again, unless a flash is up (`over` ends the flash: news of the engine). */
export function updateStatus(over = false): void {
  if (over) {
    clearTimeout(timer);
    flashing = false;
  }
  if (!flashing) {
    byId('stText').innerHTML = provider();
  }
}

/** Shows a message for a moment (plain text). */
export function flash(text: string, ms = 2400): void {
  flashing = true;
  byId('stText').textContent = text;
  clearTimeout(timer);
  timer = window.setTimeout(() => {
    flashing = false;
    updateStatus();
  }, ms);
}

/** The cursor's place, on the right (empty: none). */
export function setCursor(text: string): void {
  byId('stCursor').textContent = text;
}

/**
 * Tells the user a call failed: the engine's own message; `invalid_state`
 * as "pause first" (decision 0011), never retried.
 */
export function reportError(e: unknown, doing?: string): void {
  if (e instanceof EngineError) {
    if (e.errc === 'superseded') {
      return;
    }
    if (e.errc === 'invalid_state') {
      flash('Pause the solve first', 4000);
      return;
    }
    flash(`${doing === undefined ? '' : doing + ': '}${e.detail}`, 5000);
    return;
  }
  console.error(e);
  flash(`${doing ?? 'Something went wrong'}: ${String(e)}`, 5000);
}
