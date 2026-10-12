// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Solver's right card: the solutions as a list (the engine's sorts by
// a header's click, again to reverse, a third time back to the saved
// order; the rows paged as the list scrolls: only the rows in sight are in
// the page) or a slider (one at a time), the disassembly player's
// controls, an analysis under way, the sort of the slider view, the
// actions (the Manage menu: remove, delete disassemblies, analyse; rows
// that would go are struck through while an item is pointed at) and the
// piece chips (solid → wire frame → hidden). The disassembly status of a
// row is SolutionRow.disassembly, and "cannot be disassembled" what an
// analysis of this session found (the client remembers it: the engine
// keeps no mark of a failed take-apart). From docs/mockup/index.html:
// `renderSvSel`, `svMetaHTML`, `svDaCell`, `renderSvSortAct`, `renderPcs`,
// `renderSolverPlayer`, `svPreview`, `SVMENU`, `SVSORT`, `TG`,
// `svKeepFocus` and their handlers in `svWire`.

import type { SolutionRow } from '../../engine/api.generated.ts';
import { byId, esc, forgetHtml, setHtml } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { openPop } from '../shell/popover.ts';
import { hex } from '../shell/piece-colours.ts';
import type { SortKey } from './pager.ts';
import type { RemoveWhich, SolverWorkspace } from './workspace.ts';

/** The sort buttons (the mockup's SVSORT; `assembly` is its search order). */
const SORTS: readonly [SortKey, string, string, string][] = [
  ['assembly', '#', 'Number', 'Original search order (assembly number)'],
  [
    'level',
    'Level',
    'Level',
    'Disassembly level X.y.z — moves to remove the first piece, then the second, …',
  ],
  ['moves', 'Moves', 'Moves', 'Total moves to disassemble'],
  ['pieces', 'Pcs', 'Pieces', 'Number of pieces used by the assembly'],
];

/** The Manage menu (the mockup's SVMENU). */
const MENU =
  '<div class="ph">Remove assemblies</div><button data-k="at">This solution only<span class="kb">At</span></button><button data-k="before">All before this one<span class="kb">Before</span></button><button data-k="after">All after this one<span class="kb">After</span></button><button data-k="nda">All without a disassembly<span class="kb">w/o DA</span></button><button data-k="all">All solutions<span class="kb">All</span></button><hr><div class="ph">Disassemblies</div><button data-k="ddc">Delete the disassembly of this solution<span class="kb">D DA</span></button><button data-k="dda">Delete all disassemblies<span class="kb">D A DA</span></button><button data-k="adc">Analyse this assembly’s disassembly<span class="kb">A DA</span></button><button data-k="ada">Analyse the disassembly of every assembly<span class="kb">A A DA</span></button><button data-k="adm">Analyse only the missing disassemblies<span class="kb">A M DA</span></button>';

/** The player's buttons (the mockup's TG). */
const TG = {
  start:
    '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M6 5h2v14H6zM20 5v14L9 12z"/></svg>',
  back: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M18 5v14L7 12z"/></svg>',
  play: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M7 4.5v15L19 12z"/></svg>',
  pause:
    '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M6 5h4v14H6zM14 5h4v14h-4z"/></svg>',
  fwd: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M6 5v14l11-7z"/></svg>',
  end: '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M16 5h2v14h-2zM4 5v14l11-7z"/></svg>',
};

/** The disassembly status of a row: class, mark, text (the mockup's svDaCell). */
export function daCell(row: SolutionRow, inseparable: boolean): [string, string, string] {
  if (row.disassembly === 'tree') {
    return ['ok', ic('check', 14), 'Disassembly stored'];
  }
  if (row.disassembly === 'summary') {
    return ['', '◐', 'Level only — moves not stored'];
  }
  return inseparable
    ? ['bad', ic('close', 12), 'Cannot be disassembled']
    : ['', '·', 'Disassembly not checked'];
}

/** The moves of a row as shown: moves, and the turns when there are any. */
function movesOf(row: SolutionRow): string {
  if (row.moves === null) {
    return '—';
  }
  return (row.rotations ?? 0) > 0
    ? `${String(row.moves)}+${String(row.rotations)}R`
    : String(row.moves);
}

export class SolutionsCard {
  private readonly ws: SolverWorkspace;
  /** The largest step count of the rows read (the Moves column's bars). */
  private maxSteps = 1;
  private menuOpen = false;
  private rowHeight = 28;

  constructor(ws: SolverWorkspace) {
    this.ws = ws;
    this.wire();
  }

  /** Draws the card. */
  render(): void {
    this.renderSel();
    this.renderPlayer();
    this.renderAnalysis();
    this.renderSort();
    this.renderActions();
    this.renderChips();
  }

  // ---- the list or the slider ----

  private renderSel(): void {
    const ws = this.ws;
    const pager = ws.pager;
    const n = pager?.total ?? 0;
    byId('slvCount').textContent = String(n);
    for (const b of byId('slvViewSeg').querySelectorAll<HTMLElement>('button')) {
      b.classList.toggle('on', b.dataset.v === ws.view);
    }
    const box = byId('slvSel');
    box.dataset.total = String(n);
    box.dataset.sort = `${ws.sortBy}:${ws.descending ? 'desc' : 'asc'}`;
    box.dataset.version = String(pager?.version ?? '');
    if (n === 0) {
      setHtml(
        box,
        `<div class="slv-nav"><div class="big"><b>Solution —</b><span>0 of 0</span></div><div class="slv-empty" style="margin:0">${this.whyEmpty()}</div></div>`,
      );
      return;
    }
    if (ws.view === 'list') {
      this.renderList(n);
    } else {
      this.renderSlider(n);
    }
  }

  private whyEmpty(): string {
    const ws = this.ws;
    const s = ws.current === null ? 'idle' : ws.stateOf(ws.current);
    const p = ws.problem;
    if (ws.pager?.total === null) {
      return '<b>Reading the solutions…</b>';
    }
    if (s === 'preparing' || s === 'assembling' || s === 'draining') {
      return '<b>Searching…</b>Solutions appear here as they are found.';
    }
    if (p?.state === 'solved' && (p.assemblies ?? 0) === 0) {
      return '<b>No assemblies found</b>The pieces cannot fill the result with the current ranges and colour rules. Review the Puzzle tab.';
    }
    if (p !== null && (p.assemblies ?? 0) > 0 && ws.options.disassemble) {
      return `<b>No solutions to show</b>${String(p.assemblies)} assembl${p.assemblies === 1 ? 'y was' : 'ies were'} found, but none can be disassembled.`;
    }
    return '<b>No results yet</b>Press <b style="display:inline">Solve</b> to search for assemblies.';
  }

  private renderList(n: number): void {
    const ws = this.ws;
    const box = byId('slvSel');
    let list = document.getElementById('slvList');
    if (list === null || !box.contains(list)) {
      // drawn here, not by setHtml: the slider or the empty card must draw again after it
      forgetHtml(box);
      box.innerHTML = `<div class="lhead" id="slvHead"></div><div class="lbody" id="slvList" tabindex="0" role="listbox" aria-label="Solutions"><div class="lspace" id="slvSpace"></div></div><div class="lfoot"><span id="slvFoot"></span><span>↑↓ solution · ←→ move · Space play</span></div>`;
      list = byId('slvList');
      list.addEventListener('scroll', () => {
        this.renderRows();
      });
    }
    const arrow = (k: SortKey): string => (ws.sortBy === k ? ` ${ws.descending ? '▼' : '▲'}` : '');
    setHtml(
      byId('slvHead'),
      SORTS.map(
        ([k, t, , tip]) =>
          `<button data-sort="${k}" class="${ws.sortBy === k ? 'on' : ''}" title="${esc(tip)}${ws.sortBy === k ? ' — click again to reverse, a third time for the saved order' : ''}">${t}${arrow(k)}</button>`,
      ).join('') + '<span title="Disassembly status">DA</span>',
    );
    byId('slvFoot').textContent = `${String(n)} solution${n === 1 ? '' : 's'}`;
    this.rowHeight = density() === 'minimal' ? 26 : 28;
    byId('slvSpace').style.height = `${String(n * this.rowHeight + 10)}px`;
    this.renderRows();
  }

  /** The rows in sight (and a few around), asking the pager for them. */
  private renderRows(): void {
    const ws = this.ws;
    const pager = ws.pager;
    const list = document.getElementById('slvList');
    const space = document.getElementById('slvSpace');
    if (pager === null || list === null || space === null) {
      return;
    }
    const n = pager.total ?? 0;
    const h = this.rowHeight;
    const first = Math.max(0, Math.floor(list.scrollTop / h) - 4);
    const last = Math.min(n, Math.ceil((list.scrollTop + list.clientHeight) / h) + 4);
    pager.want(first - 20, last + 20);
    let html = '';
    for (let i = first; i < last; i++) {
      const r = pager.row(i);
      if (r === undefined) {
        html += `<div class="lrow ph" style="top:${String(4 + i * h)}px" data-i="${String(i)}"><span class="c1 mut">…</span></div>`;
        continue;
      }
      this.maxSteps = Math.max(this.maxSteps, r.steps ?? 0);
      const has = r.disassembly !== 'none';
      const da = daCell(r, ws.inseparable(r.id));
      html += `<div class="lrow ${i === ws.sel ? 'sel' : ''}" role="option" aria-selected="${String(i === ws.sel)}" style="top:${String(4 + i * h)}px" data-i="${String(i)}" data-id="${r.id}" data-index="${String(r.index)}" data-da="${r.disassembly}"><span class="c1">${String(r.assembly + 1)}</span><span class="c2 ${has ? '' : 'mut'}" title="${esc(r.movesText ?? '')}">${has ? esc(r.movesText ?? '') : '—'}</span><span class="c3 ${has ? '' : 'mut'}">${has ? `<i style="width:${String(Math.round(((r.steps ?? 0) / this.maxSteps) * 100))}%"></i><b>${movesOf(r)}</b>` : '—'}</span><span class="c4">${String(r.pieces)}</span><span class="c5 ${da[0]}" title="${da[2]}">${da[1]}</span></div>`;
    }
    setHtml(space, html);
    // the rows shown, readable from outside (the e2e tests compare them with solution.page)
    space.dataset.first = String(first);
    space.dataset.current = String(pager.current(first, Math.max(first + 1, last)));
  }

  private renderSlider(n: number): void {
    const ws = this.ws;
    const i = Math.max(0, ws.sel);
    const row = ws.selected;
    const box = byId('slvSel');
    const meta =
      row === null
        ? ''
        : (() => {
            const da = daCell(row, ws.inseparable(row.id));
            const has = row.disassembly !== 'none';
            return `<div><small>Assembly</small><b>#${String(row.assembly + 1)}</b></div><div><small>Level</small><b title="${esc(row.movesText ?? '')}">${has ? esc(row.movesText ?? '') : '—'}</b></div><div><small>Moves</small><b>${has ? movesOf(row) : '—'}</b></div><div><small>Pieces</small><b>${String(row.pieces)}</b></div><div><small>Disassembly</small><b title="${da[2]}">${row.disassembly === 'tree' ? 'stored' : row.disassembly === 'summary' ? 'level only' : ws.inseparable(row.id) ? 'none exists' : 'not checked'}</b></div><div><small>Position</small><b>${String(i + 1)} / ${String(n)}</b></div>`;
          })();
    setHtml(
      box,
      `<div class="slv-nav"><div class="big"><b id="slvNavT">Solution ${String(i + 1)}</b><span id="slvNavO">of ${String(n)}${ws.options.disassemble ? '' : ' assemblies'}</span></div><div class="ctl"><button class="nb" data-n="-1" title="Previous solution" ${i <= 0 ? 'disabled' : ''}><span style="display:grid;transform:rotate(90deg)">${ic('chevD', 14)}</span></button><input type="range" id="slvSlider" min="1" max="${String(n)}" value="${String(i + 1)}" ${n < 2 ? 'disabled' : ''}><button class="nb" data-n="1" title="Next solution" ${i >= n - 1 ? 'disabled' : ''}><span style="display:grid;transform:rotate(-90deg)">${ic('chevD', 14)}</span></button></div></div><div class="slv-meta" id="slvMeta">${meta}</div>`,
    );
  }

  /** The selection moved: the rows' marks and the slider, without a rebuild (the mockup's svSelect). */
  selectionChanged(): void {
    const ws = this.ws;
    if (ws.view === 'list') {
      const list = document.getElementById('slvList');
      if (list !== null) {
        const h = this.rowHeight;
        const top = 4 + ws.sel * h;
        if (top < list.scrollTop) {
          list.scrollTop = top - 4;
        } else if (top + h > list.scrollTop + list.clientHeight) {
          list.scrollTop = top + h - list.clientHeight + 4;
        }
      }
      this.renderRows();
    } else {
      const focused = document.activeElement?.id;
      this.renderSlider(ws.pager?.total ?? 0);
      if (focused === 'slvSlider') {
        document.getElementById('slvSlider')?.focus({ preventScroll: true });
      }
    }
    this.renderPlayer();
    this.renderChips();
  }

  private renderSort(): void {
    const ws = this.ws;
    const n = ws.pager?.total ?? 0;
    setHtml(
      byId('slvSort'),
      n > 0 && ws.view === 'slider'
        ? `<div class="slv-lab">Sort by</div><div class="seg">${SORTS.map(([k, , t, tip]) => `<button data-sort="${k}" class="${ws.sortBy === k ? 'on' : ''}" title="${esc(tip)}${ws.sortBy === k ? ' — click again to reverse' : ''}" ${n < 2 ? 'disabled' : ''}><span>${t}</span>${ws.sortBy === k ? `<i class="arr">${ws.descending ? '▼' : '▲'}</i>` : ''}</button>`).join('')}</div>`
        : '',
    );
  }

  /**
   * The actions: Manage solutions, and Export to STL (the original's
   * "Export Solution to STL", each piece's STL mesh: it comes with the STL
   * meshes; File ▸ Export ▸ STL writes a shape's flat faces meanwhile).
   */
  renderActions(): void {
    const ws = this.ws;
    const n = ws.pager?.total ?? 0;
    const busy = ws.running || ws.analysing || ws.calling;
    setHtml(
      byId('slvAct'),
      `<button class="btn" data-a="manage" ${n === 0 || busy ? 'disabled' : ''}>Manage solutions ▾</button><button class="btn" data-a="stl" disabled title="Exporting a solution's pieces to STL comes with the STL meshes, in a later version; File ▸ Export ▸ STL writes a shape's flat faces">Export to STL</button>`,
    );
    this.renderAnalysis();
  }

  /** An analysis under way: its progress and Stop. */
  private renderAnalysis(): void {
    const ws = this.ws;
    const a = ws.record?.analysis ?? null;
    const el = byId('slvAna');
    const on = a?.active === true;
    el.classList.toggle('show', on);
    el.dataset.active = on ? '1' : '0';
    if (!on) {
      setHtml(el, '');
      return;
    }
    const pct = a.total > 0 ? (a.done / a.total) * 100 : 0;
    setHtml(
      el,
      `<div class="t"><b>Analysing disassemblies</b><span id="slvAnaN">${String(a.done)} of ${String(a.total)} · ${String(a.separated)} taken apart</span></div><div class="slv-prog"><div class="bar"><i style="width:${pct.toFixed(1)}%"></i></div><button class="btn sm" data-a="stopAnalysis">Stop</button></div>`,
    );
  }

  // ---- the player (the mockup's renderSolverPlayer) ----

  /** The player's controls for the solution shown. */
  renderPlayer(): void {
    const ws = this.ws;
    const pl = ws.player;
    const el = byId('splayer');
    const head = (r = ''): string =>
      `<div class="ttlrow"><span class="ttl">Disassembly player</span>${r}</div>`;
    const row = pl.row;
    if (row === null) {
      const msg =
        (ws.pager?.total ?? 0) > 0
          ? 'Choose a solution in the list to play its disassembly.'
          : 'Nothing to play yet — run the solver to find assemblies.';
      setHtml(el, head() + `<div class="msg"><span>${msg}</span></div>`);
      return;
    }
    if (row.disassembly !== 'tree') {
      const insep = ws.inseparable(row.id);
      const txt =
        ws.family !== 'brick'
          ? // a start layout kept without its path to the goal (Find Solutions off)
            'Find Solutions was not checked: no path to the goal was searched for this start.'
          : insep
            ? 'This assembly cannot be taken apart.'
            : row.disassembly === 'summary'
              ? 'Only the level was kept for this solution — the moves were not stored.'
              : ws.options.disassemble
                ? 'No disassembly stored.'
                : 'Disassembly was not checked for this assembly.';
      setHtml(
        el,
        head() +
          `<div class="msg"><span>${txt}</span>${insep || ws.family !== 'brick' ? '' : `<button class="btn sm" data-a="analyse" ${ws.analysing || ws.running ? 'disabled' : ''}>Analyse disassembly</button>`}</div>`,
      );
      return;
    }
    const steps = pl.ready ? pl.steps : (row.steps ?? 0);
    const t = Math.max(0, Math.min(pl.t, steps));
    const off = pl.ready ? '' : 'disabled';
    const html =
      head(`<span class="lbl">Move ${t.toFixed(1)} / ${String(steps)}</span>`) +
      `<div class="tr1"><button class="tb" data-t="start" title="Back to the assembled position" ${off}>${TG.start}</button><button class="tb" data-t="back" title="One move back" ${off}>${TG.back}</button><button class="tb pri" data-t="play" title="${pl.playing ? 'Pause' : 'Play the disassembly'}" ${off}>${pl.playing ? TG.pause : TG.play}</button><button class="tb" data-t="fwd" title="One move forward" ${off}>${TG.fwd}</button><button class="tb" data-t="end" title="Fully disassembled" ${off}>${TG.end}</button><span class="sp"></span><select id="slvSpeed" title="Playback speed">${(
        [
          [0.5, '0.5×'],
          [1, '1×'],
          [2, '2×'],
          [4, '4×'],
          [8, '8×'],
        ] as const
      )
        .map(
          ([v, s]) =>
            `<option value="${String(v)}" ${pl.speed === v ? 'selected' : ''}>${s}</option>`,
        )
        .join(
          '',
        )}</select></div><div class="tr2"><input type="range" id="slvMove" min="0" max="${String(steps)}" step="0.05" value="${String(t)}" ${off}></div>`;
    // keep the seek bar being dragged and the focus (the mockup's svKeepFocus)
    const focused = document.activeElement?.id;
    setHtml(el, html);
    const bar = document.getElementById('slvMove') as HTMLInputElement | null;
    if (bar !== null && Math.abs(Number(bar.value) - t) > 1e-6) {
      bar.value = String(t);
    }
    if (focused !== undefined && focused !== '' && document.activeElement?.id !== focused) {
      document.getElementById(focused)?.focus({ preventScroll: true });
    }
  }

  // ---- the piece chips (the mockup's renderPcs) ----

  /** The chips of the solution's pieces. */
  renderChips(): void {
    const pl = this.ws.player;
    const focused = (document.activeElement as HTMLElement | null)?.dataset.p;
    setHtml(
      byId('slvPcs'),
      pl.pieces
        .map((p) => {
          const m = pl.modes.get(p.piece) ?? 'solid';
          return `<div class="pvc ${m === 'wire' ? 'wire' : m === 'hidden' ? 'hid' : ''}" data-p="${String(p.piece)}" data-mode="${m}" style="--c:${hex(p.colour)}" tabindex="0" role="button" title="${esc(p.label)} — ${m === 'wire' ? 'wireframe' : m}. Click, Space or Enter to cycle: solid → wireframe → hidden"><i class="sq"></i>${esc(p.label)}</div>`;
        })
        .join(''),
    );
    if (focused !== undefined) {
      this.focusChip(Number(focused));
    }
  }

  /** Puts the focus on a piece's chip. */
  focusChip(piece: number): void {
    document
      .querySelector<HTMLElement>(`#slvPcs .pvc[data-p="${String(piece)}"]`)
      ?.focus({ preventScroll: true });
  }

  /** ← and → on a chip: the focus to the one before or after. */
  moveChipFocus(from: HTMLElement, d: -1 | 1): void {
    const chips = [...byId('slvPcs').querySelectorAll<HTMLElement>('.pvc')];
    const i = chips.indexOf(from);
    chips[Math.max(0, Math.min(chips.length - 1, i + d))]?.focus();
  }

  // ---- the Manage menu's preview (the mockup's svPreview) ----

  private preview(k: string | null): void {
    const ws = this.ws;
    const sel = ws.selected;
    for (const r of document.querySelectorAll<HTMLElement>('#slvList .lrow[data-id]')) {
      const index = Number(r.dataset.index);
      const goes =
        k === 'at'
          ? r.dataset.id === sel?.id
          : k === 'before'
            ? sel !== null && index < sel.index
            : k === 'after'
              ? sel !== null && index > sel.index
              : k === 'nda'
                ? r.dataset.da === 'none'
                : k === 'all';
      r.classList.toggle('will-del', goes);
    }
  }

  private manage(k: string): void {
    const ws = this.ws;
    const remove: Record<string, RemoveWhich> = {
      at: 'one',
      before: 'before',
      after: 'after',
      nda: 'withoutDisassembly',
      all: 'all',
    };
    const which = remove[k];
    if (which !== undefined) {
      ws.remove(which);
    } else if (k === 'ddc' || k === 'dda') {
      void ws.deleteDisassembly(k === 'ddc');
    } else if (k === 'adc') {
      void ws.analyse('one');
    } else if (k === 'ada') {
      void ws.analyse('all');
    } else if (k === 'adm') {
      void ws.analyse('missing');
    }
  }

  // ---- events (the mockup's svWire) ----

  private wire(): void {
    const ws = this.ws;
    byId('slvViewSeg').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('button[data-v]');
      if (b !== null) {
        ws.view = b.dataset.v === 'slider' ? 'slider' : 'list';
        this.render();
      }
    });
    const sortClick = (e: Event): boolean => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-sort]');
      if (b === null || b.disabled) {
        return false;
      }
      ws.sort(b.dataset.sort as SortKey);
      return true;
    };
    byId('slvSort').addEventListener('click', sortClick);
    const sel = byId('slvSel');
    sel.addEventListener('click', (e) => {
      if (sortClick(e)) {
        return;
      }
      const t = e.target as Element;
      const nb = t.closest<HTMLButtonElement>('.nb');
      if (nb !== null && !nb.disabled) {
        ws.select(ws.sel + Number(nb.dataset.n));
        return;
      }
      const row = t.closest<HTMLElement>('.lrow');
      if (row !== null) {
        ws.select(Number(row.dataset.i));
        document.getElementById('slvList')?.focus({ preventScroll: true });
      }
    });
    sel.addEventListener('dblclick', (e) => {
      const row = (e.target as Element).closest<HTMLElement>('.lrow');
      if (row !== null) {
        ws.select(Number(row.dataset.i));
        ws.player.playWhenReady();
      }
    });
    // scrubbing the slider: no rebuild of the slider
    sel.addEventListener('input', (e) => {
      const t = e.target as HTMLInputElement;
      if (t.id === 'slvSlider') {
        ws.select(Number(t.value) - 1);
      }
    });
    byId('slvAct').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-a]');
      if (b === null || b.disabled || b.dataset.a !== 'manage') {
        return;
      }
      this.menuOpen = true;
      openPop(
        b,
        // a sliding or stacking puzzle has no take-apart to analyse (its paths are its search's)
        ws.family !== 'brick' ? MENU.replace(/<button data-k="ad[cam]">.*?<\/button>/g, '') : MENU,
        (k) => {
          this.menuOpen = false;
          this.preview(null);
          this.manage(k);
        },
        true,
      );
    });
    const pop = byId('pop');
    pop.addEventListener('mouseover', (e) => {
      if (this.menuOpen) {
        this.preview((e.target as Element).closest<HTMLElement>('[data-k]')?.dataset.k ?? null);
      }
    });
    pop.addEventListener('mouseleave', () => {
      if (this.menuOpen) {
        this.preview(null);
      }
    });
    document.addEventListener('mousedown', (e) => {
      const t = e.target as Element;
      if (this.menuOpen && t.closest('#pop') === null && t.closest('#slvAct') === null) {
        this.menuOpen = false;
        this.preview(null);
      }
    });
    byId('slvAna').addEventListener('click', (e) => {
      if ((e.target as Element).closest('[data-a="stopAnalysis"]') !== null) {
        void ws.stopAnalysis();
      }
    });
    byId('slvPcs').addEventListener('click', (e) => {
      const c = (e.target as Element).closest<HTMLElement>('.pvc');
      if (c !== null) {
        ws.player.cycle(Number(c.dataset.p));
      }
    });
    byId('slvShowAll').addEventListener('click', () => {
      ws.player.showAll();
    });
    const player = byId('splayer');
    // the buttons do not take the focus from the list or the slider (svWire's mousedown)
    player.addEventListener('mousedown', (e) => {
      const a = document.activeElement;
      if (
        (e.target as Element).closest('button[data-t]') !== null &&
        a?.closest('#slvList,#slvSlider') != null
      ) {
        e.preventDefault();
      }
    });
    player.addEventListener('click', (e) => {
      const t = e.target as Element;
      if (t.closest('[data-a="analyse"]') !== null) {
        void ws.analyse('one');
        return;
      }
      const b = t.closest<HTMLButtonElement>('button[data-t]');
      if (b === null || b.disabled) {
        return;
      }
      switch (b.dataset.t) {
        case 'play':
          ws.player.togglePlay();
          break;
        case 'start':
          ws.player.seek('start');
          break;
        case 'end':
          ws.player.seek('end');
          break;
        case 'back':
          ws.player.seek(-1);
          break;
        case 'fwd':
          ws.player.seek(1);
          break;
      }
    });
    player.addEventListener('input', (e) => {
      const t = e.target as HTMLInputElement;
      if (t.id === 'slvMove') {
        ws.player.scrub(Number(t.value));
      }
    });
    player.addEventListener('change', (e) => {
      const t = e.target as HTMLSelectElement;
      if (t.id === 'slvSpeed') {
        ws.player.speed = Number(t.value);
      }
    });
    // sticky focus: orbiting or clicking in the 3D view leaves the focus in the Solutions card
    byId('vp').addEventListener(
      'mousedown',
      (e) => {
        if (
          ws.store.snapshot === null ||
          document.querySelector('#stage[data-tab=solver]') === null
        ) {
          return;
        }
        const a = document.activeElement;
        if (
          a?.closest('#rightS') != null &&
          (e.target as Element).closest('input,select,textarea') === null
        ) {
          e.preventDefault();
        }
      },
      true,
    );
  }
}

function density(): string {
  return document.documentElement.dataset.density ?? 'standard';
}
