// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generated from api/*.json by web/tools/gen-api.ts: do not edit. `just app-gen`
// writes it; a unit test fails when it is stale.
//
// Every $defs entry is a named type: types.json's under their own names, an area
// file's too unless another file defines the same name (then the area's name goes
// in front: shape.json's Made is ShapeMade). Methods and Events map each name to
// its params, result and errors. Constraints the types cannot say (patterns,
// ranges) are in the doc comments.

// ---- engine.json: Engine and settings ----

export interface Settings {
  /**
   * Undo steps kept (default 25).
   * @minimum 1
   */
  undoDepth: number;
  /**
   * The threads of a solve when its options do not say; 0 every hardware thread (default).
   * @minimum 0
   * @maximum 256
   */
  threads: number;
  /** The solver when a solve's or analysis's options do not say (default classic). */
  solver: Solver;
  /**
   * Where a sliding search keeps its older levels while it runs, in a folder of its own that goes
   * when it ends (the original's SHAPELOGIC_SLIDE_DIR); empty keeps them in memory. Default: the
   * user's cache folder's sliding folder.
   */
  slideFolder: string;
  /**
   * The disk those levels may take, in GB (SHAPELOGIC_SLIDE_DISK_GB); 0, the default: half the
   * free space, at most 200 GB. More than the disk has room for is held to its free space but a
   * reserve (2 GB or a twentieth of it). On macOS the free space counts what the system frees on
   * demand, as Finder does.
   * @minimum 0
   */
  slideDiskGB: number;
  /**
   * Where a stacking solve's Panex search saves itself, in a folder per puzzle (the original's
   * SHAPELOGIC_PANEX_DIR): a paused stacking solve is its saved search there, which solve.continue
   * carries on (also after the engine or the document ended), solve.start and solve.abort throw
   * away, solve.status describes, and the paused solve's export and import carry. Empty saves
   * nothing (a search that outgrows its memory then finds its path by solving halves again).
   * Default: the user's cache folder's panex folder.
   */
  panexFolder: string;
  /**
   * The disk a saved search's levels may take, in GB (SHAPELOGIC_PANEX_DISK_GB); 0, the default:
   * half the free space, at most 200 GB. The fuller it gets, the fewer levels are kept and the
   * longer finding the path at the end takes. More than the disk has room for is held to its free
   * space but a reserve, as slideDiskGB is.
   * @minimum 0
   */
  panexDiskGB: number;
  /**
   * The memory a sliding or stacking search may fill, in GB; 0, the default: half the machine's
   * memory (engine.info's memoryBytes), at least 2 GB. It decides every solve's memory: a solve's
   * highMemory plays no part, and a note of a search that ran out of memory or disk names Settings
   * ▸ Solver.
   * @minimum 0
   */
  searchMemoryGB: number;
}

// ---- explore.json: Explorers ----

export interface NodeParam {
  handle: MovementExplorerHandle;
  /** @minimum 0 */
  node: number;
}

// ---- export.json: Exports ----

/**
 * One parameter of a grid's STL mesher (stlExporter_c's getParameterName, getParameterType,
 * getParameter of a new exporter, getParameterTooltip).
 */
export interface StlParameter {
  /**
   * The original's label: Unit Size, Unit Size Y, Unit Size Z, Bevel, Offset, Interior chamfers;
   * on the sphere grid Sphere radius, Connection radius, Curvature radius, Offset, Recursions,
   * Inner radius, Hole diameter, Square Hole.
   */
  name: string;
  /**
   * The field: any number, a number of 0 or more, a whole number of 0 or more, or a check box (1
   * on, 0 off).
   */
  type: 'number' | 'positiveNumber' | 'positiveInteger' | 'switch';
  default: number;
  tooltip: string;
}

/**
 * One value per parameter of the document's grid, in export.stlParameters' order (another count:
 * invalid_argument). Default: the defaults.
 * @maxItems 16
 */
export type StlValues = number[];

// ---- playback.json: Playback ----

export interface Poses {
  /** @minimum 0 */
  samples: number;
  /** @minimum 0 */
  pieces: number;
  frame: Frame;
}

// ---- problem.json: Problems ----

export interface ProblemProblemParam {
  problem: ProblemId;
}

export interface ProblemShape {
  problem: ProblemId;
  shape: ShapeId;
}

export interface ProblemMade {
  problem: ProblemId;
  revision: Revision;
}

// ---- shape.json: Shapes and cells ----

export interface ShapeParam {
  shape: ShapeId;
}

export interface ShapeMade {
  shape: ShapeId;
  revision: Revision;
}

export interface CellsChanged {
  revision: Revision;
  /**
   * Cells changed; 0 leaves no undo step.
   * @minimum 0
   */
  changed: number;
}

// ---- sliding.json: Sliding puzzles ----

/**
 * A cell [x, y, z] of a start/goal shape, y up; [x, y] is on its first layer (z = 0).
 * @minItems 2
 * @maxItems 3
 */
export type TrayCell = number[];

/**
 * A size [x, y, z] in cells; [x, y] is one layer.
 * @minItems 2
 * @maxItems 3
 */
export type TraySize = number[];

// ---- solution.json: Solutions ----

export interface SolutionParam {
  problem: ProblemId;
  solution: SolutionId;
}

export interface AnalyseProgress {
  active: boolean;
  /** @minimum 0 */
  total: number;
  /** @minimum 0 */
  done: number;
  /**
   * Taken apart so far (the others do not come apart).
   * @minimum 0
   */
  separated: number;
  /** Ended before it was through (stopped, or by an internal error). */
  stopped: boolean;
  /** What an internal error on the analysis's thread said; what was done before it stays. */
  failure: string | null;
  current: TakeApartProgress | null;
}

export interface AnalyseEvent {
  problem: ProblemId;
  /** @minimum 0 */
  total: number;
  /** @minimum 0 */
  done: number;
  /** @minimum 0 */
  separated: number;
  /** Ended before it was through (stopped, or by an internal error). */
  stopped: boolean;
  /** What an internal error on the analysis's thread said; null otherwise. */
  failure: string | null;
}

// ---- solve.json: Solving ----

export interface SolveProblemParam {
  problem: ProblemId;
}

export interface Launch {
  problem: ProblemId;
  options?: SolveOptions;
}

// ---- stacking.json: Stacking puzzles ----

/** A rod set's rules to change (RodSet's fields; absent ones stay). */
export interface Rules {
  /**
   * @minimum 1
   * @maximum 16
   */
  rodCount?: number;
  growHeight?: boolean;
  /**
   * @minimum 1
   * @maximum 64
   */
  definedHeight?: number;
  sizeMatters?: boolean;
  distanceMatters?: boolean;
  panexColumns?: boolean;
  pocketColumn?: boolean;
  /**
   * @minimum 1
   * @maximum 64
   */
  pocketHeight?: number;
  outsideChannel?: boolean;
}

export interface Rod {
  /** 'rod 2' or 'the pocket column', as the original's messages name it. */
  name: string;
  pocket: boolean;
  /** Where it stands along the board: rod r at r, the pocket at -1. */
  position: number;
  /**
   * The discs it holds: a Panex column one more than its height.
   * @minimum 0
   */
  capacity: number;
}

// ---- types.json: Shared definitions ----

/**
 * A shape, stable while the document is open.
 * @pattern ^s[1-9][0-9]*$
 */
export type ShapeId = string;

/**
 * A constraint colour.
 * @pattern ^c[1-9][0-9]*$
 */
export type ColourId = string;

/**
 * A problem; also names its solve and its analysis.
 * @pattern ^p[1-9][0-9]*$
 */
export type ProblemId = string;

/**
 * A stacking puzzle's rod set (a board), stable while the document is open.
 * @pattern ^r[1-9][0-9]*$
 */
export type RodSetId = string;

/**
 * A saved solution, unique within its problem.
 * @pattern ^sol[1-9][0-9]*$
 */
export type SolutionId = string;

/**
 * A playback opened by playback.open.
 * @pattern ^pb[1-9][0-9]*$
 */
export type PlaybackHandle = string;

/**
 * An assembly explorer opened by explore.openAssembly.
 * @pattern ^ax[1-9][0-9]*$
 */
export type AssemblyExplorerHandle = string;

/**
 * A movement explorer opened by explore.openMovements.
 * @pattern ^mx[1-9][0-9]*$
 */
export type MovementExplorerHandle = string;

/**
 * The document's revision; every change increments it.
 * @minimum 0
 */
export type Revision = number;

/**
 * The result of a mutating call: the revision after it (unchanged when the call changed nothing).
 */
export interface Changed {
  revision: Revision;
}

/** No result. */
export type Empty = Record<string, never>;

/**
 * A colour, #rrggbb (the engine writes lower case).
 * @pattern ^#[0-9a-fA-F]{6}$
 */
export type Rgb = string;

/**
 * A cell [x, y, z] in the grid's own coordinates.
 * @minItems 3
 * @maxItems 3
 */
export type Cell = [number, number, number];

/**
 * A size [x, y, z].
 * @minItems 3
 * @maxItems 3
 */
export type Size = [number, number, number];

/**
 * A point [x, y] of a layer drawing, y up.
 * @minItems 2
 * @maxItems 2
 */
export type Point2 = [number, number];

/** The grid of a puzzle, fixed for its life. */
export type Grid =
  | 'bricks'
  | 'prisms'
  | 'spheres'
  | 'rhombic'
  | 'tetraOcta'
  | 'sliding'
  | 'stacking';

/** Complete (ShapeLogic), or Fast (Crowell): Andrew Crowell's take-apart. */
export type Solver = 'classic' | 'crowell';

/**
 * The one solver of sliding puzzles, the Sliding Puzzle Solver: no limit on the arrangements it
 * visits (it keeps its older levels on disk and spreads each over threads), so it finds the fewest
 * moves or proves there is no solution. The original's Fast and Deep solvers, which stopped at
 * 250,000 and 1,000,000 arrangements, are not offered.
 */
export type SlidingSolver = 'sliding';

/**
 * The Stacking Solver (the fewest rod transfers: the Panex search whenever the puzzle fits it,
 * which pauses, carries on and saves itself, else a search of its own) or the Panex Solver (the
 * Panex search alone, for rod sets with Panex columns; a puzzle it cannot take is refused with its
 * reason).
 */
export type StackingSolver = 'stacking' | 'panex';

/** decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). */
export type SolveState =
  | 'idle'
  | 'preparing'
  | 'assembling'
  | 'draining'
  | 'paused'
  | 'finished'
  | 'error';

/**
 * What a solve is doing; search is a sliding puzzle's search from a start to the goal, or a
 * stacking puzzle's from its start stacking (SolveProgress.searched, searchDepth, searchTraced,
 * panexSearch).
 */
export type SolveStage =
  | 'idle'
  | 'prepare'
  | 'reduce'
  | 'assemble'
  | 'disassemble'
  | 'stopping'
  | 'paused'
  | 'done'
  | 'error'
  | 'search';

/**
 * What the problem holds of solving: nothing, a started solve (maybe paused), a finished one, or
 * counts lost by an edit.
 */
export type ProblemState = 'unsolved' | 'solving' | 'solved' | 'unknown';

export type CellState = 'fixed' | 'variable';

/** The grid editor's tools; paint changes only the colour of non-empty cells. */
export type CellTool = 'fixed' | 'variable' | 'erase' | 'paint';

/**
 * Mirror or span axes.
 * @maxItems 3
 */
export type Axes = ('x' | 'y' | 'z')[];

/**
 * A button of the tool tabs; what the numbered ones do depends on the grid (shape.taskSet gives
 * each one's group and label).
 */
export type ShapeTask =
  | 'nudge0'
  | 'nudge1'
  | 'nudge2'
  | 'nudge3'
  | 'nudge4'
  | 'nudge5'
  | 'rotate6'
  | 'rotate7'
  | 'rotate8'
  | 'rotate9'
  | 'rotate10'
  | 'rotate11'
  | 'flip12'
  | 'flip13'
  | 'flip14'
  | 'prune'
  | 'insideFixed'
  | 'outsideFixed'
  | 'insideVariable'
  | 'outsideVariable'
  | 'insideDecolour'
  | 'outsideDecolour'
  | 'double'
  | 'triple'
  | 'toOrigin'
  | 'centre'
  | 'minimize'
  | 'nudge27'
  | 'nudge28'
  | 'nudge29'
  | 'nudge30'
  | 'nudge31'
  | 'nudge32'
  | 'fillHoles'
  | 'gridScale';

/**
 * The 3D view's bevelled cells, the same closed with the wire-frame faces flagged, flat faces with
 * edge lines, or the STL export's mesh at the view's own parameters (the original's "Draw pieces
 * like STL export": gap 0.02 and bevel 0.05 of a cell with the interior chamfers, the sphere
 * grid's balls at two recursions; closed; the flat mesh where the mesher fails). The STL style's
 * cell faces carry the checker flag, as the others', and bevels are flagged 16; the original drew
 * it in one colour. It is made with the Manifold library on the prism, rhombic and tetra-octa
 * grids and takes a while for large shapes (seconds in a release build).
 */
export type MeshStyle = 'drawing' | 'wireframe' | 'flat' | 'stl';

export type ChangeKind = 'shapes' | 'colours' | 'problems' | 'solutions' | 'notes' | 'rodSets';

/**
 * A counter the problem moves on every change of its saved list (a solution added, removed,
 * re-sorted or edited), kept while the document is open: solution.page and solve.solutionsAdded
 * carry it, so pages read at one value belong together and a client knows when to page again.
 * @minimum 0
 */
export type SolutionsVersion = number;

/** Where a new solution goes in the saved list while solving: the original's sort-by choice. */
export type SortBy = 'unsorted' | 'moves' | 'level' | 'rotations';

/** A typed array inside a binary frame, little-endian, its offset a multiple of 4. */
export interface ArrayRef {
  type: 'f32' | 'u32' | 'i32' | 'u8';
  /** @minimum 0 */
  offset: number;
  /**
   * Elements, not bytes.
   * @minimum 0
   */
  length: number;
}

/**
 * A binary frame sent before the reply (decision 0012): its number, its size, and the arrays in
 * it.
 */
export interface Frame {
  /** @minimum 1 */
  frame: number;
  /** @minimum 0 */
  bytes: number;
  arrays: {
    [key: string]: ArrayRef;
  };
}

/**
 * A drawing mesh: arrays positions and normals (f32, xyz per vertex), indices (u32, three per
 * triangle), and per triangle voxel (u32, the cell index x + X*(y + Y*z)), voxelFace (i32, its
 * side, -1 for bevels and gaps), flags (u32, FaceFlag bits: 1 light checker colour, 2 variable
 * mark, 4 the variable marker face, 8 wire frame, 16 bevel, 32 gap, 128 inside), colour (u8,
 * colour number, 0 none; padded), polygon (u32); and edgeLines (f32, two points per line, flat
 * style only).
 */
export interface MeshFrame {
  frame: Frame;
  /** @minimum 0 */
  triangles: number;
  /** @minimum 0 */
  vertices: number;
  /**
   * @minItems 3
   * @maxItems 3
   */
  lo: [number, number, number];
  /**
   * @minItems 3
   * @maxItems 3
   */
  hi: [number, number, number];
  /**
   * The hotspot of the shape in the mesh's orientation, in grid coordinates: an assembly puts it
   * on a Placement's cell, so the mesh is drawn moved by the cell minus the hotspot (in mesh
   * units: the grid's cell spacing).
   */
  hotspot: Cell;
}

/**
 * A stacking puzzle's board: a row of rods and the rules of a move (the original's rod set; its
 * fields as the Rods panel showed them).
 */
export interface RodSet {
  id: RodSetId;
  /** R1, R2, ... by place (shown as 'R1 - name' with a name). */
  label: string;
  name: string;
  /**
   * How many rods (the pocket column not counted).
   * @minimum 1
   */
  rodCount: number;
  /**
   * The rods of a stack map: rodCount, and one more with the pocket column (the last; it stands
   * left of rod 1).
   * @minimum 1
   */
  rods: number;
  /** Grow to the height of all pieces: a rod holds every disc of the problem. */
  growHeight: boolean;
  /**
   * Defined height: the discs a rod holds when it does not grow.
   * @minimum 1
   */
  definedHeight: number;
  /** Disc size matters - no large on small (equal sizes stack); Panex columns ignore it. */
  sizeMatters: boolean;
  /** Disc can only move over 1 rod: to a neighbouring rod only. */
  distanceMatters: boolean;
  /**
   * Panex Style Columns: a disc of size s sits at most s places below the top, in any order; one
   * disc more may wait raised into the bridge, blocking moves that pass over it.
   */
  panexColumns: boolean;
  /** Add pocket column (with Panex columns): an extra rod beside rod 1 that holds any disc. */
  pocketColumn: boolean;
  /**
   * The discs the pocket column holds.
   * @minimum 1
   */
  pocketHeight: number;
  /**
   * Extra outside channel (with Panex columns and three rods or more): the first and last rods are
   * joined outside the bridge.
   */
  outsideChannel: boolean;
}

/** One copy of a disc on a rod: the shape, and which of its copies (from 0). */
export interface DiscRef {
  /**
   * Null for a shape the file names that the puzzle does not have (only an edited file; setupError
   * says so).
   */
  shape: ShapeId | null;
  /** @minimum 0 */
  instance: number;
}

/**
 * Each rod of a stack map, its discs bottom to top; rods past the board's count are kept while
 * they hold discs.
 */
export type Stacks = DiscRef[][];

/**
 * Where the rods and the discs of a stacking stand (the original's layoutBoard), in cells: what
 * the 3D view draws around the discs' meshes.
 */
export interface BoardLayout {
  /** The distance between neighbouring rods. */
  spacing: number;
  /**
   * The rods drawn, the pocket column included.
   * @minimum 0
   */
  rodCount: number;
  /**
   * The rods' drawn height.
   * @minimum 1
   */
  rodHeight: number;
  /** x of each rod, in the stack map's order (the pocket last, standing left of rod 1). */
  rodX: number[];
  /** The drawn height of each rod; the pocket column is shorter. */
  rodHeights: number[];
  /** Panex columns: drawn with a V that narrows to the bottom. */
  panex: boolean;
  /** The last rod is the pocket column, drawn as a plain peg. */
  pocket: boolean;
  /**
   * The largest disc size, so the V stays inside the rod spacing.
   * @minimum 1
   */
  maxSize: number;
  /**
   * Each disc of the problem (its pieces: parts, then copies) and the cell its hotspot (its
   * centre) stands on; null for a disc on no rod. Empty for a rod set drawn alone.
   */
  discs: ({
    shape: ShapeId;
    /** @minimum 0 */
    instance: number;
    /** @minimum 1 */
    size: number;
    at: Cell | null;
  })[];
}

export interface ShapeSummary {
  id: ShapeId;
  /**
   * S1, S2, ... by place; on a sliding puzzle B1, B2, ... for the start/goal shapes (boards) and
   * S1, S2, ... for the pieces, each by place among its kind.
   */
  label: string;
  /**
   * A start/goal shape's is the user's part: the board shows it, or '(START/GOAL n)' while it is
   * empty.
   */
  name: string;
  size: Size;
  /** @minimum 0 */
  fixed: number;
  /** @minimum 0 */
  variable: number;
  weight: number;
  /** The shape's own colour, or null for the default one. */
  colour: Rgb | null;
  /**
   * A sliding puzzle's start/goal shape: its number n (sliding.json); null for any other shape.
   * @minimum 0
   */
  startGoal: number | null;
  /**
   * A stacking puzzle's disc: its size, the radius, which the size rule compares
   * (stacking.setDiscSize); null for any other shape.
   * @minimum 1
   */
  disc: number | null;
  /**
   * A stacking puzzle's disc: the palette colour of its first coloured cell, which the 3D view
   * draws the disc in (the original's diskTint); null for a disc without one, or for any other
   * shape.
   */
  discColour: ColourId | null;
}

export interface Colour {
  id: ColourId;
  /** C1, C2, ... by place. */
  label: string;
  rgb: Rgb;
}

export interface ProblemSummary {
  id: ProblemId;
  /** P1, P2, ... by place. */
  label: string;
  name: string;
  result: ShapeId | null;
  /**
   * A stacking problem's board (stacking.setProblemRodSet, the original's Set Start/Goal Rods);
   * null for none and on other grids.
   */
  rodSet: RodSetId | null;
  /**
   * The number of pieces (the parts' maxima summed).
   * @minimum 0
   */
  pieces: number;
  state: ProblemState;
  solve: SolveState;
  analysing: boolean;
  /** Assemblies found, when known. */
  assemblies: number | null;
  /** Solutions found, when known. */
  solutions: number | null;
  /**
   * Saved solutions.
   * @minimum 0
   */
  saved: number;
  /** Time spent solving, when known. */
  timeMs: number | null;
  /**
   * How much the solves of the problem went through, kept with it in the file (the original's
   * recordSearchStats): the assembler's search steps, a sliding search's arrangements, a stacking
   * search's stackings (found, for the Panex search); 0 when unknown.
   * @minimum 0
   */
  searched: number;
  /**
   * How many moves deep a sliding or Panex search went; 0 when unknown or for the other searches.
   * @minimum 0
   */
  searchDepth: number;
  /**
   * The assemblies the solves tried to take apart; 0 when unknown.
   * @minimum 0
   */
  triedApart: number;
}

/** Everything a front end shows of the document's lists. */
export interface Snapshot {
  revision: Revision;
  dirty: boolean;
  grid: Grid;
  /** The file it was opened from or last saved to. */
  path: string | null;
  notes: string;
  canUndo: boolean;
  canRedo: boolean;
  /** A solve or an analysis is active: shapes and lists are frozen (decision 0011). */
  solving: boolean;
  shapes: ShapeSummary[];
  colours: Colour[];
  problems: ProblemSummary[];
  /** A stacking puzzle's boards (at least one); empty on other grids. */
  rodSets: RodSet[];
  /**
   * What opening the file changed, to tell the user (the original's load message). Only in the
   * result of document.open, document.openBytes and solve.importPaused, and only when there is
   * something to tell.
   */
  notice?: string;
}

/** A row of the status window. */
export interface ShapeInfo {
  shape: ShapeId;
  /** @minimum 0 */
  fixed: number;
  /** @minimum 0 */
  variable: number;
  /** An earlier shape it is identical to, mirrors allowed. */
  identicalMirror: ShapeId | null;
  /** ... in any orientation, not mirrored. */
  identicalShape: ShapeId | null;
  /** ... colours included. */
  identicalComplete: ShapeId | null;
  connectedFace: boolean;
  connectedEdge: boolean;
  connectedCorner: boolean;
  holes2D: boolean;
  holes3D: boolean;
  /** Cube grid only. */
  notchable: boolean | null;
  /** Cube grid only. */
  millable: boolean | null;
  /** The symmetry group number, when the grid's tables know it. */
  symmetry: number | null;
  /** The original's status line for the shape. */
  statusLine: string;
}

export interface Part {
  shape: ShapeId;
  /** @minimum 0 */
  min: number;
  /** @minimum 1 */
  max: number;
  groups: ({
    /** @minimum 1 */
    group: number;
    /** @minimum 1 */
    count: number;
  })[];
  /**
   * A sliding piece with a start or goal on the problem's start/goal shape: it stays in the
   * problem (the range and all-pieces commands leave it).
   */
  required: boolean;
  /**
   * A sliding piece's copies placed on the problem's start/goal shape, the more of its starts and
   * its goals (0 on other grids): problem.setPieceCount refuses a minimum below it.
   * @minimum 0
   */
  placed: number;
}

/**
 * The Solver tab's settings stored with a problem (saved in the file, the original's names; the
 * front end decides what they mean). Settings of other versions are kept as strings. These names
 * are the file's and stay: SolveOptions uses the API's (deepSymmetry is completeRotations,
 * strictColors is strictColours, justLevels is justLevels; nestedSlides, partialNested,
 * verticalMoves and highMemory are sliding's nestedSlides, partialNested, verticalMoves and
 * highMemory, a sliding puzzle's solverType, the original's fast, deep or full, is kept as stored
 * and plays no part: there is one sliding solver). highMemory belongs to the stacking solvers too.
 */
export interface SolverSettings {
  disassemble?: boolean;
  checkRotations?: boolean;
  justCount?: boolean;
  justLevels?: boolean;
  deepSymmetry?: boolean;
  keepMirrors?: boolean;
  keepRotations?: boolean;
  strictColors?: boolean;
  nestedSlides?: boolean;
  partialNested?: boolean;
  verticalMoves?: boolean;
  highMemory?: boolean;
  autosave?: boolean;
  /**
   * classic, crowell (sliding: the original's fast, deep or full, kept as stored and not written).
   */
  solverType?: string;
  /** unsorted, moves, level, rotations. */
  sortBy?: string;
  [key: string]: string | boolean | undefined;
}

export interface ProblemDetail {
  id: ProblemId;
  label: string;
  name: string;
  result: ShapeId | null;
  /**
   * A stacking problem's board (stacking.setProblemRodSet, the original's Set Start/Goal Rods);
   * null for none and on other grids.
   */
  rodSet: RodSetId | null;
  /** @minimum 0 */
  pieces: number;
  state: ProblemState;
  solve: SolveState;
  analysing: boolean;
  assemblies: number | null;
  solutions: number | null;
  /** @minimum 0 */
  saved: number;
  timeMs: number | null;
  /** @minimum 0 */
  searched: number;
  /** @minimum 0 */
  searchDepth: number;
  /** @minimum 0 */
  triedApart: number;
  parts: Part[];
  /**
   * The allowed pairs: a piece cell of colour 'piece' may sit on a result cell of colour 'result'.
   */
  colourRules: ({
    piece: ColourId;
    result: ColourId;
  })[];
  maxHoles: number | null;
  /** A paused search position is stored (solve.continue carries on from it). */
  savedPosition: boolean;
  solverSettings: SolverSettings;
  /** Null on other grids. */
  stacks: {
    start: Stacks;
    goal: Stacks;
  } | null;
}

/** Whether the pieces can fill the result (the fit summary). */
export interface FitStats {
  verdict: 'noResult' | 'fits' | 'tooLarge' | 'tooSmall';
  /**
   * The result's fixed cells.
   * @minimum 0
   */
  resultMin: number;
  /**
   * Fixed plus variable.
   * @minimum 0
   */
  resultMax: number;
  /** @minimum 0 */
  piecesMin: number;
  /** @minimum 0 */
  piecesMax: number;
  /** @minimum 0 */
  cellsMin: number;
  /** @minimum 0 */
  cellsMax: number;
  /** @minimum 0 */
  holesMin: number;
  /** @minimum 0 */
  holesMax: number;
  maxHoles: number | null;
  statusLine: string;
}

/**
 * What a solve does (the solve thread's flags); a missing field takes the engine's setting or the
 * default.
 */
export interface SolveOptions {
  solver?: Solver;
  /** Take each assembly apart; only those that come apart are solutions. */
  disassemble?: boolean;
  /** Try 90 degree turns while taking apart. */
  checkRotations?: boolean;
  /** Count, keep no solutions. */
  justCount?: boolean;
  /** Keep the level summary of each take-apart, not its moves. */
  justLevels?: boolean;
  keepMirrors?: boolean;
  keepRotations?: boolean;
  /** The thorough rotation check (the original's deep symmetry check). */
  completeRotations?: boolean;
  strictColours?: boolean;
  /** Drop placements that can never be part of an assembly first (default true). */
  reduce?: boolean;
  sortBy?: SortBy;
  /**
   * Keep at most this many solutions; 0 all (default 10).
   * @minimum 0
   */
  keepAtMost?: number;
  /**
   * Unsorted, keep every n-th (0 and 1: each).
   * @minimum 0
   */
  keepEach?: number;
  /**
   * Assembly and take-apart together; 0 every hardware thread.
   * @minimum 0
   * @maximum 256
   */
  threads?: number;
  /**
   * 0 chooses.
   * @minimum 0
   * @maximum 64
   */
  takeApartWorkers?: number;
  sliding?: SlidingOptions;
  stacking?: StackingOptions;
}

/**
 * A sliding puzzle's search (the solve thread's sliding flags). A sliding solve always keeps
 * mirrored and rotated starts and matches colours strictly (the start locks), whatever the brick
 * fields say; disassemble (the GUI's Find Solutions) searches each start to the goal; solver,
 * checkRotations, justLevels and sortBy play no part (a path found goes at the end of the list).
 * There is one sliding solver (engine.info's slidingSolvers), so there is none to choose. Where
 * the search keeps its levels is the engine's setting (slideFolder, slideDiskGB).
 */
export interface SlidingOptions {
  /** A piece may carry what is nested in its outline (Allow Nested Slides). */
  nestedSlides?: boolean;
  /** And what is partly nested in it (Allow Partially Nested); implies nestedSlides. */
  partialNested?: boolean;
  /**
   * On a tray more than one layer deep, a piece may also move up and down (Allow Vertical Moves);
   * off (the default), every piece stays in its layer.
   */
  verticalMoves?: boolean;
  /**
   * Hold up to half the machine's memory in arrangements, not about 2 GB (the original's Enable
   * High Memory). The engine's searchMemoryGB setting decides the memory instead, so this plays no
   * part.
   */
  highMemory?: boolean;
}

/**
 * A stacking puzzle's search (the solve thread's stacking flags). A stacking solve has no
 * assembler and no take-apart: one search from the start stacking to the goal, whose path is the
 * one solution (its assembly the start stacking). disassemble (the GUI's Find Solutions) or
 * justCount must be on; threads count; the brick fields play no part. solve.prepare is refused
 * (unsupported), and so is solve.savePoint: the search saves itself. Where it is saved is the
 * engine's setting (panexFolder, panexDiskGB).
 */
export interface StackingOptions {
  /**
   * Default: the Panex Solver for a problem on Panex columns, else the Stacking Solver (the
   * original's menu offered only that one).
   */
  solver?: StackingSolver;
  /**
   * Hold up to half the machine's memory in stackings (the original's Enable High Memory). The
   * engine's searchMemoryGB setting decides the memory instead, so this plays no part.
   */
  highMemory?: boolean;
  /**
   * The Panex search saves itself every 20 minutes (default true); off, only when it stops (the
   * GUI's Autosave box).
   */
  autosave?: boolean;
}

/** One take-apart under way. */
export interface TakeApartProgress {
  /** @minimum 0 */
  elapsedMs: number;
  /** @minimum 0 */
  pieces: number;
  /** @minimum 0 */
  searchPieces: number;
  /** @minimum 0 */
  separations: number;
  /** @minimum 0 */
  depth: number;
  /** @minimum 0 */
  level: number;
  /** @minimum 0 */
  levelDone: number;
  /** @minimum 0 */
  levelSize: number;
  /** @minimum 0 */
  nextLevelSize: number;
  /** @minimum 0 */
  nodes: number;
  /** @minimum 0 */
  threads: number;
}

export interface SolveProgress {
  stage: SolveStage;
  /** @minimum 0 */
  elapsedMs: number;
  /**
   * The problem's time used: this run's and the earlier runs' of a continued solve or a saved
   * search.
   * @minimum 0
   */
  usedMs: number;
  /** @minimum 0 */
  piece: number;
  /** @minimum 0 */
  pieces: number;
  assemblyFraction: number;
  /** @minimum 0 */
  iterations: number;
  /** @minimum 0 */
  assemblerThreads: number;
  /** @minimum 0 */
  assemblies: number;
  /** @minimum 0 */
  solutions: number;
  disassembly: boolean;
  /** @minimum 0 */
  disasmCompleted: number;
  /** @minimum 0 */
  disasmPending: number;
  /** @minimum 0 */
  disasmWorkers: number;
  /** The whole solve, 0 to 1; never goes back during a run. */
  overall: number;
  overallKnown: boolean;
  levelFraction: number;
  secondsLeft: number | null;
  /**
   * The original's status text (empty in the search stage, which the original GUI told as 'Sliding
   * search, N moves deep. Scanned N arrangements.', 'Panex search, N moves deep. Found N
   * stackings.', 'Panex search, tracing the path. Traced N of M moves.' or 'Stacking search.
   * Scanned N stackings.').
   */
  activity: string;
  /**
   * The sliding search under way: the arrangements it has visited; the stacking search: the
   * stackings it has searched (found, for the Panex search).
   * @minimum 0
   */
  searched: number;
  /**
   * How many moves deep a sliding search is (0 before its first level, and for the other
   * searches); how many moves deep both ends of the Panex search are together.
   * @minimum 0
   */
  searchDepth: number;
  /**
   * Once the ends of the Panex search have met: how many moves of the path are traced back (of
   * searchDepth); 0 before.
   * @minimum 0
   */
  searchTraced: number;
  /**
   * The Panex Solver runs (its words: 'Panex search'), not the Stacking Solver ('Stacking
   * search').
   */
  panexSearch: boolean;
}

/** The debug statistics panel. */
export interface SolveStats {
  state: SolveState;
  solver: Solver;
  disassemblyEnabled: boolean;
  rotationsEnabled: boolean;
  /** @minimum 0 */
  hardwareThreads: number;
  /** @minimum 0 */
  assemblerThreads: number;
  /** @minimum 0 */
  disasmWorkers: number;
  /** @minimum 0 */
  assembliesFound: number;
  /** @minimum 0 */
  solutionsFound: number;
  /** @minimum 0 */
  inseparable: number;
  /** @minimum 0 */
  iterations: number;
  assemblyProgress: number;
  /** @minimum 0 */
  pending: number;
  /** @minimum 0 */
  peakPending: number;
  /** @minimum 0 */
  disasmCompleted: number;
  /** @minimum 0 */
  elapsedMs: number;
  /** @minimum 0 */
  prepareMs: number;
  /** @minimum 0 */
  reduceMs: number;
  /** @minimum 0 */
  assemblyMs: number;
  /** @minimum 0 */
  disasmWorkMs: number;
  /** @minimum 0 */
  linearSearchMs: number;
  /** @minimum 0 */
  rotationSearchMs: number;
  /** @minimum 0 */
  drainMs: number;
  avgDisasmSeconds: number;
  running: TakeApartProgress[];
}

/** Why a run ended in error. */
export interface SolveFailure {
  /** The preparation's or the saved position's error; null for an internal error. */
  assembler:
    | 'tooManyUnits'
    | 'tooFewUnits'
    | 'cannotPlace'
    | 'cannotRestoreVersion'
    | 'cannotRestoreSyntax'
    | 'cannotRestoreInterrupted'
    | 'puzzleUnhandable'
    | null;
  /** The piece or number the error names. */
  param: number;
  message: string;
  file: string;
  line: number;
}

/** Where a piece is in an assembly. */
export interface Placement {
  /**
   * The problem's piece number (pieces are numbered part by part).
   * @minimum 0
   */
  piece: number;
  placed: boolean;
  /** @minimum 0 */
  transformation: number;
  at: Cell;
}

export interface SolutionRow {
  id: SolutionId;
  /**
   * Its place in the saved list.
   * @minimum 0
   */
  index: number;
  /**
   * The assembly number (the original shows it plus one).
   * @minimum 0
   */
  assembly: number;
  /**
   * The solution number (0 without a disassembly).
   * @minimum 0
   */
  solution: number;
  /**
   * Pieces placed.
   * @minimum 0
   */
  pieces: number;
  /**
   * The moves are stored; only the level summary; not taken apart (or found not to come apart).
   */
  disassembly: 'tree' | 'summary' | 'none';
  /**
   * A stacking solution's: its rod transfers (each three frames of its path), as the original
   * counted them; steps and movesText the same.
   */
  moves: number | null;
  rotations: number | null;
  /** Moves plus rotations. */
  steps: number | null;
  /** The level text, e.g. 3.1.1. */
  movesText: string | null;
}

/** A position of a movement explorer's tree. */
export interface MovementNode {
  /** @minimum 0 */
  node: number;
  /**
   * The root is its own parent.
   * @minimum 0
   */
  parent: number;
  /** The direction and the pieces moved, e.g. 'x: S1, S2.2'; empty for the root. */
  label: string;
  pieces: ({
    /** @minimum 0 */
    piece: number;
    at: Cell;
  })[];
  children: number[];
}

// ---- methods and events ----

/** Every method: its params, its result and the errcs it may answer. */
export interface Methods {
  /**
   * The colours in list order.
   * (colour.json)
   */
  'colour.list': {
    params: Record<string, never>;
    result: {
      colours: Colour[];
    };
    errors: never;
  };
  /**
   * Adds a colour (at most 63); every problem allows it on itself.
   * (colour.json)
   */
  'colour.add': {
    params: {
      rgb: Rgb;
    };
    result: {
      colour: ColourId;
      revision: Revision;
    };
    errors: 'invalid_argument' | 'invalid_state';
  };
  /**
   * Changes how a colour looks. Allowed while solving.
   * (colour.json)
   */
  'colour.edit': {
    params: {
      colour: ColourId;
      rgb: Rgb;
    };
    result: Changed;
    errors: 'not_found';
  };
  /**
   * Removes a colour: its cells revert to the shape's own colour, the colours after it move up,
   * the problems with shapes using it lose their solutions. Refused (invalid_argument) on a
   * sliding puzzle, whose cell colours are its start marks and locks.
   * (colour.json)
   */
  'colour.delete': {
    params: {
      colour: ColourId;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * A new empty puzzle on a grid; the grid never changes. The open document goes (as with
   * document.open, document.openBytes and solve.importPaused): its solves pause and are waited
   * for, its analyses stop, its handles are dropped, and after the reply its problems whose work
   * the events were reporting get a last solve.state idle and solution.analyseEnded (stopped); the
   * revision counts on.
   * (document.json)
   */
  'document.new': {
    params: {
      grid: Grid;
    };
    result: Snapshot;
    errors: never;
  };
  /**
   * Opens a .xmpuzzle file (gzip or plain XML; one that inflates to more than 256 MiB is refused
   * as parse_error); what this version does not model is kept. The open document goes as with
   * document.new.
   * (document.json)
   */
  'document.open': {
    params: {
      /** @minLength 1 */
      path: string;
    };
    result: Snapshot;
    errors: 'io_error' | 'parse_error';
  };
  /**
   * Opens a puzzle from its file's bytes (gzip or plain XML; gzip that inflates to more than 256
   * MiB is refused as parse_error), base64; the document has no path unless one is given. The open
   * document goes as with document.new.
   * (document.json)
   */
  'document.openBytes': {
    params: {
      data: string;
      /**
       * The file these bytes stand for: the document's path, where document.save writes (a rescue
       * copy of a puzzle that has a file).
       * @minLength 1
       */
      path?: string;
      /** The document starts with unsaved changes (a rescue copy): dirty until saved. */
      dirty?: boolean;
    };
    result: Snapshot;
    errors: 'invalid_argument' | 'parse_error';
  };
  /**
   * Imports another program's puzzle file (File ▸ Import ▸ PuzzleSolver3D or Puzzlecad (OpenSCAD))
   * as a new, untitled document with unsaved changes: a cube-grid puzzle with one problem, its
   * pieces one of each; Save then asks where to write the ShapeLogic file, never over the source.
   * The file is read by its path or from its bytes, exactly one of the two, as it is (never
   * decompressed), at most 256 MiB; one whose shapes would hold more than 2^27 cells together is
   * refused (parse_error), as is one the format's reader cannot take (the original's "Could not
   * load puzzle, sorry!"). The open document goes as with document.new.
   * (document.json)
   */
  'document.import': {
    params: {
      /**
       * puzzleSolver3D: a PuzzleSolver3D file (.puz): PIECE and RESULT size lines, each followed
       * by the shape's rows, its layers side by side. puzzlecad: a puzzlecad OpenSCAD file
       * (.scad): the pieces of its burr_plate and burr_piece calls, its first packing_box as the
       * result (else a cube of variable cells).
       */
      format: 'puzzleSolver3D' | 'puzzlecad';
      /** @minLength 1 */
      path?: string;
      /** The file's bytes, base64. */
      data?: string;
    };
    result: Snapshot;
    errors: 'invalid_argument' | 'io_error' | 'parse_error';
  };
  /**
   * The puzzlecad OpenSCAD file of a problem's pieces (File ▸ Export ▸ Puzzlecad (OpenSCAD)): the
   * distinct shapes of its parts but the result (when that leaves none, every shape but the
   * result), or every shape when no problem is given, each as the layers of its bounding box in
   * one burr_plate, in the layout of puzzlecad's MagellanTIC.scad. The header comment names the
   * document's file, or `name` when it has none ("unknown" without either). Only for cube-grid
   * puzzles with shapes (the original's "Puzzlecad export is only available for cube-grid
   * puzzles.", "Nothing to export."), and some shape must have a cell ("Could not export puzzlecad
   * file."): invalid_argument. Allowed while solving.
   * (document.json)
   */
  'document.exportPuzzlecad': {
    params: {
      problem?: ProblemId;
      /**
       * The file being written, for the header when the document has no file. At most 255 bytes,
       * no control characters.
       */
      name?: string;
    };
    result: {
      text: string;
    };
    errors: 'invalid_argument' | 'not_found';
  };
  /**
   * The puzzle on another grid (Edit ▸ Convert brick grid type to other): each cube of a cube-grid
   * puzzle becomes the cells that fill it (5x5x5 on the rhombic grid, 3x3x3 on the tetra-octa
   * grid), the shapes keep their names, the colours and the notes stay, and each problem's
   * definition is copied with its name, without its solutions. As the original, the document is
   * replaced: it has unsaved changes, nothing to undo, and keeps its path, so document.save writes
   * the converted puzzle over the file it came from. The grids a puzzle converts to are
   * engine.info's grids[].convertsTo; another is unsupported. Refused while solving
   * (invalid_state), and when the converted shapes would hold more than 2^27 cells together
   * (invalid_argument). The open document goes as with document.new.
   * (document.json)
   */
  'document.convertGrid': {
    params: {
      grid: Grid;
    };
    result: Snapshot;
    errors: 'unsupported' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Saves (gzip, as the original) to the path given, or to the document's path; the document is
   * clean afterwards. Refused while a solve runs: save at a save point (solve.savePoint).
   * (document.json)
   */
  'document.save': {
    params: {
      /** @minLength 1 */
      path?: string;
    };
    result: {
      revision: Revision;
      path: string;
    };
    errors: 'invalid_state' | 'io_error' | 'invalid_argument';
  };
  /**
   * The file's bytes (plain XML), base64, without marking the document clean.
   * (document.json)
   */
  'document.saveBytes': {
    params: Record<string, never>;
    result: {
      data: string;
    };
    errors: 'invalid_state';
  };
  /**
   * The document's lists and state.
   * (document.json)
   */
  'document.get': {
    params: Record<string, never>;
    result: Snapshot;
    errors: never;
  };
  /**
   * The puzzle's comment. Allowed while solving. Control characters other than tab, newline and
   * carriage return are refused (the file is XML 1.0); a name (shape.rename, problem.rename)
   * refuses line breaks as well, since an attribute would give them back as spaces.
   * (document.json)
   */
  'document.setNotes': {
    params: {
      text: string;
    };
    result: Changed;
    errors: 'invalid_argument';
  };
  /**
   * Undoes the last step. Refused while solving or inside edit.begin / edit.end.
   * (document.json)
   */
  'edit.undo': {
    params: Record<string, never>;
    result: Changed;
    errors: 'invalid_state';
  };
  /**
   * Redoes the last undone step.
   * (document.json)
   */
  'edit.redo': {
    params: Record<string, never>;
    result: Changed;
    errors: 'invalid_state';
  };
  /**
   * The labels of the undo and redo steps.
   * (document.json)
   */
  'edit.history': {
    params: Record<string, never>;
    result: {
      /** Oldest first; the last is undone next. */
      undo: string[];
      /** The first is redone next. */
      redo: string[];
    };
    errors: never;
  };
  /**
   * Groups the following calls into one undo step (a drag across cells); groups do not nest.
   * (document.json)
   */
  'edit.begin': {
    params: {
      label?: string;
    };
    result: Empty;
    errors: 'invalid_state';
  };
  /**
   * Ends the group; an empty group leaves no step.
   * (document.json)
   */
  'edit.end': {
    params: Record<string, never>;
    result: Changed;
    errors: 'invalid_state';
  };
  /**
   * Tells the engine which protocol the client speaks; a client calls it first. The engine answers
   * with the protocol it will speak (this version: 1) or refuses one it does not know
   * (unsupported), so that when the protocol changes, old and new clients can be told apart.
   * (engine.json)
   */
  'engine.hello': {
    params: {
      /**
       * The protocol the client speaks.
       * @minimum 1
       */
      protocol: number;
    };
    result: {
      /** @minimum 1 */
      protocol: number;
      name: 'shapelogic_engine';
      version: string;
    };
    errors: 'unsupported';
  };
  /**
   * The engine's version, the grids and solvers it knows, the machine's threads, and its methods.
   * (engine.json)
   */
  'engine.info': {
    params: Record<string, never>;
    result: {
      name: 'shapelogic_engine';
      version: string;
      /**
       * Raised when a change breaks clients.
       * @minimum 1
       */
      protocol: number;
      grids: ({
        grid: Grid;
        label: string;
        /** It has an assembler. */
        assemble: boolean;
        /** It has a take-apart. */
        disassemble: boolean;
        /**
         * This version solves its puzzles: every grid (stacking since the deferred plan's part
         * 3b).
         */
        supported: boolean;
        /**
         * The size of a new shape in each direction, in cells.
         * @minimum 1
         */
        defaultSize: number;
        /**
         * shape.resize counts in blocks of this many cells.
         * @minimum 1
         */
        sizeUnit: number;
        /**
         * The grids document.convertGrid converts its puzzles to (the original's Convert brick
         * grid type to other): rhombic and tetraOcta for bricks, none for the others.
         */
        convertsTo: Grid[];
      })[];
      solvers: ({
        solver: Solver;
        label: string;
      })[];
      /**
       * The solver of sliding puzzles, one: the Sliding Puzzle Solver (the Solver Type a sliding
       * puzzle shows; there is none to choose).
       */
      slidingSolvers: ({
        solver: SlidingSolver;
        label: string;
      })[];
      /**
       * The solvers of stacking puzzles (SolveOptions.stacking.solver), with the original's menu
       * labels. The original offered one, by the problem's rod set: the Panex Solver for Panex
       * columns, else the Stacking Solver, which a solve without a solver named uses too.
       */
      stackingSolvers: ({
        solver: StackingSolver;
        label: string;
      })[];
      /** @minimum 1 */
      hardwareThreads: number;
      /** @minimum 1 */
      maxThreads: number;
      /**
       * The machine's physical memory in bytes; 0 when it cannot be read. The automatic
       * searchMemoryGB is half of it.
       * @minimum 0
       */
      memoryBytes: number;
      methods: string[];
    };
    errors: never;
  };
  /**
   * Replies, then ends the engine: its solves pause and are waited for (end of input does the
   * same).
   * (engine.json)
   */
  'engine.shutdown': {
    params: Record<string, never>;
    result: Empty;
    errors: never;
  };
  /**
   * The engine's settings.
   * (engine.json)
   */
  'settings.get': {
    params: Record<string, never>;
    result: Settings;
    errors: never;
  };
  /**
   * Changes some settings; the others stay. A smaller undo depth drops the oldest steps.
   * (engine.json)
   */
  'settings.set': {
    params: {
      /**
       * @minimum 1
       * @maximum 10000
       */
      undoDepth?: number;
      /**
       * @minimum 0
       * @maximum 256
       */
      threads?: number;
      solver?: Solver;
      slideFolder?: string;
      /** @minimum 0 */
      slideDiskGB?: number;
      panexFolder?: string;
      /** @minimum 0 */
      panexDiskGB?: number;
      /**
       * @minimum 0
       * @maximum 1000000
       */
      searchMemoryGB?: number;
    };
    result: Settings;
    errors: never;
  };
  /**
   * Prepares an assembly search of a problem to explore (Prepare in the original).
   * (explore.json)
   */
  'explore.openAssembly': {
    params: {
      problem: ProblemId;
      solver?: Solver;
      keepMirrors?: boolean;
      keepRotations?: boolean;
      completeRotations?: boolean;
      strictColours?: boolean;
      /** Default true. */
      reduce?: boolean;
    };
    result: {
      handle: AssemblyExplorerHandle;
      /** @minimum 0 */
      pieces: number;
      placementsSupported: boolean;
      finished: number;
    };
    errors: 'not_found' | 'invalid_argument' | 'unsupported';
  };
  /**
   * Every placement of a piece in the result, in the assembler's order.
   * (explore.json)
   */
  'explore.placements': {
    params: {
      handle: AssemblyExplorerHandle;
      /** @minimum 0 */
      piece: number;
    };
    result: {
      placements: ({
        /** @minimum 0 */
        transformation: number;
        at: Cell;
      })[];
    };
    errors: 'not_found' | 'invalid_argument' | 'unsupported';
  };
  /**
   * Advances the search 'steps' steps (default 1) and returns the pieces placed now.
   * (explore.json)
   */
  'explore.step': {
    params: {
      handle: AssemblyExplorerHandle;
      /**
       * @minimum 1
       * @maximum 1000000
       */
      steps?: number;
    };
    result: {
      /** How far the search is, 0 to 1. */
      finished: number;
      placements: Placement[];
    };
    errors: 'not_found';
  };
  /**
   * Opens the movement browser on a saved solution's assembly: the root position.
   * (explore.json)
   */
  'explore.openMovements': {
    params: {
      problem: ProblemId;
      solution: SolutionId;
      /** The analysis also tries 90 degree turns. */
      rotations?: boolean;
    };
    result: {
      handle: MovementExplorerHandle;
      root: MovementNode;
      /**
       * The grid's movement directions as labelled ('x', '-x', ...): direction d forward is label
       * 2d, reversed 2d + 1.
       */
      directions: string[];
      /**
       * Direction d's step in grid coordinates (reversed: negated), one per direction: what
       * explore.findMatching's delta is one of (a label does not say how far a step goes: a
       * prism's x step is 2).
       */
      vectors: Cell[];
    };
    errors: 'not_found' | 'unsupported';
  };
  /**
   * A position of the tree.
   * (explore.json)
   */
  'explore.node': {
    params: NodeParam;
    result: MovementNode;
    errors: 'not_found';
  };
  /**
   * Adds every position one move away from a node (Analyze); returns the new ones.
   * (explore.json)
   */
  'explore.analyse': {
    params: NodeParam;
    result: {
      nodes: MovementNode[];
    };
    errors: 'not_found';
  };
  /**
   * Adds the position where the given pieces moved one step in a direction (Add Movement), valid
   * or not; returns it (or the child already there).
   * (explore.json)
   */
  'explore.addMove': {
    params: {
      handle: MovementExplorerHandle;
      /** @minimum 0 */
      node: number;
      /** @minItems 1 */
      pieces: number[];
      /** @minimum 0 */
      direction: number;
      reverse?: boolean;
    };
    result: MovementNode;
    errors: 'not_found' | 'invalid_argument';
  };
  /**
   * The first valid move that moves a piece by a vector, taking as few others as needed; its
   * position.
   * (explore.json)
   */
  'explore.findMatching': {
    params: {
      handle: MovementExplorerHandle;
      /** @minimum 0 */
      node: number;
      /** @minimum 0 */
      piece: number;
      delta: Cell;
    };
    result: MovementNode;
    errors: 'not_found' | 'invalid_argument';
  };
  /**
   * Keeps only the path from the root to a node (Prune Tree).
   * (explore.json)
   */
  'explore.prune': {
    params: NodeParam;
    result: Empty;
    errors: 'not_found';
  };
  /**
   * Closes an explorer of either kind.
   * (explore.json)
   */
  'explore.close': {
    params: {
      handle: AssemblyExplorerHandle | MovementExplorerHandle;
    };
    result: Empty;
    errors: 'not_found';
  };
  /**
   * The STL parameters of a grid, in order, as the original's export dialog showed them: name,
   * kind of value, default, tooltip. The values of export.stlMesh and export.stl follow this
   * order.
   * (export.json)
   */
  'export.stlParameters': {
    params: {
      /** Default: the document's. */
      grid?: Grid;
    };
    result: {
      parameters: StlParameter[];
    };
    errors: never;
  };
  /**
   * A shape's STL mesh for the parameter values, as a binary frame with the arrays of a MeshFrame
   * (the export dialog's preview: the mesh before its coplanar faces are merged for the file),
   * with the volume it encloses (the dialog's 'Volume: x cubic-units'). Refused with the
   * original's words for a shape with variable cells, a value out of range, more than 6 recursions
   * on the sphere grid (not a limit of the original: its mesh failed from 6 on), or a mesh that
   * comes out faulty ('The generated mesh is faulty in some way, try to tweak the parameter').
   * (export.json)
   */
  'export.stlMesh': {
    params: {
      shape: ShapeId;
      parameters?: StlValues;
    };
    result: {
      frame: Frame;
      /** @minimum 0 */
      triangles: number;
      /** @minimum 0 */
      vertices: number;
      /**
       * @minItems 3
       * @maxItems 3
       */
      lo: [number, number, number];
      /**
       * @minItems 3
       * @maxItems 3
       */
      hi: [number, number, number];
      hotspot: Cell;
      /** In cubic units of the unit size (the mesh's own units). */
      volume: number;
    };
    errors: 'not_found' | 'invalid_argument';
  };
  /**
   * A shape's STL file, as a binary frame with one array, file (u8): the mesh for the parameter
   * values with its coplanar faces merged and triangulated again, each face a fan of triangles,
   * binary or text as the original wrote it. Refused as export.stlMesh.
   * (export.json)
   */
  'export.stl': {
    params: {
      shape: ShapeId;
      parameters?: StlValues;
      /** Binary STL (the dialog's 'Binary STL'); default true. Otherwise text. */
      binary?: boolean;
      /**
       * What the file says its solid is: the binary header (its first 80 bytes) or the text's
       * 'solid' line; the original wrote the file's name there. Default empty. No control
       * characters, at most 255 bytes.
       */
      name?: string;
    };
    result: {
      frame: Frame;
    };
    errors: 'not_found' | 'invalid_argument';
  };
  /**
   * What the original's Export Solution Pieces to STL writes for a saved solution: the distinct
   * shapes its assembly places (not the problem's counts), in shape order, with how many copies
   * and their file's name: the shape's name, or S<n> without one, every byte but ASCII letters,
   * digits, '-' and '_' made '_', '-S<n>' added when an earlier piece's file has that name, then
   * '.stl'. Allowed while solving.
   * (export.json)
   */
  'export.solutionPieces': {
    params: {
      problem: ProblemId;
      solution: SolutionId;
    };
    result: {
      pieces: ({
        shape: ShapeId;
        /** @minimum 1 */
        count: number;
        /** @minLength 5 */
        fileName: string;
      })[];
    };
    errors: 'not_found';
  };
  /**
   * Opens the playback of a saved solution. With its moves stored the poses follow the steps;
   * without them (not taken apart, found not to come apart, or only the level summary kept) steps
   * is 0 and every pose is the assembled one, so every solution is shown through one path.
   * (playback.json)
   */
  'playback.open': {
    params: {
      problem: ProblemId;
      solution: SolutionId;
    };
    result: {
      handle: PlaybackHandle;
      /**
       * Moves and turns: step 0 is assembled, 'steps' apart; 0 without a disassembly.
       * @minimum 0
       */
      steps: number;
      /** The level text; null without a disassembly. */
      movesText: string | null;
      pieces: ({
        /**
         * The problem's piece number.
         * @minimum 0
         */
        piece: number;
        /** @minimum 0 */
        part: number;
        shape: ShapeId;
        /** S1, or the shape's name, with ' #2' for the second piece of a part. */
        name: string;
        /**
         * The orientation of the piece's mesh (playback.mesh).
         * @minimum 0
         */
        orientation: number;
      })[];
    };
    errors: 'not_found' | 'invalid_argument' | 'unsupported';
  };
  /**
   * The mesh of piece i (of playback.open's pieces) in its assembly orientation, centred as the
   * poses expect; in any style (wireframe pieces, showing through solid ones).
   * (playback.json)
   */
  'playback.mesh': {
    params: {
      handle: PlaybackHandle;
      /**
       * An index into pieces.
       * @minimum 0
       */
      piece: number;
      /** Default drawing. */
      style?: MeshStyle;
    };
    result: MeshFrame;
    errors: 'not_found' | 'invalid_argument';
  };
  /**
   * Every piece's pose at steps from 'from' to 'to' (default the whole disassembly),
   * samplesPerStep samples a step (default 8), as a frame: step (f32 per sample), translation
   * (f32, xyz per sample and piece), rotation (f32, quaternion xyzw), visible (u8, padded).
   * (playback.json)
   */
  'playback.poses': {
    params: {
      handle: PlaybackHandle;
      /** @minimum 0 */
      from?: number;
      /** @minimum 0 */
      to?: number;
      /**
       * @minimum 1
       * @maximum 256
       */
      samplesPerStep?: number;
    };
    result: Poses;
    errors: 'not_found' | 'invalid_argument' | 'internal';
  };
  /**
   * The keyframes of an animation (the glTF export's sampler): time (f32 seconds per keyframe),
   * translation, rotation, visible as playback.poses; forward, holds, and back again with
   * reassemble.
   * (playback.json)
   */
  'playback.track': {
    params: {
      handle: PlaybackHandle;
      /**
       * @minimum 1
       * @maximum 256
       */
      samplesPerStep?: number;
      /** @minimum 0.001 */
      secondsPerStep?: number;
      reassemble?: boolean;
    };
    result: Poses;
    errors: 'not_found' | 'internal';
  };
  /**
   * Closes a playback.
   * (playback.json)
   */
  'playback.close': {
    params: {
      handle: PlaybackHandle;
    };
    result: Empty;
    errors: 'not_found';
  };
  /**
   * The problems in list order.
   * (problem.json)
   */
  'problem.list': {
    params: Record<string, never>;
    result: {
      problems: ProblemSummary[];
    };
    errors: never;
  };
  /**
   * A problem in full (read under the solve's lock while it runs).
   * (problem.json)
   */
  'problem.get': {
    params: ProblemProblemParam;
    result: ProblemDetail;
    errors: 'not_found';
  };
  /**
   * Adds a problem; it allows each colour on itself. Refused (invalid_argument) on a sliding
   * puzzle, whose problems follow its start/goal shapes (sliding.addStartGoal).
   * (problem.json)
   */
  'problem.create': {
    params: Record<string, never>;
    result: ProblemMade;
    errors: 'invalid_argument' | 'invalid_state';
  };
  /**
   * Adds a copy of a problem's definition (no name, no solutions). Refused (invalid_argument) on a
   * sliding puzzle (shape.duplicate of the start/goal shape instead).
   * (problem.json)
   */
  'problem.duplicate': {
    params: ProblemProblemParam;
    result: ProblemMade;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Removes a problem. Refused (invalid_argument) on a sliding puzzle (shape.delete of the
   * start/goal shape instead).
   * (problem.json)
   */
  'problem.delete': {
    params: ProblemProblemParam;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Moves a problem to a place in the list.
   * (problem.json)
   */
  'problem.move': {
    params: {
      problem: ProblemId;
      /** @minimum 0 */
      toIndex: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The problem's name. A sliding board's problem is named as its board: the board is renamed
   * (shape.rename).
   * (problem.json)
   */
  'problem.rename': {
    params: {
      problem: ProblemId;
      name: string;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Makes a shape the result (it stops being a piece); a different result removes the solutions.
   * Refused (invalid_argument) on a stacking puzzle, whose problems stand on a rod set instead
   * (stacking.setProblemRodSet).
   * (problem.json)
   */
  'problem.setResult': {
    params: ProblemShape;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The count range of a shape as pieces; max 0 removes it. The solutions stay, the counts become
   * unknown. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its stacks
   * (stacking.json), and for a sliding puzzle's board, which is no piece.
   * (problem.json)
   */
  'problem.setPieceCount': {
    params: {
      problem: ProblemId;
      shape: ShapeId;
      /** @minimum 0 */
      min: number;
      /** @minimum 0 */
      max: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * One more of a shape (minimum and maximum). Refused (invalid_argument) on a stacking puzzle,
   * whose pieces follow its stacks (stacking.json).
   * (problem.json)
   */
  'problem.addPiece': {
    params: ProblemShape;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * One fewer of a shape. Refused (invalid_argument) on a stacking puzzle, whose pieces follow its
   * stacks (stacking.json).
   * (problem.json)
   */
  'problem.removePiece': {
    params: ProblemShape;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * One count range for every piece (the bulk range; a maximum below the minimum is raised to it).
   * Removes the solutions. Refused (invalid_argument) on a stacking puzzle, whose pieces follow
   * its stacks (stacking.json).
   * (problem.json)
   */
  'problem.setRangeAll': {
    params: {
      problem: ProblemId;
      /** @minimum 0 */
      min: number;
      /** @minimum 0 */
      max: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * One more of every shape but the result. Refused (invalid_argument) on a stacking puzzle, whose
   * pieces follow its stacks (stacking.json).
   * (problem.json)
   */
  'problem.addOneOfEach': {
    params: ProblemProblemParam;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Removes every piece and the solutions. Refused (invalid_argument) on a stacking puzzle, whose
   * pieces follow its stacks (stacking.json).
   * (problem.json)
   */
  'problem.removeAllPieces': {
    params: ProblemProblemParam;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Moves a shape's part one place earlier (-1) or later (+1): the pieces renumber. Refused
   * (invalid_argument) on a stacking puzzle, whose pieces follow its stacks (stacking.json).
   * (problem.json)
   */
  'problem.movePart': {
    params: {
      problem: ProblemId;
      shape: ShapeId;
      delta: -1 | 1;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Puts all pieces of a shape in one exclusive group, or none (0). Refused (invalid_argument) on
   * sliding and stacking puzzles, which have no groups.
   * (problem.json)
   */
  'problem.setGroup': {
    params: {
      problem: ProblemId;
      shape: ShapeId;
      /** @minimum 0 */
      group: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Puts 'count' pieces of a shape in group 'group' (0 takes them out): the groups editor. Refused
   * (invalid_argument) on sliding and stacking puzzles, which have no groups.
   * (problem.json)
   */
  'problem.setGroupCount': {
    params: {
      problem: ProblemId;
      shape: ShapeId;
      /** @minimum 1 */
      group: number;
      /** @minimum 0 */
      count: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Removes a group from every shape. Refused (invalid_argument) on sliding and stacking puzzles,
   * which have no groups.
   * (problem.json)
   */
  'problem.deleteGroup': {
    params: {
      problem: ProblemId;
      /** @minimum 1 */
      group: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Allows or forbids piece colour on result colour; removes the solutions.
   * (problem.json)
   */
  'problem.setColourRule': {
    params: {
      problem: ProblemId;
      pieceColour: ColourId;
      resultColour: ColourId;
      allowed: boolean;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Sets every colour rule at once: each colour on itself, all on all, or none.
   * (problem.json)
   */
  'problem.setColourRules': {
    params: {
      problem: ProblemId;
      preset: 'eachOnItself' | 'allowAll' | 'clear';
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Whether the pieces can fill the result, and the original's status line.
   * (problem.json)
   */
  'problem.fit': {
    params: ProblemProblemParam;
    result: FitStats;
    errors: 'not_found';
  };
  /**
   * Brick, sliding or stacking; this version solves all three (supported is always true, kept for
   * clients that read it).
   * (problem.json)
   */
  'problem.kind': {
    params: ProblemProblemParam;
    result: {
      kind: 'brick' | 'sliding' | 'stacking';
      supported: boolean;
    };
    errors: 'not_found';
  };
  /**
   * Stores the Solver tab's settings with the problem (saved in the file; the original's
   * cb_SolverOptions). Replaces them all. Not while the problem is being solved.
   * (problem.json)
   */
  'problem.setSolverSettings': {
    params: {
      problem: ProblemId;
      settings: SolverSettings;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * The shapes in list order.
   * (shape.json)
   */
  'shape.list': {
    params: Record<string, never>;
    result: {
      shapes: ShapeSummary[];
    };
    errors: never;
  };
  /**
   * A shape with its non-empty cells (of one layer, when asked).
   * (shape.json)
   */
  'shape.get': {
    params: {
      shape: ShapeId;
      /**
       * Only the cells with this z (the grid editor's XY plane; the cube editor's XZ and YZ planes
       * are the client's: filter the cells by y or x).
       * @minimum 0
       */
      layer?: number;
    };
    result: {
      shape: ShapeSummary;
      cells: ({
        at: Cell;
        state: CellState;
        /** Absent: the shape's own colour. */
        colour?: ColourId;
      })[];
    };
    errors: 'not_found';
  };
  /**
   * Adds an empty shape at the end: of a size in cells, or of the grid's default size. On a
   * stacking puzzle a disc of size 1 (the original's New), and no size may be given
   * (invalid_argument).
   * (shape.json)
   */
  'shape.create': {
    params: {
      size?: Size;
    };
    result: ShapeMade;
    errors: 'invalid_argument' | 'invalid_state';
  };
  /**
   * Adds a copy of a shape at the end.
   * (shape.json)
   */
  'shape.duplicate': {
    params: ShapeParam;
    result: ShapeMade;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Removes a shape; the problems drop it.
   * (shape.json)
   */
  'shape.delete': {
    params: ShapeParam;
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Moves a shape to a place in the list; labels renumber. On a sliding puzzle the boards stay
   * above the pieces (a move across them is refused, invalid_argument), the boards are numbered by
   * place again, and the starts and goals follow; a stamped piece moved past the 63rd place (a
   * mark is six bits) loses them, told in document.changed.notice.
   * (shape.json)
   */
  'shape.move': {
    params: {
      shape: ShapeId;
      /** @minimum 0 */
      toIndex: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The shape's name (empty for none). On a sliding puzzle a start/goal shape's name is the user's
   * part (its START/GOAL number stays; the name '(START/GOAL n)' it shows without one leaves it
   * empty), its problem is named so, and a piece's name may not begin with "sg:" or "__sggoal:"
   * (invalid_argument).
   * (shape.json)
   */
  'shape.rename': {
    params: {
      shape: ShapeId;
      name: string;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The weight: heavier shapes stay in place when taken apart.
   * (shape.json)
   */
  'shape.setWeight': {
    params: {
      shape: ShapeId;
      weight: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * The shape's own colour, or null for the default one.
   * (shape.json)
   */
  'shape.setColour': {
    params: {
      shape: ShapeId;
      rgb: Rgb | null;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Applies a tool to cells, each with the mirror and span options (the grid editor); cells
   * outside the space or not cells of the grid are left out. Several calls inside edit.begin /
   * edit.end are one undo step. Refused (invalid_argument) on a stacking puzzle, whose shapes are
   * discs (stacking.setDiscSize).
   * (shape.json)
   */
  'shape.setCells': {
    params: {
      shape: ShapeId;
      tool: CellTool;
      /** The colour put on the cells; absent: none (the shape's own). */
      colour?: ColourId;
      mirror?: Axes;
      span?: Axes;
      cells: Cell[];
    };
    result: CellsChanged;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The same for every cell of a box, corners included, clamped to the space (the box selection's
   * fill and clear). Refused (invalid_argument) on a stacking puzzle, whose shapes are discs
   * (stacking.setDiscSize).
   * (shape.json)
   */
  'shape.setBox': {
    params: {
      shape: ShapeId;
      tool: CellTool;
      colour?: ColourId;
      mirror?: Axes;
      span?: Axes;
      from: Cell;
      to: Cell;
    };
    result: CellsChanged;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Sets the size in the editor's units (engine.info's sizeUnit); shrinking drops cells outside.
   * With toAll the other shapes grow by as much (they never shrink). Refused (invalid_argument) on
   * a stacking puzzle, whose shapes are discs (stacking.setDiscSize).
   * (shape.json)
   */
  'shape.resize': {
    params: {
      shape: ShapeId;
      size: Size;
      toAll?: boolean;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The tool tabs' operations the document's grid offers, in the original's button order. The
   * sliding grid leaves out what would give a shape more layers (the nudges along Z, the turns
   * about X and Y, double, triple, the grid scale), as the original greyed them out.
   * (shape.json)
   */
  'shape.taskSet': {
    params: Record<string, never>;
    result: {
      tasks: ({
        task: ShapeTask;
        group: 'Flip' | 'Nudge' | 'Rotate' | 'Grid' | 'Shape' | 'Constrain' | 'Tools';
        /** The original's tooltip. */
        label: string;
        /** It can be applied to every shape at once. */
        toAll: boolean;
      })[];
    };
    errors: never;
  };
  /**
   * A tool tab operation on a shape (and, with toAll and a task that allows it, on every shape):
   * flip, rotate, nudge (refused when cells would leave the grid), prune, centre, to origin,
   * minimize, double, triple, fill holes, the surface. Refused (invalid_argument) on a stacking
   * puzzle, whose shapes are discs (stacking.setDiscSize).
   * (shape.json)
   */
  'shape.task': {
    params: {
      shape: ShapeId;
      task: ShapeTask;
      toAll?: boolean;
    };
    result: {
      revision: Revision;
      /** The shape changed at all (cells, size or hotspot). */
      changed: boolean;
      /**
       * Cells whose state or colour changed (fill holes: the holes filled).
       * @minimum 0
       */
      cellsChanged: number;
    };
    errors: 'not_found' | 'unsupported' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The mesh (as shape.mesh, the drawing style) the shape would have after shape.task, worked out
   * on a copy: nothing changes. The client draws it as a ghost over the shape while the pointer is
   * on a tool tab button. The same refusals as shape.task (a nudge out of the grid is
   * invalid_argument), but allowed while solving.
   * (shape.json)
   */
  'shape.taskPreview': {
    params: {
      shape: ShapeId;
      task: ShapeTask;
    };
    result: MeshFrame;
    errors: 'not_found' | 'unsupported' | 'invalid_argument';
  };
  /**
   * The status window's row for one shape, with its status line.
   * (shape.json)
   */
  'shape.info': {
    params: ShapeParam;
    result: ShapeInfo;
    errors: 'not_found';
  };
  /**
   * The status window: a row per shape, in list order.
   * (shape.json)
   */
  'shape.table': {
    params: Record<string, never>;
    result: {
      rows: ShapeInfo[];
    };
    errors: never;
  };
  /**
   * The shape's mesh as the original's 3D view drew it, as a binary frame: in its own orientation,
   * or turned by a transformation of the grid (a Placement's), which is how an assembled solution,
   * a placement of the assembly explorer or a position of the movement explorer is drawn: the mesh
   * of the piece's transformation, at its cell minus the turned shape's hotspot
   * (MeshFrame.hotspot). Allowed while solving.
   * (shape.json)
   */
  'shape.mesh': {
    params: {
      shape: ShapeId;
      /** Default drawing. */
      style?: MeshStyle;
      /**
       * A number of the grid's symmetry table, mirrors included (bricks 0 to 47); absent or 0 is
       * the shape as it is. Beyond the table: invalid_argument.
       * @minimum 0
       */
      transformation?: number;
    };
    result: MeshFrame;
    errors: 'not_found' | 'invalid_argument';
  };
  /**
   * The grid editor's drawing of one layer of a space of the document's grid (at most 65536 cells
   * a layer): each cell's outline (a circle on the sphere grid) and the cell across each edge.
   * Units: a cube 1 by 1, prism triangles of side 1, spheres 1 apart, rhombic 5 by 5 cells and
   * tetrahedra-octahedra 3 by 3 cells to 1 by 1.
   * (shape.json)
   */
  'grid.geometry': {
    params: {
      size: Size;
      /** @minimum 0 */
      layer: number;
    };
    result: {
      width: number;
      height: number;
      cells: ({
        at: Cell;
        /** Counter-clockwise, y up. */
        polygon?: Point2[];
        /** Per edge (corner i to i + 1): the index in cells of the cell across it, -1 for none. */
        neighbours?: number[];
        circle?: {
          centre: Point2;
          radius: number;
        };
      })[];
    };
    errors: 'invalid_argument';
  };
  /**
   * The cell across a side of a cell, in a space of the document's grid (the 3D view's tools that
   * add a cell at a face): the sides are numbered as a MeshFrame's voxelFace (Voxel::getNeighbor,
   * faces). Null when the cell across is outside the space. A cell outside the space or not a cell
   * of the grid, or a side the cell does not have: invalid_argument.
   * (shape.json)
   */
  'grid.neighbour': {
    params: {
      size: Size;
      cell: Cell;
      /**
       * The side, a MeshFrame's voxelFace.
       * @minimum 0
       */
      face: number;
    };
    result: {
      /** The cell across the side, or null when it is outside the space. */
      cell: Cell | null;
    };
    errors: 'invalid_argument';
  };
  /**
   * Adds a start/goal shape (the original's New Start/Goal) of 'size' cells (default 6 by 6 by 1,
   * the original took the selected shape's; a size [x, y] is one layer), all floor, numbered after
   * the others, with its problem.
   * (sliding.json)
   */
  'sliding.addStartGoal': {
    params: {
      size?: TraySize;
    };
    result: {
      shape: ShapeId;
      revision: Revision;
    };
    errors: 'unsupported' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The start/goal editor's click: the whole of a piece as its start (goal false) or its goal,
   * placed from a click on cell 'at' (y rises upwards, as in the grid editor): the piece's slice
   * on that layer with its top-left cell there, at the lowest height that puts a layer of the
   * piece on that layer and fits (on a tray of one layer, the piece's top-left cell there). A
   * piece the problem has one copy of moves there if it was elsewhere; one of several copies is
   * one more copy's start or goal, beside the others, refused once every copy has one; a copy that
   * is exactly there already is taken off. Refused (invalid_argument) with the reason when it does
   * not fit: off the floor, on another piece's or another copy's start or goal, a goal on a
   * corridor, every copy placed. The problems of the start/goal shape lose what they had found.
   * One undo step.
   * (sliding.json)
   */
  'sliding.place': {
    params: {
      tray: ShapeId;
      piece: ShapeId;
      at: TrayCell;
      goal: boolean;
    };
    result: {
      /** The piece is there now (false: it was there and was taken off). */
      placed: boolean;
      revision: Revision;
    };
    errors: 'not_found' | 'unsupported' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Takes off the start or goal of the copy that covers a cell (the editor's right click), not the
   * piece's other copies. One undo step; nothing when none covers it.
   * (sliding.json)
   */
  'sliding.clearAt': {
    params: {
      tray: ShapeId;
      at: TrayCell;
      goal: boolean;
    };
    result: {
      cleared: boolean;
      revision: Revision;
    };
    errors: 'not_found' | 'unsupported' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * What a start/goal shape holds: its number and name, each cell's start and goal (the piece
   * stamped there, cells indexed x + X*(y + Y*z)), each copy placed with its cells, whether it can
   * be solved (the validity bar: null, or why not: starts or goals that are not whole pieces, more
   * of them than the problem has copies, or no piece with every copy at a start and at a goal),
   * the stamps larger than their pieces' copies (null, or the message), and its floor cells.
   * (sliding.json)
   */
  'sliding.tray': {
    params: {
      tray: ShapeId;
    };
    result: {
      /** @minimum 0 */
      number: number;
      /**
       * The user's part of its name; the board shows it, or '(START/GOAL n)' while it is empty.
       */
      name: string;
      size: Size;
      starts: (ShapeId | null)[];
      goals: (ShapeId | null)[];
      error: string | null;
      overflow: string | null;
      /** @minimum 0 */
      floor: number;
      /**
       * Each copy of a piece placed, at a start (goal false) or a goal, its cells sorted lowest
       * layer first, top row first, left to right; marks that are not whole pieces come as one
       * entry.
       */
      placed: ({
        piece: ShapeId;
        goal: boolean;
        cells: Cell[];
      })[];
    };
    errors: 'not_found' | 'unsupported' | 'invalid_argument';
  };
  /**
   * Where sliding.place would put a piece: the cells, every layer (when no height fits, the lowest
   * that puts a layer of the piece on the layer of 'at'; none when no layer of the piece can be
   * there), whether it fits (and the original's reason when not), and whether a copy is exactly
   * there already (then the click takes it off). Changes nothing.
   * (sliding.json)
   */
  'sliding.preview': {
    params: {
      tray: ShapeId;
      piece: ShapeId;
      at: TrayCell;
      goal: boolean;
    };
    result: {
      cells: Cell[];
      fits: boolean;
      why: string | null;
      there: boolean;
    };
    errors: 'not_found' | 'unsupported' | 'invalid_argument';
  };
  /**
   * A slice of the saved solutions, sorted: list (the stored order), assembly (found order),
   * level, moves or pieces (the original's sort buttons); descending sorts by the swapped
   * comparison, so ties keep their order.
   * (solution.json)
   */
  'solution.page': {
    params: {
      problem: ProblemId;
      sortBy?: 'list' | 'assembly' | 'level' | 'moves' | 'pieces';
      descending?: boolean;
      /** @minimum 0 */
      offset?: number;
      /**
       * Default 100.
       * @minimum 1
       * @maximum 10000
       */
      limit?: number;
    };
    result: {
      /** @minimum 0 */
      total: number;
      /** @minimum 0 */
      offset: number;
      solutionsVersion: SolutionsVersion;
      rows: SolutionRow[];
    };
    errors: 'not_found';
  };
  /**
   * A solution: its row and the placement of every piece.
   * (solution.json)
   */
  'solution.get': {
    params: SolutionParam;
    result: {
      row: SolutionRow;
      placements: Placement[];
    };
    errors: 'not_found';
  };
  /**
   * Removes saved solutions: one, all before or after one (in the stored order), all without a
   * disassembly, or all.
   * (solution.json)
   */
  'solution.delete': {
    params: {
      problem: ProblemId;
      which: 'one' | 'before' | 'after' | 'withoutDisassembly' | 'all';
      /** Needed for one, before and after. */
      solution?: SolutionId;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Drops the moves of one solution (or of all), keeping their level summary.
   * (solution.json)
   */
  'solution.deleteDisassembly': {
    params: {
      problem: ProblemId;
      /** Absent: every solution. */
      solution?: SolutionId;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Turns assemblies into new shapes (Convert assemblies to pieces), filtered, optionally added as
   * pieces to a new or an existing problem.
   * (solution.json)
   */
  'solution.toShapes': {
    params: {
      problem: ProblemId;
      /** Absent: every saved solution. */
      solution?: SolutionId;
      dropDisconnected?: boolean;
      /** Shapes whose symmetry contains a mirror. */
      dropMirror?: boolean;
      dropSymmetric?: boolean;
      dropNonMillable?: boolean;
      dropNonNotchable?: boolean;
      dropIdentical?: boolean;
      /** @minimum 0 */
      minCells?: number;
      /** @minimum 0 */
      maxCells?: number;
      addTo?: 'nothing' | 'newProblem' | 'problem';
      /** For addTo problem. */
      target?: ProblemId;
      /** @minimum 0 */
      min?: number;
      /** @minimum 0 */
      max?: number;
    };
    result: {
      shapes: ShapeId[];
      revision: Revision;
    };
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Takes saved assemblies apart afterwards (Add disassembly): all, the ones without the moves
   * stored, or one. A job: returns at once, reports through events, stops with
   * solution.analyseStop; the results go in when it ends (kept when stopped). Refused while the
   * problem is being solved; while it runs, the document is frozen as while solving.
   * (solution.json)
   */
  'solution.analyse': {
    params: {
      problem: ProblemId;
      which: 'all' | 'missing' | 'one';
      /** For one. */
      solution?: SolutionId;
      solver?: Solver;
      /** Try 90 degree turns. */
      rotations?: boolean;
      /**
       * @minimum 0
       * @maximum 256
       */
      threads?: number;
    };
    result: {
      /**
       * Assemblies to take apart.
       * @minimum 0
       */
      total: number;
    };
    errors: 'not_found' | 'invalid_argument' | 'invalid_state' | 'unsupported';
  };
  /**
   * Stops an analysis; what it finished is kept. Does not wait: solution.analyseEnded follows.
   * (solution.json)
   */
  'solution.analyseStop': {
    params: {
      problem: ProblemId;
    };
    result: Empty;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Where an analysis stands.
   * (solution.json)
   */
  'solution.analyseStatus': {
    params: {
      problem: ProblemId;
    };
    result: AnalyseProgress;
    errors: 'not_found';
  };
  /**
   * Solves from the start, dropping what the problem holds of earlier solving (a stacking
   * problem's saved search too: the original's Solve asked first, solve.status.savedSearch tells
   * it). Refused (invalid_argument) with the original's message when the problem cannot be solved
   * as it stands (no result, no pieces; a stacking problem without a rod set, with neither
   * disassemble nor justCount, or whose stacks break its rules), and (unsupported) when its grid
   * or solver cannot take it.
   * (solve.json)
   */
  'solve.start': {
    params: Launch;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state' | 'unsupported';
  };
  /**
   * Drops what the problem holds, builds the search (placements, reduce) and pauses. A stacking
   * search has nothing to build (unsupported).
   * (solve.json)
   */
  'solve.prepare': {
    params: Launch;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state' | 'unsupported';
  };
  /**
   * Carries on a paused solve, one saved in the file, a stacking problem's saved search, or an
   * unsolved problem from the start.
   * (solve.json)
   */
  'solve.continue': {
    params: Launch;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state' | 'unsupported';
  };
  /**
   * Asks the run to pause where it can be continued; nothing is lost. Does not wait: solve.state
   * 'paused' follows. Refused when no run is under way.
   * (solve.json)
   */
  'solve.pause': {
    params: SolveProblemParam;
    result: Empty;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Asks the assembly search alone to pause where the solve can be saved; the take-aparts under
   * way finish, then 'paused'. The autosave: save, then solve.continue. Refused (unsupported) on a
   * stacking solve, which the original never autosaved: its search saves itself every 20 minutes
   * (StackingOptions.autosave) and when it stops.
   * (solve.json)
   */
  'solve.savePoint': {
    params: SolveProblemParam;
    result: Empty;
    errors: 'not_found' | 'invalid_state' | 'unsupported';
  };
  /**
   * The hard stop of a solve under way (a run active, or paused in memory): throws it away
   * (results, paused state, saved position) and waits for the run to end. Refused (invalid_state)
   * when the problem has no such solve, so a finished run's solutions are never lost by a late
   * click: remove them with solution.delete {which: all}, which can be undone; a paused solve from
   * a file is carried on with solve.continue or started over with solve.start. Two
   * document.changed follow: the abort's own, then the run's end from the poll. A stacking problem
   * whose search is saved (solve.status.savedSearch) has a solve under way though no run is in
   * memory, as the original's Abort judged it: the search and the problem's results go, and one
   * document.changed follows.
   * (solve.json)
   */
  'solve.abort': {
    params: SolveProblemParam;
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Where the solve stands, its statistics, and why it failed.
   * (solve.json)
   */
  'solve.status': {
    params: SolveProblemParam;
    result: {
      state: SolveState;
      progress: SolveProgress;
      stats: SolveStats;
      failure: SolveFailure | null;
      /**
       * What a sliding or stacking solve's last run said when it was through, or a stacking search
       * when it paused: whether a missing solution is proven, the search gave up at a limit, or
       * where it was saved (the original's solver note, shown in a message box); null otherwise.
       */
      note: string | null;
      /** Null while the problem's run is active, when there is none, and on other grids. */
      savedSearch: {
        /** e.g. 'a saved search 200 moves deep, 3.4M stackings'. */
        description: string;
        /**
         * The solving time it holds.
         * @minimum 0
         */
        ms: number;
        /**
         * The bytes its levels take (solve.exportPaused withLevels; the original asked about them
         * beyond 1 GB).
         * @minimum 0
         */
        levelBytes: number;
      } | null;
    };
    errors: 'not_found';
  };
  /**
   * Writes a paused solve as a .slsolve file (the original's BTSOLVE2: the puzzle and the
   * problem's number) to carry it on elsewhere. A stacking problem is paused when its search is
   * saved (solve.status.savedSearch): the file carries the search, packed.
   * (solve.json)
   */
  'solve.exportPaused': {
    params: {
      problem: ProblemId;
      /** @minLength 1 */
      path: string;
      /**
       * A stacking search's saved levels go into the file too (default true): left out, the file
       * is far smaller, but finding the path at the end takes longer.
       */
      withLevels?: boolean;
    };
    result: Empty;
    errors: 'not_found' | 'invalid_state' | 'io_error';
  };
  /**
   * Opens a .slsolve file as a new, untitled document (the open one goes as with document.new);
   * solve.continue on the problem carries the solve on. A stacking search it carries goes into its
   * folder in the engine's panexFolder, replacing what was there; without a panexFolder such a
   * file is refused (parse_error), and so is any import (invalid_state) while the open document is
   * a stacking puzzle being solved, whose search would write the same folder as it stops.
   * (solve.json)
   */
  'solve.importPaused': {
    params: {
      /** @minLength 1 */
      path: string;
    };
    result: {
      problem: ProblemId;
      document: Snapshot;
    };
    errors: 'io_error' | 'parse_error' | 'invalid_state';
  };
  /**
   * Adds a rod set with the default rules (the original's New: three rods that grow, size
   * matters), unnamed, at the end. One undo step.
   * (stacking.json)
   */
  'stacking.addRodSet': {
    params: Record<string, never>;
    result: {
      rodSet: RodSetId;
      revision: Revision;
    };
    errors: 'unsupported' | 'invalid_state';
  };
  /**
   * Adds a copy of a rod set, its name and rules, at the end (the original's Copy).
   * (stacking.json)
   */
  'stacking.duplicateRodSet': {
    params: {
      rodSet: RodSetId;
    };
    result: {
      rodSet: RodSetId;
      revision: Revision;
    };
    errors: 'unsupported' | 'not_found' | 'invalid_state';
  };
  /**
   * Removes a rod set (the original's Delete); the problems on it stand on none afterwards, their
   * stacks kept. The last one is kept (invalid_argument: 'Keep at least one rod set').
   * (stacking.json)
   */
  'stacking.deleteRodSet': {
    params: {
      rodSet: RodSetId;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Names a rod set (the original's Label); names cannot hold line breaks or control characters.
   * (stacking.json)
   */
  'stacking.renameRodSet': {
    params: {
      rodSet: RodSetId;
      name: string;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Moves a rod set to a place in the list, one place at a time as the original's arrows did;
   * labels renumber, the problems keep their board.
   * (stacking.json)
   */
  'stacking.moveRodSet': {
    params: {
      rodSet: RodSetId;
      /** @minimum 0 */
      toIndex: number;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Sets a rod set's rules (the Rods panel's fields): the fields given, over the rod set's as
   * shown (RodSet). The pocket column needs Panex columns, the outside channel Panex columns and
   * three rods or more: without them they are off. The problems on the rod set get its rods in
   * their stacks (discs on rods past the count are kept, setupError tells) and lose their
   * solutions. Refused while anything is solved. One undo step.
   * (stacking.json)
   */
  'stacking.setRules': {
    params: {
      rodSet: RodSetId;
      rules: Rules;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_state';
  };
  /**
   * A disc's size (the disc list's size field, 1 to 100): the disc is made anew, a square of 2
   * size + 1 cells one cell thick with its centre empty, its colour kept. Solutions stay, as the
   * original kept them. One undo step.
   * (stacking.json)
   */
  'stacking.setDiscSize': {
    params: {
      shape: ShapeId;
      /**
       * @minimum 1
       * @maximum 100
       */
      size: number;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * The board a problem stands on (the original's Set Start/Goal Rods); its stacks get the board's
   * rods. Its solutions stay.
   * (stacking.json)
   */
  'stacking.setProblemRodSet': {
    params: {
      problem: ProblemId;
      rodSet: RodSetId;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_state';
  };
  /**
   * Puts a copy of a disc on top of a rod of the start or the goal (the Problems tab's Add): the
   * next copy not on that stacking, or one copy more when every copy is (the count of the disc
   * follows). The rules are not checked here, so a stacking can be built step by step:
   * stacking.board's error tells what breaks them. Refused (invalid_argument) for a problem
   * without a rod set ('Choose a rod set with Set Start/Goal Rods') and a rod not on the board.
   * (stacking.json)
   */
  'stacking.placeDisc': {
    params: {
      problem: ProblemId;
      shape: ShapeId;
      /**
       * A rod of the problem's stack map (the pocket column is the last).
       * @minimum 0
       */
      rod: number;
      /** The goal's stacking; false the start's. */
      goal: boolean;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Puts a copy of every disc on a rod, largest first so a size rule can still stack them (the
   * Problems tab's Add all); what went before a refusal stays.
   * (stacking.json)
   */
  'stacking.placeAll': {
    params: {
      problem: ProblemId;
      /**
       * A rod of the problem's stack map (the pocket column is the last).
       * @minimum 0
       */
      rod: number;
      /** The goal's stacking; false the start's. */
      goal: boolean;
    };
    result: {
      /** @minimum 0 */
      placed: number;
      /** Why the next disc did not go; null when all went. */
      error: string | null;
      revision: Revision;
    };
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Takes a disc off (the Problems tab's Remove): that copy goes from the start and the goal, the
   * later copies renumbered, one copy fewer. Refused (invalid_argument) on an empty rod ('That rod
   * is empty').
   * (stacking.json)
   */
  'stacking.removeDisc': {
    params: {
      problem: ProblemId;
      /** The goal's stacking; false the start's. */
      goal: boolean;
      /**
       * A rod of the problem's stack map (the pocket column is the last).
       * @minimum 0
       */
      rod: number;
      /**
       * The disc's place on the rod, from the bottom (0).
       * @minimum 0
       */
      index: number;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * Empties the start's or the goal's rods (the Problems tab's Remove all); the counts stay,
   * shared by both. The problem loses its solutions. Nothing to do on an empty stacking.
   * (stacking.json)
   */
  'stacking.clear': {
    params: {
      problem: ProblemId;
      /** The goal's stacking; false the start's. */
      goal: boolean;
    };
    result: Changed;
    errors: 'unsupported' | 'not_found' | 'invalid_state';
  };
  /**
   * Swaps a disc with the one above (delta 1) or below (-1) it on its rod (the Problems tab's
   * arrows). Its solutions stay.
   * (stacking.json)
   */
  'stacking.moveDisc': {
    params: {
      problem: ProblemId;
      /** The goal's stacking; false the start's. */
      goal: boolean;
      /**
       * A rod of the problem's stack map (the pocket column is the last).
       * @minimum 0
       */
      rod: number;
      /**
       * The disc's place on the rod, from the bottom (0).
       * @minimum 0
       */
      index: number;
      /**
       * @minimum -1
       * @maximum 1
       */
      delta: number;
    };
    result: {
      /**
       * The disc's place now.
       * @minimum 0
       */
      index: number;
      revision: Revision;
    };
    errors: 'unsupported' | 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * A problem's board as the Problems tab showed it: its rods (names, the pocket, capacities), the
   * start's or the goal's stacks, where the rods and discs stand, the validity bar's sentence, and
   * the centre a playback's poses are placed around (playback.json: a stacking solution is centred
   * on its board).
   * (stacking.json)
   */
  'stacking.board': {
    params: {
      problem: ProblemId;
      /** The goal's stacking; false the start's. */
      goal: boolean;
    };
    result: {
      rodSet: RodSetId | null;
      /** The board's rods in the stack map's order; empty without a rod set. */
      rods: Rod[];
      stacks: Stacks;
      /** Null without a rod set. */
      layout: BoardLayout | null;
      /**
       * Why the problem cannot be solved as it stands (the original's setupError: a rule broken, a
       * disc missing from the start or the goal, no rod set); null when it is ready.
       */
      error: string | null;
      /** The middle of the board, half the rods' height up, in cells; null without a rod set. */
      centre: [number, number, number] | null;
    };
    errors: 'unsupported' | 'not_found';
  };
  /**
   * A rod set drawn alone, without discs (the Entities tab's view of it): rods that grow drawn
   * five high, spaced for the puzzle's largest disc.
   * (stacking.json)
   */
  'stacking.rodSetLayout': {
    params: {
      rodSet: RodSetId;
    };
    result: BoardLayout;
    errors: 'not_found';
  };
}

/** Every event (notification) the engine sends. */
export interface Events {
  /**
   * The document changed: after a call that changed it, and when a solve run ended or an analysis
   * was applied. Empty id lists mean the lists themselves changed (or anything of that kind).
   * (document.json)
   */
  'document.changed': {
    params: {
      revision: Revision;
      what: ChangeKind[];
      shapes: ShapeId[];
      problems: ProblemId[];
      /**
       * What the change did beyond what was asked, to tell the user: a sliding piece whose new
       * shape lost its starts and goals. Only when there is something to tell.
       */
      notice?: string;
    };
  };
  /**
   * Announces a binary frame: exactly 'bytes' raw bytes follow this line at once; the reply that
   * refers to it comes after.
   * (engine.json)
   */
  'frame': {
    params: {
      /** @minimum 1 */
      frame: number;
      /** @minimum 0 */
      bytes: number;
    };
  };
  /**
   * An analysis's progress; at most four times a second.
   * (solution.json)
   */
  'solution.analyseProgress': {
    params: AnalyseEvent;
  };
  /**
   * An analysis ended (through, stopped, or ended by an internal error: failure says what); its
   * results are in the problem (document.changed follows). The problems of a document that is
   * replaced get one with stopped true after the replacing call's reply.
   * (solution.json)
   */
  'solution.analyseEnded': {
    params: AnalyseEvent;
  };
  /**
   * A solve's state changed, as the engine's poll saw it: states a run passes through within one
   * poll (50 ms) are skipped, the last state of a run (finished, paused, error, or idle after an
   * abort) is always sent, and it comes before the document.changed that gives the run's end its
   * revision. A client waits for that revision, not the state, before reloading the lists. The
   * problems of a document that is replaced get a last 'idle' after the replacing call's reply.
   * (solve.json)
   */
  'solve.state': {
    params: {
      problem: ProblemId;
      state: SolveState;
    };
  };
  /**
   * Where a running solve stands; at most four times a second, and once when a run ends (after its
   * last solve.state). The live counters of the Solver card (assemblies, solutions, time) come
   * from here.
   * (solve.json)
   */
  'solve.progress': {
    params: {
      problem: ProblemId;
      state: SolveState;
      progress: SolveProgress;
    };
  };
  /**
   * The saved solutions changed while solving (new ones, or the list thinned or re-sorted): page
   * them again. At most four times a second, and once when the run ends; the last one of a run
   * carries the final count. The counts are those at the time; the live counters are
   * solve.progress's.
   * (solve.json)
   */
  'solve.solutionsAdded': {
    params: {
      problem: ProblemId;
      /** @minimum 0 */
      saved: number;
      solutionsVersion: SolutionsVersion;
      /** @minimum 0 */
      assemblies: number;
      /** @minimum 0 */
      solutions: number;
    };
  };
  /**
   * A run ended in error.
   * (solve.json)
   */
  'solve.error': {
    params: {
      problem: ProblemId;
      failure: SolveFailure;
    };
  };
}

/** The methods of the schema (engine.info's methods must be these). */
export const METHOD_NAMES = [
  'colour.add',
  'colour.delete',
  'colour.edit',
  'colour.list',
  'document.convertGrid',
  'document.exportPuzzlecad',
  'document.get',
  'document.import',
  'document.new',
  'document.open',
  'document.openBytes',
  'document.save',
  'document.saveBytes',
  'document.setNotes',
  'edit.begin',
  'edit.end',
  'edit.history',
  'edit.redo',
  'edit.undo',
  'engine.hello',
  'engine.info',
  'engine.shutdown',
  'explore.addMove',
  'explore.analyse',
  'explore.close',
  'explore.findMatching',
  'explore.node',
  'explore.openAssembly',
  'explore.openMovements',
  'explore.placements',
  'explore.prune',
  'explore.step',
  'export.solutionPieces',
  'export.stl',
  'export.stlMesh',
  'export.stlParameters',
  'grid.geometry',
  'grid.neighbour',
  'playback.close',
  'playback.mesh',
  'playback.open',
  'playback.poses',
  'playback.track',
  'problem.addOneOfEach',
  'problem.addPiece',
  'problem.create',
  'problem.delete',
  'problem.deleteGroup',
  'problem.duplicate',
  'problem.fit',
  'problem.get',
  'problem.kind',
  'problem.list',
  'problem.move',
  'problem.movePart',
  'problem.removeAllPieces',
  'problem.removePiece',
  'problem.rename',
  'problem.setColourRule',
  'problem.setColourRules',
  'problem.setGroup',
  'problem.setGroupCount',
  'problem.setPieceCount',
  'problem.setRangeAll',
  'problem.setResult',
  'problem.setSolverSettings',
  'settings.get',
  'settings.set',
  'shape.create',
  'shape.delete',
  'shape.duplicate',
  'shape.get',
  'shape.info',
  'shape.list',
  'shape.mesh',
  'shape.move',
  'shape.rename',
  'shape.resize',
  'shape.setBox',
  'shape.setCells',
  'shape.setColour',
  'shape.setWeight',
  'shape.table',
  'shape.task',
  'shape.taskPreview',
  'shape.taskSet',
  'sliding.addStartGoal',
  'sliding.clearAt',
  'sliding.place',
  'sliding.preview',
  'sliding.tray',
  'solution.analyse',
  'solution.analyseStatus',
  'solution.analyseStop',
  'solution.delete',
  'solution.deleteDisassembly',
  'solution.get',
  'solution.page',
  'solution.toShapes',
  'solve.abort',
  'solve.continue',
  'solve.exportPaused',
  'solve.importPaused',
  'solve.pause',
  'solve.prepare',
  'solve.savePoint',
  'solve.start',
  'solve.status',
  'stacking.addRodSet',
  'stacking.board',
  'stacking.clear',
  'stacking.deleteRodSet',
  'stacking.duplicateRodSet',
  'stacking.moveDisc',
  'stacking.moveRodSet',
  'stacking.placeAll',
  'stacking.placeDisc',
  'stacking.removeDisc',
  'stacking.renameRodSet',
  'stacking.rodSetLayout',
  'stacking.setDiscSize',
  'stacking.setProblemRodSet',
  'stacking.setRules',
] as const satisfies readonly (keyof Methods)[];

/** The events of the schema. */
export const EVENT_NAMES = [
  'document.changed',
  'frame',
  'solution.analyseEnded',
  'solution.analyseProgress',
  'solve.error',
  'solve.progress',
  'solve.solutionsAdded',
  'solve.state',
] as const satisfies readonly (keyof Events)[];

/**
 * The error codes of decision 0012 by errc (data.errc of every error reply).
 * parse_error has two: -32700 for a line that is not JSON, -32003 for a file or
 * bytes that are no puzzle.
 */
export const ERROR_CODES = {
  parse_error: [-32700, -32003],
  invalid_request: [-32600],
  method_not_found: [-32601],
  invalid_params: [-32602],
  internal: [-32603],
  invalid_argument: [-32001],
  not_found: [-32002],
  io_error: [-32004],
  unsupported: [-32005],
  invalid_state: [-32006],
} as const;

/** The errcs the engine answers with. */
export type Errc = keyof typeof ERROR_CODES;

/** The methods whose result carries a binary frame, and the property that holds it. */
export const FRAME_PATHS = {
  'export.stl': 'frame',
  'export.stlMesh': 'frame',
  'playback.mesh': 'frame',
  'playback.poses': 'frame',
  'playback.track': 'frame',
  'shape.mesh': 'frame',
  'shape.taskPreview': 'frame',
} as const satisfies Partial<Record<keyof Methods, string>>;

/** A method whose result carries a binary frame. */
export type FrameMethod = keyof typeof FRAME_PATHS;
