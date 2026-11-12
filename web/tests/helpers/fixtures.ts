// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fixtures the web tests go through one by one (the engine tests and the
// e2e tests): one puzzle of each grid, and the few whose content takes the
// front end down a path the others do not. Every file of tests/data/puzzles
// is checked against the original by the C++ tests and the oracle (its
// save, model, meshes and STL files), so a sweep here checks what the front
// end makes of a grid, which does not change from one puzzle to the next.

/** A fixture: its path under tests/data/puzzles and its grid (as the engine names it). */
export interface Fixture {
  file: string;
  grid: string;
}

/** One of each grid, then the files whose content differs where the front end looks. */
export const SWEEP: readonly Fixture[] = [
  { file: 'CubeInCage.xmpuzzle', grid: 'bricks' },
  { file: 'Bermuda.xmpuzzle', grid: 'prisms' },
  // three problems; written by ShapeLogic (plain XML, not gzip)
  { file: 'BallRoom_solved.xmpuzzle', grid: 'spheres' },
  { file: 'AugmentedSecondStellation.xmpuzzle', grid: 'rhombic' },
  { file: 'FourPieceTetrahedron.xmpuzzle', grid: 'tetraOcta' },
  { file: 'test_sliding_solver.xmpuzzle', grid: 'sliding' },
  { file: 'test_stacking_hanoi.xmpuzzle', grid: 'stacking' },
  // the grids the original does not have (decision 0023)
  { file: 'new-grids/HexSlab.xmpuzzle', grid: 'hexagonal' },
  { file: 'new-grids/CairoSlab.xmpuzzle', grid: 'cairo' },
  // constraint colours, and the largest mesh
  { file: 'DraculasDentalDesaster.xmpuzzle', grid: 'bricks' },
  // a piece range of 150 pieces, 26 shapes and 314 saved solutions
  { file: 'SolidSixPieceBurrs.xmpuzzle', grid: 'bricks' },
];

/** Every grid File ▸ New offers. */
export const GRIDS: readonly string[] = [...new Set(SWEEP.map((f) => f.grid))];

/** The file name a path ends in (what the title bar shows). */
export function fileName(file: string): string {
  return file.slice(file.lastIndexOf('/') + 1);
}
