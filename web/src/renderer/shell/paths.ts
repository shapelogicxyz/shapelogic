// SPDX-License-Identifier: GPL-3.0-or-later
//
// File names as the renderer shows and proposes them (cleanup item 12.6):
// a path's last part, and a puzzle's name without its .xmpuzzle, which the
// Save As, Export and Import dialogs build their names on. DOM-free.

/** A path's file name: its last part, by either separator. */
export function baseName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

/** A puzzle's name without its .xmpuzzle: the start of the names its exports propose. */
export function puzzleStem(name: string): string {
  return name.replace(/\.xmpuzzle$/i, '');
}
