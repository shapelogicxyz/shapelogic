// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces of the Puzzle workspace's left card: the result (its cells
// and "can hold n–m" from problem.fit, Change…), the pieces in the puzzle
// as chips (the problem's parts in their order; −/+ are the original's
// one fewer / one more, minimum and maximum both) with All +1 and Clear All,
// the Groups section (a table of count or range, group and weight (−/+;
// shown on a tile as "Weight: n" when not 1), and "Set range
// for all pieces"; collapsed at first), "All pieces" (every shape but the result: drag a chip in,
// double-click it or press its +; drag a tile out to take it out; a tile
// dropped on another moves it there), the right-click menus (one more, one
// fewer, remove, set as result; move earlier and later for a tile), the
// fit summary (FitStats). From docs/mockup/index.html:
// `renderPuzzle`, `pzUsedTile`, `pzLibChip`, `pzMenu`, `pzBulkRange`,
// `pzDragStart`, `pzDragEnd`, and the handlers of #pzResult, #pzUsed, #pzLib,
// #pzLibHead, #pzAll and #pzClear; the Groups section (the mockup's table
// view, now a collapsed section of its own) holds the table and "Set range
// for all pieces".

import type { Part, ShapeSummary } from '../../engine/api.generated.ts';
import { $$, byId, esc, markSelected, setHtml } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { ic } from '../shell/icons.ts';
import { openPop, openPopAt } from '../shell/popover.ts';
import { isTray, shownName } from '../shell/piece-colours.ts';
import { flash } from '../shell/status.ts';
import { countText, groupOf, groupsOf, partOf, rng } from './counts.ts';
import type { PuzzleWorkspace } from './workspace.ts';

const cells = (s: ShapeSummary): number => s.fixed + s.variable;

/** What a shape's weight does (the Groups table's tips). */
const WEIGHT = 'in the Solver, heavier shapes stay in place and lighter ones move';

/** Why a sliding piece stays in the puzzle (Part.required). */
const REQUIRED =
  'Its starts or goals on the board need this piece: take them off in Start and goal first';

/** Why a sliding piece has no fewer copies (Part.placed). */
function placedTip(p: Part): string {
  return `${String(p.placed)} ${p.placed === 1 ? 'copy is' : 'copies are'} placed at a start or goal on the board: take one off in Start and goal first`;
}

export class PiecesCard {
  private readonly ws: PuzzleWorkspace;
  private drag: { u: string; from: 'lib' | 'used' } | null = null;
  private bulk: HTMLElement | null = null;

  constructor(ws: PuzzleWorkspace) {
    this.ws = ws;
    byId('pzGrpTog').innerHTML = ic('chevD', 16);
    byId('pzLibTog').innerHTML = ic('chevD', 16);
    this.install();
  }

  private get shapes(): ShapeSummary[] {
    return this.ws.store.snapshot?.shapes ?? [];
  }

  private title(s: ShapeSummary): string {
    const name = shownName(s);
    return `${s.label}${name !== '' ? ` – ${name}` : ''} · ${String(cells(s))} voxels`;
  }

  /** Draws the result, the pieces, the library, the summary (the mockup's renderPuzzle). */
  render(): void {
    const ws = this.ws;
    const d = ws.detail;
    const shapes = this.shapes;
    const result = d?.result ?? null;
    const parts = d?.parts ?? [];
    // a sliding puzzle's boards are no pieces: only the problems show them
    const others = shapes.filter((s) => s.id !== result && !isTray(s));
    const byShape = new Map(shapes.map((s) => [s.id, s]));

    // a stacking problem's pieces are the discs on its rods (stacks-card.ts):
    // only the library shows here, its + putting a disc on the rod chosen
    const stacking = ws.store.snapshot?.grid === 'stacking';
    byId('pzLibHint').textContent = stacking
      ? 'double-click or + to put on the rod'
      : 'drag, double-click or + to add';

    // the result: a sliding puzzle's is its start/goal shape, for good
    const fixedResult = ws.store.snapshot?.grid === 'sliding';
    const change = fixedResult
      ? ''
      : '<button class="btn sm" id="pzChange" title="Choose another shape as this problem’s result">Change…</button>';
    const R = result === null ? undefined : byShape.get(result);
    const f = ws.fit;
    if (d === null) {
      byId('pzResult').innerHTML = '';
    } else if (R === undefined) {
      byId('pzResult').innerHTML =
        `<span class="chip" style="background:var(--muted)">–</span><div class="meta"><b>Result piece <span class="req">(required)</span></b><span>Choose the shape the pieces fill</span></div>${change}`;
    } else {
      const c = ws.chip(R.id);
      const hold = f === null ? '' : ` · can hold ${rng(f.resultMin, f.resultMax)}`;
      byId('pzResult').innerHTML =
        `<span class="chip" style="background:${c.bg};color:${c.fg}">${esc(R.label)}</span><div class="meta"><b>${esc(shownName(R)) || 'Result piece'}</b><span>${String(cells(R))} voxels${hold}</span></div>${change}`;
    }
    byId('pzResult').dataset.shape = result ?? '';

    // the pieces in the puzzle
    byId('pzPieceCount').textContent = String(parts.length);
    byId('pzLibCount').textContent = String(others.length);
    const ub = byId('pzUsed');
    if (parts.length === 0) {
      setHtml(
        ub,
        '<div class="pz-empty" style="padding:6px">No pieces yet. Add them from <b>All pieces</b> above — drag, double-click, or press +.</div>',
      );
    } else {
      setHtml(
        ub,
        parts
          .map((p) => {
            const s = byShape.get(p.shape);
            return s === undefined ? '' : this.usedTile(s, p);
          })
          .join(''),
      );
    }
    // the Groups section: every piece's count, range and group
    byId('pzGrpBody').classList.toggle('closed', !ws.groupsOpen);
    byId('pzGrpTog').style.transform = ws.groupsOpen ? '' : 'rotate(-90deg)';
    const gt = byId('pzGrpTable');
    if (parts.length === 0) {
      setHtml(gt, '');
    } else {
      const groups = groupsOf(d);
      // a stacking puzzle's discs have no weight
      const weighs = !stacking;
      setHtml(
        gt,
        `<table class="pzt"><thead><tr><th class="l">Shape</th><th>Count</th><th title="Allow a min–max range">Range</th><th>Group</th>${weighs ? `<th title="${WEIGHT}">Weight</th>` : ''}</tr></thead><tbody>${parts
          .map((p) => {
            const s = byShape.get(p.shape);
            if (s === undefined) {
              return '';
            }
            const rg = ws.isRange(s.id);
            const g = groupOf(p);
            const c = ws.chip(s.id);
            // a piece starts or goals need keeps a copy for each (the engine refuses fewer)
            const off = p.required ? `disabled title="${REQUIRED}"` : '';
            const least = p.placed > 0 ? ` min="${String(p.placed)}"` : ' min="0"';
            const count = rg
              ? `<input type="number"${least} data-f="min" value="${String(p.min)}" aria-label="Minimum" title="The fewest copies of this piece a solution may use"><span class="dash">–</span><input type="number"${least} data-f="max" value="${String(p.max)}" aria-label="Maximum" title="The most copies of this piece a solution may use">`
              : `<input type="number"${least} data-f="max" value="${String(p.max)}" aria-label="Copies" title="How many copies of this piece the puzzle uses${p.placed > 0 ? ` (at least ${String(p.placed)}: ${placedTip(p)})` : ''}">`;
            // a sliding puzzle has no groups
            const grp = fixedResult
              ? '–'
              : `<select data-grp aria-label="Group" title="Pieces of the same group may stay together: the disassembly is complete when only groups are left"><option value="">–</option>${groups.map((x) => `<option value="${String(x)}" ${g === x ? 'selected' : ''}>G${String(x)}</option>`).join('')}<option value="new">+ New</option></select>`;
            const wt = weighs
              ? `<td class="wt"><button data-w="-1" title="Weight −1 — ${WEIGHT}" ${s.weight <= 0 ? 'disabled' : ''}>W−</button><span class="v">${String(s.weight)}</span><button data-w="1" title="Weight +1 — ${WEIGHT}">W+</button></td>`
              : '';
            return `<tr data-u="${s.id}"><td class="sh" style="background:${c.bg};color:${c.fg}" title="${esc(this.title(s))}">${esc(s.label)}${s.name !== '' ? ` – ${esc(s.name)}` : ''}</td><td class="cn">${count}</td><td><input type="checkbox" data-range ${rg ? 'checked' : ''} aria-label="Range" title="Use a range of copies, a minimum and a maximum, instead of a fixed count" ${off}></td><td>${grp}</td>${wt}</tr>`;
          })
          .join('')}</tbody></table>`,
      );
    }
    markSelected(ub, '[data-u]', 'u', ws.sel);
    markSelected(gt, '[data-u]', 'u', ws.sel);

    // all pieces
    setHtml(byId('pzLib'), others.map((s) => this.libChip(s, partOf(d, s.id), stacking)).join(''));
    markSelected(byId('pzLib'), '.lc', 'u', ws.sel);
    byId('pzLib').classList.toggle('closed', !ws.libOpen);
    byId('pzLibTog').style.transform = ws.libOpen ? '' : 'rotate(-90deg)';

    this.renderSummary();
  }

  // the mockup's pzUsedTile
  private usedTile(s: ShapeSummary, p: Part): string {
    const g = groupOf(p);
    const c = this.ws.chip(s.id);
    // a piece starts or goals need keeps a copy for each (the engine refuses fewer)
    const fewer =
      p.placed > 0 && p.min <= p.placed
        ? `disabled title="${placedTip(p)}"`
        : 'title="One fewer (at 0 the piece leaves the puzzle)"';
    return `<div class="ut${s.weight !== 1 ? ' wt' : ''}" data-u="${s.id}" draggable="${String(!p.required)}" title="${esc(this.title(s))}${p.required ? ` — ${REQUIRED}` : ' — drag onto “All pieces” to remove'}"><i class="uc" style="background:${c.bg}"></i><div class="ub"><div class="t1"><span class="gripd">${ic('grip', 12)}</span><b>${esc(s.label)}</b>${s.name !== '' ? `<span class="pl">${esc(s.name)}</span>` : ''}${g > 0 ? `<span class="gb">G${String(g)}</span>` : ''}</div><div class="t2"><button data-d="-1" ${fewer}>−</button><span class="cnt">${countText(p)}</span><button data-d="1" title="One more">+</button></div>${s.weight !== 1 ? `<div class="t3">Weight: ${String(s.weight)}</div>` : ''}</div></div>`;
  }

  // the mockup's pzLibChip
  private libChip(s: ShapeSummary, p: Part | undefined, stacking = false): string {
    const n = p?.max ?? 0;
    const c = this.ws.chip(s.id);
    const req = p?.required ?? false;
    if (stacking) {
      // a disc: + or a double-click puts it on top of the rod chosen (the Problems tab's +1)
      return `<div class="lc ${n > 0 ? 'in' : ''}" data-u="${s.id}" draggable="false" title="${esc(s.label)}${s.name !== '' ? ` – ${esc(s.name)}` : ''} · size ${String(s.disc ?? 1)} — double-click or press + to put it on the rod chosen"><i class="sq" style="background:${c.bg}"></i><b>${esc(s.label)}</b>${s.name !== '' ? `<span class="pl">${esc(s.name)}</span>` : ''}<span class="mute">size ${String(s.disc ?? 1)}</span><button class="la" data-add="1" title="Put one on top of the rod chosen">${n > 0 ? `×${String(n)} +` : '+'}</button></div>`;
    }
    return `<div class="lc ${n > 0 ? 'in' : ''}" data-u="${s.id}" draggable="${String(!req)}" title="${esc(this.title(s))}${req ? ` — ${REQUIRED}` : ' — drag into “In this puzzle”, double-click, or press + to add one'}"><span class="gripd">${ic('grip', 12)}</span><i class="sq" style="background:${c.bg}"></i><b>${esc(s.label)}</b>${s.name !== '' ? `<span class="pl">${esc(s.name)}</span>` : ''}<button class="la" data-add="1" title="Add one to the puzzle">${n > 0 ? `×${String(n)} +` : '+'}</button></div>`;
  }

  // the fit summary: the engine's verdict and counts (FitStats)
  private renderSummary(): void {
    const f = this.ws.fit;
    const el = byId('pzSummary');
    el.dataset.fit = f === null ? '' : JSON.stringify(f);
    el.dataset.verdict = f?.verdict ?? '';
    if (f === null) {
      el.className = 'pz-sum';
      el.innerHTML = '';
      return;
    }
    const ok = f.verdict === 'fits';
    el.className = `pz-sum ${ok ? 'ok' : 'warn'}`;
    if (f.verdict === 'noResult') {
      el.innerHTML = `${ic('warn', 18)}<div><b>No result shape yet</b><span>Choose the shape the pieces fill with Change…</span></div>`;
      return;
    }
    const head =
      f.verdict === 'fits'
        ? 'Pieces can fill the result'
        : f.verdict === 'tooLarge'
          ? 'Pieces are too large for the result'
          : 'Pieces cannot reach the result';
    const holes = `Holes: ${rng(f.holesMin, f.holesMax)}${f.maxHoles === null ? '' : ` (at most ${String(f.maxHoles)})`}`;
    el.innerHTML = `${ic(ok ? 'okc' : 'warn', 18)}<div><b>${head}</b><span>Result ${rng(f.resultMin, f.resultMax)} voxels · Pieces n = ${rng(f.piecesMin, f.piecesMax)} → ${rng(f.cellsMin, f.cellsMax)} voxels · ${holes}</span></div>`;
  }

  // ---- menus ----

  // the mockup's pzMenu: one more, one fewer, remove, set as result (and a tile's moves)
  private menu(e: MouseEvent, u: string): void {
    e.preventDefault();
    const ws = this.ws;
    ws.select(u);
    const parts = ws.detail?.parts ?? [];
    const i = parts.findIndex((p) => p.shape === u);
    const inPuzzle = i >= 0;
    const moves = inPuzzle
      ? `<hr><button data-k="earlier" ${i === 0 ? 'disabled' : ''} title="Move this piece one place earlier in the puzzle">Move earlier</button><button data-k="later" ${i === parts.length - 1 ? 'disabled' : ''} title="Move this piece one place later in the puzzle">Move later</button>`
      : '';
    openPopAt(
      e.clientX,
      e.clientY,
      `<div class="ph">Piece ${esc(ws.labelOf(u))}</div><button data-k="up" title="Use one more of this piece">One more (+1)<span class="kb">+</span></button><button data-k="dn" ${inPuzzle ? '' : 'disabled'} title="Use one fewer of this piece; at 0 it leaves the puzzle">One fewer (−1)<span class="kb">−</span></button><button data-k="off" ${inPuzzle ? '' : 'disabled'} title="Take this piece out of the puzzle">Remove from puzzle<span class="kb">Del</span></button>${moves}<hr><button data-k="res" title="Make this shape the problem’s result">Set as result</button>`,
      (k) => {
        switch (k) {
          case 'up':
            void ws.add(u);
            break;
          case 'dn':
            void ws.fewer(u);
            break;
          case 'off':
            void ws.remove(u);
            break;
          case 'earlier':
            void ws.movePart(u, -1);
            break;
          case 'later':
            void ws.movePart(u, 1);
            break;
          case 'res':
            void ws.setResult(u);
            break;
        }
      },
    );
  }

  // the mockup's pzBulkRange: one minimum and maximum for every piece
  private bulkRange(): void {
    const ws = this.ws;
    const n = ws.detail?.parts.length ?? 0;
    if (n === 0) {
      flash('No pieces in this puzzle');
      return;
    }
    if (ws.refuse()) {
      return;
    }
    let mn = 0;
    let mx = 1;
    const d = document.createElement('div');
    d.className = 'cfm-scrim';
    d.id = 'pzBulk';
    const step = (k: 'mn' | 'mx', label: string, v: number): string =>
      `<div class="step sz inl" style="flex:1"><button data-r="${k}-" title="Decrease">${ic('minus', 12)}</button><span class="il">${label}</span><input class="v" value="${String(v)}" data-r="${k}" aria-label="${label}"><button data-r="${k}+" title="Increase">${ic('plus', 12)}</button></div>`;
    const draw = (): void => {
      d.innerHTML = `<div class="cfm" role="dialog" aria-modal="true"><h3>Set range for all pieces</h3><p>Every piece in this puzzle gets the same minimum and maximum count (${String(n)} pieces).</p><div style="display:flex;gap:10px;margin-bottom:14px">${step('mn', 'Min', mn)}${step('mx', 'Max', mx)}</div><div class="bt"><button class="btn" data-x="0">Cancel</button><button class="btn pri" data-x="1">Apply</button></div></div>`;
    };
    const fix = (): void => {
      mn = Math.max(0, mn);
      mx = Math.max(1, mx);
      if (mn > mx) {
        mx = mn;
      }
    };
    draw();
    byId('stage').appendChild(d);
    this.bulk = d;
    const close = (): void => {
      d.remove();
      this.bulk = null;
    };
    d.addEventListener('click', (e) => {
      const t = e.target as Element;
      if (t === d) {
        close();
        return;
      }
      const b = t.closest<HTMLElement>('[data-r],[data-x]');
      if (b === null) {
        return;
      }
      if (b.dataset.x === '0') {
        close();
        return;
      }
      if (b.dataset.x === '1') {
        close();
        void ws.setRangeAll(mn, mx);
        return;
      }
      const r = b.dataset.r;
      if (r === 'mn-') {
        mn--;
      } else if (r === 'mn+') {
        mn++;
      } else if (r === 'mx-') {
        mx--;
      } else if (r === 'mx+') {
        mx++;
      } else {
        return;
      }
      fix();
      draw();
    });
    d.addEventListener('change', (e) => {
      const i = e.target as HTMLInputElement;
      if (i.dataset.r === 'mn') {
        mn = Math.round(Number(i.value)) || 0;
      } else if (i.dataset.r === 'mx') {
        mx = Math.round(Number(i.value)) || 1;
      }
      fix();
      draw();
    });
  }

  // ---- events ----

  private install(): void {
    const ws = this.ws;
    const used = byId('pzUsed');
    const lib = byId('pzLib');
    const libHead = byId('pzLibHead');

    byId('pzResult').addEventListener('click', (e) => {
      const b = (e.target as Element).closest('#pzChange');
      if (b === null || ws.refuse()) {
        return;
      }
      const result = ws.detail?.result ?? null;
      openPop(
        b,
        `<div class="ph">Use as result</div>${this.shapes
          .map((s) => {
            const c = ws.chip(s.id);
            return `<button data-k="${s.id}" ${s.id === result ? 'disabled' : ''} title="Make this shape the problem’s result"><span class="chip" style="background:${c.bg};color:${c.fg};min-width:30px;height:20px;font-size:10px">${esc(s.label)}</span>${esc(s.name) || '&nbsp;'}<span class="kb">${String(cells(s))} vox</span></button>`;
          })
          .join('')}`,
        (k) => void ws.setResult(k),
      );
    });
    const groups = byId('pzGrpTable');
    byId('pzGrpHead').addEventListener('click', () => {
      ws.groupsOpen = !ws.groupsOpen;
      ws.render();
    });
    byId('pzRangeAll').addEventListener('click', () => {
      this.bulkRange();
    });
    byId('pzAll').addEventListener('click', () => {
      void ws.addOneOfEach();
    });
    byId('pzClear').addEventListener('click', () => {
      void ws.removeAllPieces();
    });

    // the groups table's fields
    groups.addEventListener('change', (e) => {
      const i = e.target as HTMLInputElement | HTMLSelectElement;
      const u = i.closest<HTMLElement>('tr')?.dataset.u;
      if (u === undefined) {
        return;
      }
      ws.sel = u;
      if (i.dataset.f === 'min' || i.dataset.f === 'max') {
        void ws.setCount(u, i.dataset.f, Number(i.value));
      } else if (i instanceof HTMLInputElement && i.dataset.range !== undefined) {
        void ws.setRange(u, i.checked);
      } else if (i.dataset.grp !== undefined) {
        const v = i.value;
        void ws.setGroup(u, v === 'new' ? 'new' : v === '' ? 0 : Number(v));
      }
    });
    groups.addEventListener('click', (e) => {
      const t = e.target as Element;
      const u = t.closest<HTMLElement>('tr[data-u]')?.dataset.u;
      if (u === undefined) {
        return;
      }
      const w = t.closest<HTMLElement>('button[data-w]');
      if (w !== null) {
        void ws.stepWeight(u, Number(w.dataset.w));
      } else if (t.closest('input,select') === null) {
        ws.select(u);
      }
    });
    used.addEventListener('click', (e) => {
      const t = e.target as Element;
      const u = t.closest<HTMLElement>('.ut')?.dataset.u;
      if (u === undefined) {
        return;
      }
      const b = t.closest<HTMLElement>('.t2 button');
      if (b === null) {
        ws.select(u);
      } else if (b.dataset.d === '1') {
        void ws.add(u);
      } else {
        void ws.fewer(u);
      }
    });
    lib.addEventListener('click', (e) => {
      const t = e.target as Element;
      const u = t.closest<HTMLElement>('.lc')?.dataset.u;
      if (u === undefined) {
        return;
      }
      if (t.closest('[data-add]') !== null) {
        void ws.add(u);
      } else {
        ws.select(u);
      }
    });
    lib.addEventListener('dblclick', (e) => {
      const t = e.target as Element;
      const u = t.closest<HTMLElement>('.lc')?.dataset.u;
      if (u !== undefined && t.closest('[data-add]') === null) {
        void ws.add(u);
      }
    });
    for (const el of [used, groups]) {
      el.addEventListener('contextmenu', (e) => {
        const u = (e.target as Element).closest<HTMLElement>('.ut,tr[data-u]')?.dataset.u;
        if (u !== undefined) {
          this.menu(e, u);
        }
      });
    }
    lib.addEventListener('contextmenu', (e) => {
      if (ws.store.snapshot?.grid === 'stacking') {
        return;
      }
      const u = (e.target as Element).closest<HTMLElement>('.lc')?.dataset.u;
      if (u !== undefined) {
        this.menu(e, u);
      }
    });
    libHead.addEventListener('click', () => {
      ws.libOpen = !ws.libOpen;
      ws.render();
    });
    // drag and drop between "All pieces" and "In this puzzle" (the mockup's
    // pzDragStart, pzDragEnd and drop handlers), and within the puzzle
    const start = (e: DragEvent, u: string, from: 'lib' | 'used'): void => {
      this.drag = { u, from };
      if (e.dataTransfer !== null) {
        e.dataTransfer.setData('text/plain', u);
        e.dataTransfer.effectAllowed = 'move';
      }
      (from === 'lib' ? used : lib).classList.add('drop-ok');
    };
    const end = (): void => {
      this.drag = null;
      used.classList.remove('drop-ok');
      lib.classList.remove('drop-ok');
      for (const t of $$('.ut.mv', used)) {
        t.classList.remove('mv');
      }
    };
    lib.addEventListener('dragstart', (e) => {
      const u = (e.target as Element).closest<HTMLElement>('.lc')?.dataset.u;
      if (u !== undefined) {
        start(e, u, 'lib');
      }
    });
    used.addEventListener('dragstart', (e) => {
      const u = (e.target as Element).closest<HTMLElement>('.ut')?.dataset.u;
      if (u !== undefined) {
        start(e, u, 'used');
      }
    });
    lib.addEventListener('dragend', end);
    used.addEventListener('dragend', end);
    used.addEventListener('dragover', (e) => {
      const d = this.drag;
      if (d === null) {
        return;
      }
      const over = (e.target as Element).closest<HTMLElement>('.ut');
      if (d.from === 'lib' || (over !== null && over.dataset.u !== d.u)) {
        e.preventDefault();
      }
      if (d.from === 'used') {
        for (const t of $$('.ut', used)) {
          t.classList.toggle('mv', t === over && t.dataset.u !== d.u);
        }
      }
    });
    used.addEventListener('drop', (e) => {
      const d = this.drag;
      if (d === null) {
        return;
      }
      e.preventDefault();
      const over = (e.target as Element).closest<HTMLElement>('.ut')?.dataset.u;
      end();
      if (d.from === 'lib') {
        void ws.add(d.u);
      } else if (over !== undefined && over !== d.u) {
        const to = (ws.detail?.parts ?? []).findIndex((p) => p.shape === over);
        void ws.moveTo(d.u, to);
      }
    });
    for (const el of [lib, libHead]) {
      el.addEventListener('dragover', (e) => {
        if (this.drag?.from === 'used') {
          e.preventDefault();
        }
      });
      el.addEventListener('drop', (e) => {
        const d = this.drag;
        if (d?.from !== 'used') {
          return;
        }
        e.preventDefault();
        end();
        void ws.remove(d.u);
      });
    }
    addEscapeStep(ESC_ORDER.dialog - 1, () => {
      if (this.bulk === null) {
        return false;
      }
      this.bulk.remove();
      this.bulk = null;
      return true;
    });
  }
}
