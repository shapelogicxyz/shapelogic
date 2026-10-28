// SPDX-License-Identifier: GPL-3.0-or-later
//
// Theme, density and tooltips. From docs/mockup/index.html: `applyTheme`
// (data-theme on the document; System follows the operating system),
// `isDarkTheme`, `applyDensity` (data-density; Minimal hides captions, so
// `svDensityTitles` gives every such control its caption as a tooltip),
// the font size (data-font: the stylesheet's --fs step on every text size),
// and the "Show tooltips" setting (titles held back while it is off). The
// tips themselves are drawn by the page (tooltip.ts). Also the side cards'
// widths (the "Sidebar width" setting): the --lwx and --rwx the stylesheet
// lays the stage out with, set on the document over the density's.

import { $$ } from './dom.ts';
import { DEFAULTS, onPrefs, prefs, SIDE_MAX } from './prefs.ts';
import { setBoardColour } from './piece-colours.ts';
import { installTooltips, releaseTip } from './tooltip.ts';

const themeListeners = new Set<() => void>();

/** Listens to the theme's changes (the 3D view reads its colours again). */
export function onTheme(listener: () => void): () => void {
  themeListeners.add(listener);
  return () => {
    themeListeners.delete(listener);
  };
}

function themeChanged(): void {
  // a board with no colour of its own takes the theme's
  setBoardColour(isDarkTheme() ? prefs.boardColourDark : prefs.boardColourLight);
  for (const l of [...themeListeners]) {
    l();
  }
}

/** Whether the dark colours are in use. */
export function isDarkTheme(): boolean {
  const t = document.documentElement.dataset.theme;
  return t === 'dark' || (t === undefined && matchMedia('(prefers-color-scheme: dark)').matches);
}

/** Applies the theme setting. */
export function applyTheme(): void {
  const r = document.documentElement;
  if (prefs.theme === 'Light') {
    r.dataset.theme = 'light';
  } else if (prefs.theme === 'Dark') {
    r.dataset.theme = 'dark';
  } else {
    delete r.dataset.theme;
  }
  themeChanged();
}

/** The window width from which Variable side cards grow. */
const GROW_FROM = 1600;

/** Applies the sidebar width setting. */
export function applySidebars(): void {
  const r = document.documentElement.style;
  if (prefs.sidebar === 'Static') {
    r.setProperty('--lwx', `${String(prefs.sideL)}px`);
    r.setProperty('--rwx', `${String(prefs.sideR)}px`);
    return;
  }
  // each card takes a third of the width past GROW_FROM (the 3D view the
  // last third) until it reaches SIDE_MAX
  const grow = (base: number): string =>
    `clamp(${String(base)}px, calc(${String(base)}px + (100vw - ${String(GROW_FROM)}px) / 3), ${String(SIDE_MAX)}px)`;
  r.setProperty('--lwx', grow(DEFAULTS.sideL));
  r.setProperty('--rwx', grow(DEFAULTS.sideR));
}

/** Applies the main tab bar location setting. */
export function applyTabsAt(): void {
  document.documentElement.dataset.tabs = prefs.tabsAt.toLowerCase();
}

/** Applies the font size setting. */
export function applyFontSize(): void {
  document.documentElement.dataset.font = prefs.fontSize.toLowerCase();
}

/** Applies the density setting. */
export function applyDensity(): void {
  document.documentElement.dataset.density = prefs.density.toLowerCase();
  densityTitles();
  themeChanged();
}

// Minimal hides captions and helper text: every control that loses them
// gets them as its tooltip (svDensityTitles)
function densityTitles(): void {
  if (document.documentElement.dataset.density !== 'minimal') {
    return;
  }
  const set = (el: HTMLElement, text: string | null | undefined): void => {
    if (
      el.title === '' &&
      el.dataset.tip === undefined &&
      el.dataset.hoverTitle === undefined &&
      text !== null &&
      text !== undefined
    ) {
      const t = text.replace(/\s+/g, ' ').trim();
      if (t !== '') {
        el.title = t;
      }
    }
  };
  for (const b of $$('.tbn,.rbtn,.gbtn')) {
    set(b, b.querySelector('span')?.textContent);
  }
  for (const b of $$('.abtn')) {
    const bb = b.querySelector('b')?.textContent ?? '';
    const sm = b.querySelector('small')?.textContent;
    set(b, bb + (sm === undefined ? '' : ' — ' + sm));
  }
}

// "Show tooltips" off: titles move to data-tip, and back when it is on
function applyTips(root: ParentNode = document): void {
  releaseTip();
  if (prefs.tips) {
    for (const el of $$('[data-tip]', root)) {
      el.title = el.dataset.tip ?? '';
      delete el.dataset.tip;
    }
  } else {
    for (const el of $$('[title]', root)) {
      if (el.title !== '') {
        el.dataset.tip = el.title;
      }
      el.removeAttribute('title');
    }
  }
}

/** Applies theme, density, font size, tooltips, the side cards' widths and the tab bar's place now and whenever they change. */
export function installTheme(): void {
  installTooltips();
  applyTheme();
  applyDensity();
  applyFontSize();
  applySidebars();
  applyTabsAt();
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    if (prefs.theme === 'System') {
      themeChanged();
    }
  });
  onPrefs((changed) => {
    if (changed.includes('theme')) {
      applyTheme();
    }
    if (changed.includes('density')) {
      applyDensity();
    }
    if (changed.includes('boardColourLight') || changed.includes('boardColourDark')) {
      themeChanged();
    }
    if (changed.includes('fontSize')) {
      applyFontSize();
    }
    if (changed.includes('tips')) {
      applyTips();
    }
    if (changed.includes('tabsAt')) {
      applyTabsAt();
    }
    if (changed.some((k) => k === 'sidebar' || k === 'sideL' || k === 'sideR')) {
      applySidebars();
    }
  });
  // views re-render their parts: keep the new parts to the settings
  let queued = 0;
  new MutationObserver(() => {
    if (queued !== 0) {
      return;
    }
    queued = requestAnimationFrame(() => {
      queued = 0;
      densityTitles();
      if (!prefs.tips) {
        applyTips();
      }
    });
  }).observe(document.body, { childList: true, subtree: true, attributeFilter: ['title'] });
}
