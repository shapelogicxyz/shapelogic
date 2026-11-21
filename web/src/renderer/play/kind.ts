// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the Play workspace asks of a kind of puzzle it plays (decisions
// 0039 and 0042). The workspace (workspace.ts) keeps what every game
// shares: the problem played, each problem's game, Undo and Redo, the
// playback, the buttons and the moves file's dialogs. A kind (sliding.ts,
// stacking.ts) knows what a start and a move are: it reads a problem's
// start from the engine, draws its board and makes the moves on it, draws
// a move again, and writes and reads its moves as a file's text.

import type { AnyGame } from './game.ts';

/** What a kind asks of the workspace. */
export interface PlayKindHost {
  /** The problem played, or null. */
  problem(): string | null;
  /** The game shown, or null. */
  game(): AnyGame | null;
  /** A move was made on the board: `next` is the game after it. */
  moved(next: AnyGame): void;
  /** The playback stops: a piece was taken hold of. */
  pause(): void;
  /** What the kind shows of its own changed: the page is drawn again. */
  render(): void;
}

/** A problem's start as a kind read it. */
export interface Loaded {
  /** The game to show: the one the problem had gone on with, a new one, or null when it cannot be played. */
  game: AnyGame | null;
  /** The game the problem had could not go on: one of its moves can no longer be made. */
  dropped: boolean;
}

/** A kind of puzzle played by hand. */
export interface PlayKind {
  /** The grid whose puzzles it plays. */
  readonly grid: string;
  /** What moves on its board: "piece" and "pieces", "disc" and "discs". */
  readonly noun: { one: string; many: string };
  /** How a move is made, for the status bar: "drag a piece to slide it". */
  readonly hint: string;
  /** What to say while the puzzle has no problem. */
  readonly noProblems: string;
  /** What a problem that cannot be played needs, under the reason. */
  readonly needs: string;
  /** The note under Save position as Start and as Goal. */
  readonly saveNote: string;

  /** Takes its place in the workspace (its board and its buttons beside it), or leaves it. */
  mount(on: boolean): void;
  /** Forgets the problem it read. */
  clear(): void;
  /**
   * Reads how `problem`'s game starts and makes its game: `had`, the game
   * the problem had, goes on when the start is the same and its moves can
   * still be made. Null when `live` says the read was overtaken.
   */
  load(problem: string, had: AnyGame | undefined, live: () => boolean): Promise<Loaded | null>;
  /** The problem's start was read. */
  readonly ready: boolean;
  /** Why the problem read cannot be played; null when it can, and before it is read. */
  readonly error: string | null;
  /** The problem read has a goal to play to. */
  readonly hasGoal: boolean;
  /** How many pieces the problem read has. */
  readonly count: number;
  /** A note on the problem's rules, under the count of moves; '' for none. */
  note(): string;

  /** Draws the board with `game`'s position (null: no board). */
  show(game: AnyGame | null): void;
  /** Puts the pieces at `game`'s position (the board drawn stays). */
  place(game: AnyGame): void;
  /** A piece is being dragged, or a move is being drawn. */
  readonly busy: boolean;
  /** Cuts a move being drawn short. */
  stop(): void;
  /** Reads what drawing `game`'s moves needs (a sliding move's route), once for a list of moves. */
  routes(game: AnyGame): Promise<void>;
  /**
   * Draws move number `index` of `game` being made, or with `back` being
   * taken back; the caller places the pieces after. Resolves false when it
   * was cut short.
   */
  animate(game: AnyGame, index: number, back: boolean): Promise<boolean>;
  /** Draws what is the kind's own beside the board (its buttons, a layer strip) for `game`. */
  renderOwn(game: AnyGame | null): void;

  /** Makes `game`'s position `problem`'s Start (goal false) or Goal: the engine's call. */
  save(
    problem: string,
    goal: boolean,
    game: AnyGame,
  ): Promise<{ changed: boolean; revision: number }>;
  /** The moves of `game` as a moves file's text. */
  exportText(game: AnyGame, names: { puzzle: string; problem: string }): string;
  /**
   * The game a moves file's text gives for `problem`, at its start: taken
   * when the file is of this board and start and the engine can make every
   * move. Otherwise why not, as a sentence for the user; null when the
   * problem was read again meanwhile.
   */
  importMoves(text: string, problem: string): Promise<AnyGame | string | null>;
}
