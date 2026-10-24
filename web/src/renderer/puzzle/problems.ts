// SPDX-License-Identifier: GPL-3.0-or-later
//
// The problems list of the Puzzle workspace's left card: the engine's
// problems (labels P1, P2... by place, names), one of them shown; new,
// duplicate (added last and shown), delete (never the last), rename in
// place (the pencil or a double-click; Enter commits, Esc cancels; the
// engine refuses what a name cannot hold and its message shows), reorder
// by drag (problem.move; the labels renumber). The list's changes are
// refused while anything is solved, a rename while that problem is
// (decision 0011). From docs/mockup/index.html: `renderPuzzle`'s problem
// rows, `pzRename`, `#pzNew`, the #pzProblems click, dblclick and drag
// handlers.

import type { ProblemSummary } from '../../engine/api.generated.ts';
import { $$, byId, esc, forgetHtml, markSelected, setHtml } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { ic } from '../shell/icons.ts';
import { flash } from '../shell/status.ts';
import { problemChipStyle, solveActive } from './counts.ts';
import type { PuzzleWorkspace } from './workspace.ts';

export class ProblemsList {
  /** The place of the problem shown when last drawn (its neighbour takes over when it goes). */
  lastIndex = 0;
  private readonly ws: PuzzleWorkspace;
  private readonly list = byId('pzProblems');
  private renaming: { id: string; input: HTMLInputElement; cancelled: boolean } | null = null;
  private dragFrom: string | null = null;

  constructor(ws: PuzzleWorkspace) {
    this.ws = ws;
    this.install();
  }

  private get problems(): ProblemSummary[] {
    return this.ws.store.snapshot?.problems ?? [];
  }

  /**
   * A sliding puzzle's problems follow its start/goal shapes: no new,
   * copied or deleted problem (the original greyed the buttons out; the
   * engine refuses them).
   */
  private get fixed(): boolean {
    return this.ws.store.snapshot?.grid === 'sliding';
  }

  /** Draws the list unless a rename is under way in it. */
  render(): void {
    const problems = this.problems;
    const cur = this.ws.current;
    const fixed = this.fixed;
    byId('pzCount').textContent = String(problems.length);
    byId('pzNew').style.display = fixed ? 'none' : '';
    const i = problems.findIndex((p) => p.id === cur);
    if (i >= 0) {
      this.lastIndex = i;
    }
    if (this.renaming !== null) {
      return;
    }
    setHtml(
      this.list,
      problems
        .map((q, n) => {
          const busy = solveActive(q.solve) || q.analysing;
          return `<div class="row" draggable="true" role="option" aria-selected="false" data-id="${q.id}" data-i="${n}" title="Drag the handle to reorder — double-click to rename${busy ? ' — being solved' : ''}"><span class="grip" title="Drag to reorder">${ic('grip', 14)}</span><span class="chip" style="${problemChipStyle(n)}">${esc(q.label)}</span><span class="lbl">${esc(q.name)}</span><span class="acts"><button class="ib" data-a="rename" title="Rename">${ic('edit', 14)}</button>${fixed ? '' : `<button class="ib" data-a="dup" title="Duplicate">${ic('copy', 14)}</button><button class="ib del" data-a="del" title="Delete" ${problems.length < 2 ? 'disabled' : ''}>${ic('trash', 14)}</button>`}</span></div>`;
        })
        .join(''),
    );
    markSelected(this.list, '.row', 'id', cur);
  }

  // ---- the actions ----

  private async create(): Promise<void> {
    if (this.fixed || this.ws.refuseList()) {
      return;
    }
    const r = await this.ws.mutate('New problem', () =>
      this.ws.store.engine.call('problem.create'),
    );
    if (r !== null) {
      this.ws.selectProblem(r.problem);
    }
  }

  private async action(a: string, id: string): Promise<void> {
    const problems = this.problems;
    if (!problems.some((p) => p.id === id)) {
      return;
    }
    if (a === 'rename') {
      this.startRename(id);
      return;
    }
    if (this.fixed || this.ws.refuseList()) {
      return;
    }
    const e = this.ws.store.engine;
    if (a === 'dup') {
      const r = await this.ws.mutate('Duplicate problem', () =>
        e.call('problem.duplicate', { problem: id }),
      );
      if (r !== null) {
        this.ws.selectProblem(r.problem);
      }
    } else if (a === 'del' && problems.length > 1) {
      await this.ws.mutate('Delete problem', () => e.call('problem.delete', { problem: id }));
    }
  }

  private async move(id: string, toIndex: number): Promise<void> {
    if (this.ws.refuseList()) {
      return;
    }
    const r = await this.ws.mutate('Move problem', () =>
      this.ws.store.engine.call('problem.move', { problem: id, toIndex }),
    );
    if (r !== null) {
      flash(`Order changed — problems renumbered P1…P${String(this.problems.length)}`);
    }
  }

  // ---- rename in place (the mockup's pzRename) ----

  private startRename(id: string): void {
    this.endRename(true);
    this.ws.selectProblem(id);
    const q = this.problems.find((p) => p.id === id);
    const lbl = this.list.querySelector(`.row[data-id="${id}"] .lbl`);
    if (q === undefined || lbl === null) {
      return;
    }
    if (this.ws.edits.refuse(solveActive(q.solve) || q.analysing)) {
      return;
    }
    lbl.innerHTML = `<input value="${esc(q.name)}" placeholder="Label" aria-label="Problem name">`;
    // the row holds a field now: the next render draws the list again
    forgetHtml(this.list);
    const input = lbl.querySelector('input');
    if (input === null) {
      return;
    }
    this.renaming = { id, input, cancelled: false };
    input.focus();
    input.select();
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        input.blur();
      }
    });
    input.addEventListener('blur', () => {
      this.endRename(false);
    });
  }

  private endRename(cancel: boolean): void {
    const r = this.renaming;
    if (r === null) {
      return;
    }
    this.renaming = null;
    const name = r.input.value;
    const q = this.problems.find((p) => p.id === r.id);
    if (!cancel && !r.cancelled && q !== undefined && name !== q.name) {
      void this.ws.mutate('Rename', () =>
        this.ws.store.engine.call('problem.rename', { problem: r.id, name }),
      );
    }
    this.render();
  }

  // ---- events ----

  private install(): void {
    const list = this.list;
    byId('pzNew').addEventListener('click', () => void this.create());
    list.addEventListener('click', (e) => {
      const t = e.target as Element;
      const id = t.closest<HTMLElement>('.row')?.dataset.id;
      if (id === undefined) {
        return;
      }
      const b = t.closest<HTMLElement>('[data-a]');
      if (b === null) {
        if (t.closest('input') === null) {
          this.ws.selectProblem(id);
        }
        return;
      }
      void this.action(b.dataset.a ?? '', id);
    });
    list.addEventListener('dblclick', (e) => {
      const t = e.target as Element;
      const id = t.closest<HTMLElement>('.row')?.dataset.id;
      if (id !== undefined && t.closest('[data-a],input') === null) {
        this.startRename(id);
      }
    });
    list.addEventListener('dragstart', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      if (row?.dataset.id !== undefined && e.dataTransfer !== null) {
        this.dragFrom = row.dataset.id;
        e.dataTransfer.effectAllowed = 'move';
        e.dataTransfer.setData('text/plain', row.dataset.id);
      }
    });
    list.addEventListener('dragover', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      if (row !== null && this.dragFrom !== null) {
        e.preventDefault();
        for (const r of $$('.row', list)) {
          r.classList.toggle('dragover', r === row);
        }
      }
    });
    list.addEventListener('drop', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.row');
      const from = this.dragFrom;
      this.dragFrom = null;
      if (row === null || from === null) {
        return;
      }
      e.preventDefault();
      const to = Number(row.dataset.i);
      if (this.problems.findIndex((p) => p.id === from) !== to) {
        void this.move(from, to);
      }
    });
    list.addEventListener('dragend', () => {
      this.dragFrom = null;
      for (const r of $$('.row', list)) {
        r.classList.remove('dragover');
      }
    });
    addEscapeStep(ESC_ORDER.rename, () => {
      if (this.renaming === null) {
        return false;
      }
      this.renaming.cancelled = true;
      this.renaming.input.blur();
      return true;
    });
  }
}
