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

const DELAY = 600;
/** A tip shown less than this long ago makes the next one show at once. */
const WARM = 400;
const GAP = 16;

let tip: HTMLDivElement | null = null;
/** The control whose title is held, null when none is. */
let held: HTMLElement | null = null;
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
  if (held === null || pressed || !held.isConnected) {
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
  if (h.title === '' && text !== undefined) {
    h.title = text;
  }
}

function over(e: PointerEvent): void {
  x = e.clientX;
  y = e.clientY;
  const target = e.target instanceof Element ? e.target : null;
  // the nearest title is the one shown, an empty one shows none
  const el = target?.closest<HTMLElement>('[title],[data-hover-title]') ?? null;
  if (el === held) {
    return;
  }
  releaseTip();
  if (el === null || el.title === '') {
    return;
  }
  held = el;
  el.dataset.hoverTitle = el.title;
  el.setAttribute('title', '');
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
      if (h !== null && h.title !== '') {
        held = h;
        pressed = true;
        h.dataset.hoverTitle = h.title;
        h.setAttribute('title', '');
      }
    },
    true,
  );
  for (const type of ['keydown', 'wheel', 'scroll']) {
    document.addEventListener(type, hide, { capture: true, passive: true });
  }
  window.addEventListener('blur', releaseTip);
}
