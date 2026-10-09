// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings of docs/webui-spec.md section 1 as the client holds them
// (the mockup's `st.set`). Undo depth and threads are the engine's
// (settings.set; the store mirrors them); the rest are the client's,
// kept in memory until stage 6 stores them.

/** The settings and their values. */
export interface Prefs {
  density: 'Standard' | 'Minimal';
  theme: 'Light' | 'Dark' | 'System';
  undo: number;
  tips: boolean;
  cube: boolean;
  rev: boolean;
  rot: 'Drag' | 'Arc-ball';
  lights: boolean;
  fade: boolean;
  /** Worker threads; 0 until the engine said (its 0 is every hardware thread). */
  threads: number;
}

/** The defaults (the engine's for undo depth and threads). */
export const DEFAULTS: Readonly<Prefs> = {
  density: 'Standard',
  theme: 'System',
  undo: 25,
  tips: true,
  cube: true,
  rev: false,
  rot: 'Drag',
  lights: true,
  fade: true,
  threads: 0,
};

/** The settings now. */
export const prefs: Prefs = { ...DEFAULTS };

const listeners = new Set<(changed: (keyof Prefs)[]) => void>();

/** Changes settings and tells the listeners which. */
export function setPrefs(change: Partial<Prefs>): void {
  const changed: (keyof Prefs)[] = [];
  for (const [k, v] of Object.entries(change) as [keyof Prefs, Prefs[keyof Prefs]][]) {
    if (prefs[k] !== v) {
      (prefs as unknown as Record<string, unknown>)[k] = v;
      changed.push(k);
    }
  }
  if (changed.length > 0) {
    for (const l of [...listeners]) {
      l(changed);
    }
  }
}

/** Listens to changes of the settings; returns the unsubscribe. */
export function onPrefs(listener: (changed: (keyof Prefs)[]) => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
