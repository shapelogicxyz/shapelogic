// SPDX-License-Identifier: GPL-3.0-or-later
//
// A sliding puzzle in the Play workspace (decision 0039; the workspace's
// kind.ts): its start read from the engine, its board (board.ts) and the
// moves made on it, a move drawn again along its route, the layer strip of
// a deeper board, the buttons beside the board that show or hide the
// goal's tint and outlines, the grid and the pieces' names and draw the
// pieces simple, one that opens a picture of the goal in a window of its
// own (goal-picture.ts), a position saved as the problem's Start or Goal
// (sliding.setLayout), and the moves as a file's text (moves-file.ts).
//
// The engine is asked what a position allows and keeps no game
// (sliding.playStart, playReach, playReplay). The rules the problem does
// not fix (nested slides, partial nesting) are read from its stored Solver
// options; a piece stays in its layer when played by hand.

import type {
  Methods,
  ProblemDetail,
  ShapeImage,
  ShapeSummary,
} from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId, setHtml } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { tab } from '../shell/layout.ts';
import { copyColor, fromHex, shapeColor } from '../shell/piece-colors.ts';
import { onPieceImage, stackPicture } from '../shell/piece-images.ts';
import { onPrefs } from '../shell/prefs.ts';
import { reportError } from '../shell/status.ts';
import { shownBorder } from '../shell/tray-border.ts';
import { onTheme } from '../shell/theme.ts';
import { readOptions } from '../solver/settings.ts';
import { PlayBoard } from './board.ts';
import { toggleButton } from './board-shell.ts';
import type { BoardModel, BoardPiece, ReachGroup } from './board.ts';
import { made, newGame, placesAt, withMoves } from './game.ts';
import type { AnyGame, Cell, Game, Move } from './game.ts';
import type { Loaded, PlayKind, PlayKindHost } from './kind.ts';
import { goalPicture } from './goal-picture.ts';
import { goalPlaces, goalTitle } from './goal-places.ts';
import { boardOf, matchStart, parse, remap, serialize } from './moves-file.ts';

type PlayStart = Methods['sliding.playStart']['result'];
type Step = Methods['sliding.playReplay']['result']['steps'][number];
type Rules = NonNullable<Methods['sliding.playReach']['params']['rules']>;

const cell = (c: readonly number[]): Cell => [c[0] ?? 0, c[1] ?? 0, c[2] ?? 0];

// a game of this kind: the workspace hands back the games this kind made
const mine = (g: AnyGame): Game => g as Game;

/** The buttons beside the board that are a sliding puzzle's own. */
const OWN =
  '<button class="pl-big" id="plShades" aria-pressed="false"></button>' +
  '<button class="pl-big" id="plLines" aria-pressed="false"></button>' +
  `<button class="pl-big" id="plGoalWin" title="Show Goal Image: the board with every piece on its goal, in a window of its own to move and size as you like">${ic('image', 22)}<span>Show Goal Image</span></button>` +
  '<button class="pl-big" id="plGrid" aria-pressed="false"></button>' +
  '<button class="pl-big" id="plLabels" aria-pressed="false"></button>' +
  '<button class="pl-big" id="plSimple" aria-pressed="false"></button>';

export class SlidingPlay implements PlayKind {
  readonly grid = 'sliding';
  readonly noun = { one: 'piece', many: 'pieces' };
  readonly hint = 'drag a piece to slide it';
  readonly noProblems = 'No problems yet: make one on the Puzzle tab, with a board as its result.';
  readonly needs =
    'A problem is played from its Start: place every piece on the Start board first.';
  readonly saveNote =
    'The position on the board replaces the problem’s Start or Goal. Goals that are not wanted are taken off on the Puzzle tab.';
  private readonly store: Store;
  private readonly host: PlayKindHost;
  private readonly board: PlayBoard;
  private mounted = false;
  /** How the problem's game starts, as last read (null: not read yet). */
  private start: PlayStart | null = null;
  private detail: ProblemDetail | null = null;
  private layer = 0;
  /** The goal's tint left off the board's floor (Hide Goal Shadows), for every problem played. */
  private shadesHidden = false;
  /** The goal's outlines left off the board (Hide Goal Lines), for every problem played. */
  private linesHidden = false;
  /** The grid's lines left off the tray's floor (Hide Grid), for every problem played. */
  private gridHidden = false;
  /** The pieces' names left off them (Hide Piece Labels), for every problem played. */
  private labelsHidden = false;
  /** Simple Pieces: flat square pieces and tray, for every problem played. */
  private simplePieces = false;
  /** The goal's window was opened: the goal is sent to it as it changes. */
  private goalWindow = false;
  /** The routes of a game's moves (sliding.playReplay), by its list of moves. */
  private routed: { moves: readonly Move[]; steps: Step[] } | null = null;

  constructor(store: Store, host: PlayKindHost) {
    this.store = store;
    this.host = host;
    this.board = new PlayBoard(byId('playBoard'), byId('plMid'), {
      reach: (piece) => this.reach(piece),
      moved: (move, solved) => {
        const g = this.host.game();
        if (g !== null) {
          this.host.moved(made(mine(g), move, solved));
        }
      },
    });
    const redraw = (): void => {
      if (this.mounted) {
        this.show(this.host.game());
      }
    };
    onPrefs((changed) => {
      if (changed.includes('boardColorLight') || changed.includes('boardColorDark')) {
        redraw();
      }
    });
    // the tray's color follows the theme
    onTheme(redraw);
    // a piece's picture, decoded: drawn now
    onPieceImage(() => {
      if (tab === 'play') {
        redraw();
      }
    });
    byId('plOwn').addEventListener('click', (e) => {
      if (this.mounted) {
        this.toggle((e.target as Element).closest('button')?.id ?? '');
      }
    });
    byId('plLayers').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('.lchip');
      if (b !== null && this.mounted) {
        this.layer = Number(b.dataset.l);
        this.show(this.host.game());
        this.host.render();
      }
    });
  }

  /**
   * Hides the goal's shadows and lines, the grid and the pieces' names, or
   * shows them, as their buttons do (the website's demo sets them for each
   * of its examples).
   */
  setHidden(hidden: { shades: boolean; lines: boolean; grid: boolean; labels: boolean }): void {
    this.shadesHidden = hidden.shades;
    this.linesHidden = hidden.lines;
    this.gridHidden = hidden.grid;
    this.labelsHidden = hidden.labels;
    if (this.mounted) {
      this.showGoalButtons();
      this.showGridButton();
      this.showLabelsButton();
      this.sendGoal(false);
    }
  }

  mount(on: boolean): void {
    this.mounted = on;
    byId('playBoard').hidden = !on;
    if (on) {
      byId('plOwn').innerHTML = OWN;
      this.showGoalButtons();
      this.showGridButton();
      this.showLabelsButton();
      this.showSimpleButton();
    } else {
      this.board.show(null, [], 0);
      byId('plOwn').innerHTML = '';
      byId('plLayers').innerHTML = '';
    }
  }

  clear(): void {
    this.start = null;
    this.detail = null;
    this.layer = 0;
  }

  get ready(): boolean {
    return this.start !== null;
  }

  get error(): string | null {
    return this.start?.error ?? null;
  }

  get hasGoal(): boolean {
    return this.start?.hasGoal === true;
  }

  get count(): number {
    return this.start?.pieces.length ?? 0;
  }

  get busy(): boolean {
    return this.board.busy;
  }

  /** The rules the game is played by: the problem's stored Solver options. */
  private get rules(): Rules {
    const o = readOptions(this.detail?.solverSettings ?? {}, undefined, 'sliding');
    // a piece stays in its layer: vertical moves are not played by hand yet
    return { nestedSlides: o.nestedSlides, partialNested: o.partialNested, verticalMoves: false };
  }

  async load(
    problem: string,
    had: AnyGame | undefined,
    live: () => boolean,
  ): Promise<Loaded | null> {
    const engine = this.store.engine;
    const [start, detail] = await Promise.all([
      engine.call('sliding.playStart', { problem }),
      engine.call('problem.get', { problem }),
    ]);
    if (!live()) {
      return null;
    }
    this.start = start;
    this.detail = detail;
    let game: Game | null = null;
    let dropped = false;
    if (start.error === null) {
      const key = JSON.stringify(start.pieces);
      const places = start.pieces.map((p) => cell(p.at));
      const old = had === undefined ? undefined : mine(had);
      game = newGame(key, places, start.solved);
      if (old?.key === key && old.moves.length > 0) {
        // the same start: its moves are made again, as the problem is now
        const replay = await engine.call('sliding.playReplay', {
          problem,
          places,
          moves: old.moves,
          rules: this.rules,
        });
        if (!live()) {
          return null;
        }
        if (replay.illegal === null) {
          game = withMoves(
            game,
            old.moves,
            replay.steps.map((s) => s.solved),
            old.at,
            replay.solved,
          );
          this.routed = { moves: game.moves, steps: replay.steps };
        } else {
          dropped = true;
        }
      }
    }
    this.layer = Math.max(0, Math.min(this.layer, (start.board?.size[2] ?? 1) - 1));
    return { game, dropped };
  }

  note(): string {
    const depth = this.start?.board?.size[2] ?? 1;
    const stored = readOptions(this.detail?.solverSettings ?? {}, undefined, 'sliding');
    return depth > 1 && stored.verticalMoves
      ? 'This problem allows vertical moves; by hand a piece stays in its layer for now.'
      : stored.nestedSlides
        ? 'Nested slides: a piece that cannot move alone carries what is nested in it, or moves the piece it is nested in (Shift-drag to move them together first).'
        : '';
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
    // each stack of pictures as one (decision 0047), once decoded
    const drawn = (images: readonly ShapeImage[] | undefined): BoardPiece['picture'] => {
      const p = stackPicture(this.store.engine, images ?? []);
      return p === null
        ? null
        : {
            image: p.image,
            dataUrl: p.picture.dataUrl,
            width: p.picture.width,
            height: p.picture.height,
          };
    };
    // the tray's border as the board's section chose it (decision 0046)
    const border = shownBorder(tray);
    return {
      size: cell(start.board.size),
      color: tray?.color == null ? null : fromHex(tray.color),
      floor: start.board.cells.map((c) => ({ at: cell(c.at), variable: c.state === 'variable' })),
      picture: drawn(tray?.images),
      border:
        border === null
          ? null
          : {
              round: border.corners === 'round',
              material: border.material,
              color: border.color === null ? null : fromHex(border.color),
              thickness: border.thickness,
              picture: drawn(border.images),
            },
      goals: start.goals.map((g) => {
        const i = indexOf(g.piece);
        return {
          color: shapeColor(shapes[i]?.color ?? null, Math.max(0, i)),
          cells: g.cells.map(cell),
        };
      }),
      pieces: start.pieces.map((p) => {
        const i = indexOf(p.piece);
        const s: ShapeSummary | undefined = shapes[i];
        const sub = copies.get(p.piece) ?? 0;
        copies.set(p.piece, sub + 1);
        return {
          cells: p.cells.map(cell),
          origin: cell(p.origin),
          color: copyColor(s, Math.max(0, i), sub, snap.colors),
          name: s?.name ?? '',
          picture: drawn(s?.images),
        };
      }),
    };
  }

  show(game: AnyGame | null): void {
    this.board.show(
      game === null ? null : this.model(),
      game === null ? [] : placesAt(mine(game)),
      this.layer,
    );
    this.sendGoal(false);
  }

  // the board with each piece on its goal, to the goal's window: `raise`
  // opens the window; else only an open one is drawn again
  private sendGoal(raise: boolean): void {
    const start = this.start;
    const g = this.host.game();
    const svg = byId('playBoard').querySelector('svg');
    if (!this.goalWindow || start === null || !start.hasGoal || g === null || svg === null) {
      return;
    }
    const onGoal = goalPlaces(start);
    const p = this.store.snapshot?.problems.find((q) => q.id === this.host.problem());
    const picture = goalPicture(goalTitle(p), svg, {
      // the goals' tint and outlines lie under the pieces: left out
      drop: ['.pl-goals', '.pl-marks', '.pl-hit'],
      edit: (copy) => {
        // each piece put on its goal; a piece with none left out
        for (const e of copy.querySelectorAll<SVGGElement>('.pl-piece, .pl-label')) {
          const to = onGoal.get(Number(e.dataset.i));
          if (to === undefined) {
            e.remove();
            continue;
          }
          e.style.transform = this.board.transformAt(to);
          if (e.dataset.at !== undefined) {
            e.dataset.at = to.join(',');
          }
        }
      },
    });
    window.slApp.goalWindow(picture, raise);
  }

  place(game: AnyGame): void {
    this.board.place(placesAt(mine(game)));
  }

  stop(): void {
    this.board.stop();
  }

  // the engine's answer to a drag begun: where one move can take the piece
  private async reach(piece: number): Promise<ReachGroup[] | null> {
    const g = this.host.game();
    const problem = this.host.problem();
    if (g === null || problem === null) {
      return null;
    }
    this.host.pause();
    try {
      const r = await this.store.engine.call('sliding.playReach', {
        problem,
        places: placesAt(mine(g)),
        piece,
        rules: this.rules,
      });
      return this.host.game() === g ? r.groups : null;
    } catch (e) {
      reportError(e, 'Move the piece');
      return null;
    }
  }

  // the routes of the game's moves, read once for each list of moves
  async routes(game: AnyGame): Promise<void> {
    const g = mine(game);
    const problem = this.host.problem();
    if (this.routed?.moves === g.moves || problem === null) {
      return;
    }
    try {
      const r = await this.store.engine.call('sliding.playReplay', {
        problem,
        places: g.start,
        moves: g.moves,
        rules: this.rules,
      });
      if (r.illegal === null) {
        this.routed = { moves: g.moves, steps: r.steps };
      }
    } catch (e) {
      reportError(e, 'Read the moves');
    }
  }

  animate(game: AnyGame, index: number, back: boolean): Promise<boolean> {
    const g = mine(game);
    const move = g.moves[index];
    const route = this.routed?.moves === g.moves ? this.routed.steps[index]?.route : undefined;
    if (move === undefined || route === undefined) {
      return Promise.resolve(true);
    }
    return this.board.animate(move.movers, route, back);
  }

  renderOwn(game: AnyGame | null): void {
    for (const id of ['plShades', 'plLines', 'plGrid', 'plLabels', 'plSimple']) {
      byId<HTMLButtonElement>(id).disabled = game === null;
    }
    byId<HTMLButtonElement>('plGoalWin').disabled = game === null || this.start?.hasGoal !== true;
    // the layers of a deeper board
    const depth = this.start?.board?.size[2] ?? 1;
    setHtml(
      byId('plLayers'),
      game === null || depth <= 1
        ? ''
        : `<span class="lh">Z layer</span>${Array.from({ length: depth }, (_, l) => `<button class="lchip ${l === this.layer ? 'on' : ''}" data-l="${String(l)}" title="Layer ${String(l + 1)}">${String(l + 1)}</button>`).join('')}`,
    );
  }

  // one of the buttons beside the board
  private toggle(id: string): void {
    switch (id) {
      case 'plGoalWin':
        this.goalWindow = true;
        this.sendGoal(true);
        return;
      case 'plShades':
        this.shadesHidden = !this.shadesHidden;
        this.showGoalButtons();
        break;
      case 'plLines':
        this.linesHidden = !this.linesHidden;
        this.showGoalButtons();
        break;
      case 'plGrid':
        this.gridHidden = !this.gridHidden;
        this.showGridButton();
        break;
      case 'plLabels':
        this.labelsHidden = !this.labelsHidden;
        this.showLabelsButton();
        break;
      case 'plSimple':
        this.simplePieces = !this.simplePieces;
        this.showSimpleButton();
        break;
      default:
        return;
    }
    // the goal's window drawn as the board now is
    this.sendGoal(false);
  }

  // the goal's tint and outlines shown on the board or hidden, and the buttons that say which they will do
  private showGoalButtons(): void {
    const board = byId('playBoard');
    board.classList.toggle('noshades', this.shadesHidden);
    toggleButton('plShades', this.shadesHidden, {
      pressed: {
        icon: 'shade',
        label: 'Show Goal Shadows',
        does: 'tint the goal’s cells on the tray’s floor again',
      },
      released: {
        icon: 'shadeOff',
        label: 'Hide Goal Shadows',
        does: 'leave the goal’s tint off the tray’s floor',
      },
    });
    board.classList.toggle('nolines', this.linesHidden);
    toggleButton('plLines', this.linesHidden, {
      pressed: {
        icon: 'outline',
        label: 'Show Goal Lines',
        does: 'outline where each piece must end again',
      },
      released: {
        icon: 'outlineOff',
        label: 'Hide Goal Lines',
        does: 'leave the outlines of where the pieces must end off the board',
      },
    });
  }

  // the pieces and tray simple (flat and square) or not, and the button that says which it will do
  private showSimpleButton(): void {
    this.board.setSimple(this.simplePieces);
    toggleButton('plSimple', this.simplePieces, {
      pressed: {
        icon: 'cube',
        label: '3D Pieces',
        does: 'rounded pieces with their sides showing, in a tray with walls',
      },
      released: {
        icon: 'square',
        label: 'Simple Pieces',
        does: 'flat, square pieces in plain colors, in a flat square tray',
      },
    });
  }

  // the pieces' names shown or hidden, and the button that says which it will do
  private showLabelsButton(): void {
    byId('playBoard').classList.toggle('nolabels', this.labelsHidden);
    toggleButton('plLabels', this.labelsHidden, {
      pressed: {
        icon: 'tag',
        label: 'Show Piece Labels',
        does: 'write each piece’s name on it again',
      },
      released: {
        icon: 'tagOff',
        label: 'Hide Piece Labels',
        does: 'leave the pieces’ names off them',
      },
    });
  }

  // the floor's grid shown or hidden, and the button that says which it will do
  private showGridButton(): void {
    byId('playBoard').classList.toggle('nogrid', this.gridHidden);
    toggleButton('plGrid', this.gridHidden, {
      pressed: {
        icon: 'grid',
        label: 'Show Grid',
        does: 'draw the lines between the cells on the tray’s floor again',
      },
      released: {
        icon: 'gridOff',
        label: 'Hide Grid',
        does: 'leave the lines between the cells off the tray’s floor',
      },
    });
  }

  save(
    problem: string,
    goal: boolean,
    game: AnyGame,
  ): Promise<{ changed: boolean; revision: number }> {
    return this.store.engine.call('sliding.setLayout', {
      problem,
      goal,
      places: placesAt(mine(game)),
    });
  }

  exportText(game: AnyGame, names: { puzzle: string; problem: string }): string {
    const start = this.start;
    const rules = this.rules;
    return serialize({
      puzzle: names.puzzle,
      problem: names.problem,
      board: boardOf(start?.board ?? { size: [0, 0, 0], cells: [] }),
      rules: {
        nestedSlides: rules.nestedSlides === true,
        partialNested: rules.partialNested === true,
        verticalMoves: false,
      },
      pieces: (start?.pieces ?? []).map((x) => ({ cells: x.cells.map(cell), at: cell(x.at) })),
      moves: mine(game).moves,
    });
  }

  async importMoves(text: string, problem: string): Promise<AnyGame | string | null> {
    const start = this.start;
    if (start?.board == null || start.error !== null) {
      return null;
    }
    const file = parse(text);
    if (typeof file === 'string') {
      return file;
    }
    const map = matchStart(file, start.board, start.pieces);
    if (typeof map === 'string') {
      return map;
    }
    const moves = remap(file.moves, map);
    const places = start.pieces.map((p) => cell(p.at));
    const replay = await this.store.engine.call('sliding.playReplay', {
      problem,
      places,
      moves,
      rules: this.rules,
    });
    if (start !== this.start) {
      return null;
    }
    if (replay.illegal !== null) {
      return `Move ${String(replay.illegal + 1)} of ${String(moves.length)} cannot be made here: ${replay.why ?? ''}`;
    }
    const game = withMoves(
      newGame(JSON.stringify(start.pieces), places, replay.solved),
      moves,
      replay.steps.map((s) => s.solved),
      0,
    );
    this.routed = { moves: game.moves, steps: replay.steps };
    return game;
  }
}
