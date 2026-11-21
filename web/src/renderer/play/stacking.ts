// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stacking puzzle in the Play workspace (decision 0042; the workspace's
// kind.ts): its start read from the engine, its board (stack-board.ts) and
// the transfers made on it, a transfer drawn again, the goal's small
// picture (stack-look.ts) and the buttons beside the board that hide it and the discs'
// labels, a position saved as the problem's Start or Goal
// (stacking.setStacks), and the transfers as a file's text
// (stack-moves-file.ts).
//
// The engine is asked what a position allows and keeps no game
// (stacking.playStart, playMoves, playReplay): which transfers the rules
// allow, where a disc comes to rest and whether a position is the goal are
// its answers, never worked out here.

import type { Methods } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { copyColour } from '../shell/piece-colours.ts';
import { flash, reportError } from '../shell/status.ts';
import type { AnyGame } from './game.ts';
import type { Loaded, PlayKind, PlayKindHost } from './kind.ts';
import { StackBoard } from './stack-board.ts';
import type { Allowed } from './stack-board.ts';
import { discNumbers, newStackGame, stacksAt, transferred, withTransfers } from './stack-game.ts';
import type { StackGame, Stacks, Transfer } from './stack-game.ts';
import { stackLook, stackSvg } from './stack-look.ts';
import type { StackModel } from './stack-look.ts';
import { parseStack, serialiseStack, stackMismatch } from './stack-moves-file.ts';
import type { StackSetup } from './stack-moves-file.ts';

type PlayStart = Methods['stacking.playStart']['result'];
type Replay = Methods['stacking.playReplay']['result'];

// a game of this kind: the workspace hands back the games this kind made
const mine = (g: AnyGame): StackGame => g as StackGame;

/** The goal's picture and the buttons beside the board that are a stacking puzzle's own. */
const OWN =
  '<div class="sk-goal" id="plGoalPic" hidden><div class="cap">Goal</div><div class="pic" id="plGoalSvg"></div></div>' +
  '<button class="pl-big" id="plGoalShow" aria-pressed="false"></button>' +
  '<button class="pl-big" id="plDiscLabels" aria-pressed="false"></button>';

/** The room the goal's picture is drawn in, in pixels. */
const GOAL_ROOM = { w: 178, h: 132 };

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
  /** The goal's picture left off (Hide Goal), for every problem played. */
  private goalHidden = false;
  /** The discs' labels left off them (Hide Disc Labels), for every problem played. */
  private labelsHidden = false;
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
    byId('plOwn').addEventListener('click', (e) => {
      if (!this.mounted) {
        return;
      }
      switch ((e.target as Element).closest('button')?.id) {
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
    return start.panex
      ? 'Panex columns: a disc rests no deeper than its size allows. One disc more than a column’s height may wait raised in the bridge, where it blocks the moves that pass over it.'
      : '';
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
      discs: start.discs.map((d) => {
        const i = shapes.findIndex((s) => s.id === d.shape);
        const s = shapes[i];
        return {
          size: d.size,
          colour: copyColour(s, Math.max(0, i), d.instance, snap.colours),
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

  // the goal's stacking as a small picture beside the board
  private drawGoal(): void {
    const model = this.model();
    const goal: Stacks | null = this.start?.goal ?? null;
    byId('plGoalSvg').innerHTML =
      model === null || goal === null
        ? ''
        : stackSvg(model, goal, stackLook(model, GOAL_ROOM.w, GOAL_ROOM.h, true), true);
  }

  // the goal's picture and the discs' labels shown or hidden, and the buttons that say which they will do
  private showButtons(): void {
    const goal = this.goalHidden;
    const g = byId('plGoalShow');
    g.innerHTML = `${ic(goal ? 'outline' : 'outlineOff', 22)}<span>${goal ? 'Show Goal' : 'Hide Goal'}</span>`;
    g.title = goal
      ? 'Show Goal: the picture of where the discs must end, beside the board, again'
      : 'Hide Goal: leave the picture of where the discs must end off the page';
    g.setAttribute('aria-pressed', String(goal));
    const labels = this.labelsHidden;
    byId('stackBoard').classList.toggle('nolabels', labels);
    const l = byId('plDiscLabels');
    l.innerHTML = `${ic(labels ? 'tag' : 'tagOff', 22)}<span>${labels ? 'Show Disc Labels' : 'Hide Disc Labels'}</span>`;
    l.title = labels
      ? 'Show Disc Labels: write each disc’s name, or its size, on it again'
      : 'Hide Disc Labels: leave the discs’ names and sizes off them';
    l.setAttribute('aria-pressed', String(labels));
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
    return serialiseStack({
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
