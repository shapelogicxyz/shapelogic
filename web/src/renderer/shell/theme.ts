// SPDX-License-Identifier: GPL-3.0-or-later
//
// Theme, density and tooltips. From docs/mockup/index.html: `applyTheme`
// (data-theme on the document; System follows the operating system),
// `isDarkTheme`, `applyDensity` (data-density; Minimal hides captions, so
// `svDensityTitles` gives every such control its caption as a tooltip),
// and the "Show tooltips" setting (titles held back while it is off). The
// tips themselves are drawn by the page (tooltip.ts).

import { $$ } from './dom.ts';
import { onPrefs, prefs } from './prefs.ts';
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

/** Applies theme, density and tooltips now and whenever they change. */
export function installTheme(): void {
  installTooltips();
  applyTheme();
  applyDensity();
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
    if (changed.includes('tips')) {
      applyTips();
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
