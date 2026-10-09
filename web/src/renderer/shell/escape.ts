// SPDX-License-Identifier: GPL-3.0-or-later
//
// Esc steps back one thing at a time, in the order of docs/webui-spec.md
// section 6: close dialog → close menu → cancel rename → pause playback →
// (clear the box selection) → exit focus mode. From docs/mockup/index.html:
// the keydown handler's Escape branch. Each view registers its step; the
// first that had something to undo ends the press.

/** The order of the steps. */
export const ESC_ORDER = {
  dialog: 10,
  menu: 20,
  rename: 30,
  playback: 40,
  selection: 50,
  focus: 90,
} as const;

const steps: { order: number; step: () => boolean }[] = [];

/** Registers a step: it returns true when it stepped back. */
export function addEscapeStep(order: number, step: () => boolean): void {
  steps.push({ order, step });
  steps.sort((a, b) => a.order - b.order);
}

/** Runs the first step that has something to do; false when none had. */
export function stepBack(): boolean {
  return steps.some((s) => s.step());
}
