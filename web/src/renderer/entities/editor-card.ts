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

import type { Colour, ShapeSummary } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import type { Tool } from '../shell/cursors.ts';
import { byId, esc } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { applyLayout, layout } from '../shell/layout.ts';
import { hex, shapeColour } from '../shell/piece-colours.ts';
import { openPop, openPopAt } from '../shell/popover.ts';
import type { Axis, EditorState, PlaneMap } from './editor-state.ts';
import { TOOLS } from './editor-state.ts';
import { G } from './glyphs.ts';
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
  deleteColour(id: string): void;
}

const EXTRA = ['#ef4444', '#06b6d4', '#ec4899', '#84cc16', '#f97316'];

/** The next colour the palette's + adds (the mockup's list). */
export function nextColour(count: number): string {
  return EXTRA[(count + 1) % EXTRA.length] ?? '#ef4444';
}

export class EditorCard {
  private readonly host: CardHost;

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

  /** Draws everything of the card but the grid. */
  render(): void {
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
    byId('toolStrip').innerHTML = TOOLS.map(
      (t) =>
        `<button class="gbtn ${st.tool === t.id && !st.boxMode ? 'on' : ''}" data-t="${t.id}" title="${t.tip}  (${t.k})" aria-pressed="${st.tool === t.id && !st.boxMode}">${G.tool(t.id, 30)}<span>${t.short}</span><i class="kb">${t.k}</i></button>`,
    ).join('');
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

  renderSelbar(): void {
    const n = Number(byId('garea').querySelector('svg')?.dataset.selected ?? '0');
    const box = this.host.state.box;
    const b = byId('selbar');
    b.classList.toggle('show', box !== null);
    b.innerHTML = `<span><b>${n}</b> cell${n === 1 ? '' : 's'} selected</span><span class="sep"></span>
  <button class="btn sm" data-x="fill" title="Fill the box with fixed voxels in the drawing colour (mirror and span apply)">Fill</button><button class="btn sm" data-x="clear" title="Erase the voxels of the box (mirror and span apply)">Clear</button><button class="ib" data-x="x" title="Deselect (Esc)">${ic('close', 14)}</button>`;
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
              return `<div class="step"><b class="ax-${a.toLowerCase()}">${a}</b><button data-d="${i}" data-s="-1" title="Decrease ${a}${u > 1 ? ` (blocks of ${u} cells)` : ''}" ${v <= 1 ? 'disabled' : ''}>${ic('minus', 12)}</button><span class="v" data-v="${i}">${v}</span><button data-d="${i}" data-s="1" title="Increase ${a}${u > 1 ? ` (blocks of ${u} cells)` : ''}">${ic('plus', 12)}</button></div>`;
            })
            .join('');
    const sw = byId('applyAll');
    sw.classList.toggle('on', st.applyAll);
    sw.setAttribute('aria-checked', String(st.applyAll));
  }

  renderColours(): void {
    const st = this.host.state;
    const cols = this.colours;
    const s = this.shape;
    const own = hex(shapeColour(s?.colour ?? null, s === null ? 0 : this.host.store.indexOf(s.id)));
    const chex = this.drawingHex();
    const cur = st.colour === null ? undefined : cols.find((c) => c.id === st.colour);
    const name = cur?.label ?? 'Default';
    byId('colFoot').innerHTML =
      `<div class="bigsw" style="background:${chex}"></div><div class="colmeta"><small>Drawing colour</small><b>${esc(name)}</b></div>
  <div class="pal"><button class="sw ${st.colour === null ? 'on' : ''} def" data-c="" style="background:${own}" title="Default — the piece’s own colour"></button>${cols
    .map(
      (c) =>
        `<button class="sw ${c.id === st.colour ? 'on' : ''}" data-c="${c.id}" style="background:${c.rgb}" title="${esc(c.label)}"></button>`,
    )
    .join('')}
  <button class="ib" id="cAdd" title="Add colour">${ic('plus')}</button><button class="ib" id="cMore" title="Edit / remove the current colour">${ic('more')}</button></div>
  <input type="color" id="cPick" tabindex="-1" aria-hidden="true" style="position:absolute;opacity:0;pointer-events:none;width:0;height:0">`;
    byId('rrail').innerHTML =
      `<button class="ib" id="btnRE" title="Expand Voxel editor">${ic('panelR')}</button><div class="bigsw" style="background:${chex};width:28px;height:28px;border-radius:8px"></div><span class="vt2">Voxel editor</span>`;
  }

  private colourMenu(): string {
    const off = this.host.state.colour === null ? 'disabled' : '';
    return `<button data-k="edit" ${off}>Edit colour…</button><button data-k="rm" ${off}>Remove colour</button>`;
  }

  private colourAction(k: string): void {
    const id = this.host.state.colour;
    if (id === null) {
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
      if (pick.id === 'cPick' && id !== null) {
        this.host.editColour(id, pick.value.toLowerCase());
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
