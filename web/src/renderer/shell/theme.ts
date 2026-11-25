// SPDX-License-Identifier: GPL-3.0-or-later
//
// Theme, density and tooltips. From docs/mockup/index.html: `applyTheme`
// (data-theme on the document; System follows the operating system),
// `isDarkTheme`, `applyDensity` (data-density; Minimal hides captions,
// which tooltip.ts then shows as the controls' tips) and the font size
// (data-font: the stylesheet's --fs step on every text size). The tips,
// and the "Show tooltips" setting, are tooltip.ts's: it reads the settings
// at each hover. Also the side cards'
// widths (the "Sidebar width" setting): the --lwx and --rwx the stylesheet
// lays the stage out with, set on the document over the density's. And
// the top bar's look (the "Top bar theme" setting: data-topbar).

import { DEFAULTS, onPrefs, prefs, SIDE_MAX } from './prefs.ts';
import { setBoardColor } from './piece-colors.ts';
import { installTooltips, releaseTip } from './tooltip.ts';

const themeListeners = new Set<() => void>();

/** Listens to the theme's changes (the 3D view reads its colors again). */
export function onTheme(listener: () => void): () => void {
  themeListeners.add(listener);
  return () => {
    themeListeners.delete(listener);
  };
}

function themeChanged(): void {
  // a board with no color of its own takes the theme's
  setBoardColor(isDarkTheme() ? prefs.boardColorDark : prefs.boardColorLight);
  for (const l of [...themeListeners]) {
    l();
  }
}

/** Whether the dark colors are in use. */
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

/** How much wider the right side card is than the left in Variable (and the defaults in Static). */
const RIGHT_WIDER = 1.1;

/** Applies the sidebar width setting. */
export function applySidebars(): void {
  const r = document.documentElement.style;
  if (prefs.sidebar === 'Static') {
    r.setProperty('--lwx', `${String(prefs.sideL)}px`);
    r.setProperty('--rwx', `${String(prefs.sideR)}px`);
    return;
  }
  // each card takes a third of the width past GROW_FROM (the 3D view the
  // last third) until it reaches SIDE_MAX; the right card is the left's
  // width made RIGHT_WIDER, at every window width
  const grow = (base: number): string =>
    `clamp(${String(base)}px, calc(${String(base)}px + (100vw - ${String(GROW_FROM)}px) / 3), ${String(SIDE_MAX)}px)`;
  r.setProperty('--lwx', grow(DEFAULTS.sideL));
  r.setProperty('--rwx', `calc(${grow(DEFAULTS.sideL)} * ${String(RIGHT_WIDER)})`);
}

/** Applies the main tab bar location setting. */
export function applyTabsAt(): void {
  document.documentElement.dataset.tabs = prefs.tabsAt.toLowerCase();
}

/** Applies the top bar theme setting. */
export function applyTopBar(): void {
  document.documentElement.dataset.topbar = prefs.topBar.toLowerCase();
}

/** Applies the font size setting. */
export function applyFontSize(): void {
  document.documentElement.dataset.font = prefs.fontSize.toLowerCase();
}

/** Applies the density setting. */
export function applyDensity(): void {
  releaseTip();
  document.documentElement.dataset.density = prefs.density.toLowerCase();
  themeChanged();
}

/** Applies theme, density, font size, tooltips, the side cards' widths, the tab bar's place and the top bar's look now and whenever they change. */
export function installTheme(): void {
  installTooltips();
  applyTheme();
  applyDensity();
  applyFontSize();
  applySidebars();
  applyTabsAt();
  applyTopBar();
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
    if (changed.includes('boardColorLight') || changed.includes('boardColorDark')) {
      themeChanged();
    }
    if (changed.includes('fontSize')) {
      applyFontSize();
    }
    if (changed.includes('tips')) {
      releaseTip();
    }
    if (changed.includes('tabsAt')) {
      applyTabsAt();
    }
    if (changed.includes('topBar')) {
      applyTopBar();
    }
    if (changed.some((k) => k === 'sidebar' || k === 'sideL' || k === 'sideR')) {
      applySidebars();
    }
  });
}
