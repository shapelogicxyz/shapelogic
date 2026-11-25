// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tooltips the page draws itself. Since Chromium 140 (Electron 38) macOS
// shows a title's native tooltip on the first hover at most, often not at
// all (electron/electron#49843), so the control under the pointer has its
// title shown here instead: the title is held in data-hover-title while the
// pointer is on the control and goes back when the pointer leaves or
// presses. The held control keeps an empty title, not none: without one the
// native tooltip would show a titled ancestor's (the shape list's on a
// shape's button). A press hides the tip until the pointer leaves the
// control, as the native one does.
//
// The settings are read here, at the hover, so no page scan keeps titles
// to them: with "Show tooltips" off the title is still held (no native
// tip) and nothing is drawn; in Minimal density, which hides captions and
// helper text, a control that loses them and has no title of its own
// shows its caption (from docs/mockup/index.html: `svDensityTitles`).

import './tooltip.css';
import { prefs } from './prefs.ts';

const DELAY = 600;
/** A tip shown less than this long ago makes the next one show at once. */
const WARM = 400;
const GAP = 16;

let tip: HTMLDivElement | null = null;
/** The control whose title is held, null when none is. */
let held: HTMLElement | null = null;
/** The held control shows its caption, not a title of its own. */
let heldCaption = false;
/** The held caption control had a title attribute (an empty one) before. */
let hadTitle = false;
/** The pointer pressed on the held control: no tip until it leaves. */
let pressed = false;
let timer = 0;
let lastShown = 0;
let x = 0;
let y = 0;

function tipEl(): HTMLDivElement {
  if (tip === null) {
    tip = document.createElement('div');
    tip.className = 'hovertip';
    tip.setAttribute('aria-hidden', 'true');
    document.body.append(tip);
  }
  return tip;
}

function hide(): void {
  clearTimeout(timer);
  timer = 0;
  if (tip !== null && !tip.hidden) {
    tip.hidden = true;
    lastShown = performance.now();
  }
}

function show(): void {
  timer = 0;
  if (held === null || pressed || !held.isConnected || !prefs.tips) {
    return;
  }
  const t = tipEl();
  t.textContent = held.dataset.hoverTitle ?? '';
  t.hidden = false;
  // under the pointer, kept inside the window; above it when there is no room below
  const r = t.getBoundingClientRect();
  const left = Math.max(4, Math.min(x, innerWidth - r.width - 4));
  const top = y + GAP + r.height <= innerHeight - 4 ? y + GAP : Math.max(4, y - r.height - 6);
  t.style.left = `${String(left)}px`;
  t.style.top = `${String(top)}px`;
}

/** Gives the held control its title back (a title set while it was held wins). */
export function releaseTip(): void {
  hide();
  pressed = false;
  if (held === null) {
    return;
  }
  const h = held;
  held = null;
  const text = h.dataset.hoverTitle;
  delete h.dataset.hoverTitle;
  if (h.title !== '') {
    return;
  }
  if (!heldCaption) {
    if (text !== undefined) {
      h.title = text;
    }
  } else if (!hadTitle) {
    h.removeAttribute('title');
  }
}

/** The controls that lose their captions in Minimal density. */
const CAPTIONED = '.tbn,.rbtn,.gbtn,.abtn';

/** A control's caption, which Minimal density hides ('' for none). */
function caption(el: HTMLElement): string {
  let text: string | null | undefined;
  if (el.matches('.abtn')) {
    const sm = el.querySelector('small')?.textContent;
    text = (el.querySelector('b')?.textContent ?? '') + (sm === undefined ? '' : ' — ' + sm);
  } else {
    text = el.querySelector('span')?.textContent;
  }
  return (text ?? '').replace(/\s+/g, ' ').trim();
}

/** Holds a control's tip text: what it shows, and an empty title for the native tooltip. */
function hold(el: HTMLElement, text: string, isCaption: boolean): void {
  held = el;
  heldCaption = isCaption;
  hadTitle = el.hasAttribute('title');
  el.dataset.hoverTitle = text;
  el.setAttribute('title', '');
}

/** The tip of a control: its title, or in Minimal density its caption ('' for none). */
function tipOf(el: HTMLElement): { text: string; isCaption: boolean } {
  if (el.title !== '') {
    return { text: el.title, isCaption: false };
  }
  const minimal = document.documentElement.dataset.density === 'minimal';
  return { text: minimal && el.matches(CAPTIONED) ? caption(el) : '', isCaption: true };
}

function over(e: PointerEvent): void {
  x = e.clientX;
  y = e.clientY;
  const target = e.target instanceof Element ? e.target : null;
  // the nearest title is the one shown, an empty one shows none; in
  // Minimal density a captioned control counts as titled
  const minimal = document.documentElement.dataset.density === 'minimal';
  const el =
    target?.closest<HTMLElement>(
      minimal ? `[title],[data-hover-title],${CAPTIONED}` : '[title],[data-hover-title]',
    ) ?? null;
  if (el === held) {
    return;
  }
  releaseTip();
  if (el === null) {
    return;
  }
  const { text, isCaption } = tipOf(el);
  if (text === '') {
    return;
  }
  hold(el, text, isCaption);
  timer = window.setTimeout(show, performance.now() - lastShown < WARM ? 0 : DELAY);
}

/** Shows titles as tooltips drawn by the page. */
export function installTooltips(): void {
  document.addEventListener('pointerover', over, true);
  document.addEventListener(
    'pointermove',
    (e) => {
      x = e.clientX;
      y = e.clientY;
      // the view re-rendered the held control away
      if (held !== null && !held.isConnected) {
        releaseTip();
      }
    },
    true,
  );
  document.addEventListener(
    'pointerout',
    (e) => {
      if (e.relatedTarget === null) {
        releaseTip();
      }
    },
    true,
  );
  // a press gives the title back at once: what reads it next sees it in place
  document.addEventListener(
    'pointerdown',
    () => {
      const h = held;
      releaseTip();
      if (h !== null) {
        const { text, isCaption } = tipOf(h);
        if (text !== '') {
          hold(h, text, isCaption);
          pressed = true;
        }
      }
    },
    true,
  );
  for (const type of ['keydown', 'wheel', 'scroll']) {
    document.addEventListener(type, hide, { capture: true, passive: true });
  }
  window.addEventListener('blur', releaseTip);
}
