// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which tool a press uses and where it goes, without a page: the quick
// tools of the modifiers in the 2D grid and in the 3D view, and the cube
// grid's faces as directions (the cell across a face, the client's table:
// VoxelBricks::getNeighbor, faces 0 to 5: -x, -y, -z, +x, +y, +z). From
// docs/mockup/index.html: `eff2d`, `actFor`, `mods`.

import type { CellTool as Tool, Size } from '../../engine/api.generated.ts';
import type { Cell } from './editor-state.ts';

/** The modifier keys of a press or a hover. */
export interface Mods {
  ctrl: boolean;
  shift: boolean;
  alt: boolean;
}

/** The modifiers of an event (Cmd counts as Alt, as the mockup's `mods`). */
export function modsOf(e: {
  ctrlKey: boolean;
  shiftKey: boolean;
  altKey: boolean;
  metaKey: boolean;
}): Mods {
  return { ctrl: e.ctrlKey, shift: e.shiftKey, alt: e.altKey || e.metaKey };
}

/** The tool a press in the 2D grid uses: a modifier's quick tool, else the active one (the mockup's eff2d). */
export function effectiveTool(m: Mods, tool: Tool): Tool {
  return m.ctrl ? 'erase' : m.alt ? 'variable' : m.shift ? 'fixed' : tool;
}

/** The tool a click in the 3D view uses: a modifier's quick tool, else the active one in edit mode (the mockup's actFor). */
export function actFor(m: Mods, edit3d: boolean, tool: Tool): Tool | null {
  if (m.ctrl) {
    return 'erase';
  }
  if (m.alt) {
    return 'variable';
  }
  if (m.shift) {
    return 'fixed';
  }
  return edit3d ? tool : null;
}

/** The brick grid's faces as directions (VoxelBricks::getNeighbor, faces). */
export const BRICK_FACES: readonly Cell[] = [
  [-1, 0, 0],
  [0, -1, 0],
  [0, 0, -1],
  [1, 0, 0],
  [0, 1, 0],
  [0, 0, 1],
];

/** The cell a cube grid's face points at, or null outside the space. */
export function brickNeighbour(cell: Cell, face: number, size: Size): Cell | null {
  const d = BRICK_FACES[face];
  if (d === undefined) {
    return null;
  }
  const n: Cell = [cell[0] + d[0], cell[1] + d[1], cell[2] + d[2]];
  return n.every((v, i) => v >= 0 && v < (size[i] ?? 1)) ? n : null;
}
