// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Entities workspace's right card around the grid: the tool strip
// (with the box selection's tool), mirror and span, the planes, the layer
// strip (marks on the layers that hold cells; PgUp and PgDn), the box
// selection's bar (Fill, Clear, deselect), the grid size steppers in the
// grid's units with "Apply to all shapes", the palette (Default is the
// shape's own colour; add, edit, remove) and the collapsed card's rail.
// From docs/mockup/index.html: `renderTools`, `renderMod`, `renderPlane`,
// `renderLayers`, `renderSelbar`, `renderInsp`'s size steppers and the
// applyAll switch, `renderColors` and their handlers. The engine changes
// the shape; the card shows the editor's state and the engine's answers.
// On a sliding puzzle (docs/webui-spec.md, section 8.1) there is no Paint
// tool and no palette (a cell's colour is a start mark), the Z size is
// locked (one layer), and a start/goal shape has the "Tray edit" switch
// (Walls, Start, Goal: the labels of the original's unused
// ToolTab_Sliding, for its Start/Goal tab) with the piece list and the
// chosen piece drawn flat (tooltabs.cpp: `refreshPieceList`,
// `slidePiecePreview_c`).

import type { Colour, ShapeSummary } from '../../engine/api.generated.ts';
import { EngineError } from '../../engine/errors.ts';
import type { Store } from '../state/store.ts';
import type { Tool } from '../shell/cursors.ts';
import { byId, esc } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { applyLayout, layout } from '../shell/layout.ts';
import { contrast, hex, isTray, shapeColour } from '../shell/piece-colours.ts';
import { openPop, openPopAt } from '../shell/popover.ts';
import type { Axis, EditorState, PlaneMap, TrayMode } from './editor-state.ts';
import { TOOLS } from './editor-state.ts';
import { G } from './glyphs.ts';
import { pieceText, stampPieces, validity } from './tray.ts';
import type { TrayReply } from './tray.ts';
import type { Plane } from '../view3d/overlays.ts';

/** What the card asks of the workspace. */
export interface CardHost {
  store: Store;
  state: EditorState;
  /** The editor's state changed (the grid, the view, the cursor follow). */
  changed(): void;
  /** The plane map of the shape shown, or null. */
  plane(): PlaneMap | null;
  /** The layers holding cells, along the plane's layer axis. */
  layersWithCells(): Set<number>;
  /** The grid's size unit (shape.resize counts in it). */
  unit(): number;
  /** Whether the grid has more planes than XY (the cube grid). */
  allPlanes(): boolean;
  setPlane(p: Plane): void;
  setLayer(l: number): void;
  resize(axis: number, delta: number): void;
  fillBox(clear: boolean): void;
  addColour(): void;
  editColour(id: string, rgb: string): void;
  /** The shape's own color, which Default draws in (shape.setColour). */
  setShapeColour(shape: string, rgb: string): void;
  deleteColour(id: string): void;
  /** What the engine says of the tray shown (null: no tray, or not read yet). */
  tray(): TrayReply | null;
  /** The tray edit mode or the piece to place changed. */
  trayChanged(): void;
}

/** The tools' tooltips on a start/goal shape's Walls (the tray's cells). */
const WALL_TIP: Record<string, string> = {
  fixed: 'Floor: a cell pieces may stop on (fixed)',
  variable: 'Corridor: pieces may cross it but not stop on it (variable)',
  erase: 'Wall: no piece goes there (erase)',
};

/** The Tray edit switch (the original's ToolTab_Sliding labels and tooltips). */
const TRAY_MODES: readonly { id: TrayMode; label: string; tip: string }[] = [
  { id: 'walls', label: 'Walls', tip: 'Toggle floor and wall cells on the tray' },
  { id: 'start', label: 'Start', tip: 'Place or remove a piece’s starting position on the tray' },
  { id: 'goal', label: 'Goal', tip: 'Place or clear a piece’s goal position on the tray' },
];

const EXTRA = ['#ef4444', '#06b6d4', '#ec4899', '#84cc16', '#f97316'];

/** The next colour the palette's + adds (the mockup's list). */
export function nextColour(count: number): string {
  return EXTRA[(count + 1) % EXTRA.length] ?? '#ef4444';
}

export class EditorCard {
  private readonly host: CardHost;
  /** The chosen piece's cells as last read (the flat drawing), by piece and revision. */
  private preview: { key: string; cells: [number, number][] } | null = null;
  private previewLoads = 0;

  constructor(host: CardHost) {
    this.host = host;
    this.install();
  }

  private get shape(): ShapeSummary | null {
    return this.host.store.shape;
  }

  private get colours(): Colour[] {
    return this.host.store.snapshot?.colours ?? [];
  }

  /** The drawing colour as #rrggbb (Default: the shape's own). */
  drawingHex(): string {
    const st = this.host.state;
    const c = st.colour === null ? undefined : this.colours.find((x) => x.id === st.colour);
    if (c !== undefined) {
      return c.rgb;
    }
    const s = this.shape;
    return hex(shapeColour(s?.colour ?? null, s === null ? 0 : this.host.store.indexOf(s.id)));
  }

  private get sliding(): boolean {
    return this.host.store.snapshot?.grid === 'sliding';
  }

  /** Whether the shape shown is a sliding start/goal shape. */
  private get onTray(): boolean {
    return this.sliding && isTray(this.shape);
  }

  /** Whether a click places a start or goal (the Start/Goal tab). */
  get stamping(): boolean {
    return this.onTray && this.host.state.tray !== 'walls';
  }

  /** Draws everything of the card but the grid. */
  render(): void {
    this.renderTray();
    this.renderTools();
    this.renderMod();
    this.renderPlane();
    this.renderLayers();
    this.renderSelbar();
    this.renderSize();
    this.renderColours();
  }

  renderTools(): void {
    const st = this.host.state;
    const stamping = this.stamping;
    const tray = this.onTray;
    // no paint on a sliding puzzle: a cell's colour is a start mark there
    const tools = this.sliding ? TOOLS.filter((t) => t.id !== 'paint') : TOOLS;
    for (const id of ['toolStrip', 'modRow', 'boxSeg']) {
      byId(id).style.display = stamping ? 'none' : '';
    }
    byId('toolStrip').innerHTML = tools
      .map(
        (t) =>
          `<button class="gbtn ${st.tool === t.id && !st.boxMode ? 'on' : ''}" data-t="${t.id}" title="${tray ? (WALL_TIP[t.id] ?? t.tip) : t.tip}  (${t.k})" aria-pressed="${st.tool === t.id && !st.boxMode}">${G.tool(t.id, 30)}<span>${t.short}</span><i class="kb">${t.k}</i></button>`,
      )
      .join('');
    byId('boxSeg').innerHTML =
      `<button class="ib ${st.boxMode ? 'on' : ''}" data-box="1" title="Box selection — drag a rectangle in the grid, then Fill or Clear it (Esc deselects)" aria-pressed="${st.boxMode}">${ic('selbox', 18)}</button>`;
  }

  renderMod(): void {
    const st = this.host.state;
    const grp = (
      k: 'mirror' | 'span',
      label: string,
      tip: (a: string) => string,
      gf: (a: Axis, sz: number) => string,
    ): string =>
      `<div class="mg"><div class="gl2">${label}</div><div class="grp" data-g="${k}">` +
      (['x', 'y', 'z'] as const)
        .map((a) => {
          const on = st[k].has(a);
          return `<button class="gbtn ${on ? 'on' : ''}" data-a="${a}" title="${tip(a.toUpperCase())}" aria-pressed="${on}">${gf(a, 28)}<span class="ax-${a}">${a.toUpperCase()}</span></button>`;
        })
        .join('') +
      '</div></div>';
    byId('modRow').innerHTML =
      grp(
        'mirror',
        'Mirror',
        (a) => `Also place a mirrored voxel across the ${a} centre plane`,
        (a, sz) => G.mirror(a, sz),
      ) +
      grp(
        'span',
        'Span',
        (a) => `Fill the whole grid along ${a} on each click`,
        (a, sz) => G.span(a, sz),
      );
  }

  renderPlane(): void {
    const st = this.host.state;
    const all = this.host.allPlanes();
    byId('planeSeg').innerHTML = (['XY', 'XZ', 'YZ'] as const)
      .map((p) => {
        const off = !all && p !== 'XY';
        return `<button class="gbtn ${st.plane === p ? 'on' : ''}" data-p="${p}" ${off ? 'disabled' : ''} title="${off ? 'Only the XY layer plane on this grid (as the original’s grid editor)' : `Edit the ${p} plane`}" aria-pressed="${st.plane === p}">${G.plane(p, 30)}<span>${p}</span></button>`;
      })
      .join('');
  }

  renderLayers(): void {
    const st = this.host.state;
    const p = this.host.plane();
    const el = byId('lstrip');
    if (p === null) {
      el.innerHTML = '';
      byId('layerLbl').textContent = '';
      return;
    }
    const has = this.host.layersWithCells();
    let h = `<div class="lh" style="color:var(--a${p.la.toLowerCase()})">${p.la} layer</div>`;
    for (let l = p.layers - 1; l >= 0; l--) {
      h += `<button class="lchip ${l === st.layer ? 'on' : ''} ${has.has(l) ? 'has' : ''}" data-l="${l}" title="Layer ${l + 1}">${l + 1}</button>`;
    }
    el.innerHTML = h;
    byId('layerLbl').textContent = `Layer ${st.layer + 1} of ${p.layers}`;
    el.querySelector('.lchip.on')?.scrollIntoView({ block: 'nearest' });
  }

  /** The Tray edit switch and, while a start or goal is placed, the piece list and preview. */
  renderTray(): void {
    const st = this.host.state;
    const row = byId('trayRow');
    const pick = byId('stampRow');
    const onTray = this.onTray;
    row.classList.toggle('show', onTray);
    pick.classList.toggle('show', this.stamping);
    if (!onTray) {
      row.innerHTML = '';
      pick.innerHTML = '';
      return;
    }
    // Start and Goal take the validity's colour (the Start/Goal tab's)
    const t = this.host.tray();
    const v = t === null ? null : validity(t.error).valid;
    row.innerHTML =
      `<span class="gl2">Tray edit</span><div class="seg" role="radiogroup" aria-label="Tray edit">` +
      TRAY_MODES.map(
        (m) =>
          `<button data-tm="${m.id}" class="${st.tray === m.id ? 'on' : ''} ${m.id !== 'walls' && v !== null ? (v ? 'ok' : 'bad') : ''}" role="radio" aria-checked="${String(st.tray === m.id)}" title="${m.tip}">${m.label}</button>`,
      ).join('') +
      '</div>';
    if (!this.stamping) {
      pick.innerHTML = '';
      return;
    }
    const snap = this.host.store.snapshot;
    const pieces = stampPieces(snap?.shapes ?? []);
    const chosen = pieces.find((p) => p.id === st.stampPiece) ?? null;
    pick.innerHTML = `<div class="sglist" role="listbox" aria-label="Pieces" title="Select the piece whose start or goal to place">${
      pieces.length === 0
        ? '<div class="mute">No pieces yet: add shapes with + New</div>'
        : pieces
            .map((p) => {
              const i = this.host.store.indexOf(p.id);
              const c = shapeColour(p.colour, i);
              const on = p.id === st.stampPiece;
              return `<button class="sgp ${on ? 'on' : ''}" data-sp="${p.id}" role="option" aria-selected="${String(on)}" title="${esc(pieceText(p))}"><span class="chip" style="background:${hex(c)};color:${contrast(c)}">${esc(p.label)}</span><span class="lbl">${esc(p.name)}</span></button>`;
            })
            .join('')
    }</div><div class="sgprev" id="sgPrev" title="The piece chosen in the list">${this.previewSvg(chosen)}</div>`;
  }

  // the chosen piece drawn flat, its cells in its colour (slidePiecePreview_c)
  private previewSvg(p: ShapeSummary | null): string {
    if (p === null) {
      return '';
    }
    const store = this.host.store;
    const k = `${String(store.generation)}:${p.id}:${String(store.snapshot?.revision ?? 0)}`;
    const have = this.preview;
    if (have?.key !== k) {
      const n = ++this.previewLoads;
      store.engine.call('shape.get', { shape: p.id }, { coalesce: 'stamp-preview' }).then(
        (r) => {
          if (n === this.previewLoads) {
            this.preview = { key: k, cells: r.cells.map((c) => [c.at[0], c.at[1]]) };
            const el = document.getElementById('sgPrev');
            if (el !== null) {
              el.innerHTML = this.previewSvg(p);
            }
          }
        },
        (e: unknown) => {
          if (!(e instanceof EngineError) || e.errc !== 'superseded') {
            console.warn('the piece could not be drawn', e);
          }
        },
      );
      if (!have?.key.startsWith(`${String(store.generation)}:${p.id}:`)) {
        return '';
      }
    }
    const cells = this.preview?.cells ?? [];
    if (cells.length === 0) {
      return '';
    }
    const xs = cells.map((c) => c[0]);
    const ys = cells.map((c) => c[1]);
    const x0 = Math.min(...xs);
    const y0 = Math.min(...ys);
    const w = Math.max(...xs) - x0 + 1;
    const h = Math.max(...ys) - y0 + 1;
    const c = shapeColour(p.colour, store.indexOf(p.id));
    const fill = hex(c);
    // edges that show on dark pieces as on light ones
    const edge = contrast(c) === '#ffffff' ? 'rgba(255,255,255,.65)' : 'rgba(0,0,0,.55)';
    // y rises upwards, as in the grid editor
    const rects = cells
      .map(
        ([x, y]) =>
          `<rect x="${String(x - x0)}" y="${String(h - 1 - (y - y0))}" width="1" height="1" fill="${fill}" stroke="${edge}" stroke-width="0.06"/>`,
      )
      .join('');
    return `<svg viewBox="-0.15 -0.15 ${String(w + 0.3)} ${String(h + 0.3)}" preserveAspectRatio="xMidYMid meet" data-cells="${String(cells.length)}">${rects}</svg>`;
  }

  renderSelbar(): void {
    const n = Number(byId('garea').querySelector('svg')?.dataset.selected ?? '0');
    const box = this.host.state.box;
    const b = byId('selbar');
    b.classList.toggle('show', box !== null);
    b.innerHTML = `<span><b>${n}</b> cell${n === 1 ? '' : 's'} selected</span><span class="sep"></span>
  <button class="btn sm" data-x="fill" title="Fill the box with fixed voxels in the drawing color (mirror and span apply)">Fill</button><button class="btn sm" data-x="clear" title="Erase the voxels of the box (mirror and span apply)">Clear</button><button class="ib" data-x="x" title="Deselect (Esc)">${ic('close', 14)}</button>`;
  }

  renderSize(): void {
    const s = this.shape;
    const st = this.host.state;
    const u = this.host.unit();
    byId('sizeSteppers').innerHTML =
      s === null
        ? ''
        : (['X', 'Y', 'Z'] as const)
            .map((a, i) => {
              const v = Math.ceil((s.size[i] ?? 1) / u);
              // sliding shapes are one layer deep (the original's lockZ)
              const flat = this.sliding && i === 2;
              const tip = (what: string): string =>
                flat
                  ? 'Sliding shapes are one layer deep'
                  : `${what} ${a}${u > 1 ? ` (blocks of ${String(u)} cells)` : ''}`;
              return `<div class="step"><b class="ax-${a.toLowerCase()}">${a}</b><button data-d="${i}" data-s="-1" title="${tip('Decrease')}" ${v <= 1 || flat ? 'disabled' : ''}>${ic('minus', 12)}</button><span class="v" data-v="${i}">${v}</span><button data-d="${i}" data-s="1" title="${tip('Increase')}" ${flat ? 'disabled' : ''}>${ic('plus', 12)}</button></div>`;
            })
            .join('');
    const sw = byId('applyAll');
    sw.classList.toggle('on', st.applyAll);
    sw.setAttribute('aria-checked', String(st.applyAll));
  }

  renderColours(): void {
    const st = this.host.state;
    // a sliding puzzle has no constraint colours (the original hid the group)
    byId('colFoot').style.display = this.sliding ? 'none' : '';
    const cols = this.colours;
    const s = this.shape;
    const own = hex(shapeColour(s?.colour ?? null, s === null ? 0 : this.host.store.indexOf(s.id)));
    const chex = this.drawingHex();
    const cur = st.colour === null ? undefined : cols.find((c) => c.id === st.colour);
    const name = cur?.label ?? 'Default';
    byId('colFoot').innerHTML =
      `<div class="bigsw" style="background:${chex}"></div><div class="colmeta"><small>Drawing color</small><b>${esc(name)}</b></div>
  <div class="pal"><button class="sw ${st.colour === null ? 'on' : ''} def" data-c="" style="background:${own}" title="Default — the piece’s own color"></button>${cols
    .map(
      (c) =>
        `<button class="sw ${c.id === st.colour ? 'on' : ''}" data-c="${c.id}" style="background:${c.rgb}" title="${esc(c.label)}"></button>`,
    )
    .join('')}
  <button class="ib" id="cAdd" title="Add color">${ic('plus')}</button><button class="ib" id="cMore" title="Edit / remove the current color">${ic('more')}</button></div>
  <input type="color" id="cPick" tabindex="-1" aria-hidden="true" style="position:absolute;opacity:0;pointer-events:none;width:0;height:0">`;
    byId('rrail').innerHTML =
      `<button class="ib" id="btnRE" title="Expand Voxel editor">${ic('panelR')}</button><div class="bigsw" style="background:${chex};width:28px;height:28px;border-radius:8px"></div><span class="vt2">Voxel editor</span>`;
  }

  private colourMenu(): string {
    const def = this.host.state.colour === null;
    return `<button data-k="edit" ${def && this.shape === null ? 'disabled' : ''} title="${def ? 'Change this shape’s own color, which Default draws in' : 'Change the current drawing color'}">Edit color…</button><button data-k="rm" ${def ? 'disabled' : ''} title="Remove the current drawing color from the puzzle">Remove color</button>`;
  }

  private colourAction(k: string): void {
    const id = this.host.state.colour;
    if (id === null) {
      // Default is the shape's own color: edit that
      const s = this.shape;
      if (k === 'edit' && s !== null) {
        const pick = byId<HTMLInputElement>('cPick');
        pick.value = hex(shapeColour(s.colour, this.host.store.indexOf(s.id)));
        pick.click();
      }
      return;
    }
    if (k === 'edit') {
      const pick = byId<HTMLInputElement>('cPick');
      pick.value = this.colours.find((c) => c.id === id)?.rgb ?? '#000000';
      pick.click();
    } else if (k === 'rm') {
      this.host.deleteColour(id);
    }
  }

  private install(): void {
    const st = this.host.state;
    byId('toolStrip').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-t]');
      if (b !== null) {
        st.tool = b.dataset.t as Tool;
        st.boxMode = false;
        this.host.changed();
      }
    });
    byId('trayRow').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-tm]');
      if (b !== null) {
        st.tray = b.dataset.tm as TrayMode;
        st.boxMode = false;
        st.box = null;
        this.host.trayChanged();
      }
    });
    byId('stampRow').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-sp]');
      if (b?.dataset.sp !== undefined) {
        st.stampPiece = b.dataset.sp;
        this.host.trayChanged();
      }
    });
    byId('boxSeg').addEventListener('click', (e) => {
      if ((e.target as Element).closest('[data-box]') !== null) {
        st.boxMode = !st.boxMode;
        this.host.changed();
      }
    });
    byId('modRow').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('button[data-a]');
      const k = b?.parentElement?.dataset.g;
      if (b === null || (k !== 'mirror' && k !== 'span')) {
        return;
      }
      const a = b.dataset.a as Axis;
      if (st[k].has(a)) {
        st[k].delete(a);
      } else {
        st[k].add(a);
      }
      this.host.changed();
    });
    byId('planeSeg').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-p]');
      if (b !== null && !b.disabled) {
        this.host.setPlane(b.dataset.p as Plane);
      }
    });
    byId('lstrip').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('.lchip');
      if (b !== null) {
        this.host.setLayer(Number(b.dataset.l));
      }
    });
    byId('selbar').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('[data-x]');
      if (b === null) {
        return;
      }
      if (b.dataset.x === 'x') {
        st.box = null;
        this.host.changed();
      } else {
        this.host.fillBox(b.dataset.x === 'clear');
      }
    });
    byId('sizeSteppers').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-d]');
      if (b !== null && !b.disabled) {
        this.host.resize(Number(b.dataset.d), Number(b.dataset.s));
      }
    });
    const toggleAll = (): void => {
      st.applyAll = !st.applyAll;
      this.host.changed();
    };
    byId('applyAll').addEventListener('click', toggleAll);
    byId('applyAll').addEventListener('keydown', (e) => {
      if (e.key === ' ' || e.key === 'Enter') {
        e.preventDefault();
        toggleAll();
      }
    });
    const foot = byId('colFoot');
    foot.addEventListener('click', (e) => {
      const t = e.target as Element;
      const sw = t.closest<HTMLElement>('.sw');
      if (sw !== null) {
        st.colour = sw.dataset.c === '' || sw.dataset.c === undefined ? null : sw.dataset.c;
        this.host.changed();
      } else if (t.closest('#cAdd') !== null) {
        this.host.addColour();
      } else if (t.closest('#cMore') !== null) {
        openPop(byId('cMore'), this.colourMenu(), (k) => {
          this.colourAction(k);
        });
      }
    });
    // the colour picker edits the drawing colour (the system's dialog says when)
    foot.addEventListener('change', (e) => {
      const pick = e.target as HTMLInputElement;
      const id = st.colour;
      if (pick.id !== 'cPick') {
        return;
      }
      if (id !== null) {
        this.host.editColour(id, pick.value.toLowerCase());
      } else if (this.shape !== null) {
        this.host.setShapeColour(this.shape.id, pick.value.toLowerCase());
      }
    });
    foot.addEventListener('contextmenu', (e) => {
      const sw = (e.target as Element).closest<HTMLElement>('.sw');
      if (sw === null) {
        return;
      }
      e.preventDefault();
      st.colour = sw.dataset.c === '' || sw.dataset.c === undefined ? null : sw.dataset.c;
      this.host.changed();
      openPopAt(e.clientX, e.clientY, this.colourMenu(), (k) => {
        this.colourAction(k);
      });
    });
    byId('rrail').addEventListener('click', (e) => {
      if ((e.target as Element).closest('#btnRE') !== null) {
        layout.focus = '';
        layout.rc = false;
        applyLayout();
      }
    });
    byId('btnF2D').addEventListener('click', () => {
      layout.focus = layout.focus === '2d' ? '' : '2d';
      applyLayout();
    });
    byId('btnF2D').innerHTML = ic('focus');
  }
}
