// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stacking problem's board and stacks in the Puzzle workspace
// (docs/webui-spec.md, section 9.2). In the left card, in the result's
// place: "Result: R1 - name" (or "No Start/Goal Rods" in red), the rod
// sets to choose from and Set Start/Goal Rods. In the right card, in the
// selected piece's place: Start or Goal, the rod slider ("Rod: 1", the
// pocket "Rod: P" first), Stack Order (the discs on the rod, the top
// first, in the pieces' colours), the original's ↑ ↓ +1 −1 All +1 and
// Clear All, and the validity bar ("Valid" or "Invalid - reason"). From
// the original's mainwindow_layout.cpp (the widgets, their labels and
// tooltips), resultviewer.cpp (the board's text), rodbars.h
// (`RodIndexBar_c`, `StackValidBar_c`), BlockList.cpp (`RodSelector`,
// `RodStackList`) and mainwindow_rods.cpp / mainwindow.cpp
// (`selectedStackRod`, `refreshStackList`, `refreshStackValid`,
// `cb_ProblemRod`, `cb_StackMode`, cb_ShapeToResult, cb_AddShapeToProblem,
// cb_RemoveShapeFromProblem, cb_AddAllShapesToProblem,
// cb_RemoveAllShapesFromProblem and cb_ProbShapeExchange's stacking
// branches). The board is the engine's (stacking.board); every action is a
// stacking.* call awaited until the cards show its revision.

import type { DiscRef } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import { byId, esc, setHtml } from '../shell/dom.ts';
import { chipColour, contrast, hex, ROD_CHIP } from '../shell/piece-colours.ts';
import { flash, reportError } from '../shell/status.ts';
import { rodCaption, rodPlaces, rodSetText, stackIndex, stackValidity } from '../state/stacking.ts';
import type { RodPlaces } from '../state/stacking.ts';
import type { PuzzleWorkspace } from './workspace.ts';

export class StacksCard {
  private readonly ws: PuzzleWorkspace;

  constructor(ws: PuzzleWorkspace) {
    this.ws = ws;
    this.install();
  }

  /** The discs on the rod chosen, bottom to top (empty without a board, or one read for the other stacking). */
  get stack(): DiscRef[] {
    const b = this.ws.board;
    return b === null || this.ws.boardGoal !== this.ws.goal ? [] : (b.stacks[this.ws.rod] ?? []);
  }

  /** The slider's places for the board shown. */
  get places(): RodPlaces {
    const rods = this.ws.board?.rods ?? [];
    return rodPlaces(
      rods.length,
      rods.some((r) => r.pocket),
    );
  }

  render(): void {
    this.renderBoard();
    this.renderStacks();
  }

  // the left card: the board (resultviewer.cpp), the rod sets, Set Start/Goal Rods
  private renderBoard(): void {
    const ws = this.ws;
    const el = byId('pzBoard');
    const snap = ws.store.snapshot;
    if (snap?.grid !== 'stacking' || ws.detail === null) {
      setHtml(el, '');
      return;
    }
    const sets = snap.rodSets;
    const own = sets.find((r) => r.id === ws.detail?.rodSet);
    const head =
      own === undefined
        ? '<span class="chip" style="background:var(--muted)">–</span><div class="meta"><b style="color:var(--danger)">No Start/Goal Rods</b><span>Choose a rod set and Set Start/Goal Rods</span></div>'
        : `<span class="chip" style="background:${ROD_CHIP};color:#fff">${esc(own.label)}</span><div class="meta"><b>Result: ${esc(rodSetText(own))}</b><span>${String(own.rodCount)} rod${own.rodCount === 1 ? '' : 's'}${own.panexColumns ? ', Panex columns' : ''}${own.pocketColumn ? ', a pocket' : ''}</span></div>`;
    const rows = sets
      .map(
        (r) =>
          `<div class="rodrow ${r.id === ws.boardSel ? 'sel' : ''}" data-board="${r.id}" role="option" aria-selected="${String(r.id === ws.boardSel)}" title="Rod sets that can be this problem's board"><span class="chip" style="background:${ROD_CHIP};color:#fff">${esc(r.label)}</span><span class="lbl">${esc(r.name)}</span>${r.id === ws.detail?.rodSet ? '<span class="mute">board</span>' : ''}</div>`,
      )
      .join('');
    const frozen = ws.problemFrozen;
    setHtml(
      el,
      `<div class="pz-result">${head}<button class="btn sm" id="pzSetRods" title="Set the selected rod set as this problem's board" ${ws.boardSel === null || ws.boardSel === ws.detail.rodSet || frozen ? 'disabled' : ''}>Set Start/Goal Rods</button></div><div class="rodlist" role="listbox" aria-label="Rods">${rows}</div>`,
    );
    el.dataset.rodSet = ws.detail.rodSet ?? '';
  }

  // the right card: Start or Goal, the rod, the stack order, the buttons, the validity
  private renderStacks(): void {
    const ws = this.ws;
    const el = byId('pzStacks');
    const snap = ws.store.snapshot;
    const d = ws.detail;
    if (snap?.grid !== 'stacking') {
      setHtml(el, '');
      return;
    }
    const board = ws.board;
    const places = this.places;
    const place = Math.max(0, Math.min(places.count - 1, ws.rodPlace));
    const stack = this.stack;
    const depth = stack.length;
    const row = Math.min(ws.stackRow, Math.max(0, depth - 1));
    const frozen = ws.problemFrozen;
    const hasBoard = d?.rodSet !== null && d !== null;
    const fresh = ws.boardGoal === ws.goal;
    // +1 and All +1 without a board say why (the original's status line), the others need one
    const on = (b: boolean, board = true): string =>
      b && !frozen && fresh && (hasBoard || !board) ? '' : 'disabled';
    const shapes = snap.shapes;
    const list =
      depth === 0
        ? '<div class="pz-empty" style="padding:6px">No discs on this rod. Add them from <b>All pieces</b> with + or +1.</div>'
        : [...stack]
            .reverse()
            .map((disc, i) => {
              const at = shapes.findIndex((s) => s.id === disc.shape);
              const s = shapes[at];
              const c = chipColour(s, Math.max(0, at));
              const text =
                s === undefined ? '?' : s.name === '' ? s.label : `${s.label} - ${s.name}`;
              return `<div class="stackrow ${i === row ? 'sel' : ''}" data-row="${String(i)}" role="option" aria-selected="${String(i === row)}"><span class="chip" style="background:${hex(c)};color:${contrast(c)}">${esc(s?.label ?? '?')}</span><span class="lbl">${esc(text)}</span>${disc.instance > 0 ? `<span class="mute">#${String(disc.instance + 1)}</span>` : ''}</div>`;
            })
            .join('');
    const v = stackValidity(board?.error ?? (d === null ? 'First create a problem.' : null));
    const ticks = Array.from({ length: places.count }, () => '<i></i>').join('');
    setHtml(
      el,
      `<div class="seg stackmode" role="radiogroup"><button data-mode="start" class="${ws.goal ? '' : 'on'}" title="Order the discs that start on the selected rod, bottom to top">Start</button><button data-mode="goal" class="${ws.goal ? 'on' : ''}" title="Order the discs the solver must reach on the selected rod">Goal</button></div>` +
        `<div class="rodbar" title="Select which rod in the chosen rod set to stack discs on"><span class="cap" id="pzRodCap">${rodCaption(places, place)}</span><div class="rodtrack"><input type="range" id="pzRod" min="0" max="${String(places.count - 1)}" step="1" value="${String(place)}" aria-label="Rod" ${hasBoard ? '' : 'disabled'}><div class="ticks">${ticks}</div></div></div>` +
        `<div class="pzh">Stack Order</div>` +
        `<div class="stackbtns"><button class="btn sm" data-s="up" title="Move the selected disc up the rod" ${on(row > 0 && row < depth)}>↑</button><button class="btn sm" data-s="down" title="Move the selected disc down the rod" ${on(row + 1 < depth)}>↓</button><button class="btn sm" data-s="add" title="Add another one of the selected shape" ${on(ws.sel !== null, false)}>+1</button><button class="btn sm" data-s="remove" title="Remove one of the selected shapes" ${on(depth > 0)}>−1</button><button class="btn sm" data-s="all" title="Add one of all shapes except result" ${on(shapes.length > 0, false)}>All +1</button><button class="btn sm" data-s="clear" title="Remove all pieces" ${on(true)}>Clear All</button></div>` +
        `<div class="stackvalid ${v.valid ? '' : 'bad'}" id="pzStackValid" role="status" title="Whether the start and goal stackings obey the rod set's rules" data-valid="${v.valid ? '1' : '0'}">${esc(v.text)}</div>` +
        `<div class="stacklist" role="listbox" aria-label="Discs on the selected rod, top of the list is the top of the stack">${list}</div>`,
    );
    el.dataset.rod = String(ws.rod);
    el.dataset.goal = ws.goal ? '1' : '0';
    // the stacking the board shown was read for (the e2e tests wait on it)
    el.dataset.boardGoal = ws.boardGoal ? '1' : '0';
    el.dataset.depth = String(depth);
    el.dataset.row = String(row);
  }

  // ---- the actions ----

  private async run<T extends { revision: number }>(
    doing: string,
    call: () => Promise<T>,
  ): Promise<T | null> {
    const ws = this.ws;
    if (ws.current === null || ws.refuse()) {
      return null;
    }
    try {
      return await ws.mutateQuiet(call);
    } catch (e) {
      // the original put a refusal's reason in the status line
      if (e instanceof EngineError && e.errc === 'invalid_argument') {
        flash(e.detail, 5000);
      } else {
        reportError(e, doing);
      }
      return null;
    }
  }

  /** +1: the piece chosen on top of the rod (placeOneDisk). */
  async add(shape: string | null = this.ws.sel): Promise<void> {
    const ws = this.ws;
    const problem = ws.current;
    if (problem === null) {
      return;
    }
    if (shape === null) {
      flash('Choose a disc in All pieces first', 4000);
      return;
    }
    const r = await this.run('Add disc', () =>
      ws.store.engine.call('stacking.placeDisc', {
        problem,
        shape,
        rod: ws.rod,
        goal: ws.goal,
      }),
    );
    if (r !== null) {
      ws.stackRow = 0;
      ws.render();
    }
  }

  /** −1: the disc selected in the list taken off (from the start and the goal). */
  async remove(): Promise<void> {
    const ws = this.ws;
    const problem = ws.current;
    const depth = this.stack.length;
    if (problem === null) {
      return;
    }
    const index = depth === 0 ? 0 : stackIndex(depth, Math.min(ws.stackRow, depth - 1));
    await this.run('Remove disc', () =>
      ws.store.engine.call('stacking.removeDisc', {
        problem,
        goal: ws.goal,
        rod: ws.rod,
        index,
      }),
    );
  }

  private async all(): Promise<void> {
    const ws = this.ws;
    const problem = ws.current;
    if (problem === null) {
      return;
    }
    const r = await this.run('Add all discs', () =>
      ws.store.engine.call('stacking.placeAll', { problem, rod: ws.rod, goal: ws.goal }),
    );
    if (r !== null) {
      // why the next disc did not go, in the status line
      if (r.error !== null) {
        flash(r.error, 5000);
      }
      ws.stackRow = 0;
      ws.render();
    }
  }

  private async clear(): Promise<void> {
    const ws = this.ws;
    const problem = ws.current;
    if (problem === null) {
      return;
    }
    await this.run('Clear All', () =>
      ws.store.engine.call('stacking.clear', { problem, goal: ws.goal }),
    );
  }

  // ↑ and ↓ (cb_ProbShapeExchange): the selection follows the disc
  private async move(up: boolean): Promise<void> {
    const ws = this.ws;
    const problem = ws.current;
    const depth = this.stack.length;
    const row = ws.stackRow;
    if (problem === null || row >= depth) {
      return;
    }
    const r = await this.run('Move disc', () =>
      ws.store.engine.call('stacking.moveDisc', {
        problem,
        goal: ws.goal,
        rod: ws.rod,
        index: stackIndex(depth, row),
        delta: up ? 1 : -1,
      }),
    );
    if (r !== null) {
      ws.stackRow = stackIndex(depth, r.index);
      ws.render();
    }
  }

  private async setRods(): Promise<void> {
    const ws = this.ws;
    const problem = ws.current;
    const rodSet = ws.boardSel;
    if (problem === null || rodSet === null) {
      return;
    }
    await this.run('Set Start/Goal Rods', () =>
      ws.store.engine.call('stacking.setProblemRodSet', { problem, rodSet }),
    );
  }

  private install(): void {
    const ws = this.ws;
    byId('pzBoard').addEventListener('click', (e) => {
      const t = e.target as Element;
      if (t.closest('#pzSetRods:not(:disabled)') !== null) {
        void this.setRods();
        return;
      }
      const row = t.closest<HTMLElement>('[data-board]');
      if (row?.dataset.board !== undefined) {
        // another rod set than the board shows alone in the view (cb_ProblemRod)
        ws.boardSel = row.dataset.board;
        ws.render();
      }
    });
    const el = byId('pzStacks');
    el.addEventListener('click', (e) => {
      const t = e.target as Element;
      const mode = t.closest<HTMLElement>('[data-mode]');
      if (mode !== null) {
        const goal = mode.dataset.mode === 'goal';
        if (goal !== ws.goal) {
          ws.setGoal(goal);
        }
        return;
      }
      const b = t.closest<HTMLButtonElement>('button[data-s]');
      if (b !== null) {
        if (b.disabled) {
          return;
        }
        switch (b.dataset.s) {
          case 'up':
            void this.move(true);
            break;
          case 'down':
            void this.move(false);
            break;
          case 'add':
            void this.add();
            break;
          case 'remove':
            void this.remove();
            break;
          case 'all':
            void this.all();
            break;
          case 'clear':
            void this.clear();
            break;
        }
        return;
      }
      const row = t.closest<HTMLElement>('[data-row]');
      if (row?.dataset.row !== undefined) {
        ws.stackRow = Number(row.dataset.row);
        this.renderStacks();
      }
    });
    // the slider's caption follows a drag; the rod is taken when it is let go
    el.addEventListener('input', (e) => {
      const t = e.target as HTMLInputElement;
      if (t.id === 'pzRod') {
        byId('pzRodCap').textContent = rodCaption(this.places, Number(t.value));
      }
    });
    el.addEventListener('change', (e) => {
      const t = e.target as HTMLInputElement;
      if (t.id === 'pzRod') {
        ws.setRodPlace(Number(t.value));
      }
    });
  }
}
