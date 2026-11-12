// SPDX-License-Identifier: GPL-3.0-or-later
//
// A sliding problem's starts and goals in the Puzzle workspace
// (docs/webui-spec.md, section 8.2), in the selected piece's place: the
// right card holds the board twice, Start above and Goal below, each a
// grid of the board's layer (the board's tiles, the copies placed in their
// pieces' colours, the chosen piece outlined under the mouse: green where
// it fits, red where it does not, orange on a copy a click takes off), its
// pieces with the copies placed of each ("S2 1/3"), a hint, and the
// layers of a board of several; its Focus 2D editor button puts Start and
// Goal side by side, their grids taking the room the left card and the
// view give up. The piece placed is the one selected in
// "In this puzzle"; the problem's count of it is how many copies each grid
// may hold. A click places a copy (`sliding.place`), a right click takes
// off the copy under the mouse (`sliding.clearAt`); the reason of a
// refusal goes to the status bar. Under the view, the validity bar. From
// the original's tooltabs.cpp (the Start/Goal tab), grideditor.cpp
// (`drawStampPreview`) and mainwindow_rods.cpp (`refreshSlideValid`), which
// the original had on the Entities tab.

import type { Methods } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import { askConfirm } from '../shell/confirm.ts';
import { byId, esc, setHtml } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { applyLayout, layout } from '../shell/layout.ts';
import { isTray } from '../shell/piece-colours.ts';
import { openPop } from '../shell/popover.ts';
import { flash, reportError, setCursor } from '../shell/status.ts';
import { EditorState, key, planeMap } from '../entities/editor-state.ts';
import type { Cell } from '../entities/editor-state.ts';
import { Grid2D } from '../entities/grid2d.ts';
import { squareLayer } from '../entities/layer-geometry.ts';
import type { CellValue } from '../entities/shape-cells.ts';
import {
  copiesOf,
  marksOf,
  namesOf,
  outlineColour,
  TrayState,
  validity,
} from '../entities/tray.ts';
import type { TrayModel } from '../view3d/trays.ts';
import { partOf } from './counts.ts';
import type { PuzzleWorkspace } from './workspace.ts';

type ShapeReply = Methods['shape.get']['result'];

/** One of the two grids: Start (goal false) or Goal. */
interface Pane {
  goal: boolean;
  grid: Grid2D;
  state: EditorState;
  layer: number;
  /** The cell under the mouse (null: none), for the outline. */
  at: Cell | null;
  loads: number;
}

export class SlideCard {
  private readonly ws: PuzzleWorkspace;
  private readonly trays: TrayState;
  private readonly panes: [Pane, Pane];
  /** The board's cells as last read, and what they were read for. */
  private cells: Map<string, CellValue> | null = null;
  private cellsOf = '';
  private cellsWanted = '';
  private cellLoads = 0;

  constructor(ws: PuzzleWorkspace) {
    this.ws = ws;
    this.trays = new TrayState(
      ws.store.engine,
      () => {
        this.draw();
        this.ws.trayRead();
      },
      (e) => {
        reportError(e, 'The board could not be read');
      },
    );
    const pane = (goal: boolean): Pane => {
      const state = new EditorState();
      const p: Pane = {
        goal,
        grid: null as unknown as Grid2D,
        state,
        layer: 0,
        at: null,
        loads: 0,
      };
      p.grid = new Grid2D(
        byId(goal ? 'sgGoalGrid' : 'sgStartGrid'),
        {
          state,
          frozen: () => this.ws.refuse(),
          drawingColour: () => null,
          strokeStart: () => undefined,
          strokeAdd: () => undefined,
          strokeEnd: () => undefined,
          strokeHas: () => false,
          expand: (c) => [c],
          hover: () => undefined,
          boxChanged: () => undefined,
          stamping: () => true,
          stampClick: (cell, right) => void this.click(p, cell, right),
          stampHover: (cell) => {
            p.at = cell;
            this.hover(p);
          },
          focused: () => layout.focus === '2d',
        },
        24,
      );
      return p;
    };
    this.panes = [pane(false), pane(true)];
    this.install();
  }

  /** Whether the problem shown is a sliding one on a board. */
  get board(): string | null {
    const snap = this.ws.store.snapshot;
    const r = this.ws.detail?.result ?? null;
    if (snap?.grid !== 'sliding' || r === null) {
      return null;
    }
    return isTray(snap.shapes.find((s) => s.id === r)) ? r : null;
  }

  /** What the board holds as the engine last said (null: no board, or not read yet). */
  get reply(): Methods['sliding.tray']['result'] | null {
    return this.board === null ? null : this.trays.reply;
  }

  /** The two trays the view draws (null: no board, or not read yet). */
  trayModel(): TrayModel | null {
    const r = this.reply;
    const b = this.board;
    return r === null || b === null
      ? null
      : { tray: b, starts: copiesOf(r, false), goals: copiesOf(r, true) };
  }

  /** The piece a click places: the one selected, while it is one of the problem's pieces. */
  get piece(): string | null {
    const s = this.ws.sel;
    return s !== null && s !== this.board && partOf(this.ws.detail, s) !== undefined ? s : null;
  }

  render(): void {
    const store = this.ws.store;
    const sliding = store.snapshot?.grid === 'sliding';
    const stage = byId('stage');
    const was = stage.classList.contains('sld');
    stage.classList.toggle('sld', sliding);
    // the 2D focus shows on the Puzzle tab only for a sliding problem
    if (was !== sliding && layout.focus === '2d') {
      applyLayout();
    }
    const board = this.board;
    this.trays.show(board, this.ws.current, store.generation, this.ws.shown);
    this.loadCells(board);
    this.draw();
  }

  // the board's cells, read again at each revision while shown
  private loadCells(board: string | null): void {
    const store = this.ws.store;
    const want =
      board === null ? '' : `${String(store.generation)}:${board}:${String(this.ws.shown)}`;
    if (want === this.cellsWanted) {
      return;
    }
    this.cellsWanted = want;
    const n = ++this.cellLoads;
    if (board === null) {
      this.cells = null;
      this.cellsOf = '';
      return;
    }
    store.engine.call('shape.get', { shape: board }, { coalesce: 'sg:cells' }).then(
      (r: ShapeReply) => {
        if (n === this.cellLoads) {
          const m = new Map<string, CellValue>();
          for (const c of r.cells) {
            const at: Cell = [c.at[0], c.at[1], c.at[2]];
            m.set(key(at), { at, state: c.state, colour: null });
          }
          this.cells = m;
          this.cellsOf = `${String(store.generation)}:${board}`;
          this.draw();
        }
      },
      (e: unknown) => {
        if (n === this.cellLoads && !(e instanceof EngineError && e.errc === 'superseded')) {
          this.cellsWanted = '';
          reportError(e, 'The board could not be read');
        }
      },
    );
  }

  // the two panes and the validity bar
  private draw(): void {
    const store = this.ws.store;
    const snap = store.snapshot;
    const board = this.board;
    const shape = snap?.shapes.find((s) => s.id === board);
    const r = this.reply;
    this.renderValidity(r?.error);
    // Start and Goal are small to work in on a board of 20 or more cells either way: the focus makes them larger
    byId('pzGridTip').hidden = shape === undefined || (shape.size[0] < 20 && shape.size[1] < 20);
    for (const p of this.panes) {
      const head = byId(p.goal ? 'sgGoalHead' : 'sgStartHead');
      const parts = byId(p.goal ? 'sgGoalParts' : 'sgStartParts');
      const hint = byId(p.goal ? 'sgGoalHint' : 'sgStartHint');
      const layers = byId(p.goal ? 'sgGoalLayers' : 'sgStartLayers');
      if (snap === null || board === null || shape === undefined) {
        setHtml(head, p.goal ? 'Goal' : 'Start');
        setHtml(parts, '');
        setHtml(layers, '');
        hint.textContent =
          snap?.grid === 'sliding' ? 'Add a board with + New Board on the Shapes workspace.' : '';
        p.grid.show(null);
        continue;
      }
      const depth = shape.size[2];
      p.layer = Math.max(0, Math.min(p.layer, depth - 1));
      setHtml(
        head,
        `${p.goal ? 'Goal' : 'Start'}<span class="sp"></span><span class="mute" style="font-size:calc(11px + var(--fs));font-weight:400">${esc(shape.label)}${shape.name !== '' ? ` – ${esc(shape.name)}` : ''}</span>`,
      );
      setHtml(parts, this.partsHtml(p.goal));
      hint.textContent = this.hintText(p.goal);
      setHtml(
        layers,
        `<div class="lh" style="color:var(--az)">Z layer</div>` +
          Array.from({ length: depth }, (_, l) => depth - 1 - l)
            .map(
              (l) =>
                `<button class="lchip ${l === p.layer ? 'on' : ''}" data-l="${String(l)}" title="Layer ${String(l + 1)}">${String(l + 1)}</button>`,
            )
            .join(''),
      );
      const ready = this.cells !== null && this.cellsOf === `${String(store.generation)}:${board}`;
      if (!ready) {
        continue;
      }
      p.grid.show({
        grid: 'sliding',
        geo: squareLayer('XY', shape.size, p.layer),
        plane: planeMap('XY', shape.size),
        layer: p.layer,
        cells: this.cells ?? new Map(),
        shapeColour: '#888888',
        palette: new Map(),
        tray: {
          marks: r === null ? new Map() : marksOf(r, p.goal, snap.shapes),
          names: r === null || !this.ws.labels.grids ? [] : namesOf(r, p.goal, snap.shapes),
        },
      });
    }
  }

  // each piece of the problem with its copies placed in this grid: "S2 1/3"
  private partsHtml(goal: boolean): string {
    const ws = this.ws;
    const d = ws.detail;
    const r = this.reply;
    if (d === null) {
      return '';
    }
    return d.parts
      .map((part) => {
        const s = ws.store.snapshot?.shapes.find((x) => x.id === part.shape);
        if (s === undefined) {
          return '';
        }
        const placed =
          r === null ? 0 : r.placed.filter((c) => c.goal === goal && c.piece === s.id).length;
        const c = ws.chip(s.id);
        const on = s.id === this.piece;
        const all = placed >= part.max && part.max > 0;
        return `<button class="sgpart ${on ? 'on' : ''} ${all ? 'all' : ''}" data-sp="${s.id}" aria-pressed="${String(on)}" title="${esc(s.label)}${s.name !== '' ? ` – ${esc(s.name)}` : ''}: ${String(placed)} of ${String(part.max)} ${part.max === 1 ? 'copy' : 'copies'} with a ${goal ? 'goal' : 'start'} — select it to place one"><span class="chip" style="background:${c.bg};color:${c.fg}">${esc(s.label)}</span><span class="n">${String(placed)}/${String(part.max)}</span></button>`;
      })
      .join('');
  }

  // what a click does, for the piece chosen
  private hintText(goal: boolean): string {
    const ws = this.ws;
    const where = goal ? 'goal' : 'start';
    if ((ws.detail?.parts.length ?? 0) === 0) {
      return 'Add pieces to “In this puzzle”, then select one to place it here.';
    }
    const piece = this.piece;
    if (piece === null) {
      return `Select a piece in “In this puzzle” (or above), then click the grid to place its ${where}.`;
    }
    const part = partOf(ws.detail, piece);
    const max = part?.max ?? 1;
    const label = ws.labelOf(piece);
    if (max <= 1) {
      return `Click to place the ${where} of ${label}, or to move it there. Right-click takes it off.`;
    }
    return `${label} has ${String(max)} copies: each click places one more ${where}. Click a copy or right-click it to take it off.`;
  }

  // the validity bar under the view (refreshSlideValid)
  private renderValidity(error: string | null | undefined): void {
    const el = byId('slideValid');
    if (error === undefined) {
      el.classList.remove('show');
      el.textContent = '';
      delete el.dataset.valid;
      return;
    }
    const v = validity(error);
    el.textContent = v.text;
    el.classList.add('show');
    el.classList.toggle('bad', !v.valid);
    el.dataset.valid = v.valid ? '1' : '0';
  }

  // the outline of the piece to place under the mouse (sliding.preview)
  private hover(p: Pane): void {
    const tray = this.board;
    const problem = this.ws.current;
    const piece = this.piece;
    const cell = p.at;
    const n = ++p.loads;
    if (cell === null || tray === null || problem === null || piece === null) {
      p.grid.setStamp(null);
      return;
    }
    this.ws.store.engine
      .call(
        'sliding.preview',
        { tray, problem, piece, at: cell, goal: p.goal },
        { coalesce: p.goal ? 'sg:preview:goal' : 'sg:preview:start' },
      )
      .then(
        (r) => {
          if (n === p.loads) {
            p.grid.setStamp({ cells: r.cells, colour: outlineColour(r) });
            if (r.why !== null) {
              setCursor(r.why);
            }
          }
        },
        (e: unknown) => {
          if (n === p.loads && !(e instanceof EngineError && e.errc === 'superseded')) {
            p.grid.setStamp(null);
            reportError(e, 'The piece could not be shown');
          }
        },
      );
  }

  // a click on a grid: the chosen piece placed (or the copy there taken
  // off), a right click takes off the copy under the mouse
  private async click(p: Pane, at: Cell, right: boolean): Promise<void> {
    const tray = this.board;
    const ws = this.ws;
    const problem = ws.current;
    if (tray === null || problem === null || ws.refuse()) {
      return;
    }
    const piece = this.piece;
    if (!right && piece === null) {
      flash('Select a piece in “In this puzzle” first', 4000);
      return;
    }
    const e = ws.store.engine;
    const goal = p.goal;
    try {
      await ws.mutateQuiet<{ revision: number }>(() =>
        right || piece === null
          ? e.call('sliding.clearAt', { tray, at, goal, problem })
          : e.call('sliding.place', { tray, problem, piece, at, goal }),
      );
    } catch (err) {
      // the original put the reason in the status line
      if (err instanceof EngineError && err.errc === 'invalid_argument') {
        flash(err.detail, 5000);
      } else {
        reportError(err, goal ? 'The goal could not be placed' : 'The start could not be placed');
      }
    } finally {
      this.hover(p);
    }
  }

  // More options: Start cloned to Goal or Goal to Start, asked first when the target has pieces;
  // either board cleared, asked first (off while it has none)
  private more(anchor: Element): void {
    const none = this.board === null ? 'disabled' : '';
    const off = (goal: boolean): string =>
      this.reply?.placed.some((c) => c.goal === goal) === true ? '' : 'disabled';
    openPop(
      anchor,
      `<button data-k="goal" ${none} title="Copy the start positions to the goal, replacing what it has">Clone start position to goal position</button><button data-k="start" ${none} title="Copy the goal positions to the start, replacing what it has">Clone goal position to start position</button><hr><button data-k="clearStart" ${off(false)} title="Take every piece off the start board">Clear Start position board</button><button data-k="clearGoal" ${off(true)} title="Take every piece off the goal board">Clear Goal position board</button>`,
      (k) => {
        const r = this.reply;
        if (r === null || this.ws.refuse()) {
          return;
        }
        if (k === 'clearStart' || k === 'clearGoal') {
          const goal = k === 'clearGoal';
          const side = goal ? 'goal' : 'start';
          askConfirm(
            goal ? 'Clear the goal board?' : 'Clear the start board?',
            `Every piece will be taken off the ${side} board.`,
            'Clear',
            () => void this.clear(goal),
          );
          return;
        }
        const toGoal = k === 'goal';
        if (!r.placed.some((c) => c.goal === toGoal)) {
          void this.clone(toGoal);
          return;
        }
        const target = toGoal ? 'goal' : 'start';
        askConfirm(
          toGoal ? 'Clone start to goal?' : 'Clone goal to start?',
          `Pieces were found on the ${target} board. The ${target} board will be cleared and replaced with a copy of the ${toGoal ? 'start' : 'goal'}.`,
          'Clear and clone',
          () => void this.clone(toGoal),
        );
      },
    );
  }

  // one board cleared (sliding.clear): one undo step
  private async clear(goal: boolean): Promise<void> {
    const tray = this.board;
    const ws = this.ws;
    const problem = ws.current;
    if (tray === null || problem === null || ws.refuse()) {
      return;
    }
    try {
      await ws.mutateQuiet(() => ws.store.engine.call('sliding.clear', { tray, goal, problem }));
    } catch (err) {
      reportError(err, goal ? 'The goal could not be cleared' : 'The start could not be cleared');
    }
  }

  // the clone itself (sliding.clone): one undo step; the reason of a refusal goes to the status bar
  private async clone(toGoal: boolean): Promise<void> {
    const tray = this.board;
    const ws = this.ws;
    const problem = ws.current;
    if (tray === null || problem === null || ws.refuse()) {
      return;
    }
    try {
      await ws.mutateQuiet(() => ws.store.engine.call('sliding.clone', { tray, toGoal, problem }));
    } catch (err) {
      if (err instanceof EngineError && err.errc === 'invalid_argument') {
        flash(err.detail, 5000);
      } else {
        reportError(err, toGoal ? 'The start could not be cloned' : 'The goal could not be cloned');
      }
    }
  }

  private install(): void {
    byId('pzMore').addEventListener('click', (e) => {
      this.more(e.currentTarget as Element);
    });
    byId('pzMore').innerHTML = ic('menu');
    // the focus: Start and Goal side by side, the card taking the room the left card and the view give up
    byId('pzF2D').addEventListener('click', () => {
      layout.focus = layout.focus === '2d' ? '' : '2d';
      applyLayout();
    });
    byId('pzF2D').innerHTML = ic('focus');
    for (const p of this.panes) {
      byId(p.goal ? 'sgGoalLayers' : 'sgStartLayers').addEventListener('click', (e) => {
        const b = (e.target as Element).closest<HTMLElement>('.lchip');
        if (b !== null) {
          p.layer = Number(b.dataset.l);
          this.draw();
          this.hover(p);
        }
      });
      byId(p.goal ? 'sgGoalParts' : 'sgStartParts').addEventListener('click', (e) => {
        const b = (e.target as Element).closest<HTMLElement>('[data-sp]');
        if (b?.dataset.sp !== undefined) {
          this.ws.select(b.dataset.sp);
        }
      });
    }
  }
}
