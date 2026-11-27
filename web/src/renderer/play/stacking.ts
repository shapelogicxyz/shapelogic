// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stacking puzzle in the Play workspace (decision 0042; the workspace's
// kind.ts): its start read from the engine, its board (stack-board.ts) and
// the transfers made on it, a transfer drawn again, the goal's small
// picture (stack-look.ts), a button that opens it larger in a window of its
// own (goal-picture.ts), and the buttons beside the board that hide it and the discs'
// labels, a position saved as the problem's Start or Goal
// (stacking.setStacks), and the transfers as a file's text
// (stack-moves-file.ts).
//
// The engine is asked what a position allows and keeps no game
// (stacking.playStart, playMoves, playReplay): which transfers the rules
// allow, where a disc comes to rest and whether a position is the goal are
// its answers, never worked out here.

import './stacking.css';
import type { Methods } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { copyColor } from '../shell/piece-colors.ts';
import { flash, reportError } from '../shell/status.ts';
import { onTheme } from '../shell/theme.ts';
import { toggleButton } from './board-shell.ts';
import type { AnyGame } from './game.ts';
import type { Loaded, PlayKind, PlayKindHost } from './kind.ts';
import { StackBoard } from './stack-board.ts';
import type { Allowed } from './stack-board.ts';
import { discNumbers, newStackGame, stacksAt, transferred, withTransfers } from './stack-game.ts';
import type { StackGame, Stacks, Transfer } from './stack-game.ts';
import { goalPicture } from './goal-picture.ts';
import { goalTitle } from './goal-places.ts';
import { stackLook, stackSvg } from './stack-look.ts';
import type { StackModel } from './stack-look.ts';
import { parseStack, serializeStack, stackMismatch } from './stack-moves-file.ts';
import type { StackSetup } from './stack-moves-file.ts';

type PlayStart = Methods['stacking.playStart']['result'];
type Replay = Methods['stacking.playReplay']['result'];

// a game of this kind: the workspace hands back the games this kind made
const mine = (g: AnyGame): StackGame => g as StackGame;

/** The goal's picture and the buttons beside the board that are a stacking puzzle's own. */
const OWN =
  `<button class="sk-goal" id="plGoalPic" hidden title="Open the Goal in a window of its own, to move and size as you like"><span class="cap">Goal</span><span class="pop">${ic('popout', 15)}</span><span class="pic" id="plGoalSvg"></span></button>` +
  '<button class="pl-big" id="plGoalShow" aria-pressed="false"></button>' +
  '<button class="pl-big" id="plDiscLabels" aria-pressed="false"></button>';

/** The room the goal's picture is drawn in, in pixels. */
const GOAL_ROOM = { w: 178, h: 132 };
/** The room the goal's window's picture is drawn in, in pixels. */
const WINDOW_ROOM = { w: 900, h: 600 };

// what a game's start is of: the rods, the discs and where they start
function keyOf(start: PlayStart): string {
  return JSON.stringify([start.rods, start.panex, start.discs, start.stacks]);
}

// the file's or a game's transfers with where each disc comes to rest, as a replay told
function rested(moves: readonly { from: number; to: number }[], replay: Replay): Transfer[] {
  return moves.map((m, i) => ({ from: m.from, to: m.to, z: replay.steps[i]?.z ?? 0 }));
}

export class StackingPlay implements PlayKind {
  readonly grid = 'stacking';
  readonly noun = { one: 'disc', many: 'discs' };
  readonly hint = 'drag a top disc to another rod, or click it and then the rod';
  readonly noProblems = 'No problems yet: make one on the Puzzle tab, on a rod set.';
  readonly needs =
    'A problem is played from its Start: put every disc on a rod of the Start first.';
  readonly saveNote =
    'The position on the board replaces the problem’s Start or Goal, with every disc where it is now.';
  private readonly store: Store;
  private readonly host: PlayKindHost;
  private readonly board: StackBoard;
  private mounted = false;
  /** How the problem's game starts, as last read (null: not read yet). */
  private start: PlayStart | null = null;
  /** The problem that start is of. */
  private problem: string | null = null;
  /** The goal's picture left off (Hide Goal), for every problem played. */
  private goalHidden = false;
  /** The discs' labels left off them (Hide Disc Labels), for every problem played. */
  private labelsHidden = false;
  /** The goal's window was opened: the goal is sent to it as it changes. */
  private goalWindow = false;
  /** The transfers a position allows, as last asked: of this game at this place. */
  private asked: { game: AnyGame; moves: Promise<Allowed[]> } | null = null;

  constructor(store: Store, host: PlayKindHost) {
    this.store = store;
    this.host = host;
    this.board = new StackBoard(byId('stackBoard'), byId('plMid'), {
      moves: () => this.moves(),
      moved: (move) => {
        const g = this.host.game();
        if (g !== null) {
          this.host.moved(
            transferred(mine(g), { from: move.from, to: move.to, z: move.z }, move.solved),
          );
        }
      },
      refused: (from, to) => {
        void this.tellWhy(from, to);
      },
    });
    // an open goal window takes the theme's colors (its GOAL caption, its background)
    onTheme(() => {
      this.sendGoal(false);
    });
    byId('plOwn').addEventListener('click', (e) => {
      if (!this.mounted) {
        return;
      }
      switch ((e.target as Element).closest('button')?.id) {
        case 'plGoalPic':
          this.goalWindow = true;
          this.sendGoal(true);
          return;
        case 'plGoalShow':
          this.goalHidden = !this.goalHidden;
          break;
        case 'plDiscLabels':
          this.labelsHidden = !this.labelsHidden;
          break;
        default:
          return;
      }
      this.showButtons();
      this.host.render();
      this.sendGoal(false);
    });
  }

  mount(on: boolean): void {
    this.mounted = on;
    byId('stackBoard').hidden = !on;
    if (on) {
      byId('plOwn').innerHTML = OWN;
      this.showButtons();
    } else {
      this.board.show(null, []);
      byId('plOwn').innerHTML = '';
    }
  }

  clear(): void {
    this.start = null;
    this.problem = null;
    this.asked = null;
  }

  get ready(): boolean {
    return this.start !== null;
  }

  get error(): string | null {
    return this.start?.error ?? null;
  }

  get hasGoal(): boolean {
    return this.start?.goal != null;
  }

  get count(): number {
    return this.start?.discs.length ?? 0;
  }

  get busy(): boolean {
    return this.board.busy;
  }

  async load(
    problem: string,
    had: AnyGame | undefined,
    live: () => boolean,
  ): Promise<Loaded | null> {
    const engine = this.store.engine;
    const start = await engine.call('stacking.playStart', { problem });
    if (!live()) {
      return null;
    }
    this.start = start;
    this.problem = problem;
    this.asked = null;
    let game: StackGame | null = null;
    let dropped = false;
    if (start.error === null) {
      const key = keyOf(start);
      const old = had === undefined ? undefined : mine(had);
      game = newStackGame(key, start.stacks, start.solved);
      if (old?.key === key && old.moves.length > 0) {
        // the same start: its transfers are made again, as the problem is now
        const moves = old.moves.map((m) => ({ from: m.from, to: m.to }));
        const replay = await engine.call('stacking.playReplay', {
          problem,
          stacks: discNumbers(start.stacks),
          moves,
        });
        if (!live()) {
          return null;
        }
        if (replay.illegal === null) {
          game = withTransfers(
            game,
            rested(moves, replay),
            replay.steps.map((s) => s.solved),
            old.at,
            replay.solved,
          );
        } else {
          dropped = true;
        }
      }
    }
    return { game, dropped };
  }

  note(): string {
    const start = this.start;
    if (start === null) {
      return '';
    }
    if (start.goalError !== null) {
      return `The Goal is not played to yet: ${start.goalError}`;
    }
    if (!start.panex) {
      return '';
    }
    const panex =
      'Panex columns: a disc rests no deeper than its size allows. One disc more than a column’s height may wait raised in the bridge, where it blocks the moves that pass over it.';
    return this.channel()
      ? `${panex} The outside channel joins the first and last columns: a move between them goes round, past a raised disc.`
      : panex;
  }

  // the problem's rod set has the outside channel (the engine's hasOutsideChannel)
  private channel(): boolean {
    const snap = this.store.snapshot;
    const rodSet = snap?.problems.find((p) => p.id === this.problem)?.rodSet ?? null;
    return snap?.rodSets.find((r) => r.id === rodSet)?.outsideChannel === true;
  }

  // the board's drawing: the rods and the discs as the snapshot shows them
  private model(): StackModel | null {
    const start = this.start;
    const snap = this.store.snapshot;
    if (start?.error !== null || start.rods.length === 0 || snap === null) {
      return null;
    }
    const shapes = snap.shapes;
    return {
      rods: start.rods,
      panex: start.panex,
      channel: this.channel(),
      discs: start.discs.map((d) => {
        const i = shapes.findIndex((s) => s.id === d.shape);
        const s = shapes[i];
        return {
          size: d.size,
          color: copyColor(s, Math.max(0, i), d.instance, snap.colors),
          // its name, or its size: what the size rule and a Panex column go by
          label: s === undefined || s.name === '' ? String(d.size) : s.name,
        };
      }),
    };
  }

  show(game: AnyGame | null): void {
    this.asked = null;
    this.board.show(game === null ? null : this.model(), game === null ? [] : stacksAt(mine(game)));
    this.drawGoal();
  }

  place(game: AnyGame): void {
    this.board.place(stacksAt(mine(game)));
  }

  stop(): void {
    this.board.stop();
  }

  // the engine's answer to a disc taken hold of: the transfers the position allows
  private async moves(): Promise<Allowed[] | null> {
    const g = this.host.game();
    const problem = this.host.problem();
    if (g === null || problem === null) {
      return null;
    }
    this.host.pause();
    // asked once for a position: a click and the click that follows it share the answer
    if (this.asked?.game !== g) {
      this.asked = {
        game: g,
        moves: this.store.engine
          .call('stacking.playMoves', { problem, stacks: discNumbers(stacksAt(mine(g))) })
          .then((r) => r.moves),
      };
    }
    try {
      const moves = await this.asked.moves;
      return this.host.game() === g ? moves : null;
    } catch (e) {
      this.asked = null;
      reportError(e, 'Move the disc');
      return null;
    }
  }

  // a disc let go over a rod it may not go onto: the rule that refuses it, in the status bar
  private async tellWhy(from: number, to: number): Promise<void> {
    const g = this.host.game();
    const problem = this.host.problem();
    if (g === null || problem === null) {
      return;
    }
    try {
      const r = await this.store.engine.call('stacking.playReplay', {
        problem,
        stacks: discNumbers(stacksAt(mine(g))),
        moves: [{ from, to }],
      });
      if (r.why !== null && this.host.game() === g) {
        flash(r.why, 3500);
        const stage = byId('playStage');
        stage.dataset.refused = String(Number(stage.dataset.refused ?? '0') + 1);
      }
    } catch (e) {
      reportError(e, 'Move the disc');
    }
  }

  routes(): Promise<void> {
    // a transfer is drawn from the game alone
    return Promise.resolve();
  }

  animate(game: AnyGame, index: number, back: boolean): Promise<boolean> {
    const g = mine(game);
    const move = g.moves[index];
    const top = move === undefined ? undefined : stacksAt(g, index)[move.from]?.at(-1);
    if (move === undefined || top === undefined) {
      return Promise.resolve(true);
    }
    const before = { rod: move.from, z: top.z };
    const after = { rod: move.to, z: move.z };
    return back
      ? this.board.animate(top.disc, after, before)
      : this.board.animate(top.disc, before, after);
  }

  renderOwn(game: AnyGame | null): void {
    byId<HTMLButtonElement>('plGoalShow').disabled = game === null || !this.hasGoal;
    byId<HTMLButtonElement>('plDiscLabels').disabled = game === null;
    byId('plGoalPic').hidden = game === null || !this.hasGoal || this.goalHidden;
  }

  // the goal's stacking as a small picture beside the board, and in its window
  private drawGoal(): void {
    const model = this.model();
    const goal: Stacks | null = this.start?.goal ?? null;
    byId('plGoalSvg').innerHTML =
      model === null || goal === null
        ? ''
        : stackSvg(model, goal, stackLook(model, GOAL_ROOM.w, GOAL_ROOM.h, true), true);
    this.sendGoal(false);
  }

  // the goal drawn large, as the board draws it, to its window: `raise`
  // opens the window; else only an open one is drawn again
  private sendGoal(raise: boolean): void {
    const model = this.model();
    const goal: Stacks | null = this.start?.goal ?? null;
    if (!this.goalWindow || model === null || goal === null) {
      return;
    }
    // drawn in the page, out of sight, so the stylesheet styles it
    const room = document.createElement('div');
    room.className = `pl-board goal-room${this.labelsHidden ? ' nolabels' : ''}`;
    room.innerHTML = stackSvg(model, goal, stackLook(model, WINDOW_ROOM.w, WINDOW_ROOM.h));
    document.body.append(room);
    const svg = room.querySelector('svg');
    if (svg !== null) {
      // cropped to what is drawn: no room above the rods for a moving disc
      const parts = [
        ...svg.querySelectorAll<SVGGraphicsElement>('.sk-back > :not(.sk-glow), .sk-disc'),
      ];
      // measured where they are drawn: getBBox leaves out an element's own
      // transform, and a disc stands where its style's translate puts it
      const at = svg.getBoundingClientRect();
      const view = svg.viewBox.baseVal;
      const k = at.width > 0 ? view.width / at.width : 1;
      const boxes = parts.map((e) => {
        const r = e.getBoundingClientRect();
        return {
          x: view.x + (r.left - at.left) * k,
          y: view.y + (r.top - at.top) * k,
          width: r.width * k,
          height: r.height * k,
        };
      });
      if (boxes.length > 0) {
        const pad = 12;
        const x0 = Math.min(...boxes.map((b) => b.x)) - pad;
        const y0 = Math.min(...boxes.map((b) => b.y)) - pad;
        const x1 = Math.max(...boxes.map((b) => b.x + b.width)) + pad;
        const y1 = Math.max(...boxes.map((b) => b.y + b.height)) + pad;
        svg.setAttribute(
          'viewBox',
          `${String(x0)} ${String(y0)} ${String(x1 - x0)} ${String(y1 - y0)}`,
        );
        svg.setAttribute('width', String(Math.round(x1 - x0)));
        svg.setAttribute('height', String(Math.round(y1 - y0)));
      }
      const p = this.store.snapshot?.problems.find((q) => q.id === this.problem);
      window.slApp.goalWindow(
        goalPicture(goalTitle(p), svg, { drop: ['.sk-cols', '.sk-glow'] }),
        raise,
      );
    }
    room.remove();
  }

  // the goal's picture and the discs' labels shown or hidden, and the buttons that say which they will do
  private showButtons(): void {
    toggleButton('plGoalShow', this.goalHidden, {
      pressed: {
        icon: 'outline',
        label: 'Show Goal',
        does: 'the picture of where the discs must end, beside the board, again',
      },
      released: {
        icon: 'outlineOff',
        label: 'Hide Goal',
        does: 'leave the picture of where the discs must end off the page',
      },
    });
    byId('stackBoard').classList.toggle('nolabels', this.labelsHidden);
    toggleButton('plDiscLabels', this.labelsHidden, {
      pressed: {
        icon: 'tag',
        label: 'Show Disc Labels',
        does: 'write each disc’s name, or its size, on it again',
      },
      released: {
        icon: 'tagOff',
        label: 'Hide Disc Labels',
        does: 'leave the discs’ names and sizes off them',
      },
    });
  }

  save(
    problem: string,
    goal: boolean,
    game: AnyGame,
  ): Promise<{ changed: boolean; revision: number }> {
    return this.store.engine.call('stacking.setStacks', {
      problem,
      goal,
      stacks: discNumbers(stacksAt(mine(game))),
    });
  }

  // the rods and the start as a moves file is matched by them
  private setup(start: PlayStart): StackSetup {
    return {
      rods: start.rods.map((r) => r.capacity),
      panex: start.panex,
      start: start.stacks.map((rod) => rod.map((d) => start.discs[d.disc]?.size ?? 1)),
    };
  }

  exportText(game: AnyGame, names: { puzzle: string; problem: string }): string {
    const start = this.start;
    return serializeStack({
      puzzle: names.puzzle,
      problem: names.problem,
      ...(start === null ? { rods: [], panex: false, start: [] } : this.setup(start)),
      moves: mine(game).moves.map((m) => ({ from: m.from, to: m.to })),
    });
  }

  async importMoves(text: string, problem: string): Promise<AnyGame | string | null> {
    const start = this.start;
    if (start?.error !== null) {
      return null;
    }
    const file = parseStack(text);
    if (typeof file === 'string') {
      return file;
    }
    const mismatch = stackMismatch(file, this.setup(start));
    if (mismatch !== null) {
      return mismatch;
    }
    const replay = await this.store.engine.call('stacking.playReplay', {
      problem,
      stacks: discNumbers(start.stacks),
      moves: file.moves,
    });
    if (start !== this.start) {
      return null;
    }
    if (replay.illegal !== null) {
      return `Move ${String(replay.illegal + 1)} of ${String(file.moves.length)} cannot be made here: ${replay.why ?? ''}`;
    }
    return withTransfers(
      newStackGame(keyOf(start), start.stacks, replay.solved),
      rested(file.moves, replay),
      replay.steps.map((s) => s.solved),
      0,
    );
  }
}
