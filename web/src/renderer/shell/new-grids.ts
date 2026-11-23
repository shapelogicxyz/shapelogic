// SPDX-License-Identifier: GPL-3.0-or-later
//
// What File ▸ New says about each grid: its name, description, warning and
// example picture. From the original's src/gui/tutorials.cpp (`grids[]`,
// `selectorHtml`) and src/gui/images/examples/*.png (the pictures, copied
// to ./new-grid-images). Besides the grids, Tilted Square (decision 0045):
// a brick puzzle with a tray, which starts with the default tray (its
// picture drawn by files.ts) and is changed in Tilt & tolerance. DOM-free
// apart from the picture URLs.

import type { Grid } from '../../engine/api.generated.ts';
import { esc } from './dom.ts';
import { NEW_TRAY, slopeText } from '../state/tilted.ts';
import brick from './new-grid-images/brick.png?url';
import cairo from './new-grid-images/cairo.png?url';
import hexagonal from './new-grid-images/hexagonal.png?url';
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

// Spheres only: the other grids' pieces are polyhedra, and their take-apart
// comes from the cells' geometry (the engine's derived movement checks).
const noDisassembler =
  'This space grid has no disassembler. The solver finds every assembly, every way the pieces fill ' +
  'the result, but cannot check whether an assembly can be taken apart, or show how. The Solver ' +
  'tab offers Assemble Only; the Solutions count stays empty and the assemblies are what you get.';

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
    warning: null,
    pictureOf: 'Diagonal Cube by Stewart Coffin',
    picture: rhombic,
  },
  tetraOcta: {
    name: 'Tetrahedra-Octahedra',
    description:
      'Pieces are made of tetrahedra and octahedra, which together fill space. Good for pyramid, ' +
      'tetrahedron and octahedron shaped puzzles. A piece may use both kinds of cell.',
    warning: null,
    pictureOf: 'Four Piece Tetrahedron by Wayne Daniel',
    picture: tetraocta,
  },
  hexagonal: {
    name: 'Hexagonal Prism',
    description:
      'Pieces are made of hexagonal prisms, like a honeycomb stacked in layers. Each row of a layer ' +
      'sits half a cell to the right of the row below, so a shape\u2019s space is a slanted ' +
      'parallelogram of cells. New in ShapeLogic: BurrTools has no such grid.',
    warning: null,
    pictureOf: 'A honeycomb slab (an example shape)',
    picture: hexagonal,
  },
  cairo: {
    name: 'Cairo Prism',
    description:
      'Pieces are made of prisms on the Cairo pentagonal tiling: pentagons with five equal sides, ' +
      'four of them meeting at every other corner, in four orientations. Only the cells with x + y ' +
      'odd exist, and a shape keeps its look when moved by two cells diagonally or four along an ' +
      'axis. New in ShapeLogic: BurrTools has no such grid.',
    warning: null,
    pictureOf: 'A pentagon slab (an example shape)',
    picture: cairo,
  },
};

/** What New lists: a grid, or Tilted Square (a brick puzzle with a tray). */
export type NewKind = Grid | 'tilted';

/** What New says of Tilted Square. */
export const tiltedInfo: Readonly<Pick<GridInfo, 'name' | 'description'>> = {
  name: 'Tilted Square',
  description:
    'Flat pieces of squares in a square tray whose grid is turned: 1 down and 2 over, 1 down and 3 ' +
    'over, and so on. The cells the tray’s edge cuts hold no piece; the whole ones make the board, ' +
    'which the pieces must fit. Squares of the tray may be walls. An allowance lets a cell reach ' +
    'a little past the edge, as a real tray’s slack does. New in ShapeLogic.',
};

/** What New's example picture of Tilted Square shows: the tray a new puzzle starts with. */
export const tiltedExample = `A new tray: ${String(NEW_TRAY.size)} by ${String(NEW_TRAY.size)}, ${slopeText(NEW_TRAY.down, NEW_TRAY.over)} (change it in Tilt & tolerance)`;

/** A kind's name in the list. */
export function kindName(kind: NewKind): string {
  return kind === 'tilted' ? tiltedInfo.name : gridInfo[kind].name;
}

/** The dialog's body: the list of kinds on the left, the information on the right. */
export function newGridBody(kinds: readonly NewKind[]): string {
  const items = kinds
    .map(
      (g, i) =>
        `<button class="ng-item" data-grid="${g}" aria-selected="false"${i === 0 ? ' data-focus' : ''}>${esc(kindName(g))}</button>`,
    )
    .join('');
  return `<div class="ng"><div class="ng-list" role="listbox" aria-label="Space grid" title="The kind of puzzle: the grid its shapes are made on">${items}</div><div class="ng-info" id="newInfo"></div></div>`;
}

/**
 * The right side for Tilted Square, as for a grid: the description and an
 * example, a picture of the tray a new puzzle starts with, which files.ts
 * draws from tray.preview into #ngtPic. Its settings are changed after,
 * in Tilt & tolerance.
 */
export function newTiltedHtml(): string {
  return (
    `<p class="ng-desc"><b>Description:</b><br>${esc(tiltedInfo.description)}</p>` +
    `<p class="ng-ex"><b>Example:</b><br>${esc(tiltedExample)}</p>` +
    '<div class="ng-pic ngt-pic" id="ngtPic" role="img" aria-label="' +
    esc(tiltedExample) +
    '"></div>'
  );
}

/** What the right side says about a grid: description, warning, example. */
export function newGridInfoHtml(kind: NewKind): string {
  if (kind === 'tilted') {
    return newTiltedHtml();
  }
  const g = gridInfo[kind];
  const warning =
    g.warning === null ? '' : `<p class="ng-warn"><b>Warning:</b><br>${esc(g.warning)}</p>`;
  return (
    `<p class="ng-desc"><b>Description:</b><br>${esc(g.description)}</p>${warning}` +
    `<p class="ng-ex"><b>Example:</b><br>${esc(g.pictureOf)}</p>` +
    `<img class="ng-pic" src="${esc(g.picture)}" alt="${esc(g.pictureOf)}">`
  );
}
