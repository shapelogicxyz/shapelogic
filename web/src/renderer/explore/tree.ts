// SPDX-License-Identifier: GPL-3.0-or-later
//
// The movement explorer's tree as the client keeps it: the positions the
// engine returned (explore.openMovements' root, explore.analyse's,
// addMove's and findMatching's nodes), each under its parent in the order
// the engine added it, and Prune Tree as the engine does it (only the path
// from the root to a node is kept, that node's children go too). DOM-free,
// so the unit tests and the engine tests drive it. From
// src/gui/movementbrowser.cpp (the Fl_Tree of addNode and cb_Prune).

import type { MovementNode } from '../../engine/api.generated.ts';

/** A row of the tree as drawn: a node and how deep it is (the root 0). */
export interface TreeRow {
  node: MovementNode;
  depth: number;
}

export class MovementTree {
  readonly root: number;
  private readonly nodes = new Map<number, MovementNode>();

  constructor(root: MovementNode) {
    this.root = root.node;
    this.nodes.set(root.node, root);
  }

  /** A node, if it is in the tree. */
  get(id: number): MovementNode | undefined {
    return this.nodes.get(id);
  }

  /** How many nodes the tree holds. */
  get size(): number {
    return this.nodes.size;
  }

  /**
   * Puts in nodes the engine returned: new ones go under their parent (the
   * engine added them to its children), known ones are replaced.
   */
  put(nodes: readonly MovementNode[]): void {
    for (const n of nodes) {
      this.nodes.set(n.node, n);
      const parent = this.nodes.get(n.parent);
      if (parent !== undefined && n.node !== parent.node && !parent.children.includes(n.node)) {
        this.nodes.set(parent.node, { ...parent, children: [...parent.children, n.node] });
      }
    }
  }

  /** The nodes from the root to `id` (empty when it is not in the tree). */
  path(id: number): number[] {
    const out: number[] = [];
    let n = this.nodes.get(id);
    while (n !== undefined) {
      out.unshift(n.node);
      if (n.node === n.parent) {
        return out;
      }
      n = this.nodes.get(n.parent);
    }
    return [];
  }

  /** Prune Tree: only the path from the root to `id` stays, without the children of `id`. */
  prune(id: number): void {
    const keep = new Set(this.path(id));
    if (keep.size === 0) {
      return;
    }
    for (const k of [...this.nodes.keys()]) {
      if (!keep.has(k)) {
        this.nodes.delete(k);
      }
    }
    for (const [k, n] of this.nodes) {
      this.nodes.set(k, { ...n, children: n.children.filter((c) => keep.has(c)) });
    }
  }

  /** The rows as the tree is drawn: depth first, children in the engine's order. */
  rows(): TreeRow[] {
    const out: TreeRow[] = [];
    const visit = (id: number, depth: number): void => {
      const n = this.nodes.get(id);
      if (n === undefined) {
        return;
      }
      out.push({ node: n, depth });
      for (const c of n.children) {
        if (c !== id) {
          visit(c, depth + 1);
        }
      }
    };
    visit(this.root, 0);
    return out;
  }
}

/**
 * The move an Alt-drag on the screen asks for: of the grid's directions
 * (each forward and reversed, as they look on the screen when the piece
 * moves one step), the one closest in angle to the drag, if any is within
 * 45 degrees. Returns the index into `onScreen` and whether it is reversed.
 */
export function dragDirection(
  drag: readonly [number, number],
  onScreen: readonly (readonly [number, number])[],
): { direction: number; reverse: boolean } | null {
  const len = Math.hypot(drag[0], drag[1]);
  if (len === 0) {
    return null;
  }
  let best: { direction: number; reverse: boolean; cos: number } | null = null;
  onScreen.forEach((v, i) => {
    const l = Math.hypot(v[0], v[1]);
    if (l < 1e-6) {
      return;
    }
    const cos = (drag[0] * v[0] + drag[1] * v[1]) / (len * l);
    for (const [c, reverse] of [
      [cos, false],
      [-cos, true],
    ] as const) {
      if (best === null || c > best.cos) {
        best = { direction: i, reverse, cos: c };
      }
    }
  });
  const b = best as { direction: number; reverse: boolean; cos: number } | null;
  return b !== null && b.cos >= Math.SQRT1_2
    ? { direction: b.direction, reverse: b.reverse }
    : null;
}
