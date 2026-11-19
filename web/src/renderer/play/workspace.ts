// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Play workspace (decision 0039): a sliding problem played by hand.
// The left card has the problems, the count of moves, Restart, Save
// position as Start and as Goal, and the moves file's Export and Import;
// the centre has the board (board.ts) with Undo and Redo beside it, the
// "Solved!" banner, a layer strip for a deeper board and the playback bar.
// The tab is shown only while a sliding puzzle is open.
//
// The engine is asked what a position allows and keeps no game
// (sliding.playStart, playReach, playReplay); the game is this page's
// (game.ts): the start, the moves and the place in them shown, kept for
// each problem while the document is open and not saved in the puzzle's
// file (the moves file is how a game is kept). After every change of the
// document the start is read again: a game of the same start is checked
// move by move and goes on, any other starts anew. Ctrl+Z and Shift+Ctrl+Z
// are the game's here, never the document's. A position saved as the
// Start or the Goal is a change of the document (sliding.setLayout), one
// undo step there, refused while a solve runs.

import type {
  Methods,
  ProblemDetail,
  ProblemSummary,
  ShapeSummary,
} from '../../engine/api.generated.ts';
import type { BtApp } from '../../engine/app-bridge.ts';
import type { Edits } from '../state/edits.ts';
import type { Store } from '../state/store.ts';
import { problemChipStyle } from '../puzzle/counts.ts';
import { askConfirm } from '../shell/confirm.ts';
import { tell } from '../shell/dialog.ts';
import { byId, esc, markSelected, setHtml } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { ic } from '../shell/icons.ts';
import { onKey } from '../shell/keyboard.ts';
import { onLayout, setTab, setWorkspaceShown, tab } from '../shell/layout.ts';
import { copyColour, fromHex, shapeColour } from '../shell/piece-colours.ts';
import { onPieceImage, pieceImage } from '../shell/piece-images.ts';
import { onPrefs } from '../shell/prefs.ts';
import { flash, reportError, updateStatus } from '../shell/status.ts';
import { readOptions } from '../solver/settings.ts';
import { PlayBoard } from './board.ts';
import type { BoardModel, ReachGroup } from './board.ts';
import { made, movesText, newGame, placesAt, seek, solvedAt, withMoves } from './game.ts';
import type { Cell, Game, Move } from './game.ts';
import { boardOf, matchStart, parse, remap, serialise } from './moves-file.ts';

type PlayStart = Methods['sliding.playStart']['result'];
type Step = Methods['sliding.playReplay']['result']['steps'][number];
type Rules = NonNullable<Methods['sliding.playReach']['params']['rules']>;

/** The playback's glyphs (the Solver player's, solver/solutions-card.ts). */
const TG = {
  start:
    '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M6 5h2v14H6zM20 5v14L9 12z"/></svg>',
  back: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M18 5v14L7 12z"/></svg>',
  play: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M7 4.5v15L19 12z"/></svg>',
  pause:
    '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M6 5h4v14H6zM14 5h4v14h-4z"/></svg>',
  fwd: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M6 5v14l11-7z"/></svg>',
  end: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M16 5h2v14h-2zM4 5v14l11-7z"/></svg>',
};

/** The pause between two moves played back, in ms. */
const PLAY_GAP_MS = 160;

/** What the workspace needs of the shell. */
export interface PlayHost {
  /** The puzzle's file name, for the moves file's. */
  readonly name: string;
}

const cell = (c: readonly number[]): Cell => [c[0] ?? 0, c[1] ?? 0, c[2] ?? 0];

export class PlayWorkspace {
  /** The problem played, or null. */
  current: string | null = null;
  private readonly store: Store;
  private readonly edits: Edits;
  private readonly app: BtApp;
  private readonly host: PlayHost;
  private readonly board: PlayBoard;
  /** How the problem's game starts, as last read (null: not read yet). */
  private start: PlayStart | null = null;
  private detail: ProblemDetail | null = null;
  private game: Game | null = null;
  /** Each problem's game, while the document is open. */
  private readonly games = new Map<string, Game>();
  private generation = -1;
  private layer = 0;
  private loads = 0;
  /** The document changed while the tab was out of sight. */
  private stale = true;
  /** The routes of a game's moves (sliding.playReplay), by its list of moves. */
  private routes: { moves: readonly Move[]; steps: Step[] } | null = null;
  private playing = false;
  /** Counts the seeks: one under way gives up when another began. */
  private seeks = 0;
  /** The steps asked for run one after another: the last of them, and how many wait. */
  private stepping: Promise<boolean> = Promise.resolve(true);
  private queued = 0;

  constructor(store: Store, edits: Edits, app: BtApp, host: PlayHost) {
    this.store = store;
    this.edits = edits;
    this.app = app;
    this.host = host;
    this.board = new PlayBoard(byId('playBoard'), byId('plMid'), {
      reach: (piece) => this.reach(piece),
      moved: (move, solved) => {
        this.moved(move, solved);
      },
    });
    byId('plUndo').innerHTML = `${ic('ccw', 22)}<span>Undo</span>`;
    byId('plRedo').innerHTML = `${ic('cw', 22)}<span>Redo</span>`;
    this.listen();
    store.subscribe((c) => {
      if (c.kind === 'document') {
        this.documentChanged();
      }
    });
    onLayout(() => {
      if (tab === 'play') {
        if (this.stale) {
          this.load();
        }
        this.render();
      } else {
        // out of sight: the playback stops
        this.pause();
      }
    });
    onPrefs((changed) => {
      if (changed.includes('boardColourLight') || changed.includes('boardColourDark')) {
        this.showBoard();
      }
    });
    // a piece's picture, decoded: drawn now
    onPieceImage(() => {
      if (tab === 'play') {
        this.showBoard();
      }
    });
    this.installKeys();
    this.documentChanged();
  }

  /** The problem played, as the snapshot has it. */
  get problem(): ProblemSummary | null {
    return this.store.snapshot?.problems.find((p) => p.id === this.current) ?? null;
  }

  /** The game's Undo and Redo as the Edit menu names them (null: nothing to undo or redo). */
  history(): { undo: string | null; redo: string | null } {
    const g = this.game;
    return {
      undo: g !== null && g.at > 0 ? 'Move' : null,
      redo: g !== null && g.at < g.moves.length ? 'Move' : null,
    };
  }

  private get sliding(): boolean {
    return this.store.snapshot?.grid === 'sliding';
  }

  /** The rules the game is played by: the problem's stored Solver options. */
  private get rules(): Rules {
    const o = readOptions(this.detail?.solverSettings ?? {}, undefined, 'sliding');
    // a piece stays in its layer: vertical moves are not played by hand yet
    return { nestedSlides: o.nestedSlides, partialNested: o.partialNested, verticalMoves: false };
  }

  // the document is another, or changed: the tab's place in the rail, the problem, the start
  private documentChanged(): void {
    const snap = this.store.snapshot;
    setWorkspaceShown('play', this.sliding);
    if (this.store.generation !== this.generation) {
      this.generation = this.store.generation;
      this.games.clear();
      this.current = null;
      this.start = null;
      this.detail = null;
      this.game = null;
      this.routes = null;
      this.layer = 0;
      this.pause();
    }
    const problems = snap?.problems ?? [];
    if (!problems.some((p) => p.id === this.current)) {
      this.current = problems[0]?.id ?? null;
      this.start = null;
      this.detail = null;
      this.game = null;
    }
    if (tab === 'play') {
      this.load();
      this.render();
    } else {
      this.stale = true;
    }
  }

  /** Plays another problem. */
  selectProblem(id: string): void {
    if (id === this.current) {
      return;
    }
    this.pause();
    this.current = id;
    this.start = null;
    this.detail = null;
    this.game = this.games.get(id) ?? null;
    this.layer = 0;
    this.load();
    this.render();
  }

  // reads how the problem's game starts; its own game goes on when the start is the same
  private load(): void {
    this.stale = false;
    const store = this.store;
    const problem = this.current;
    const n = ++this.loads;
    const stage = byId('playStage');
    if (problem === null || !this.sliding || !store.running) {
      this.start = null;
      this.detail = null;
      this.game = null;
      stage.dataset.loaded = String(n);
      this.showBoard();
      return;
    }
    const revision = store.snapshot?.revision ?? 0;
    void Promise.all([
      store.engine.call('sliding.playStart', { problem }),
      store.engine.call('problem.get', { problem }),
    ])
      .then(async ([start, detail]) => {
        if (n !== this.loads) {
          return;
        }
        this.start = start;
        this.detail = detail;
        let game: Game | null = null;
        if (start.error === null) {
          const key = JSON.stringify(start.pieces);
          const places = start.pieces.map((p) => cell(p.at));
          const had = this.games.get(problem);
          game = newGame(key, places, start.solved);
          if (had?.key === key && had.moves.length > 0) {
            // the same start: its moves are made again, as the problem is now
            const replay = await store.engine.call('sliding.playReplay', {
              problem,
              places,
              moves: had.moves,
              rules: this.rules,
            });
            if (n !== this.loads) {
              return;
            }
            if (replay.illegal === null) {
              game = withMoves(
                game,
                had.moves,
                replay.steps.map((s) => s.solved),
                had.at,
                replay.solved,
              );
              this.routes = { moves: game.moves, steps: replay.steps };
            } else {
              flash('The puzzle changed: the game starts again', 4000);
            }
          }
        }
        this.setGame(game);
        this.layer = Math.max(0, Math.min(this.layer, (start.board?.size[2] ?? 1) - 1));
        stage.dataset.revision = String(revision);
        stage.dataset.loaded = String(n);
        this.showBoard();
        this.render();
      })
      .catch((e: unknown) => {
        if (n === this.loads) {
          stage.dataset.loaded = String(n);
          reportError(e, 'Read the game');
        }
      });
  }

  private setGame(game: Game | null): void {
    this.game = game;
    if (this.current !== null) {
      if (game === null) {
        this.games.delete(this.current);
      } else {
        this.games.set(this.current, game);
      }
    }
    if (game === null || this.routes?.moves !== game.moves) {
      this.routes = null;
    }
  }

  // the board's drawing: the floor, the goals and the pieces as the snapshot shows them
  private model(): BoardModel | null {
    const start = this.start;
    const snap = this.store.snapshot;
    if (start?.board == null || start.error !== null || snap === null) {
      return null;
    }
    const shapes = snap.shapes;
    const indexOf = (id: string): number => shapes.findIndex((s) => s.id === id);
    const boardShape = start.board.shape;
    const tray = shapes.find((s) => s.id === boardShape);
    const copies = new Map<string, number>();
    return {
      size: cell(start.board.size),
      colour: tray?.colour == null ? null : fromHex(tray.colour),
      floor: start.board.cells.map((c) => ({ at: cell(c.at), variable: c.state === 'variable' })),
      goals: start.goals.map((g) => {
        const i = indexOf(g.piece);
        return {
          colour: shapeColour(shapes[i]?.colour ?? null, Math.max(0, i)),
          cells: g.cells.map(cell),
        };
      }),
      pieces: start.pieces.map((p) => {
        const i = indexOf(p.piece);
        const s: ShapeSummary | undefined = shapes[i];
        const sub = copies.get(p.piece) ?? 0;
        copies.set(p.piece, sub + 1);
        const image = s?.image ?? null;
        const picture = pieceImage(this.store.engine, image);
        return {
          cells: p.cells.map(cell),
          origin: cell(p.origin),
          colour: copyColour(s, Math.max(0, i), sub, snap.colours),
          name: s?.name ?? '',
          picture:
            image === null || picture === null
              ? null
              : { image, dataUrl: picture.dataUrl, width: picture.width, height: picture.height },
        };
      }),
    };
  }

  private showBoard(): void {
    const g = this.game;
    this.board.show(g === null ? null : this.model(), g === null ? [] : placesAt(g), this.layer);
  }

  // the engine's answer to a drag begun: where one move can take the piece
  private async reach(piece: number): Promise<ReachGroup[] | null> {
    const g = this.game;
    const problem = this.current;
    if (g === null || problem === null) {
      return null;
    }
    this.pause();
    try {
      const r = await this.store.engine.call('sliding.playReach', {
        problem,
        places: placesAt(g),
        piece,
        rules: this.rules,
      });
      return this.game === g ? r.groups : null;
    } catch (e) {
      reportError(e, 'Move the piece');
      return null;
    }
  }

  // a drag ended somewhere new
  private moved(move: Move, solved: boolean): void {
    const g = this.game;
    if (g === null) {
      return;
    }
    const next = made(g, move, solved);
    this.setGame(next);
    this.board.place(placesAt(next));
    this.render();
  }

  // the routes of the game's moves, read once for each list of moves
  private async routesOf(g: Game): Promise<Step[] | null> {
    if (this.routes?.moves === g.moves) {
      return this.routes.steps;
    }
    const problem = this.current;
    if (problem === null) {
      return null;
    }
    try {
      const r = await this.store.engine.call('sliding.playReplay', {
        problem,
        places: g.start,
        moves: g.moves,
        rules: this.rules,
      });
      if (r.illegal !== null) {
        return null;
      }
      this.routes = { moves: g.moves, steps: r.steps };
      return r.steps;
    } catch (e) {
      reportError(e, 'Read the moves');
      return null;
    }
  }

  /** Shows the position after `at` moves at once. */
  seekTo(at: number): void {
    const g = this.game;
    if (g === null) {
      return;
    }
    this.seeks++;
    this.board.stop();
    const next = seek(g, at);
    this.setGame(next);
    this.board.place(placesAt(next));
    this.render();
  }

  /**
   * One move forward (Redo) or back (Undo), drawn along its route; false
   * when there is none that way or the step was overtaken.
   */
  private async step(dir: 1 | -1, drawn = true): Promise<boolean> {
    const g = this.game;
    if (g === null || this.board.busy) {
      // a piece is being dragged
      return false;
    }
    const index = dir === 1 ? g.at : g.at - 1;
    const move = g.moves[index];
    if (move === undefined) {
      return false;
    }
    const n = ++this.seeks;
    const steps = await this.routesOf(g);
    if (n !== this.seeks || this.game !== g) {
      return false;
    }
    const route = steps?.[index]?.route;
    if (route !== undefined && drawn) {
      const whole = await this.board.animate(move.movers, route, dir === -1);
      if (!whole || n !== this.seeks || this.game !== g) {
        return false;
      }
    }
    const next = seek(g, g.at + dir);
    this.setGame(next);
    this.board.place(placesAt(next));
    this.render();
    return true;
  }

  // a step after those asked for before it; presses that pile up are not drawn, to keep up
  private queue(dir: 1 | -1): Promise<boolean> {
    const drawn = this.queued < 2;
    this.queued++;
    this.stepping = this.stepping
      .then(() => this.step(dir, drawn))
      .finally(() => {
        this.queued--;
      });
    return this.stepping;
  }

  /** Takes the last move back. */
  undo(): void {
    this.pause();
    void this.queue(-1);
  }

  /** Makes the move taken back again. */
  redo(): void {
    this.pause();
    void this.queue(1);
  }

  /** Plays the moves from the position shown (from the start, when at the end). */
  play(): void {
    const g = this.game;
    if (g === null || g.moves.length === 0 || this.playing) {
      return;
    }
    if (g.at >= g.moves.length) {
      this.seekTo(0);
    }
    this.playing = true;
    this.render();
    const next = (): void => {
      if (!this.playing) {
        return;
      }
      void this.queue(1).then((went) => {
        if (!this.playing) {
          return;
        }
        const now = this.game;
        if (!went || now === null || now.at >= now.moves.length) {
          this.pause();
          return;
        }
        window.setTimeout(next, PLAY_GAP_MS);
      });
    };
    next();
  }

  /** Stops the playback; true when it was playing. */
  pause(): boolean {
    if (!this.playing) {
      return false;
    }
    this.playing = false;
    this.render();
    return true;
  }

  // the game begins again: its moves are gone
  private restart(): void {
    const g = this.game;
    if (g === null || g.moves.length === 0) {
      return;
    }
    askConfirm(
      'Restart the game?',
      `The ${movesText(g.moves.length)} made are thrown away and the pieces go back to the Start.`,
      'Restart',
      () => {
        this.pause();
        this.seeks++;
        this.board.stop();
        this.setGame(newGame(g.key, g.start, g.startSolved));
        this.showBoard();
        this.render();
      },
    );
  }

  // the position shown becomes the problem's Start, or its Goal
  private save(goal: boolean): void {
    const g = this.game;
    const p = this.problem;
    if (g === null || p === null || this.edits.refuse()) {
      return;
    }
    this.pause();
    const side = goal ? 'Goal' : 'Start';
    const places = placesAt(g);
    const run = (): void => {
      void this.edits
        .mutate(`Save the position as the ${side}`, () =>
          this.store.engine.call('sliding.setLayout', { problem: p.id, goal, places }),
        )
        .then((r) => {
          if (r !== null) {
            flash(
              r.changed
                ? `The position is now the ${side} of ${p.label}`
                : `The position is the ${side} of ${p.label} already`,
              3500,
            );
          }
        });
    };
    // what the problem had found goes with its old Start or Goal
    const found = p.saved > 0 || (p.assemblies ?? 0) > 0 || p.state === 'solving';
    if (found) {
      askConfirm(
        `Save the position as the ${side}?`,
        `The ${side} of ${p.label} is replaced by the position shown, and what the Solver found for it (its solutions, a paused solve) is removed. Edit ▸ Undo on another tab takes it back.`,
        `Save as ${side}`,
        run,
      );
    } else {
      run();
    }
  }

  // the moves as a file
  private async exportMoves(): Promise<void> {
    const g = this.game;
    const start = this.start;
    const p = this.problem;
    if (g === null || start?.board == null || p === null) {
      return;
    }
    if (g.moves.length === 0) {
      flash('No moves to export yet');
      return;
    }
    const base = this.host.name.replace(/\.xmpuzzle$/i, '');
    const path = await this.app.chooseSave('moves', `${base} ${p.label}.slmoves`);
    if (path === null) {
      return;
    }
    const rules = this.rules;
    const text = serialise({
      puzzle: this.host.name,
      problem: p.name === '' ? p.label : `${p.label} ${p.name}`,
      board: boardOf(start.board),
      rules: {
        nestedSlides: rules.nestedSlides === true,
        partialNested: rules.partialNested === true,
        verticalMoves: false,
      },
      pieces: start.pieces.map((x) => ({ cells: x.cells.map(cell), at: cell(x.at) })),
      moves: g.moves,
    });
    try {
      await this.app.writeExport(path, new TextEncoder().encode(text));
      const stage = byId('playStage');
      stage.dataset.exported = String(Number(stage.dataset.exported ?? '0') + 1);
      flash(`Exported ${movesText(g.moves.length)}`);
    } catch (e) {
      reportError(e, 'Export the moves');
    }
  }

  // a moves file chosen: taken when it is of this board and start and every move can be made
  private async importMoves(text: string): Promise<void> {
    const start = this.start;
    const problem = this.current;
    if (start?.board == null || start.error !== null || problem === null) {
      return;
    }
    const file = parse(text);
    if (typeof file === 'string') {
      tell(file);
      return;
    }
    const map = matchStart(file, start.board, start.pieces);
    if (typeof map === 'string') {
      tell(map);
      return;
    }
    const moves = remap(file.moves, map);
    const places = start.pieces.map((p) => cell(p.at));
    try {
      const replay = await this.store.engine.call('sliding.playReplay', {
        problem,
        places,
        moves,
        rules: this.rules,
      });
      if (problem !== this.current || start !== this.start) {
        return;
      }
      if (replay.illegal !== null) {
        tell(
          `Move ${String(replay.illegal + 1)} of ${String(moves.length)} cannot be made here: ${replay.why ?? ''}`,
        );
        return;
      }
      const take = (): void => {
        this.pause();
        this.seeks++;
        this.board.stop();
        const game = withMoves(
          newGame(JSON.stringify(start.pieces), places, replay.solved),
          moves,
          replay.steps.map((s) => s.solved),
          0,
        );
        this.routes = { moves: game.moves, steps: replay.steps };
        this.setGame(game);
        this.showBoard();
        this.render();
        const stage = byId('playStage');
        stage.dataset.imported = String(Number(stage.dataset.imported ?? '0') + 1);
        flash(`${movesText(moves.length)} imported: press Play to watch them`, 4000);
      };
      const had = this.game?.moves.length ?? 0;
      if (had > 0) {
        askConfirm(
          'Replace your moves?',
          `The ${movesText(had)} made here are replaced by the file’s ${movesText(moves.length)}.`,
          'Replace',
          take,
        );
      } else {
        take();
      }
    } catch (e) {
      reportError(e, 'Import the moves');
    }
  }

  /** Draws the card, the banner, the buttons and the bar. */
  render(): void {
    if (tab !== 'play') {
      return;
    }
    const snap = this.store.snapshot;
    const problems = snap?.problems ?? [];
    const list = byId('plProbs');
    setHtml(
      list,
      problems
        .map(
          (q, n) =>
            `<div class="row" role="option" aria-selected="false" data-id="${q.id}" title="Play this problem"><span class="chip" style="${problemChipStyle(n)}">${esc(q.label)}</span><span class="lbl">${esc(q.name) || 'Problem'}</span></div>`,
        )
        .join(''),
    );
    markSelected(list, '.row', 'id', this.current);

    const g = this.game;
    const start = this.start;
    const p = this.problem;
    const n = g?.moves.length ?? 0;
    const at = g?.at ?? 0;
    const solved = g !== null && start?.hasGoal === true && solvedAt(g);
    const depth = start?.board?.size[2] ?? 1;
    const stored = readOptions(this.detail?.solverSettings ?? {}, undefined, 'sliding');

    byId('plProbMeta').textContent =
      p === null
        ? ''
        : start === null
          ? ''
          : start.error !== null
            ? 'Cannot be played yet'
            : `${String(start.pieces.length)} ${start.pieces.length === 1 ? 'piece' : 'pieces'} · ${start.hasGoal ? 'play to the Goal' : 'no Goal: free play'}`;
    byId('plCount').innerHTML =
      g === null
        ? '<span class="mute">—</span>'
        : `<b>${String(at)}</b> ${at === 1 ? 'move' : 'moves'}${at < n ? `<span class="mute"> of ${String(n)}</span>` : ''}`;
    byId('plNote').textContent =
      depth > 1 && stored.verticalMoves
        ? 'This problem allows vertical moves; by hand a piece stays in its layer for now.'
        : stored.nestedSlides
          ? 'Nested slides: a piece carries what is nested in it when it cannot move alone (Shift-drag to carry first).'
          : '';

    const dis = (id: string, off: boolean): void => {
      byId<HTMLButtonElement>(id).disabled = off;
    };
    dis('plUndo', g === null || at === 0);
    dis('plRedo', g === null || at >= n);
    dis('plRestart', g === null || n === 0);
    dis('plSaveStart', g === null);
    dis('plSaveGoal', g === null);
    dis('plExport', g === null || n === 0);
    dis('plImport', g === null);

    // the banner: the goal reached by playing, or a start that is the goal
    const banner = byId('plSolved');
    banner.hidden = !solved;
    banner.classList.toggle('quiet', solved && at === 0);
    banner.textContent = !solved
      ? ''
      : at === 0
        ? 'The Start is the Goal already'
        : `Solved! — ${movesText(at)}`;

    // the tag: the problem's chip and name
    const index = problems.findIndex((q) => q.id === this.current);
    byId('plTag').innerHTML =
      p === null
        ? ''
        : `<span class="chip" style="${problemChipStyle(Math.max(0, index))}">${esc(p.label)}</span><span class="nm">${esc(p.name) || 'Problem'}</span>`;

    // the layers of a deeper board
    setHtml(
      byId('plLayers'),
      g === null || depth <= 1
        ? ''
        : `<span class="lh">Z layer</span>${Array.from({ length: depth }, (_, l) => `<button class="lchip ${l === this.layer ? 'on' : ''}" data-l="${String(l)}" title="Layer ${String(l + 1)}">${String(l + 1)}</button>`).join('')}`,
    );

    // nothing to play: why, and where to go
    const empty = byId('plEmpty');
    const why =
      snap === null
        ? ''
        : problems.length === 0
          ? 'No problems yet: make one on the Puzzle tab, with a board as its result.'
          : start === null
            ? ''
            : (start.error ?? '');
    empty.hidden = g !== null || why === '';
    setHtml(
      empty,
      why === ''
        ? ''
        : `<div class="pl-why">${esc(why)}</div><div class="mute">A problem is played from its Start: place every piece on the Start board first.</div><button class="btn pri" data-go="puzzle" title="Open the Puzzle tab, where the Start and Goal are placed">Go to Puzzle</button>`,
    );
    byId('plMid').hidden = g === null;

    // the playback bar
    const bar = byId('plBar');
    bar.hidden = g === null;
    setHtml(
      byId('plTransport'),
      `<button class="tb" data-t="start" title="Back to the Start  (Shift+←)" ${at === 0 ? 'disabled' : ''}>${TG.start}</button><button class="tb" data-t="back" title="One move back  (←)" ${at === 0 ? 'disabled' : ''}>${TG.back}</button><button class="tb" data-t="play" title="${this.playing ? 'Pause' : 'Play the moves'}  (Space)" ${n === 0 ? 'disabled' : ''}>${this.playing ? TG.pause : TG.play}</button><button class="tb" data-t="fwd" title="One move forward  (→)" ${at >= n ? 'disabled' : ''}>${TG.fwd}</button><button class="tb" data-t="end" title="To the last move  (Shift+→)" ${at >= n ? 'disabled' : ''}>${TG.end}</button>`,
    );
    const range = byId<HTMLInputElement>('plSeek');
    range.max = String(n);
    range.value = String(at);
    range.disabled = n === 0;
    byId('plPos').textContent = n === 0 ? 'No moves yet' : `Move ${String(at)} of ${String(n)}`;

    // what the page shows, readable from outside (the e2e tests)
    const stage = byId('playStage');
    stage.dataset.problem = this.current ?? '';
    stage.dataset.moves = String(n);
    stage.dataset.at = String(at);
    stage.dataset.solved = String(solved);
    stage.dataset.playing = String(this.playing);
    stage.dataset.game = g === null ? '' : '1';
    updateStatus();
  }

  /** The status bar in the Play workspace (HTML). */
  statusLine(): string {
    const snap = this.store.snapshot;
    const p = this.problem;
    if (snap !== null && snap.problems.length === 0) {
      return 'No problems — make one in the <b>Puzzle</b> workspace';
    }
    if (p === null || this.start === null) {
      return '';
    }
    const g = this.game;
    if (g === null) {
      return `<b>${esc(p.label)}</b> cannot be played: ${esc(this.start.error ?? '')}`;
    }
    const solved = this.start.hasGoal && solvedAt(g) && g.at > 0;
    return `Playing <b>${esc(p.label)}</b>${p.name === '' ? '' : ` ${esc(p.name)}`} · ${movesText(g.at)}${g.at < g.moves.length ? ` of ${String(g.moves.length)}` : ''}${solved ? ' · <b>Solved!</b>' : ''} · drag a piece to slide it`;
  }

  // the buttons, the lists and the file chooser
  private listen(): void {
    byId('plProbs').addEventListener('click', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      if (row?.dataset.id !== undefined) {
        this.selectProblem(row.dataset.id);
      }
    });
    byId('plUndo').addEventListener('click', () => {
      this.undo();
    });
    byId('plRedo').addEventListener('click', () => {
      this.redo();
    });
    byId('plRestart').addEventListener('click', () => {
      this.restart();
    });
    byId('plSaveStart').addEventListener('click', () => {
      this.save(false);
    });
    byId('plSaveGoal').addEventListener('click', () => {
      this.save(true);
    });
    byId('plExport').addEventListener('click', () => {
      void this.exportMoves();
    });
    const file = byId<HTMLInputElement>('plFile');
    byId('plImport').addEventListener('click', () => {
      file.value = '';
      file.click();
    });
    file.addEventListener('change', () => {
      const f = file.files?.[0];
      if (f !== undefined) {
        f.text().then(
          (text) => this.importMoves(text),
          (e: unknown) => {
            reportError(e, 'Read the moves file');
          },
        );
      }
    });
    byId('plTransport').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-t]');
      const g = this.game;
      if (b === null || g === null) {
        return;
      }
      switch (b.dataset.t) {
        case 'start':
          this.pause();
          this.seekTo(0);
          break;
        case 'back':
          this.undo();
          break;
        case 'play':
          if (!this.pause()) {
            this.play();
          }
          break;
        case 'fwd':
          this.redo();
          break;
        default:
          this.pause();
          this.seekTo(g.moves.length);
      }
    });
    byId<HTMLInputElement>('plSeek').addEventListener('input', (e) => {
      this.pause();
      this.seekTo(Number((e.target as HTMLInputElement).value));
    });
    byId('plLayers').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('.lchip');
      if (b !== null) {
        this.layer = Number(b.dataset.l);
        this.showBoard();
        this.render();
      }
    });
    byId('plEmpty').addEventListener('click', (e) => {
      if ((e.target as Element).closest('[data-go]') !== null) {
        setTab('puzzle');
      }
    });
  }

  // the keys: Undo and Redo are the game's on this tab; the arrows and Space act on the board
  private installKeys(): void {
    const here = (): boolean => tab === 'play';
    // on the board, or nowhere: a focused button or the seekbar keeps its own keys
    const onBoard = (wide: boolean): boolean => {
      if (!here()) {
        return false;
      }
      const a = document.activeElement;
      if (a === null || a === document.body || a === byId('playBoard')) {
        return true;
      }
      return wide && byId('playStage').contains(a) && !(a instanceof HTMLInputElement);
    };
    onKey(
      'undo',
      () => {
        if (!here()) {
          return false;
        }
        // never the document's undo from here: it would take back an edit out of sight
        this.undo();
        return true;
      },
      true,
    );
    onKey(
      'redo',
      () => {
        if (!here()) {
          return false;
        }
        this.redo();
        return true;
      },
      true,
    );
    onKey(
      'move.back',
      () => {
        if (!onBoard(true)) {
          return false;
        }
        this.undo();
        return true;
      },
      true,
    );
    onKey(
      'move.forward',
      () => {
        if (!onBoard(true)) {
          return false;
        }
        this.redo();
        return true;
      },
      true,
    );
    onKey(
      'move.start',
      () => {
        if (!onBoard(true)) {
          return false;
        }
        this.pause();
        this.seekTo(0);
        return true;
      },
      true,
    );
    onKey(
      'move.end',
      () => {
        if (!onBoard(true) || this.game === null) {
          return false;
        }
        this.pause();
        this.seekTo(this.game.moves.length);
        return true;
      },
      true,
    );
    onKey(
      'activate',
      (e) => {
        // Space plays and pauses; Enter stays a focused control's
        if (e.key !== ' ' || !onBoard(false) || this.game === null) {
          return false;
        }
        if (!this.pause()) {
          this.play();
        }
        return true;
      },
      true,
    );
    addEscapeStep(ESC_ORDER.playback, () => here() && this.pause());
  }
}
