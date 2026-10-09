// SPDX-License-Identifier: GPL-3.0-or-later
//
// Autosave (the problem's stored setting, the original's "Autosave every
// 20 min"): twenty minutes into a solve and every twenty minutes after, the
// solves under way pause at a save point (solve.savePoint: the assembly
// stops where it can be saved, the take-aparts under way finish), the
// puzzle is saved to its file (document.save: refused while any run is
// active, so every run is paused first, decision 0010), and the solves
// paused for it carry on (solve.continue, with the options they ran
// with). A run's pause is waited for by its revision (the solve record's
// end), not its state. A puzzle without a file (opened by its bytes) is
// not saved: it says so once a solve. `saveAtSavePoint` is the path a save
// refused while solving offers (stage 6's Save). The interval is 20
// minutes; BT_AUTOSAVE_SECONDS (the main process passes it in the page's
// address) shortens it for the e2e tests.

import type { SolveOptions } from '../../engine/api.generated.ts';
import { isActive } from '../state/solves.ts';
import type { Solves } from '../state/solves.ts';
import type { Store } from '../state/store.ts';
import { byId } from '../shell/dom.ts';
import { flash, reportError } from '../shell/status.ts';

/** The interval in milliseconds: 20 minutes, or the page's `autosave` parameter (seconds). */
function interval(): number {
  const s = Number(new URLSearchParams(window.location.search).get('autosave'));
  return Number.isFinite(s) && s > 0 ? s * 1000 : 20 * 60 * 1000;
}

export class Autosave {
  /** Saves made (readable from outside: `#leftS[data-autosaved]`). */
  saves = 0;
  private readonly store: Store;
  private readonly solves: Solves;
  private readonly optionsOf: (problem: string) => SolveOptions;
  /** Which problems want autosave (their stored setting), by id. */
  private readonly wanted = new Map<string, boolean>();
  private readonly every = interval();
  /** When the solves under way started or were last saved. */
  private since: number | null = null;
  private saving = false;
  private toldNoFile = false;

  constructor(store: Store, solves: Solves, optionsOf: (problem: string) => SolveOptions) {
    this.store = store;
    this.solves = solves;
    this.optionsOf = optionsOf;
    window.setInterval(
      () => {
        this.tick();
      },
      Math.min(1000, this.every / 4),
    );
  }

  /** A problem's setting (the workspace tells it when it reads or stores the options). */
  want(problem: string, on: boolean): void {
    this.wanted.set(problem, on);
  }

  private tick(): void {
    const active = this.activeRuns();
    const now = Date.now();
    if (active.length === 0) {
      this.since = null;
      this.toldNoFile = false;
      return;
    }
    if (this.since === null) {
      this.since = now;
      return;
    }
    if (this.saving || now - this.since < this.every) {
      return;
    }
    if (!active.some((p) => this.wanted.get(p) ?? true)) {
      return;
    }
    this.since = now;
    if (this.store.snapshot?.path === null) {
      if (!this.toldNoFile) {
        this.toldNoFile = true;
        flash('Autosave: the puzzle has no file to save to yet', 5000);
      }
      return;
    }
    void this.saveAtSavePoint();
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
   * Pauses the solves under way at a save point, saves the puzzle to its
   * file, and carries the paused ones on; false when it could not save.
   */
  async saveAtSavePoint(): Promise<boolean> {
    if (this.saving) {
      return false;
    }
    this.saving = true;
    const e = this.store.engine;
    const paused: string[] = [];
    try {
      const runs = this.activeRuns();
      for (const p of runs) {
        try {
          await e.call('solve.savePoint', { problem: p });
          paused.push(p);
        } catch {
          // the run ended meanwhile: nothing to pause
        }
      }
      // each run's end (its revision), then the snapshot that shows it
      let rev = 0;
      for (const p of paused) {
        rev = Math.max(rev, await this.solves.whenEnded(p));
      }
      await this.store.whenRevision(rev);
      if (this.activeRuns().length > 0) {
        // a run started meanwhile: no save this time
        return false;
      }
      await e.call('document.save', {});
      this.saves++;
      byId('leftS').dataset.autosaved = String(this.saves);
      flash('Autosaved — the solve carries on');
      return true;
    } catch (x) {
      reportError(x, 'Autosave');
      return false;
    } finally {
      // the solves paused for the save carry on
      for (const p of paused) {
        if (this.solves.peek(p)?.state === 'paused') {
          const options = this.optionsOf(p);
          try {
            await e.call('solve.continue', { problem: p, options });
            this.solves.launch(p, options.solver ?? null);
          } catch (x) {
            reportError(x, 'Continue');
          }
        }
      }
      this.saving = false;
    }
  }
}
