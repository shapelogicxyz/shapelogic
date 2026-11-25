// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's questions and notes around a run (cleanup item 13.2): a
// sliding or stacking run's note at its end, with Full Search after
// optimized paths found nothing (decision 0029); at Solve, whether to use
// optimized paths; at Continue, whether to carry on a search an earlier
// version saved from the start alone (decision 0033); after a run's end,
// what to do when the cache folder cannot be written (decision 0028).
// What an answer does (a new run, Continue, Abort) is the workspace's,
// through `RunQuestionsHost`.

import type { SolveOptions } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Store } from '../state/store.ts';
import { askChoice, tell, VIEW_LOGS } from '../shell/dialog.ts';
import { chooseCacheFolder } from '../shell/settings.ts';
import { flash, reportError } from '../shell/status.ts';
import { LABEL } from './option-text.ts';
import { noteIsTrouble } from './problem-note.ts';
import type { Family } from './settings.ts';

/** What the questions read of the workspace, and the runs an answer starts. */
export interface RunQuestionsHost {
  readonly store: Pick<Store, 'generation' | 'snapshot' | 'engine'>;
  /** Resolves when a problem's run has ended. */
  whenEnded(problem: string): Promise<unknown>;
  /** Whether the autosave carries a problem's run on (its pause is not the user's). */
  carriesOn(problem: string): boolean;
  family(): Family;
  /** Whether a run call is under way (Solve, Continue, Pause, Abort). */
  calling(): boolean;
  /** Full Search: solve the problem again, searching every stop. */
  searchFully(problem: string): Promise<void>;
  /** Continue the problem's paused solve with the options it was started with. */
  carryOn(problem: string): Promise<void>;
  /** Abort the problem's solve. */
  abortSolve(problem: string): Promise<void>;
}

/** The questions and notes of the Solver's runs. */
export class RunQuestions {
  private readonly host: RunQuestionsHost;
  /** Problems whose cache folder question is open (askFolder): asked once at a time. */
  private readonly askingFolder = new Set<string>();

  constructor(host: RunQuestionsHost) {
    this.host = host;
  }

  /**
   * A sliding or stacking run's note when it ends (the original's
   * bt_message of the solver note): whether a missing solution is proven,
   * the search gave up at a limit, or (a stacking search paused) where it
   * was saved. Not for a pause to autosave, which carries on (a pause the
   * user asked for meanwhile is told).
   */
  async tellNote(problem: string): Promise<void> {
    const store = this.host.store;
    const gen = store.generation;
    await this.host.whenEnded(problem);
    if (gen !== store.generation || this.host.carriesOn(problem)) {
      return;
    }
    try {
      const st = await store.engine.call('solve.status', { problem });
      if (st.note === null || gen !== store.generation) {
        return;
      }
      if (this.host.family() === 'sliding') {
        const p = await store.engine.call('problem.get', { problem });
        if (gen !== store.generation) {
          return;
        }
        if (p.slideGrid > 0 && p.solutions === 0 && p.state === 'solved') {
          await this.offerFullSearch(problem, st.note);
          return;
        }
      }
      tell(st.note, noteIsTrouble(st.note));
    } catch (e) {
      if (!(e instanceof EngineError) || e.errc !== 'not_found') {
        reportError(e, 'The solver’s note could not be read');
      }
    }
  }

  /**
   * Optimized paths found no solution (decision 0029): the note, and a
   * full search of every stop offered in its place.
   */
  private async offerFullSearch(problem: string, note: string): Promise<void> {
    const gen = this.host.store.generation;
    const k = await askChoice('optimizedNoPath', 'No Solution with Optimized Paths', note, [
      { key: 'ok', label: 'OK' },
      {
        key: 'full',
        label: 'Full Search',
        kind: 'pri',
        default: true,
        tip: 'Solve again, searching every stop',
      },
    ]);
    if (k !== 'full' || gen !== this.host.store.generation || this.host.calling()) {
      return;
    }
    await this.host.searchFully(problem);
  }

  /**
   * Continue on a sliding puzzle whose paused search an earlier version
   * saved from the start alone, where a new search would go from both ends
   * (decision 0033, sliding.resumeAdvice): whether to carry the saved one
   * on, the question saying that Abort, then Solve, uses the far faster
   * search. True without a question for every other paused search; false
   * when the question was canceled or dismissed (the solve stays paused).
   */
  async askFromStartAlone(problem: string, options: SolveOptions): Promise<boolean> {
    let advice;
    try {
      advice = await this.host.store.engine.call('sliding.resumeAdvice', { problem, options });
    } catch (e) {
      reportError(e, 'The saved search could not be looked at');
      return true;
    }
    if (!advice.fromStartAlone) {
      return true;
    }
    const k = await askChoice(
      'fromStartAlone',
      'Carry On the Slower Search?',
      'This solve was paused in an earlier version, which searched from the start only. This version searches from the start and from the goal until the two meet, which is far faster: a solve of hours can take seconds.\n\nThe saved search can only be carried on as it began, from the start alone. To use the faster search instead, press Cancel, then Abort and Solve: Abort throws the saved search away, and Solve begins a new search from both ends.\n\nCarrying on is only reasonable when the search has already run for a long time and is likely very close to its end.',
      [
        {
          key: 'cancel',
          label: 'Cancel',
          kind: 'pri',
          default: true,
          tip: 'Leave the solve paused (Abort, then Solve, searches again from both ends)',
        },
        {
          key: 'continue',
          label: 'Continue',
          tip: 'Carry on the saved search to its end, from the start alone',
        },
      ],
    );
    return k === 'continue';
  }

  /**
   * Solve on a sliding puzzle whose Automatically use optimized paths is
   * off (decision 0029): when the board fits a grid (sliding.gridAdvice),
   * whether this solve should hold the stops to it. True to use it, false
   * for the full search (and when the board fits none), null when the
   * question was canceled.
   */
  async askOptimizedPaths(problem: string): Promise<boolean | null> {
    let advice;
    try {
      advice = await this.host.store.engine.call('sliding.gridAdvice', { problem });
    } catch (e) {
      reportError(e, 'The board could not be looked at for optimized paths');
      return false;
    }
    if (advice.grid === 0) {
      return false;
    }
    const k = await askChoice(
      'optimizedPaths',
      'Use Optimized Paths?',
      `This board fits a ${String(advice.grid)}-cell grid. With optimized paths every piece stops only on it: blocks on its lines, smaller pieces where they fit in a block’s hole or where they start or end. The pieces can then stop in ${advice.gridStops.toLocaleString('en-US')} places instead of ${advice.stops.toLocaleString('en-US')}.\n\nThe search is far smaller and faster, but a solution it finds may have more moves than the fewest, and if it finds none a full search may still find one.\n\nTo use optimized paths without being asked, turn on ${LABEL.optimizedPaths} in the Search Options.`,
      [
        { key: 'cancel', label: 'Cancel', tip: 'Solve nothing' },
        {
          key: 'full',
          label: 'Full Search',
          tip: 'Search every stop: the fewest moves, or proof that there is no solution',
        },
        {
          key: 'optimized',
          label: 'Use Optimized Paths',
          kind: 'pri',
          default: true,
          tip: 'Search only the stops on the grid, for this solve',
        },
      ],
    );
    return k === null || k === 'cancel' ? null : k === 'optimized';
  }

  /**
   * A sliding or stacking run that paused before its search began because
   * its cache folder could not be written (decision 0028; a drive that is
   * gone, a folder without write access): whether to choose another cache
   * folder, try again with the folder as it is, or abort the solve. Asked
   * after any run's end (a run of another problem, one the autosave carried
   * on); Escape leaves the solve paused for Continue.
   */
  async askFolder(problem: string): Promise<void> {
    const store = this.host.store;
    const gen = store.generation;
    if (this.askingFolder.has(problem)) {
      return;
    }
    this.askingFolder.add(problem);
    try {
      let st;
      try {
        st = await store.engine.call('solve.status', { problem });
      } catch (e) {
        if (!(e instanceof EngineError) || e.errc !== 'not_found') {
          reportError(e, 'The solve’s state could not be read');
        }
        return;
      }
      const f = st.folderFailure;
      if (f === null || st.state !== 'paused') {
        return;
      }
      const p = store.snapshot?.problems.find((q) => q.id === problem);
      const name = p === undefined ? 'the problem' : p.name !== '' ? p.name : `problem ${p.label}`;
      for (;;) {
        if (gen !== store.generation) {
          return;
        }
        const k = await askChoice(
          'cacheUnwritable',
          'Cache Folder Can’t Be Written',
          `The solve of ${name} is paused before its search began: the search can’t write its files in the cache folder.\n\n${f.folder}: ${f.reason}.\n\nChoose another cache folder, try again with the cache folder as it is (once its drive is back, say), or abort the solve.`,
          [
            VIEW_LOGS,
            {
              key: 'abort',
              label: 'Abort Solve',
              kind: 'danger',
              tip: 'Throw the solve away: its solutions and its paused state',
            },
            {
              key: 'retry',
              label: 'Try Again',
              tip: 'Continue the solve with the cache folder as it is',
            },
            {
              key: 'choose',
              label: 'Choose Folder…',
              kind: 'pri',
              default: true,
              tip: 'Choose another cache folder in Settings, then continue the solve',
            },
          ],
        );
        if (gen !== store.generation) {
          return;
        }
        if (k === 'choose' && !(await chooseCacheFolder())) {
          // not changed (canceled, or refused): asked again
          continue;
        }
        if (k === 'choose' || k === 'retry') {
          await this.host.carryOn(problem);
        } else if (k === 'abort') {
          await this.host.abortSolve(problem);
        } else {
          flash('The solve stays paused: Continue tries again');
        }
        return;
      }
    } finally {
      this.askingFolder.delete(problem);
    }
  }
}
