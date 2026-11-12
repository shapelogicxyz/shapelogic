// SPDX-License-Identifier: GPL-3.0-or-later
//
// The crash recovery journal (in the place of the original's "Autosave
// every 20 min"): while a solve runs, the puzzle with the solve's results
// and position is written to the rescue copy now and then, so that a crash
// loses little. It is always on, and it never writes the puzzle's own file:
// that stays what the user last saved (File ▸ Save), whatever was edited
// before the solve and whatever the solve has found. The rescue copy
// (shell/rescue.ts) is in the app's data directory and names the puzzle's
// file; after a crash the next start offers it, and it reopens as that
// puzzle with its unsaved changes, the solve paused where the copy was
// written, to be continued.
//
// How: the solves under way pause at a save point (solve.savePoint: the
// assembly stops where it can be saved, the take-aparts under way finish;
// the engine refuses the document's bytes while any run is active, so every
// run is paused first, decision 0010), the rescue copy is written
// (`rescue`), and the solves paused for it carry on (solve.continue, with
// the options they ran with).
//
// When: the first save a minute into the solves, then spaced by what the
// last save cost (cadence.ts: its pause, the take-aparts it waited for, the
// write and the restart, 50 times that, never under a minute), and only
// when the solves have moved since the last one. The original's fixed 20
// minutes threw away what each thread had done in its task at every save,
// so it could not come more often; a save point costs no search work now
// (decision 0024). The page's `autosave` parameter (BT_AUTOSAVE_SECONDS,
// which the main process passes in the page's address) sets the least time
// between saves in seconds, for the e2e tests.
//
// A run's pause is waited for by its revision (the solve record's end),
// not its state. `saveAtSavePoint` is also what a save asked for while
// solving does (File ▸ Save and Save As): the same pause, the puzzle saved
// to its file by the store (which waits for the snapshot that shows the
// document clean), the solves carried on. A solve the user pauses while a
// save is under way is the user's: it stays paused (`hold`). A stacking
// puzzle is not written from here (its search saves itself by the same
// rule, in its own files), and a save while its search runs pauses it
// (solve.pause: the search saves itself; a stacking solve has no save
// point), saves, and carries it on. A sliding puzzle's solve is paused
// too: a save point lets the slide search under way run to its end, which
// on a large puzzle is the whole solve, so the save would wait for it;
// paused, the search writes its levels to its folder (what it wrote since
// its own last save) and Continue carries it on from there, redoing at
// most the level cut short. The problems' stored `autosave` (the
// original's box) is not looked at.

import type { SolveOptions } from '../../engine/api.generated.ts';
import { isActive } from '../state/solves.ts';
import type { Solves } from '../state/solves.ts';
import type { Store } from '../state/store.ts';
import { byId } from '../shell/dom.ts';
import { flash, reportError } from '../shell/status.ts';
import { FLOOR_MS, nextWait } from './cadence.ts';

/** The least time between saves in milliseconds: a minute, or the page's `autosave` parameter (seconds). */
function floor(): number {
  const s = Number(new URLSearchParams(window.location.search).get('autosave'));
  return Number.isFinite(s) && s > 0 ? s * 1000 : FLOOR_MS;
}

export class Autosave {
  /** Saves made (readable from outside: `#leftS[data-autosaved]`). */
  saves = 0;
  private readonly store: Store;
  private readonly solves: Solves;
  private readonly optionsOf: (problem: string) => SolveOptions;
  private readonly floor = floor();
  /** The wait before the next save, from `since` (cadence.ts). */
  private wait = this.floor;
  /** When the solves under way started or were last saved. */
  private since: number | null = null;
  /** How far the solves were at the last save (`mark`). */
  private saved = '';
  /**
   * Writes the rescue copy now (shell/rescue.ts); true when it was written.
   * Set by the shell once the copy exists; without it (the demo, which has
   * nowhere to keep one) there is no journal.
   */
  rescue: (() => Promise<boolean>) | null = null;
  /** A solve paused for the save was continued (the workspace waits for its end again). */
  private readonly continued: (problem: string) => void;
  private saving = false;
  /** The solves the user paused while the save under way waited: not carried on. */
  private readonly held = new Set<string>();
  /** The solves the save under way pauses, to carry them on. */
  private readonly pausing = new Set<string>();

  constructor(
    store: Store,
    solves: Solves,
    optionsOf: (problem: string) => SolveOptions,
    continued: (problem: string) => void = () => undefined,
  ) {
    this.store = store;
    this.solves = solves;
    this.optionsOf = optionsOf;
    this.continued = continued;
    window.setInterval(
      () => {
        this.tick();
      },
      Math.min(1000, this.floor / 4),
    );
  }

  private tick(): void {
    const active = this.activeRuns();
    const now = Date.now();
    if (active.length === 0) {
      this.since = null;
      this.wait = this.floor;
      this.saved = '';
      return;
    }
    if (this.since === null) {
      this.since = now;
      return;
    }
    if (this.saving || now - this.since < this.wait) {
      return;
    }
    // nowhere to write, or a stacking search, which saves itself
    if (this.rescue === null || this.store.snapshot?.grid === 'stacking') {
      return;
    }
    // nothing new since the last save: not yet
    const mark = this.mark(active);
    if (mark === this.saved) {
      return;
    }
    void this.journal(mark);
  }

  // How far the solves under way are: what their last progress told.
  private mark(active: readonly string[]): string {
    return active.map((p) => JSON.stringify(this.solves.peek(p)?.progress ?? null)).join('|');
  }

  // One save of the journal, timed: the next comes no sooner than its cost allows.
  private async journal(mark: string): Promise<void> {
    const began = Date.now();
    try {
      await this.saveAtSavePoint(undefined, false, true);
    } finally {
      const now = Date.now();
      this.wait = nextWait(now - began, this.floor);
      this.since = now;
      this.saved = mark;
    }
  }

  /** A save at a save point is under way (its pauses, its save, its continues). */
  get busy(): boolean {
    return this.saving;
  }

  /** The user paused this solve (Pause): a save under way leaves it paused. */
  hold(problem: string): void {
    if (this.saving) {
      this.held.add(problem);
    }
  }

  /** Whether the save under way paused this solve and will carry it on when it is through. */
  carriesOn(problem: string): boolean {
    return this.saving && this.pausing.has(problem) && !this.held.has(problem);
  }

  /** Whether a run is under way (a save must wait for a save point). */
  runsUnderWay(): boolean {
    return this.activeRuns().length > 0;
  }

  // the problems whose run is under way (an active state)
  private activeRuns(): string[] {
    const snap = this.store.snapshot;
    if (snap === null) {
      return [];
    }
    return snap.problems
      .map((p) => p.id)
      .filter((id) => {
        const r = this.solves.peek(id);
        return r?.phase === 'active' && isActive(r.state);
      });
  }

  /**
   * Pauses the solves under way at a save point, saves, and carries the
   * paused ones on. File ▸ Save and Save As (`byUser`): the puzzle to its
   * file, or to `path`; the file saved to, or null when it could not save.
   * The crash recovery journal (`journal`): the rescue copy, never the
   * puzzle's file; null.
   */
  async saveAtSavePoint(path?: string, byUser = false, journal = false): Promise<string | null> {
    if (this.saving) {
      if (byUser) {
        flash('A save is under way already');
      }
      return null;
    }
    this.saving = true;
    const e = this.store.engine;
    // the document being saved: if another replaces it while we wait (a run's
    // end, then the snapshot), its solves are gone and its file must not be
    // written with the new document's bytes
    const gen = this.store.generation;
    const paused: string[] = [];
    try {
      const runs = this.activeRuns();
      // a stacking search has no save point, and a sliding one reaches it
      // only with its start searched through: they pause, saving themselves
      const grid = this.store.snapshot?.grid;
      const method = grid === 'stacking' || grid === 'sliding' ? 'solve.pause' : 'solve.savePoint';
      for (const p of runs) {
        this.pausing.add(p);
        try {
          await e.call(method, { problem: p });
          paused.push(p);
        } catch {
          // the run ended meanwhile: nothing to pause
          this.pausing.delete(p);
        }
      }
      // each run's end (its revision), then the snapshot that shows it
      let rev = 0;
      for (const p of paused) {
        rev = Math.max(rev, await this.solves.whenEnded(p));
      }
      await this.store.whenRevision(rev);
      if (this.store.generation !== gen) {
        // another document replaced this one while we waited: do not save it
        return null;
      }
      if (this.activeRuns().length > 0) {
        // a run started meanwhile: no save this time
        if (byUser) {
          flash('A solve started meanwhile: not saved', 5000);
        }
        return null;
      }
      if (journal) {
        // the rescue copy keeps the solve; the puzzle's file is the user's to save
        if (this.rescue !== null && (await this.rescue())) {
          this.saves++;
          byId('leftS').dataset.autosaved = String(this.saves);
          flash('Crash recovery copy saved — the solve carries on');
        }
        return null;
      }
      const saved = await this.store.save(path);
      this.saves++;
      byId('leftS').dataset.autosaved = String(this.saves);
      flash(paused.length === 0 ? 'Saved' : 'Saved at a save point — the solve carries on');
      return saved;
    } catch (x) {
      reportError(x, byUser ? 'Save' : 'Crash recovery journal');
      return null;
    } finally {
      // the solves paused for the save carry on (unless the document is gone)
      for (const p of paused) {
        if (
          this.store.generation === gen &&
          !this.held.has(p) &&
          this.solves.peek(p)?.state === 'paused'
        ) {
          const options = this.optionsOf(p);
          try {
            await e.call('solve.continue', { problem: p, options });
            this.solves.launch(p, options.solver ?? null);
            this.continued(p);
          } catch (x) {
            reportError(x, 'Continue');
          }
        }
      }
      this.held.clear();
      this.pausing.clear();
      this.saving = false;
    }
  }
}
