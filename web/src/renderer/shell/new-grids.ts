// SPDX-License-Identifier: GPL-3.0-or-later
//
// What File ▸ New says about each grid: its name, description, warning and
// example picture. From the original's src/gui/tutorials.cpp (`grids[]`,
// `selectorHtml`) and src/gui/images/examples/*.png (the pictures, copied
// to ./new-grid-images). DOM-free apart from the picture URLs.

import type { Grid } from '../../engine/api.generated.ts';
import { esc } from './dom.ts';
import brick from './new-grid-images/brick.png?url';
import prism from './new-grid-images/prism.png?url';
import rhombic from './new-grid-images/rhombic.png?url';
import sliding from './new-grid-images/sliding.png?url';
import spheres from './new-grid-images/spheres.png?url';
import stacking from './new-grid-images/stacking.png?url';
import tetraocta from './new-grid-images/tetraocta.png?url';

/** What New shows for one grid. */
export interface GridInfo {
  /** The name in the list. */
  name: string;
  description: string;
  /** A warning, or null. */
  warning: string | null;
  /** What the example picture shows. */
  pictureOf: string;
  /** The example picture's URL. */
  picture: string;
}

const noDisassembler =
  'This space grid has no disassembler. The solver finds every assembly, every way the pieces fill ' +
  'the result, but cannot check whether an assembly can be taken apart, or show how. Untick ' +
  'Disassemble on the Solver tab before solving; the Solutions count then stays empty and the ' +
  'assemblies are what you get.';

/** Every grid's information, by grid. */
export const gridInfo: Readonly<Record<Grid, GridInfo>> = {
  bricks: {
    name: 'Brick',
    description:
      'Pieces are made of cubes. This is the usual interlocking burr or packing puzzle: ShapeLogic finds ' +
      'every way the pieces fill the result shape, then works out which of those can really be taken ' +
      'apart, and how.',
    warning: null,
    pictureOf: 'Cube in Cage by Mineyuki Uyematsu',
    picture: brick,
  },
  sliding: {
    name: 'Sliding',
    description:
      'Pieces are flat shapes in a tray. Mark where each piece starts and where it must end, and the ' +
      'solver finds the fewest moves that slide them from start to goal, one piece at a time. Pieces ' +
      'can carry others nested inside them when that is allowed.',
    warning: null,
    pictureOf: 'Sliding puzzle by GiiKER Super Slide',
    picture: sliding,
  },
  stacking: {
    name: 'Stacking',
    description:
      'Pieces are discs on vertical rods. A move takes the top disc of one rod and puts it on another, ' +
      'as in the Tower of Hanoi or Panex. Rules such as no large disc on a smaller one are set for each ' +
      'rod set, and the solver finds the fewest moves from the start stacks to the goal stacks.',
    warning: null,
    pictureOf: 'Panex Level 8 by Toshio Akanuma',
    picture: stacking,
  },
  prisms: {
    name: 'Triangular Prism',
    description:
      'Like Brick, with a different cell. Each cell is a prism with a triangular base, and the cells of ' +
      'a layer are triangles, half pointing up and half pointing down. Layers stack on top of one ' +
      'another. Pieces are found, and taken apart, as for Brick.',
    warning: null,
    pictureOf: 'Prisgon by Markus Götz',
    picture: prism,
  },
  spheres: {
    name: 'Spheres',
    description:
      'Pieces are made of balls. The balls sit in the tightest packing there is, each layer resting in ' +
      'the hollows of the one below, as oranges are stacked at a market. Good for pyramid and ball ' +
      'packing puzzles.',
    warning: noDisassembler,
    pictureOf: 'Ball Room by Stewart Coffin',
    picture: spheres,
  },
  rhombic: {
    name: 'Rhombic',
    description:
      'Pieces are made of small tetrahedra cut from cubes, so that they build rhombic dodecahedra, ' +
      'stellations and other shapes with faces that are not square.',
    warning: noDisassembler,
    pictureOf: 'Diagonal Cube by Stewart Coffin',
    picture: rhombic,
  },
  tetraOcta: {
    name: 'Tetrahedra-Octahedra',
    description:
      'Pieces are made of tetrahedra and octahedra, which together fill space. Good for pyramid, ' +
      'tetrahedron and octahedron shaped puzzles. A piece may use both kinds of cell.',
    warning: noDisassembler,
    pictureOf: 'Four Piece Tetrahedron by Wayne Daniel',
    picture: tetraocta,
  },
};

/** The dialog's body: the list of grids on the left, the information on the right. */
export function newGridBody(grids: readonly Grid[]): string {
  const items = grids
    .map(
      (g, i) =>
        `<button class="ng-item" data-grid="${g}" aria-selected="false"${i === 0 ? ' data-focus' : ''}>${esc(gridInfo[g].name)}</button>`,
    )
    .join('');
  return `<div class="ng"><div class="ng-list" role="listbox" aria-label="Space grid" title="The kind of puzzle: the grid its shapes are made on">${items}</div><div class="ng-info" id="newInfo"></div></div>`;
}

/** What the right side says about a grid: description, warning, example. */
export function newGridInfoHtml(grid: Grid): string {
  const g = gridInfo[grid];
  const warning =
    g.warning === null ? '' : `<p class="ng-warn"><b>Warning:</b><br>${esc(g.warning)}</p>`;
  return (
    `<p class="ng-desc"><b>Description:</b><br>${esc(g.description)}</p>${warning}` +
    `<p class="ng-ex"><b>Example:</b><br>${esc(g.pictureOf)}</p>` +
    `<img class="ng-pic" src="${esc(g.picture)}" alt="${esc(g.pictureOf)}">`
  );
}
