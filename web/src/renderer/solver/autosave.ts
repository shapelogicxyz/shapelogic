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
// (decision 0024). The page's `autosave` parameter (SHAPELOGIC_AUTOSAVE_SECONDS,
// which the main process passes in the page's address) sets the least time
// between saves in seconds, for the e2e tests. A run still preparing its
// search (building and reducing its placements, minutes for a large
// puzzle) has no save point yet, and the engine drops one asked for then:
// the save waits till it searches, and File ▸ Save says so.
//
// An Abort while a save waits for its runs (`abort`): the save no longer
// shows that run as its own, does not carry it on, and the journal writes
// no copy of a solve thrown away.
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
// point), saves, and carries it on.
//
// A sliding puzzle's search stops at a save point too, at the end of the
// level it is on, saved in its folder: nothing is searched twice, and each
// run ends at least a level further than it began. The journal asks for
// that and waits, however long the level takes, while the search works on
// (and is shown working: `worksOn`). It used to pause the search, which
// cuts the level under way short to be searched again: once a level took
// longer than the time between two saves, every save threw the level away
// and the search stood still for good. Only the time from the run's end
// counts as the save's cost. A save the user asks for (File ▸ Save) does
// not wait for a level's end: it pauses the search, which is carried on
// redoing the level cut short, once; asked for while the journal asks its
// runs or waits for them, it takes the journal's place (`join`); asked for
// later (the rescue copy being written, the runs being carried on), it
// follows the journal's save, and pauses the runs that save has just
// carried on (`activeRuns` counts a run from its call's answer).
//
// A run is carried on only when it paused for the save: at the save point
// asked for (its last progress says `atSavePoint`), or by the pause asked
// for. One that ended on its own meanwhile (finished, a slide search out
// of disk) is the user's to see: it is left as it is, and its note is told
// (`carriesOn` is false of it). The problems' stored `autosave` (the
// original's box) is not looked at.
//
// A reload of the page (View ▸ Reload in development, the dev server's
// when a source file is saved) leaves the engine and its runs going, and
// the page that asked a run for a save point may be gone before the run
// gets there: a sliding search works on to the end of its level, which can
// take hours, then pauses for a save nobody makes, and stands still. So a
// page that came to solves it did not start takes what the page before
// left (`handover`), until a save of its own has asked each of them: a run
// under way is asked for a save point at once, not a minute in (the pause
// is then this page's own to carry on), and a solve that pauses at a save
// point meanwhile, or was found paused at one, is carried on (`pickUp`).
// A run is carried on without options: the engine carries a solve paused
// in memory on as it ran (solve.continue), so no page has to have kept
// what it was started with; with other options a sliding search would be
// another search. A solve the user paused is paused by the
// pause, not at a save point, and stays so; one the user paused in the
// moment a save had it at its save point is carried on by the page that
// follows a reload, which cannot tell it from a save left unfinished.

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
  /** The solves the user aborted while the save under way waited (`abort`). */
  private readonly aborted = new Set<string>();
  /**
   * The page before this one (a reload) may have left a save unfinished: a
   * save point asked for, its pause not carried on. From the page's start
   * until the engine holds no solve, running or paused, that this page
   * neither started nor asked for a pause itself (`asked`).
   */
  private handover = true;
  /** The solves a save of this page has asked to pause: their pauses are its own. */
  private readonly asked = new Set<string>();
  /** The page's first look at the engine's solves is still to come. */
  private arrived = false;
  /** The paused solves looked at in the handover (`pickUp`), once each. */
  private readonly picked = new Set<string>();
  /** The solves the save under way pauses, to carry them on, and how each was asked. */
  private readonly pausing = new Map<string, 'pause' | 'savePoint'>();
  /** The save under way asked sliding searches for a save point: they work on until it. */
  private levelEnd = false;
  /** The save under way when it is the journal's (a save the user asks for joins or follows it). */
  private journalling: Promise<string | null> | null = null;
  /** The save under way asks its runs or waits for them to end: a save the user asks for can still join it. */
  private waiting = false;
  /** A save the user asked for while the journal's waited: made in its place. */
  private joined: { path: string | undefined; done: (saved: string | null) => void } | null = null;
  /** When the sliding searches a save let work on to their save point had ended (`costFrom`). */
  private slideEndedAt: number | null = null;

  constructor(
    store: Store,
    solves: Solves,
    continued: (problem: string) => void = () => undefined,
  ) {
    this.store = store;
    this.solves = solves;
    this.continued = continued;
    window.setInterval(
      () => {
        this.tick();
      },
      Math.min(1000, this.floor / 4),
    );
  }

  private tick(): void {
    if (this.handover) {
      this.takeOver();
    }
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
    // a run still preparing its search has no save point yet: the save
    // waits till it searches (`preparing`)
    if (this.preparing(active)) {
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

  // The handover from the page before this one (a reload). Runs under way
  // that this page did not start: their journal is due at once, so that a
  // save point that page asked for and left is asked for again, and its
  // pause is this page's to carry on. A solve paused meanwhile (the save
  // point came before this page's save had begun) or found paused: carried
  // on when it is paused at a save point. Over when the engine holds no
  // solve, running or paused, but those this page started or has asked for
  // a pause itself.
  private takeOver(): void {
    const snap = this.store.snapshot;
    if (snap === null) {
      return;
    }
    // a run this page started or carried on is `launched` until it ends
    const found = this.activeRuns().filter(
      (p) => !this.asked.has(p) && this.solves.peek(p)?.launched !== true,
    );
    if (!this.arrived) {
      this.arrived = true;
      if (found.length > 0) {
        this.since = Date.now() - this.wait;
      }
    }
    const paused = snap.problems
      .map((p) => p.id)
      .filter((p) => !this.asked.has(p) && this.stateOf(p) === 'paused');
    for (const p of paused) {
      if (!this.picked.has(p)) {
        this.picked.add(p);
        void this.pickUp(p);
      }
    }
    if (found.length === 0 && paused.length === 0) {
      this.handover = false;
    }
  }

  // read through a call: the flag changes across the awaits of pickUp()
  private savingNow(): boolean {
    return this.saving;
  }

  // The state of a problem's solve: the events', else the snapshot's.
  private stateOf(problem: string): string {
    return this.solves.stateOf(problem, this.store.snapshot);
  }

  // Carries on a solve paused at a save point that no save of this page
  // asked for: the page before asked, and went before it carried the solve
  // on. Not one paused for anything else (the user's Pause, a slide search
  // out of disk: `atSavePoint` is false). The run's end is waited for
  // first (its revision, then the snapshot that shows it), as a save does.
  private async pickUp(problem: string): Promise<void> {
    const e = this.store.engine;
    const gen = this.store.generation;
    const atSavePoint = async (): Promise<boolean> => {
      const st = await e.call('solve.status', { problem });
      return st.state === 'paused' && st.progress.atSavePoint;
    };
    try {
      if (!(await atSavePoint())) {
        return;
      }
      await this.store.whenRevision(await this.solves.whenEnded(problem));
      if (this.store.generation !== gen || this.asked.has(problem)) {
        return;
      }
      if (this.savingNow()) {
        // a save under way (the user's): looked at again after it
        this.picked.delete(problem);
        return;
      }
      // still there: not carried on or thrown away meanwhile
      if (this.solves.peek(problem)?.launched === true || !(await atSavePoint())) {
        return;
      }
      if (this.savingNow() || this.store.generation !== gen) {
        this.picked.delete(problem);
        return;
      }
      // as it ran: the engine keeps a paused solve's options
      await e.call('solve.continue', { problem });
      this.solves.launch(problem);
      this.continued(problem);
      flash('The solve was paused for a save — it carries on');
    } catch (x) {
      reportError(x, 'Continue');
    }
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
      this.wait = nextWait(now - this.costFrom(began), this.floor);
      this.since = now;
      this.saved = mark;
    }
  }

  // When the save that began at `began` started to cost the solves time: a
  // sliding search worked on until its save point, so that wait cost it
  // nothing and the cost counts from its run's end.
  private costFrom(began: number): number {
    const ended = this.slideEndedAt;
    this.slideEndedAt = null;
    return ended ?? began;
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

  /**
   * The user aborted this solve: a save under way is no longer the one
   * that stops it (`carriesOn` is false), and the journal's, if this was
   * all it waited for, writes no copy of a solve thrown away.
   */
  abort(problem: string): void {
    if (this.saving) {
      this.held.add(problem);
      this.aborted.add(problem);
    }
  }

  // Whether one of these runs is still preparing its search (building and
  // reducing its placements, which can take minutes): the engine has no
  // save point before its search begins, and drops a solve.savePoint asked
  // for then, so a save that asked would wait for the whole run's end.
  private preparing(runs: readonly string[]): boolean {
    return runs.some((p) => {
      const r = this.solves.peek(p);
      return r?.phase === 'active' && r.state === 'preparing';
    });
  }

  /**
   * Whether the save under way paused this solve and will carry it on when
   * it is through: not one whose run is over for another reason (finished,
   * failed, or paused by something else than the save).
   */
  carriesOn(problem: string): boolean {
    if (
      !this.saving ||
      !this.pausing.has(problem) ||
      this.held.has(problem) ||
      this.aborted.has(problem)
    ) {
      return false;
    }
    const r = this.solves.peek(problem);
    // till the run's end is told whole (its last progress), it is the save's
    if (r?.phase !== 'none' || r.launched) {
      return true;
    }
    return this.pausedForSave(problem);
  }

  /**
   * Whether the save under way lets this solve's search work on until its
   * save point (a sliding search, to the end of its level): it is shown
   * searching, not saving, until its run ends.
   */
  worksOn(problem: string): boolean {
    return this.saving && this.levelEnd && this.pausing.get(problem) === 'savePoint';
  }

  // Whether the run is paused, and for the save: by the pause asked for, or
  // at the save point asked for (not by anything that happened meanwhile).
  private pausedForSave(problem: string): boolean {
    const r = this.solves.peek(problem);
    return (
      r?.state === 'paused' &&
      (this.pausing.get(problem) === 'pause' || r.progress?.atSavePoint === true)
    );
  }

  /** Whether a run is under way (a save must wait for a save point). */
  runsUnderWay(): boolean {
    return this.activeRuns().length > 0;
  }

  // The problems whose run is under way: an active state told, or a run
  // this page started or carried on whose first state is still to come
  // (`launched`: the engine answered the call, so the run is there). Without
  // the second, a save that follows another's solve.continue at once (the
  // user's, asked for as the journal's ended) saw no run, paused nothing,
  // and the engine refused the document's bytes: "Pause the solve first".
  private activeRuns(): string[] {
    const snap = this.store.snapshot;
    if (snap === null) {
      return [];
    }
    return snap.problems
      .map((p) => p.id)
      .filter((id) => {
        const r = this.solves.peek(id);
        return r !== undefined && (r.launched || (r.phase === 'active' && isActive(r.state)));
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
      // the user's save while the journal's is under way: in its place while
      // it still waits for its runs, else after it (it is through in a moment)
      if (byUser && this.journalling !== null && this.joined === null) {
        if (this.waiting) {
          return this.join(path);
        }
        await this.journalling;
        return this.saveAtSavePoint(path, true);
      }
      if (byUser) {
        flash('A save is under way already');
      }
      return null;
    }
    const save = this.save(path, byUser, journal);
    this.journalling = journal ? save : null;
    return save;
  }

  // saveAtSavePoint with no save under way; `saving` from its first step to its last.
  private async save(
    path: string | undefined,
    byUser: boolean,
    journal: boolean,
  ): Promise<string | null> {
    this.saving = true;
    const e = this.store.engine;
    // the document being saved: if another replaces it while we wait (a run's
    // end, then the snapshot), its solves are gone and its file must not be
    // written with the new document's bytes
    const gen = this.store.generation;
    const paused: string[] = [];
    let saved: string | null = null;
    try {
      const runs = this.activeRuns();
      // A stacking search has no save point: it pauses, saving itself. A
      // sliding search reaches its save point at the end of the level it is
      // on: the journal waits for that, a save the user asked for pauses it.
      const grid = this.store.snapshot?.grid;
      const pause = grid === 'stacking' || (grid === 'sliding' && !journal);
      const toLevelEnd = grid === 'sliding' && !pause;
      if (!pause && this.preparing(runs)) {
        // no save point before the search begins (`preparing`)
        if (byUser) {
          flash('The solve is still preparing its search: save once it is searching', 5000);
        }
        return null;
      }
      this.levelEnd = toLevelEnd;
      // from the first run asked until the last has ended, a save the user
      // asks for takes the journal's place (`join`): while a run is still
      // being asked too, or that save would wait for a level's end
      this.waiting = true;
      for (const p of runs) {
        // joined meanwhile: the runs still to ask are paused as `join` pauses the others
        const byPause = pause || this.joined !== null;
        this.pausing.set(p, byPause ? 'pause' : 'savePoint');
        try {
          await e.call(byPause ? 'solve.pause' : 'solve.savePoint', { problem: p });
          paused.push(p);
          // from here on its pauses are of this page's asking (`takeOver`)
          this.asked.add(p);
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
      this.waiting = false;
      this.slideEndedAt = toLevelEnd ? Date.now() : null;
      // a save the user asked for meanwhile is made in the journal's place
      const user = byUser || this.joined !== null;
      if (this.store.generation !== gen) {
        // another document replaced this one while we waited: do not save it
        return null;
      }
      if (this.activeRuns().length > 0) {
        // a run started meanwhile: no save this time
        if (user) {
          flash('A solve started meanwhile: not saved', 5000);
        }
        return null;
      }
      if (journal && this.joined === null) {
        // every solve it paused was aborted meanwhile: nothing to keep
        if (paused.length > 0 && paused.every((p) => this.aborted.has(p))) {
          return null;
        }
        // the rescue copy keeps the solve; the puzzle's file is the user's to save
        if (this.rescue !== null && (await this.rescue())) {
          this.saves++;
          byId('leftS').dataset.autosaved = String(this.saves);
          flash('Crash recovery copy saved — the solve carries on');
        }
        return null;
      }
      saved = await this.store.save(this.joined !== null ? this.joined.path : path);
      this.saves++;
      byId('leftS').dataset.autosaved = String(this.saves);
      flash(paused.length === 0 ? 'Saved' : 'Saved at a save point — the solve carries on');
      return saved;
    } catch (x) {
      reportError(x, byUser || this.joined !== null ? 'Save' : 'Crash recovery journal');
      return null;
    } finally {
      this.waiting = false;
      // The solves paused for the save carry on (unless the document is
      // gone); one that paused for another reason stays as it is.
      for (const p of paused) {
        if (this.store.generation !== gen || this.held.has(p) || !this.pausedForSave(p)) {
          continue;
        }
        try {
          // as it ran: the engine keeps a paused solve's options
          await e.call('solve.continue', { problem: p });
          this.solves.launch(p);
          this.continued(p);
        } catch (x) {
          reportError(x, 'Continue');
        }
      }
      const joined = this.joined;
      this.joined = null;
      this.held.clear();
      this.aborted.clear();
      this.pausing.clear();
      this.levelEnd = false;
      this.journalling = null;
      this.saving = false;
      joined?.done(saved);
    }
  }

  // A save the user asks for while the journal waits for its runs to end:
  // made in the journal's place once they have, the puzzle to its file. A
  // sliding search the journal let work on to its save point is paused for
  // it (the user does not wait for a level's end), and carried on after.
  private join(path: string | undefined): Promise<string | null> {
    const result = new Promise<string | null>((done) => {
      this.joined = { path, done };
    });
    if (this.levelEnd) {
      this.levelEnd = false;
      for (const p of this.pausing.keys()) {
        this.pausing.set(p, 'pause');
        // refused when the run has ended meanwhile: nothing to pause
        this.store.engine.call('solve.pause', { problem: p }).catch(() => undefined);
      }
    }
    return result;
  }
}
