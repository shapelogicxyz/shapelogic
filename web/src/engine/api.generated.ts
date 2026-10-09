// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generated from api/*.json by web/tools/gen-api.ts: do not edit. `just web-gen`
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
}

// ---- explore.json: Explorers ----

export interface NodeParam {
  handle: MovementExplorerHandle;
  /** @minimum 0 */
  node: number;
}

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

/**
 * The grid of a puzzle, fixed for its life. Sliding and stacking puzzles load and save but are not
 * solved yet.
 */
export type Grid =
  | 'bricks'
  | 'prisms'
  | 'spheres'
  | 'rhombic'
  | 'tetraOcta'
  | 'sliding'
  | 'stacking';

/** BurrTools Classic, Andrew Crowell's take-apart, BurrTools 2. */
export type Solver = 'classic' | 'crowell' | 'bt2';

/** decision 0004; 'stopping' is an active state with a stop requested (SolveProgress.stage). */
export type SolveState =
  | 'idle'
  | 'preparing'
  | 'assembling'
  | 'draining'
  | 'paused'
  | 'finished'
  | 'error';

export type SolveStage =
  | 'idle'
  | 'prepare'
  | 'reduce'
  | 'assemble'
  | 'disassemble'
  | 'stopping'
  | 'paused'
  | 'done'
  | 'error';

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
 * The 3D view's bevelled cells, the same closed with the wire-frame faces flagged, or flat faces
 * with edge lines.
 */
export type MeshStyle = 'drawing' | 'wireframe' | 'flat';

export type ChangeKind = 'shapes' | 'colours' | 'problems' | 'solutions' | 'notes';

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
}

export interface ShapeSummary {
  id: ShapeId;
  /** S1, S2, ... by place. */
  label: string;
  name: string;
  size: Size;
  /** @minimum 0 */
  fixed: number;
  /** @minimum 0 */
  variable: number;
  weight: number;
  /** The shape's own colour, or null for the default one. */
  colour: Rgb | null;
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
}

/**
 * The Solver tab's settings stored with a problem (saved in the file, the original's names; the
 * front end decides what they mean). Settings of other versions are kept as strings. These names
 * are the file's and stay: SolveOptions uses the API's (deepSymmetry is completeRotations,
 * strictColors is strictColours, justLevels is justLevels). highMemory belongs to the sliding and
 * stacking solvers and maps to no SolveOptions field.
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
  highMemory?: boolean;
  autosave?: boolean;
  /** classic, crowell, bt2 (sliding: fast, deep, full). */
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
  /** The original's status text. */
  activity: string;
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
   * the problems with shapes using it lose their solutions.
   * (colour.json)
   */
  'colour.delete': {
    params: {
      colour: ColourId;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
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
   * MiB is refused as parse_error), base64; the document has no path. The open document goes as
   * with document.new.
   * (document.json)
   */
  'document.openBytes': {
    params: {
      data: string;
    };
    result: Snapshot;
    errors: 'invalid_argument' | 'parse_error';
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
      name: 'bt-engine';
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
      name: 'bt-engine';
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
        /** This version solves its puzzles (sliding and stacking: not yet). */
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
      })[];
      solvers: ({
        solver: Solver;
        label: string;
      })[];
      /** @minimum 1 */
      hardwareThreads: number;
      /** @minimum 1 */
      maxThreads: number;
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
   * Adds a problem; it allows each colour on itself.
   * (problem.json)
   */
  'problem.create': {
    params: Record<string, never>;
    result: ProblemMade;
    errors: 'invalid_state';
  };
  /**
   * Adds a copy of a problem's definition (no name, no solutions).
   * (problem.json)
   */
  'problem.duplicate': {
    params: ProblemProblemParam;
    result: ProblemMade;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Removes a problem.
   * (problem.json)
   */
  'problem.delete': {
    params: ProblemProblemParam;
    result: Changed;
    errors: 'not_found' | 'invalid_state';
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
   * The problem's name.
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
   * (problem.json)
   */
  'problem.setResult': {
    params: ProblemShape;
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * The count range of a shape as pieces; max 0 removes it. The solutions stay, the counts become
   * unknown.
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
   * One more of a shape (minimum and maximum).
   * (problem.json)
   */
  'problem.addPiece': {
    params: ProblemShape;
    result: Changed;
    errors: 'not_found' | 'invalid_argument' | 'invalid_state';
  };
  /**
   * One fewer of a shape.
   * (problem.json)
   */
  'problem.removePiece': {
    params: ProblemShape;
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * One count range for every piece (the bulk range; a maximum below the minimum is raised to it).
   * Removes the solutions.
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
   * One more of every shape but the result.
   * (problem.json)
   */
  'problem.addOneOfEach': {
    params: ProblemProblemParam;
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Removes every piece and the solutions.
   * (problem.json)
   */
  'problem.removeAllPieces': {
    params: ProblemProblemParam;
    result: Changed;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Moves a shape's part one place earlier (-1) or later (+1): the pieces renumber.
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
   * Puts all pieces of a shape in one exclusive group, or none (0).
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
   * Puts 'count' pieces of a shape in group 'group' (0 takes them out): the groups editor.
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
   * Removes a group from every shape.
   * (problem.json)
   */
  'problem.deleteGroup': {
    params: {
      problem: ProblemId;
      /** @minimum 1 */
      group: number;
    };
    result: Changed;
    errors: 'not_found' | 'invalid_state';
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
   * Brick, sliding or stacking; this version solves bricks only.
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
   * Adds an empty shape at the end: of a size in cells, or of the grid's default size.
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
   * Moves a shape to a place in the list; labels renumber.
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
   * The shape's name (empty for none).
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
   * edit.end are one undo step.
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
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * The same for every cell of a box, corners included, clamped to the space (the box selection's
   * fill and clear).
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
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * Sets the size in the editor's units (engine.info's sizeUnit); shrinking drops cells outside.
   * With toAll the other shapes grow by as much (they never shrink).
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
   * The tool tabs' operations the document's grid offers, in the original's button order.
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
   * minimize, double, triple, fill holes, the surface.
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
   * of the piece's transformation, at its cell minus the turned shape's hotspot. Allowed while
   * solving.
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
   * Solves from the start, dropping what the problem holds of earlier solving.
   * (solve.json)
   */
  'solve.start': {
    params: Launch;
    result: Changed;
    errors: 'not_found' | 'invalid_state' | 'unsupported';
  };
  /**
   * Drops what the problem holds, builds the search (placements, reduce) and pauses.
   * (solve.json)
   */
  'solve.prepare': {
    params: Launch;
    result: Changed;
    errors: 'not_found' | 'invalid_state' | 'unsupported';
  };
  /**
   * Carries on a paused solve, one saved in the file, or an unsolved problem from the start.
   * (solve.json)
   */
  'solve.continue': {
    params: Launch;
    result: Changed;
    errors: 'not_found' | 'invalid_state' | 'unsupported';
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
   * way finish, then 'paused'. The autosave: save, then solve.continue.
   * (solve.json)
   */
  'solve.savePoint': {
    params: SolveProblemParam;
    result: Empty;
    errors: 'not_found' | 'invalid_state';
  };
  /**
   * The hard stop of a solve under way (a run active, or paused in memory): throws it away
   * (results, paused state, saved position) and waits for the run to end. Refused (invalid_state)
   * when the problem has no such solve, so a finished run's solutions are never lost by a late
   * click: remove them with solution.delete {which: all}, which can be undone; a paused solve from
   * a file is carried on with solve.continue or started over with solve.start. Two
   * document.changed follow: the abort's own, then the run's end from the poll.
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
    };
    errors: 'not_found';
  };
  /**
   * Writes a paused solve as a .btsolve file (the original's BTSOLVE2: the puzzle and the
   * problem's number) to carry it on elsewhere.
   * (solve.json)
   */
  'solve.exportPaused': {
    params: {
      problem: ProblemId;
      /** @minLength 1 */
      path: string;
    };
    result: Empty;
    errors: 'not_found' | 'invalid_state' | 'io_error';
  };
  /**
   * Opens a .btsolve file as a new, untitled document (the open one goes as with document.new);
   * solve.continue on the problem carries the solve on.
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
    errors: 'io_error' | 'parse_error';
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
  'document.get',
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
  'grid.geometry',
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
  'shape.taskSet',
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
  'playback.mesh': 'frame',
  'playback.poses': 'frame',
  'playback.track': 'frame',
  'shape.mesh': 'frame',
} as const satisfies Partial<Record<keyof Methods, string>>;

/** A method whose result carries a binary frame. */
export type FrameMethod = keyof typeof FRAME_PATHS;
