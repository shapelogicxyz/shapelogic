// SPDX-License-Identifier: GPL-3.0-or-later
//
// The status bar: what the workspace says on the left, a flash after an
// action for 2.4 s, the cursor on the right. From docs/mockup/index.html:
// `flash`, `updateStatus`, `#stCursor`.

import { EngineError } from '../../engine/errors.ts';
import { writeHint } from '../../engine/write-hint.ts';
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

/** The platform the page runs on (`btApp.platform`; '' without the app's bridge). */
function platform(): string {
  return (globalThis as { btApp?: { platform?: string } }).btApp?.platform ?? '';
}

// the message box of dialog.ts (`tell`), given by the app: this file is
// also compiled without the page's DOM
let teller: ((text: string) => void) | null = null;

/** Where a failed write's message with its hint goes (dialog.ts's tell, with View Logs). */
export function setHintTeller(tell: (text: string) => void): void {
  teller = tell;
}

/**
 * A failed write that Windows may have blocked (Controlled Folder Access,
 * cleanup item 2.5) is told in a message with the hint, not in a flash;
 * true when it was.
 */
function toldWithHint(text: string): boolean {
  const hint = writeHint(platform(), text);
  if (hint === null || teller === null) {
    return false;
  }
  teller(`${text}\n\n${hint}`);
  return true;
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
    const text = `${doing === undefined ? '' : doing + ': '}${e.detail}`;
    if (!toldWithHint(text)) {
      flash(text, 5000);
    }
    return;
  }
  console.error(e);
  const text = `${doing ?? 'Something went wrong'}: ${e instanceof Error ? e.message : String(e)}`;
  if (!toldWithHint(text)) {
    flash(text, 5000);
  }
}
