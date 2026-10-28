// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Entities workspace's right card around the grid: the tool strip
// (Fixed, Variable or Erase, one always chosen; its last button switches
// between drawing and the box selection, whose Fill puts the chosen tool
// in the box: it reads Select while drawing, Paint while selecting), mirror and span, the planes, the layer
// strip (marks on the layers that hold cells; PgUp and PgDn), the box
// selection's bar (Fill, Clear, deselect), the grid size steppers in the
// grid's units with "Apply to all shapes", the palette (Default is the
// shape's own colour; add, edit, remove, and the Paint tool's brush while a
// constraint colour is chosen; the constraint colours on rows below, one row
// shown at a time) and the collapsed card's rail.
// From docs/mockup/index.html: `renderTools`, `renderMod`, `renderPlane`,
// `renderLayers`, `renderSelbar`, `renderInsp`'s size steppers and the
// applyAll switch, `renderColors` and their handlers. The engine changes
// the shape; the card shows the editor's state and the engine's answers.
// On a sliding puzzle (docs/webui-spec.md, section 8.1) there is no
// constraint colour, so no Paint (a cell's colour is a start mark: the
// palette keeps only the shape's own colour, as on a stacking one), the shapes are
// edited in the XY plane, layer by layer, and a board (a start/goal shape)
// is drawn with the ordinary tools, which say what its cells are (floor,
// corridor, wall); its starts and goals are placed on the Puzzle
// workspace (puzzle/slide-card.ts).

import type { Colour, ShapeSummary } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId, esc, forgetHtml, setHtml } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { applyLayout, layout } from '../shell/layout.ts';
import { chipColour, contrast, fromHex, hex, isTray } from '../shell/piece-colours.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { flash } from '../shell/status.ts';
import { openColourPicker } from '../shell/colour-picker.ts';
import { openPopAt } from '../shell/popover.ts';
import type { Axis, EditorState, PlaneMap, VoxelTool } from './editor-state.ts';
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
  /** The shape's own color, which Default draws in (shape.setColour; null: back to the default one). */
  setShapeColour(shape: string, rgb: string | null): void;
  deleteColour(id: string): void;
}

/** The tools' tooltips on a board (its cells are floor, corridors and walls). */
const WALL_TIP: Record<string, string> = {
  fixed: 'Floor: a cell pieces may stop on (fixed)',
  variable: 'Corridor: pieces may cross it but not stop on it (variable)',
  erase: 'Wall: no piece goes there (erase)',
};

// the colours + may add: hues round the wheel at three lightnesses, two saturations
const CANDIDATES: readonly Rgb3[] = [0.5, 0.38, 0.62].flatMap((l) =>
  [0.9, 0.6].flatMap((sat) => Array.from({ length: 24 }, (_, i) => hsl(i * 15, sat, l))),
);

function hsl(h: number, s: number, l: number): Rgb3 {
  const k = (n: number): number => (n + h / 30) % 12;
  const a = s * Math.min(l, 1 - l);
  const f = (n: number): number => l - a * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1));
  return [f(0), f(8), f(4)];
}

// how far apart two colours look ("redmean": RGB weighted by the red level)
function distance(a: Rgb3, b: Rgb3): number {
  const r = (a[0] + b[0]) / 2;
  const [dr, dg, db] = [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  return (2 + r) * dr * dr + 4 * dg * dg + (3 - r) * db * db;
}

/** The next colour the palette's + adds: the one least like the colours already there. */
export function nextColour(existing: readonly string[]): string {
  const have = existing.map(fromHex);
  let best = CANDIDATES[0] ?? [1, 0, 0];
  let far = -1;
  for (const c of CANDIDATES) {
    const d = Math.min(Infinity, ...have.map((h) => distance(c, h)));
    if (d > far) {
      best = c;
      far = d;
    }
  }
  return hex(best);
}

export class EditorCard {
  private readonly host: CardHost;
  /** The palette's colour row shown (one at a time; the arrows step through them). */
  private palRow = 0;
  /** The drawing colour and document the palette was last drawn for (a new choice shows its row). */
  private palSeen: { colour: string | null; generation: number } | null = null;

  constructor(host: CardHost) {
    this.host = host;
    this.install();
  }

  private get shape(): ShapeSummary | null {
    return this.host.store.shape;
  }

  // none where the puzzle type has no constraint colours
  private get colours(): Colour[] {
    return this.ownOnly ? [] : (this.host.store.snapshot?.colours ?? []);
  }

  /** Whether the puzzle type has no constraint colours (sliding, stacking): the palette is the shape's own. */
  private get ownOnly(): boolean {
    const g = this.host.store.snapshot?.grid;
    return g === 'sliding' || g === 'stacking';
  }

  /** The drawing colour as #rrggbb (Default: the shape's own). */
  drawingHex(): string {
    return this.colourHex(this.host.state.colour);
  }

  /** The shape's own colour as #rrggbb (a board's is the colour of its swatch in the list). */
  private ownHex(): string {
    const s = this.shape;
    return hex(chipColour(s ?? undefined, s === null ? 0 : this.host.store.indexOf(s.id)));
  }

  /** A constraint colour's #rrggbb (null, or one gone: the shape's own). */
  colourHex(colour: string | null): string {
    const c = colour === null ? undefined : this.colours.find((x) => x.id === colour);
    if (c !== undefined) {
      return c.rgb;
    }
    return this.ownHex();
  }

  private get sliding(): boolean {
    return this.host.store.snapshot?.grid === 'sliding';
  }

  /** Whether the shape shown is a sliding start/goal shape. */
  private get onTray(): boolean {
    return this.sliding && isTray(this.shape);
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
    const tray = this.onTray;
    // Paint is the restriction color's brush, by the palette
    const tools = TOOLS.filter((t) => t.id !== 'paint');
    // the last button names the mode in use; a press cycles to the next
    const toggle = st.boxMode
      ? `<button class="gbtn cyc" data-box="1" title="Box selection — drag a rectangle in the grid, then Fill it with the chosen tool or Clear it (Esc deselects). Click to go back to drawing" aria-pressed="true">${G.select(30)}<span>Select</span></button>`
      : `<button class="gbtn cyc" data-box="1" title="Drawing: a click or a drag in the grid uses the chosen tool. Click for box selection" aria-pressed="false">${G.tool('paint', 30)}<span>Paint</span></button>`;
    byId('toolStrip').innerHTML =
      tools
        .map(
          (t) =>
            `<button class="gbtn ${st.voxel === t.id ? 'on' : ''}" data-t="${t.id}" title="${tray ? (WALL_TIP[t.id] ?? t.tip) : t.tip}  (${t.k})" aria-pressed="${st.voxel === t.id}">${G.tool(t.id, 30)}<span>${t.short}</span><i class="kb">${t.k}</i></button>`,
        )
        .join('') + toggle;
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
    const st = this.host.state;
    const box = st.box;
    const b = byId('selbar');
    b.classList.toggle('show', box !== null);
    const fillTip =
      st.voxel === 'erase'
        ? 'Erase the voxels of the box, as Clear does: the Erase tool is chosen (mirror and span apply)'
        : `Fill the box with ${st.voxel} voxels in the drawing color (mirror and span apply)`;
    b.innerHTML = `<span><b>${n}</b> cell${n === 1 ? '' : 's'} selected</span><span class="sep"></span>
  <button class="btn sm" data-x="fill" title="${fillTip}">Fill</button><button class="btn sm" data-x="clear" title="Erase the voxels of the box (mirror and span apply)">Clear</button><button class="ib" data-x="x" title="Deselect (Esc)">${ic('close', 14)}</button>`;
  }

  renderSize(): void {
    const s = this.shape;
    const st = this.host.state;
    const u = this.host.unit();
    // setHtml: a re-render while a size is being typed keeps its field
    setHtml(
      byId('sizeSteppers'),
      s === null
        ? ''
        : (['X', 'Y', 'Z'] as const)
            .map((a, i) => {
              const v = Math.ceil((s.size[i] ?? 1) / u);
              const tip = (what: string): string =>
                `${what} ${a}${u > 1 ? ` (blocks of ${String(u)} cells)` : ''}`;
              return `<div class="step"><b class="ax-${a.toLowerCase()}">${a}</b><button data-d="${i}" data-s="-1" title="${tip('Decrease')}" ${v <= 1 ? 'disabled' : ''}>${ic('minus', 12)}</button><input class="v" data-v="${i}" value="${v}" inputmode="numeric" aria-label="Size ${a}" title="${tip('Type the size')}"><button data-d="${i}" data-s="1" title="${tip('Increase')}">${ic('plus', 12)}</button></div>`;
            })
            .join(''),
    );
    const sw = byId('applyAll');
    sw.classList.toggle('on', st.applyAll);
    sw.setAttribute('aria-checked', String(st.applyAll));
  }

  renderColours(): void {
    const st = this.host.state;
    const cols = this.colours;
    const s = this.shape;
    const own = this.ownHex();
    const chex = this.drawingHex();
    const cur = st.colour === null ? undefined : cols.find((c) => c.id === st.colour);
    const def = cur === undefined;
    // the shape's own colour is Default until it is given one (Custom)
    const name = cur?.label ?? (s?.colour == null ? 'Default' : 'Custom');
    const what = def ? 'Shape color' : 'Restriction color';
    const paint = st.tool === 'paint' && !st.boxMode;
    // sliding and stacking have no constraint colours to add or remove
    const btns =
      (this.ownOnly
        ? ''
        : `<button class="ib" id="cAdd" title="Add color">${ic('plus', 22)}</button>`) +
      `<button class="ib" id="cEdit" title="${def ? 'Change this shape’s own color' : 'Change this restriction color'}">${ic('edit', 22)}</button>` +
      (this.ownOnly
        ? ''
        : `<button class="ib" id="cRm" ${def ? 'disabled' : ''} title="${def ? 'Choose a restriction color to remove it from the puzzle' : 'Remove this restriction color from the puzzle'}">${ic('trash', 22)}</button>`) +
      // the Paint tool, with a restriction color to paint
      (def
        ? ''
        : `<button class="ib ${paint ? 'on' : ''}" id="cPaint" title="Paint: give existing voxels this restriction color  (4)" aria-pressed="${String(paint)}">${ic('brush', 22)}</button>`);
    // Default and the constraint colours on rows below, once there is one
    const rows =
      cols.length === 0
        ? ''
        : `<div class="colrow"><div class="palv" id="palView"><div class="pal"><button class="sw ${def ? 'on' : ''} def" data-c="" style="background:${own}" title="The shape’s own color (no restriction)"></button>${cols
            .map(
              (c) =>
                `<button class="sw c ${c.id === st.colour ? 'on' : ''}" data-c="${c.id}" style="background:${c.rgb};color:${contrast(fromHex(c.rgb))}" title="${esc(c.label)}">${esc(c.label)}</button>`,
            )
            .join(
              '',
            )}</div></div><div class="palnav" id="palNav"><button class="ib" data-pr="-1" title="Previous row of colors">${ic('chevU', 12)}</button><button class="ib" data-pr="1" title="Next row of colors">${ic('chevD', 12)}</button></div></div>`;
    byId('colFoot').innerHTML =
      `<div class="colhead"><div class="bigsw" style="background:${chex}"></div><div class="colmeta"><small>${what}</small><b>${esc(name)}</b></div><div class="colbtns">${btns}</div></div>${rows}`;
    byId('rrail').innerHTML =
      `<button class="ib" id="btnRE" title="Expand Voxel editor">${ic('panelR')}</button><div class="bigsw" style="background:${chex};width:28px;height:28px;border-radius:8px"></div><span class="vt2">Voxel editor</span>`;
    // another document starts on the first row; a colour chosen (or added) shows its own
    const gen = this.host.store.generation;
    const seen = this.palSeen;
    this.palSeen = { colour: st.colour, generation: gen };
    if (seen?.generation !== gen) {
      this.palRow = 0;
      this.showPalRow(false);
    } else {
      this.showPalRow(seen.colour !== st.colour && st.colour !== null);
    }
  }

  // the palette's rows are where its swatches wrap: one is scrolled into
  // view, and the arrows show once there are two
  private showPalRow(toChosen: boolean): void {
    const view = document.getElementById('palView');
    // not laid out (the card collapsed): the row stays as it was
    if (!view?.offsetParent) {
      return;
    }
    const sws = [...view.querySelectorAll<HTMLElement>('.sw')];
    const tops = [...new Set(sws.map((b) => b.offsetTop))];
    if (toChosen) {
      const on = sws.find((b) => b.classList.contains('on'));
      this.palRow = on === undefined ? this.palRow : tops.indexOf(on.offsetTop);
    }
    this.palRow = Math.max(0, Math.min(this.palRow, tops.length - 1));
    view.scrollTop = (tops[this.palRow] ?? 0) - (tops[0] ?? 0);
    const nav = byId('palNav');
    nav.classList.toggle('show', tops.length > 1);
    for (const b of nav.querySelectorAll<HTMLButtonElement>('[data-pr]')) {
      const up = b.dataset.pr === '-1';
      b.disabled = up ? this.palRow === 0 : this.palRow >= tops.length - 1;
    }
  }

  private colourMenu(): string {
    const def = this.host.state.colour === null;
    return `<button data-k="edit" title="${def ? 'Change this shape’s own color' : 'Change this restriction color'}">Edit color…</button><button data-k="rm" ${def ? 'disabled' : ''} title="Remove this restriction color from the puzzle">Remove color</button>`;
  }

  private colourAction(k: string): void {
    const id = this.host.state.colour;
    if (id === null) {
      // Default is the shape's own color: edit that
      const s = this.shape;
      if (k !== 'edit') {
        return;
      }
      if (s === null) {
        flash('Add a shape first: Default is the shape’s own color');
        return;
      }
      openColourPicker(byId('cEdit'), {
        rgb: this.ownHex(),
        onPick: (rgb) => {
          this.host.setShapeColour(s.id, rgb);
        },
        onDefault: () => {
          this.host.setShapeColour(s.id, null);
        },
        isDefault: s.colour === null,
      });
      return;
    }
    if (k === 'edit') {
      openColourPicker(byId('cEdit'), {
        rgb: this.colours.find((c) => c.id === id)?.rgb ?? '#000000',
        onPick: (rgb) => {
          this.host.editColour(id, rgb);
        },
      });
    } else if (k === 'rm') {
      this.host.deleteColour(id);
    }
  }

  private install(): void {
    const st = this.host.state;
    byId('toolStrip').addEventListener('click', (e) => {
      const t = e.target as Element;
      const b = t.closest<HTMLElement>('[data-t]');
      if (b !== null) {
        // the box selection stays: the tool is what its Fill puts in
        st.setVoxel(b.dataset.t as VoxelTool);
        this.host.changed();
      } else if (t.closest('[data-box]') !== null) {
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
    byId('sizeSteppers').addEventListener('change', (e) => {
      const i = e.target as HTMLInputElement;
      const s = this.shape;
      if (i.dataset.v === undefined || s === null) {
        return;
      }
      const axis = Number(i.dataset.v);
      const was = Math.ceil((s.size[axis] ?? 1) / this.host.unit());
      const n = Math.round(Number(i.value));
      if (Number.isFinite(n) && n >= 1 && n !== was) {
        // drawn again after the resize, even one refused
        forgetHtml(byId('sizeSteppers'));
        this.host.resize(axis, n - was);
      } else {
        // nothing to change: the field shows the size again
        forgetHtml(byId('sizeSteppers'));
        this.renderSize();
      }
    });
    byId('sizeSteppers').addEventListener('focusin', (e) => {
      (e.target as Element).closest<HTMLInputElement>('input.v')?.select();
    });
    byId('sizeSteppers').addEventListener('keydown', (e) => {
      const i = (e.target as Element).closest<HTMLInputElement>('input.v');
      if (i === null) {
        return;
      }
      if (e.key === 'Enter') {
        i.blur();
      } else if (e.key === 'Escape') {
        // back to the size shown, without resizing
        e.preventDefault();
        forgetHtml(byId('sizeSteppers'));
        this.renderSize();
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
      } else if (t.closest('#cEdit') !== null) {
        this.colourAction('edit');
      } else if (t.closest('#cRm') !== null) {
        this.colourAction('rm');
      } else if (t.closest('#cPaint') !== null) {
        // the brush toggles: off again goes back to the voxel tool
        st.tool = st.tool === 'paint' && !st.boxMode ? st.voxel : 'paint';
        st.boxMode = false;
        this.host.changed();
      } else {
        const step = t.closest<HTMLElement>('[data-pr]');
        if (step !== null) {
          this.palRow += Number(step.dataset.pr);
          this.showPalRow(false);
        }
      }
    });
    // a swatch focused on a hidden row scrolls it in: that row is the one shown
    foot.addEventListener(
      'scroll',
      (e) => {
        const view = e.target as HTMLElement;
        if (view.id !== 'palView') {
          return;
        }
        const tops = [
          ...new Set([...view.querySelectorAll<HTMLElement>('.sw')].map((b) => b.offsetTop)),
        ];
        const at = view.scrollTop + (tops[0] ?? 0);
        const row = tops.findIndex((y) => y >= at - 1);
        if (row >= 0 && row !== this.palRow) {
          this.palRow = row;
          this.showPalRow(false);
        }
      },
      true,
    );
    // a wider or narrower card wraps the swatches into other rows
    new ResizeObserver(() => {
      this.showPalRow(false);
    }).observe(foot);
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
