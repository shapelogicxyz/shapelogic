// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Play workspace (decisions 0039 and 0042): a sliding or a stacking
// problem played by hand. The left card has the problems, the count of
// moves, Restart, Save position as Start and as Goal, and the moves file's
// Export and Import; the center has the board with Reset, Undo and Redo
// beside it, the "Solved!" banner and the playback bar. The tab is shown
// only while a puzzle of a kind that is played is open.
//
// What a board, a start and a move are is the kind's (kind.ts: sliding.ts
// for a sliding puzzle, stacking.ts for a stacking one); this file keeps
// what every game shares. The engine is asked what a position allows and
// keeps no game; the game is this page's (game.ts): the start, the moves
// and the place in them shown, kept for each problem while the document is
// open and not saved in the puzzle's file (the moves file is how a game is
// kept). After every change of the document the start is read again: a
// game of the same start is checked move by move and goes on, any other
// starts anew. Ctrl+Z and Shift+Ctrl+Z are the game's here, never the
// document's. A position saved as the Start or the Goal is a change of the
// document, one undo step there, refused while a solve runs.

import type { ProblemSummary } from '../../engine/api.generated.ts';
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
import { flash, reportError, updateStatus } from '../shell/status.ts';
import { movesText, restarted, seek, solvedAt } from './game.ts';
import type { AnyGame } from './game.ts';
import type { PlayKind } from './kind.ts';
import { SlidingPlay } from './sliding.ts';
import { StackingPlay } from './stacking.ts';

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

export class PlayWorkspace {
  /** The problem played, or null. */
  current: string | null = null;
  private readonly store: Store;
  private readonly edits: Edits;
  private readonly app: BtApp;
  private readonly host: PlayHost;
  /** The sliding puzzles' play (the demo sets what its board hides). */
  readonly sliding: SlidingPlay;
  /** The kinds of puzzle played, and the one of the puzzle open (null: it is not played). */
  private readonly kinds: readonly PlayKind[];
  private kind: PlayKind | null = null;
  private game: AnyGame | null = null;
  /** Each problem's game, while the document is open. */
  private readonly games = new Map<string, AnyGame>();
  private generation = -1;
  private loads = 0;
  /** The document changed while the tab was out of sight. */
  private stale = true;
  private playing = false;
  /** Counts the playbacks: a chain whose count is not the newest stops (Pause then Play). */
  private playback = 0;
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
    const mine = {
      problem: () => this.current,
      game: () => this.game,
      moved: (next: AnyGame) => {
        this.moved(next);
      },
      pause: () => {
        this.pause();
      },
      render: () => {
        this.render();
      },
    };
    this.sliding = new SlidingPlay(store, mine);
    this.kinds = [this.sliding, new StackingPlay(store, mine)];
    for (const k of this.kinds) {
      k.mount(false);
    }
    byId('plReset').innerHTML = `${ic('reset', 22)}<span>Reset</span>`;
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

  // the document is another, or changed: the tab's place in the rail, the kind, the problem, the start
  private documentChanged(): void {
    const snap = this.store.snapshot;
    const kind = this.kinds.find((k) => k.grid === snap?.grid) ?? null;
    if (kind !== this.kind) {
      this.pause();
      this.kind?.mount(false);
      this.kind = kind;
      kind?.mount(true);
      this.dress();
    }
    setWorkspaceShown('play', kind !== null);
    if (this.store.generation !== this.generation) {
      this.generation = this.store.generation;
      this.games.clear();
      this.current = null;
      this.game = null;
      kind?.clear();
      this.pause();
    }
    const problems = snap?.problems ?? [];
    if (!problems.some((p) => p.id === this.current)) {
      this.current = problems[0]?.id ?? null;
      this.game = null;
      kind?.clear();
    }
    if (tab === 'play') {
      this.load();
      this.render();
    } else {
      this.stale = true;
    }
  }

  // the words that follow the kind of puzzle: what moves on its board
  private dress(): void {
    const kind = this.kind;
    if (kind === null) {
      return;
    }
    const one = kind.noun.one;
    byId('plSaveStart').title =
      `Make the position shown this problem’s Start: every ${one} starts where it is now`;
    byId('plSaveGoal').title =
      `Make the position shown this problem’s Goal: every ${one} must end where it is now`;
    byId('plSaveNote').textContent = kind.saveNote;
    byId('plReset').title =
      `Reset: the ${kind.noun.many} back at the Start (Redo makes the moves again)  (Shift+←)`;
  }

  /** Plays another problem. */
  selectProblem(id: string): void {
    if (id === this.current) {
      return;
    }
    this.pause();
    this.current = id;
    this.kind?.clear();
    this.game = this.games.get(id) ?? null;
    this.load();
    this.render();
  }

  // reads how the problem's game starts; its own game goes on when the start is the same
  private load(): void {
    this.stale = false;
    const store = this.store;
    const problem = this.current;
    const kind = this.kind;
    const n = ++this.loads;
    const stage = byId('playStage');
    if (problem === null || kind === null || !store.running) {
      kind?.clear();
      this.game = null;
      stage.dataset.loaded = String(n);
      kind?.show(null);
      return;
    }
    const revision = store.snapshot?.revision ?? 0;
    kind
      .load(problem, this.games.get(problem), () => n === this.loads)
      .then((loaded) => {
        if (loaded === null || n !== this.loads) {
          return;
        }
        if (loaded.dropped) {
          flash('The puzzle changed: the game starts again', 4000);
        }
        this.setGame(loaded.game);
        stage.dataset.revision = String(revision);
        stage.dataset.loaded = String(n);
        kind.show(this.game);
        this.render();
      })
      .catch((e: unknown) => {
        if (n === this.loads) {
          stage.dataset.loaded = String(n);
          reportError(e, 'Read the game');
        }
      });
  }

  private setGame(game: AnyGame | null): void {
    this.game = game;
    if (this.current !== null) {
      if (game === null) {
        this.games.delete(this.current);
      } else {
        this.games.set(this.current, game);
      }
    }
  }

  // a move made on the board
  private moved(next: AnyGame): void {
    if (this.game === null || this.kind === null) {
      return;
    }
    this.setGame(next);
    this.kind.place(next);
    this.render();
  }

  /** Shows the position after `at` moves at once. */
  seekTo(at: number): void {
    const g = this.game;
    const kind = this.kind;
    if (g === null || kind === null) {
      return;
    }
    this.seeks++;
    kind.stop();
    const next = seek(g, at);
    this.setGame(next);
    kind.place(next);
    this.render();
  }

  /**
   * One move forward (Redo) or back (Undo), drawn as it is made; false
   * when there is none that way or the step was overtaken.
   */
  private async step(dir: 1 | -1, drawn = true): Promise<boolean> {
    const g = this.game;
    const kind = this.kind;
    if (g === null || kind === null || kind.busy) {
      // a piece is being dragged
      return false;
    }
    const index = dir === 1 ? g.at : g.at - 1;
    if (g.moves[index] === undefined) {
      return false;
    }
    const n = ++this.seeks;
    await kind.routes(g);
    if (n !== this.seeks || this.game !== g) {
      return false;
    }
    if (drawn) {
      const whole = await kind.animate(g, index, dir === -1);
      if (!whole || n !== this.seeks || this.game !== g) {
        return false;
      }
    }
    const next = seek(g, g.at + dir);
    this.setGame(next);
    kind.place(next);
    this.render();
    return true;
  }

  // a step after those asked for before it; presses that pile up are not drawn, to keep up
  private queue(dir: 1 | -1): Promise<boolean> {
    const drawn = this.queued < 2;
    this.queued++;
    // a step that fails is said, and the next ones still run
    this.stepping = this.stepping
      .then(() => this.step(dir, drawn))
      .catch((e: unknown) => {
        reportError(e, 'The move could not be made');
        return false;
      })
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
    // a chain of an earlier playback (its timer, a step under way) stops: one at a time
    const run = ++this.playback;
    this.render();
    const next = (): void => {
      if (run !== this.playback) {
        return;
      }
      // the steps playbacks asked for, readable from outside (the e2e tests: one playback's, never a stale one's)
      const stage = byId('playStage');
      stage.dataset.playSteps = String(Number(stage.dataset.playSteps ?? '0') + 1);
      void this.queue(1).then((went) => {
        if (run !== this.playback) {
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
    this.playback++;
    this.render();
    return true;
  }

  // the game begins again: its moves are gone
  private restart(): void {
    const g = this.game;
    const kind = this.kind;
    if (g === null || kind === null || g.moves.length === 0) {
      return;
    }
    askConfirm(
      'Restart the game?',
      `The ${movesText(g.moves.length)} made are thrown away and the ${kind.noun.many} go back to the Start.`,
      'Restart',
      () => {
        this.pause();
        this.seeks++;
        kind.stop();
        this.setGame(restarted(g));
        kind.show(this.game);
        this.render();
      },
    );
  }

  // the position shown becomes the problem's Start, or its Goal
  private save(goal: boolean): void {
    const g = this.game;
    const p = this.problem;
    const kind = this.kind;
    if (g === null || p === null || kind === null || this.edits.refuse()) {
      return;
    }
    this.pause();
    const side = goal ? 'Goal' : 'Start';
    const run = (): void => {
      void this.edits
        .mutate(`Save the position as the ${side}`, () => kind.save(p.id, goal, g))
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
    const p = this.problem;
    const kind = this.kind;
    if (g === null || p === null || kind === null) {
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
    const text = kind.exportText(g, {
      puzzle: this.host.name,
      problem: p.name === '' ? p.label : `${p.label} ${p.name}`,
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
    const problem = this.current;
    const kind = this.kind;
    if (problem === null || kind === null || !kind.ready || kind.error !== null) {
      return;
    }
    try {
      const game = await kind.importMoves(text, problem);
      if (game === null || problem !== this.current || kind !== this.kind) {
        return;
      }
      if (typeof game === 'string') {
        tell(game);
        return;
      }
      const take = (): void => {
        this.pause();
        this.seeks++;
        kind.stop();
        this.setGame(game);
        kind.show(this.game);
        this.render();
        const stage = byId('playStage');
        stage.dataset.imported = String(Number(stage.dataset.imported ?? '0') + 1);
        flash(`${movesText(game.moves.length)} imported: press Play to watch them`, 4000);
      };
      const had = this.game?.moves.length ?? 0;
      if (had > 0) {
        askConfirm(
          'Replace your moves?',
          `The ${movesText(had)} made here are replaced by the file’s ${movesText(game.moves.length)}.`,
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
    const kind = this.kind;
    const p = this.problem;
    const n = g?.moves.length ?? 0;
    const at = g?.at ?? 0;
    const ready = kind?.ready === true;
    const error = kind?.error ?? null;
    const solved = g !== null && kind?.hasGoal === true && solvedAt(g);
    const count = kind?.count ?? 0;

    byId('plProbMeta').textContent =
      p === null || kind === null || !ready
        ? ''
        : error !== null
          ? 'Cannot be played yet'
          : `${String(count)} ${count === 1 ? kind.noun.one : kind.noun.many} · ${kind.hasGoal ? 'play to the Goal' : 'no Goal: free play'}`;
    byId('plCount').innerHTML =
      g === null
        ? '<span class="mute">—</span>'
        : `<b>${String(at)}</b> ${at === 1 ? 'move' : 'moves'}${at < n ? `<span class="mute"> of ${String(n)}</span>` : ''}`;
    byId('plNote').textContent = g === null || kind === null ? '' : kind.note();

    const dis = (id: string, off: boolean): void => {
      byId<HTMLButtonElement>(id).disabled = off;
    };
    dis('plReset', g === null || at === 0);
    dis('plUndo', g === null || at === 0);
    dis('plRedo', g === null || at >= n);
    // the moves made, beside the board
    byId('plMoves').innerHTML = `<b>${String(at)}</b><span>${at === 1 ? 'Move' : 'Moves'}</span>`;
    dis('plRestart', g === null || n === 0);
    dis('plSaveStart', g === null);
    dis('plSaveGoal', g === null);
    dis('plExport', g === null || n === 0);
    dis('plImport', g === null);
    kind?.renderOwn(g);

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

    // nothing to play: why, and where to go
    const empty = byId('plEmpty');
    const why =
      snap === null || kind === null
        ? ''
        : problems.length === 0
          ? kind.noProblems
          : !ready
            ? ''
            : (error ?? '');
    empty.hidden = g !== null || why === '';
    setHtml(
      empty,
      why === '' || kind === null
        ? ''
        : `<div class="pl-why">${esc(why)}</div><div class="mute">${esc(kind.needs)}</div><button class="btn pri" data-go="puzzle" title="Open the Puzzle tab, where the Start and Goal are placed">Go to Puzzle</button>`,
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
    const kind = this.kind;
    if (snap !== null && snap.problems.length === 0) {
      return 'No problems — make one in the <b>Puzzle</b> workspace';
    }
    if (p === null || kind?.ready !== true) {
      return '';
    }
    const g = this.game;
    if (g === null) {
      return `<b>${esc(p.label)}</b> cannot be played: ${esc(kind.error ?? '')}`;
    }
    const solved = kind.hasGoal && solvedAt(g) && g.at > 0;
    return `Playing <b>${esc(p.label)}</b>${p.name === '' ? '' : ` ${esc(p.name)}`} · ${movesText(g.at)}${g.at < g.moves.length ? ` of ${String(g.moves.length)}` : ''}${solved ? ' · <b>Solved!</b>' : ''} · ${kind.hint}`;
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
    byId('plReset').addEventListener('click', () => {
      this.pause();
      this.seekTo(0);
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
