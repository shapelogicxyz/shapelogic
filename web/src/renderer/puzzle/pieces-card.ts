// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces of the Puzzle workspace's left card: the result (its cells
// and "can hold n–m" from problem.fit, Change…), the pieces in the puzzle
// as chips or a table (the problem's parts in their order: count or range,
// group; −/+ are the original's one fewer / one more, minimum and maximum
// both), "All pieces" (every shape but the result: drag a chip in,
// double-click it or press its +; drag a tile out to take it out; a tile
// dropped on another moves it there), the More menu (add one of each, set
// range for all pieces…, remove all), the right-click menus (one more, one
// fewer, remove, set as result; move earlier and later for a tile), the
// fit summary (FitStats), and the notice for the puzzles this version
// does not solve (problem.kind). From docs/mockup/index.html:
// `renderPuzzle`, `pzUsedTile`, `pzLibChip`, `pzMenu`, `pzBulkRange`,
// `pzDragStart`, `pzDragEnd`, and the handlers of #pzResult, #pzViewSeg,
// #pzUsed, #pzLib, #pzLibHead and #pzMore.

import type { Part, ShapeSummary } from '../../engine/api.generated.ts';
import { $$, byId, esc, markSelected, setHtml } from '../shell/dom.ts';
import { addEscapeStep, ESC_ORDER } from '../shell/escape.ts';
import { ic } from '../shell/icons.ts';
import { openPop, openPopAt } from '../shell/popover.ts';
import { flash } from '../shell/status.ts';
import { countText, groupOf, groupsOf, partOf, rng } from './counts.ts';
import type { PuzzleWorkspace } from './workspace.ts';

const cells = (s: ShapeSummary): number => s.fixed + s.variable;

/** Why a sliding piece's count cannot change (Part.required). */
const REQUIRED =
  'Its start or goal on the start/goal shape needs exactly this piece: place or clear it on the Entities workspace';

export class PiecesCard {
  private readonly ws: PuzzleWorkspace;
  private drag: { u: string; from: 'lib' | 'used' } | null = null;
  private bulk: HTMLElement | null = null;

  constructor(ws: PuzzleWorkspace) {
    this.ws = ws;
    byId('pzMore').innerHTML = ic('more');
    byId('pzLibTog').innerHTML = ic('chevD', 16);
    this.install();
  }

  private get shapes(): ShapeSummary[] {
    return this.ws.store.snapshot?.shapes ?? [];
  }

  private title(s: ShapeSummary): string {
    return `${s.label}${s.name !== '' ? ` – ${s.name}` : ''} · ${String(cells(s))} voxels`;
  }

  /** Draws the result, the pieces, the library, the summary (the mockup's renderPuzzle). */
  render(): void {
    const ws = this.ws;
    const d = ws.detail;
    const shapes = this.shapes;
    const result = d?.result ?? null;
    const parts = d?.parts ?? [];
    const others = shapes.filter((s) => s.id !== result);
    const byShape = new Map(shapes.map((s) => [s.id, s]));

    // the notice for a puzzle this version does not solve (stacking)
    const k = ws.kind;
    const note = byId('pzKind');
    note.classList.toggle('show', k !== null && !k.supported);
    note.innerHTML =
      k !== null && !k.supported
        ? `<b>${k.kind === 'sliding' ? 'Sliding' : 'Stacking'} puzzle</b> — it opens and shows here, but this version does not solve ${k.kind} puzzles yet; the Solver is disabled for it.`
        : '';
    note.dataset.kind = k?.kind ?? '';

    // the result: a sliding puzzle's is its start/goal shape, for good
    const fixedResult = ws.store.snapshot?.grid === 'sliding';
    const change = fixedResult ? '' : '<button class="btn sm" id="pzChange">Change…</button>';
    const R = result === null ? undefined : byShape.get(result);
    const f = ws.fit;
    if (d === null) {
      byId('pzResult').innerHTML = '';
    } else if (R === undefined) {
      byId('pzResult').innerHTML =
        `<span class="chip" style="background:var(--muted)">–</span><div class="meta"><b>No result</b><span>Choose the shape the pieces fill</span></div>${change}`;
    } else {
      const c = ws.chip(R.id);
      const hold = f === null ? '' : ` · can hold ${rng(f.resultMin, f.resultMax)}`;
      byId('pzResult').innerHTML =
        `<span class="chip" style="background:${c.bg};color:${c.fg}">${esc(R.label)}</span><div class="meta"><b>${esc(R.name) || 'Result piece'}</b><span>${String(cells(R))} voxels${hold}</span></div>${change}`;
    }
    byId('pzResult').dataset.shape = result ?? '';

    // the pieces in the puzzle
    byId('pzPieceCount').textContent = String(parts.length);
    byId('pzLibCount').textContent = String(others.length);
    for (const b of $$('#pzViewSeg button')) {
      b.classList.toggle('on', b.dataset.v === ws.view);
    }
    const ub = byId('pzUsed');
    ub.classList.toggle('tbl', ws.view === 'table');
    if (parts.length === 0) {
      setHtml(
        ub,
        '<div class="pz-empty" style="padding:6px">No pieces yet. Add them from <b>All pieces</b> below — drag, double-click, or press +.</div>',
      );
    } else if (ws.view === 'chips') {
      setHtml(
        ub,
        parts
          .map((p) => {
            const s = byShape.get(p.shape);
            return s === undefined ? '' : this.usedTile(s, p);
          })
          .join(''),
      );
    } else {
      const groups = groupsOf(d);
      setHtml(
        ub,
        `<table class="pzt"><thead><tr><th class="l">Shape</th><th>Count</th><th title="Allow a min–max range">Range</th><th>Group</th></tr></thead><tbody>${parts
          .map((p) => {
            const s = byShape.get(p.shape);
            if (s === undefined) {
              return '';
            }
            const rg = ws.isRange(s.id);
            const g = groupOf(p);
            const c = ws.chip(s.id);
            // a piece a start or goal needs keeps its count (the engine refuses)
            const off = p.required ? `disabled title="${REQUIRED}"` : '';
            const count = rg
              ? `<input type="number" min="0" data-f="min" value="${String(p.min)}" aria-label="Minimum" ${off}><span class="dash">–</span><input type="number" min="0" data-f="max" value="${String(p.max)}" aria-label="Maximum" ${off}>`
              : `<input type="number" min="0" data-f="max" value="${String(p.max)}" aria-label="Copies" ${off}>`;
            // a sliding puzzle has no groups
            const grp = fixedResult
              ? '–'
              : `<select data-grp aria-label="Group"><option value="">–</option>${groups.map((x) => `<option value="${String(x)}" ${g === x ? 'selected' : ''}>G${String(x)}</option>`).join('')}<option value="new">+ New</option></select>`;
            return `<tr data-u="${s.id}"><td class="sh" style="background:${c.bg};color:${c.fg}" title="${esc(this.title(s))}">${esc(s.label)}${s.name !== '' ? ` – ${esc(s.name)}` : ''}</td><td class="cn">${count}</td><td><input type="checkbox" data-range ${rg ? 'checked' : ''} aria-label="Range" ${off}></td><td>${grp}</td></tr>`;
          })
          .join('')}</tbody></table>`,
      );
    }
    markSelected(ub, '[data-u]', 'u', ws.sel);

    // all pieces
    setHtml(byId('pzLib'), others.map((s) => this.libChip(s, partOf(d, s.id))).join(''));
    markSelected(byId('pzLib'), '.lc', 'u', ws.sel);
    byId('pzLib').classList.toggle('closed', !ws.libOpen);
    byId('pzLibTog').style.transform = ws.libOpen ? '' : 'rotate(-90deg)';

    this.renderSummary();
  }

  // the mockup's pzUsedTile
  private usedTile(s: ShapeSummary, p: Part): string {
    const g = groupOf(p);
    const c = this.ws.chip(s.id);
    // a piece a start or goal needs keeps its count (the engine refuses)
    const off = p.required ? `disabled title="${REQUIRED}"` : '';
    return `<div class="ut" data-u="${s.id}" draggable="${String(!p.required)}" title="${esc(this.title(s))}${p.required ? ` — ${REQUIRED}` : ' — drag onto “All pieces” to remove'}"><i class="uc" style="background:${c.bg}"></i><div class="ub"><div class="t1"><span class="gripd">${ic('grip', 12)}</span><b>${esc(s.label)}</b>${s.name !== '' ? `<span class="pl">${esc(s.name)}</span>` : ''}${g > 0 ? `<span class="gb">G${String(g)}</span>` : ''}</div><div class="t2"><button data-d="-1" ${off || 'title="One fewer (at 0 the piece leaves the puzzle)"'}>−</button><span class="cnt">${countText(p)}</span><button data-d="1" ${off || 'title="One more"'}>+</button></div></div></div>`;
  }

  // the mockup's pzLibChip
  private libChip(s: ShapeSummary, p: Part | undefined): string {
    const n = p?.max ?? 0;
    const c = this.ws.chip(s.id);
    const req = p?.required ?? false;
    return `<div class="lc ${n > 0 ? 'in' : ''}" data-u="${s.id}" draggable="${String(!req)}" title="${esc(this.title(s))}${req ? ` — ${REQUIRED}` : ' — drag into “In this puzzle”, double-click, or press + to add one'}"><span class="gripd">${ic('grip', 12)}</span><i class="sq" style="background:${c.bg}"></i><b>${esc(s.label)}</b>${s.name !== '' ? `<span class="pl">${esc(s.name)}</span>` : ''}<button class="la" data-add="1" ${req ? `disabled title="${REQUIRED}"` : 'title="Add one to the puzzle"'}>${n > 0 ? `×${String(n)} +` : '+'}</button></div>`;
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
      ? `<hr><button data-k="earlier" ${i === 0 ? 'disabled' : ''}>Move earlier</button><button data-k="later" ${i === parts.length - 1 ? 'disabled' : ''}>Move later</button>`
      : '';
    openPopAt(
      e.clientX,
      e.clientY,
      `<div class="ph">Piece ${esc(ws.labelOf(u))}</div><button data-k="up">One more (+1)<span class="kb">+</span></button><button data-k="dn" ${inPuzzle ? '' : 'disabled'}>One fewer (−1)<span class="kb">−</span></button><button data-k="off" ${inPuzzle ? '' : 'disabled'}>Remove from puzzle<span class="kb">Del</span></button>${moves}<hr><button data-k="res">Set as result</button>`,
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
            return `<button data-k="${s.id}" ${s.id === result ? 'disabled' : ''}><span class="chip" style="background:${c.bg};color:${c.fg};min-width:30px;height:20px;font-size:10px">${esc(s.label)}</span>${esc(s.name) || '&nbsp;'}<span class="kb">${String(cells(s))} vox</span></button>`;
          })
          .join('')}`,
        (k) => void ws.setResult(k),
      );
    });
    byId('pzViewSeg').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('button');
      if (b?.dataset.v === 'chips' || b?.dataset.v === 'table') {
        ws.view = b.dataset.v;
        ws.render();
      }
    });

    // the table's fields
    used.addEventListener('change', (e) => {
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
    used.addEventListener('click', (e) => {
      const t = e.target as Element;
      const tr = t.closest<HTMLElement>('tr[data-u]');
      if (tr?.dataset.u !== undefined) {
        if (t.closest('input,select') === null) {
          ws.select(tr.dataset.u);
        }
        return;
      }
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
    used.addEventListener('contextmenu', (e) => {
      const u = (e.target as Element).closest<HTMLElement>('.ut,tr[data-u]')?.dataset.u;
      if (u !== undefined) {
        this.menu(e, u);
      }
    });
    lib.addEventListener('contextmenu', (e) => {
      const u = (e.target as Element).closest<HTMLElement>('.lc')?.dataset.u;
      if (u !== undefined) {
        this.menu(e, u);
      }
    });
    libHead.addEventListener('click', () => {
      ws.libOpen = !ws.libOpen;
      ws.render();
    });
    byId('pzMore').addEventListener('click', (e) => {
      e.stopPropagation();
      openPop(
        e.currentTarget as Element,
        '<button data-k="all">Add one of each piece</button><button data-k="range">Set range for all pieces…</button><button data-k="clr">Remove all pieces</button>',
        (k) => {
          if (k === 'all') {
            void ws.addOneOfEach();
          } else if (k === 'range') {
            this.bulkRange();
          } else if (k === 'clr') {
            void ws.removeAllPieces();
          }
        },
      );
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
