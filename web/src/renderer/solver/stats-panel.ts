// SPDX-License-Identifier: GPL-3.0-or-later
//
// The debug statistics panel (FRONTEND-PLAN.md, stage 5; webui-spec.md
// section 7): the statistics of the solve of the problem the Solver shows
// (solve.status's `stats`), read again whenever the events tell of that
// solve (its state, its progress at most four times a second, its end) and
// when another problem is shown, worded by explore/debug-stats.ts as the
// original's panel. The original showed it in place of the 3D view on a
// Debug tab when "Enable Debug Statistics" was set; here it is a window of
// its own beside the view (shell/panel.ts), opened from the Solver's
// advanced section. As the original's, it holds the figures of a run that
// stopped (`freezeLive`) until a run is under way again or Refresh is
// pressed: the session counts the wall time on from the run's start.
// Ported from src/gui/debugstatspanel.cpp (`showStats`; the report is
// debug-stats.ts's).

import type { SolveStats } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import { isActive } from '../state/solves.ts';
import type { Solves } from '../state/solves.ts';
import type { Store } from '../state/store.ts';
import { esc } from '../shell/dom.ts';
import { openPanel } from '../shell/panel.ts';
import type { Panel } from '../shell/panel.ts';
import { reportError } from '../shell/status.ts';
import { statsReport } from '../explore/debug-stats.ts';

export class StatsPanel {
  private readonly store: Store;
  private panel: Panel | null = null;
  private problem: string | null = null;
  private loads = 0;
  private stats: SolveStats | null = null;
  /** The figures of a stopped run are held until a run is under way again (the original's freezeLive). */
  private frozen = false;
  /** The problem the Solver shows. */
  private readonly current: () => string | null;

  constructor(store: Store, solves: Solves, problem: () => string | null) {
    this.store = store;
    this.current = problem;
    solves.subscribe((c) => {
      if (this.panel !== null && (c.problem === null || c.problem === this.problem)) {
        this.load();
      }
    });
    store.subscribe((c) => {
      if (this.panel === null) {
        return;
      }
      if (c.kind === 'document' || c.kind === 'changed') {
        // another document, or a run's end put in: the problem shown may be another one
        const p = problem();
        if (p !== this.problem || c.kind === 'changed') {
          this.show(p);
        }
      }
    });
  }

  /** Whether the panel is open. */
  get isOpen(): boolean {
    return this.panel !== null;
  }

  /** Opens the panel on the problem the Solver shows. */
  open(): void {
    if (this.panel === null) {
      const p = openPanel('statsWin', 'Debug statistics', () => {
        this.panel = null;
      });
      p.el.classList.add('dbg');
      p.foot.innerHTML =
        '<span class="mute" id="statsFor"></span><span class="sp"></span><button class="btn sm" data-a="refresh">Refresh</button><button class="btn sm" data-a="close">Close</button>';
      p.foot.addEventListener('click', (e) => {
        const a = (e.target as Element).closest<HTMLElement>('[data-a]')?.dataset.a;
        if (a === 'refresh') {
          this.load(true);
        } else if (a === 'close') {
          p.close();
        }
      });
      this.panel = p;
    }
    this.show(this.current());
  }

  /** The Solver shows another problem: its statistics. */
  show(problem: string | null): void {
    this.problem = problem;
    this.stats = null;
    this.frozen = false;
    this.load();
  }

  private load(force = false): void {
    if (force) {
      this.frozen = false;
    }
    const panel = this.panel;
    const problem = this.problem;
    if (panel === null || !this.store.running) {
      return;
    }
    if (problem === null) {
      this.stats = null;
      this.render();
      return;
    }
    const n = ++this.loads;
    this.store.engine.call('solve.status', { problem }, { coalesce: 'stats:solve.status' }).then(
      (r) => {
        if (n !== this.loads || this.panel !== panel || problem !== this.problem) {
          return;
        }
        // a run's figures are shown live; once it stopped, the first figures
        // after are held (the session's wall time counts on from the run's start)
        const live = isActive(r.stats.state);
        if (this.frozen && !live) {
          return;
        }
        this.frozen = !live;
        this.stats = r.stats;
        this.render();
      },
      (e: unknown) => {
        if (
          e instanceof EngineError &&
          (e.errc === 'superseded' || e.errc === 'not_found' || e.errc === 'disconnected')
        ) {
          return;
        }
        reportError(e, 'The statistics could not be read');
      },
    );
  }

  private render(): void {
    const panel = this.panel;
    if (panel === null) {
      return;
    }
    const snap = this.store.snapshot;
    const label = snap?.problems.find((p) => p.id === this.problem)?.label ?? '';
    const forEl = panel.foot.querySelector('#statsFor');
    if (forEl !== null) {
      forEl.textContent = label === '' ? '' : `Problem ${label}`;
    }
    panel.el.dataset.problem = this.problem ?? '';
    const s = this.stats;
    if (s === null) {
      panel.body.innerHTML = '';
      panel.el.dataset.state = '';
      return;
    }
    const solvers = this.store.info?.solvers ?? [];
    const report = statsReport(s, (v) => solvers.find((x) => x.solver === v)?.label ?? v);
    panel.el.dataset.state = s.state;
    // the numbers as the engine gave them, for whoever compares (the e2e tests)
    panel.el.dataset.stats = JSON.stringify(s);
    panel.body.innerHTML =
      report.sections
        .map(
          (sec) =>
            (sec.title === null ? '' : `<h4>${esc(sec.title)}</h4>`) +
            `<table class="kv">${sec.rows
              .map(
                (r) =>
                  `<tr class="${r.indent ? 'in' : ''}"><th>${esc(r.label)}</th><td data-k="${esc(r.label)}">${esc(r.value)}</td></tr>`,
              )
              .join('')}</table>`,
        )
        .join('') +
      `<h4>Analysis</h4><ul class="notes">${report.notes.map((t) => `<li>${esc(t)}</li>`).join('')}</ul>`;
  }
}
