// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Entities workspace's right card around the grid: the tool strip
// (Fixed, Variable or Erase, one always chosen; its last button switches
// between drawing and the box selection, whose Fill puts the chosen tool
// in the box: it reads Select while drawing, Paint while selecting), span,
// then mirror and the planes behind the Mirror & Plane button (hidden
// again by the < after the planes), the layer
// strip (marks on the layers that hold cells; PgUp and PgDn), the box
// selection's bar (Fill, Clear, deselect; a strip under the grid, beside
// the layers, while selecting; Fill left out under Erase), the grid size steppers in the
// grid's units with "Apply to all shapes", the palette (Default is the
// shape's own color; add, edit, remove, and the Paint tool's brush while a
// constraint color is chosen; the constraint colors on rows below, one row
// shown at a time), the collapsed card's rail, and once a document the
// prompt under the focus button when the grid is very large.
// From docs/mockup/index.html: `renderTools`, `renderMod`, `renderPlane`,
// `renderLayers`, `renderSelbar`, `renderInsp`'s size steppers and the
// applyAll switch, `renderColors` and their handlers. The engine changes
// the shape; the card shows the editor's state and the engine's answers.
// On a sliding puzzle (docs/webui-spec.md, section 8.1) the palette has
// the restriction colors as on the cube grid; a piece (not a board) may
// also show an image, chosen with the button beside the pencil (decision
// 0038). The shapes are
// edited in the XY plane, layer by layer, and a board (a start/goal shape)
// is drawn with the ordinary tools, which say what its cells are (floor,
// corridor, wall); its starts and goals are placed on the Puzzle
// workspace (puzzle/slide-card.ts).

import type { Color, ShapeSummary } from '../../engine/api.generated.ts';
import type { Store } from '../state/store.ts';
import { byId, esc, forgetHtml, setHtml } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import { keyLabel } from '../shell/keys.ts';
import { applyLayout, layout, onLayout, tab } from '../shell/layout.ts';
import { chipColor, contrast, fromHex, hex, isTray } from '../shell/piece-colors.ts';
import type { Rgb3 } from '../shell/piece-colors.ts';
import { flash } from '../shell/status.ts';
import { stepperHtml, wireSteppers } from '../shell/widgets.ts';
import { openColorPicker } from '../shell/color-picker.ts';
import { openPopAt } from '../shell/popover.ts';
import type { Axis, EditorState, PlaneMap, VoxelTool } from './editor-state.ts';
import { TOOLS } from './editor-state.ts';
import { G } from './glyphs.ts';
import type { Plane } from '../view3d/overlays.ts';

/** Sets an element's text only when it changed. */
function setText(el: HTMLElement, text: string): void {
  if (el.textContent !== text) {
    el.textContent = text;
  }
}

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
  addColor(): void;
  editColor(id: string, rgb: string): void;
  /** The shape's own color, which Default draws in (shape.setColor; null: back to the default one). */
  setShapeColor(shape: string, rgb: string | null): void;
  /** The Apply image dialog for the shape shown (a sliding puzzle's piece, decision 0038). */
  editImage(): void;
  /** Takes the shape's image off (shape.clearImage). */
  clearImage(shape: string): void;
  deleteColor(id: string): void;
}

/** The tools' tooltips on a board (its cells are floor, corridors and walls). */
/** A shape wider or taller than this (in the grid's units) is offered the focus. */
const BIG_GRID = 18;

const WALL_TIP: Record<string, string> = {
  fixed: 'Floor: a cell pieces may stop on (fixed)',
  variable: 'Corridor: pieces may cross it but not stop on it (variable)',
  erase: 'Wall: no piece goes there (erase)',
};

// the colors + may add: hues round the wheel at three lightnesses, two saturations
const CANDIDATES: readonly Rgb3[] = [0.5, 0.38, 0.62].flatMap((l) =>
  [0.9, 0.6].flatMap((sat) => Array.from({ length: 24 }, (_, i) => hsl(i * 15, sat, l))),
);

function hsl(h: number, s: number, l: number): Rgb3 {
  const k = (n: number): number => (n + h / 30) % 12;
  const a = s * Math.min(l, 1 - l);
  const f = (n: number): number => l - a * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1));
  return [f(0), f(8), f(4)];
}

// how far apart two colors look ("redmean": RGB weighted by the red level)
function distance(a: Rgb3, b: Rgb3): number {
  const r = (a[0] + b[0]) / 2;
  const [dr, dg, db] = [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  return (2 + r) * dr * dr + 4 * dg * dg + (3 - r) * db * db;
}

/** The next color the palette's + adds: the one least like the colors already there. */
export function nextColor(existing: readonly string[]): string {
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
  /** The palette's color row shown (one at a time; the arrows step through them). */
  private palRow = 0;
  /** The drawing color and document the palette was last drawn for (a new choice shows its row). */
  /** Whether the Mirror and Plane icons are shown (by default only Span's). */
  private mpOpen = false;
  /** The document generation the large-grid prompt was shown for (-1: none). */
  private bigTipFor = -1;
  private palSeen: { color: string | null; generation: number } | null = null;
  /** The layer strip's plane, layer count and layer last scrolled to. */
  private layerShown = '';
  /** What the Mirror and Span row last drew (its layer label kept out of it). */
  private modHtml = '';

  constructor(host: CardHost) {
    this.host = host;
    this.install();
  }

  private get shape(): ShapeSummary | null {
    return this.host.store.shape;
  }

  // none where the puzzle type has no constraint colors
  private get colors(): Color[] {
    return this.ownOnly ? [] : (this.host.store.snapshot?.colors ?? []);
  }

  /** Whether the shape shown may take an image: a sliding puzzle's piece or board, with Default (its own color) chosen. */
  private get imageable(): boolean {
    return (
      this.host.store.snapshot?.grid === 'sliding' &&
      this.shape !== null &&
      this.host.state.color === null
    );
  }

  /** "piece" or "board", for the image button's tips. */
  private get imageNoun(): string {
    return isTray(this.shape) ? 'board' : 'piece';
  }

  /** Whether the puzzle type has no constraint colors (stacking): the palette is the shape's own. */
  private get ownOnly(): boolean {
    return this.host.store.snapshot?.grid === 'stacking';
  }

  /** The drawing color as #rrggbb (Default: the shape's own). */
  drawingHex(): string {
    return this.colorHex(this.host.state.color);
  }

  /** The shape's own color as #rrggbb (a board's is the color of its swatch in the list). */
  private ownHex(): string {
    const s = this.shape;
    return hex(chipColor(s ?? undefined, s === null ? 0 : this.host.store.indexOf(s.id)));
  }

  /** A constraint color's #rrggbb (null, or one gone: the shape's own). */
  colorHex(color: string | null): string {
    const c = color === null ? undefined : this.colors.find((x) => x.id === color);
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
    this.renderColors();
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
    setHtml(
      byId('toolStrip'),
      tools
        .map(
          (t) =>
            `<button class="gbtn ${st.voxel === t.id ? 'on' : ''}" data-t="${t.id}" title="${tray ? (WALL_TIP[t.id] ?? t.tip) : t.tip}  (${keyLabel(t.action)})" aria-pressed="${st.voxel === t.id}">${G.tool(t.id, 30)}<span>${t.short}</span><i class="kb">${keyLabel(t.action)}</i></button>`,
        )
        .join('') + toggle,
    );
  }

  renderMod(): void {
    const st = this.host.state;
    const grp = (
      k: 'mirror' | 'span',
      tip: (a: string) => string,
      gf: (a: Axis, sz: number) => string,
    ): string =>
      `<div class="grp" data-g="${k}">` +
      (['x', 'y', 'z'] as const)
        .map((a) => {
          const on = st[k].has(a);
          return `<button class="gbtn ${on ? 'on' : ''}" data-a="${a}" title="${tip(a.toUpperCase())}" aria-pressed="${on}">${gf(a, 28)}<span class="ax-${a}">${a.toUpperCase()}</span></button>`;
        })
        .join('') +
      '</div>';
    const mirror = grp(
      'mirror',
      (a) => `Mirror: also place a mirrored voxel across the ${a} center plane`,
      (a, sz) => G.mirror(a, sz),
    );
    const span = grp(
      'span',
      (a) => `Span: fill the whole grid along ${a} on each click`,
      (a, sz) => G.span(a, sz),
    );
    // the layer label moves between the rows: kept out of the HTML compared,
    // and put back where it belongs when the row was drawn again
    const lbl = byId('layerLbl');
    const row = byId('modRow');
    row.classList.toggle('short', !this.mpOpen);
    // Span alone, then the button that shows Mirror and the planes (marked while either is in use)
    const used = st.mirror.size > 0 || st.plane !== 'XY';
    const html = this.mpOpen
      ? mirror + span
      : span +
        `<div class="mpr"><button class="gbtn mpbtn ${used ? 'used' : ''}" data-mp="1" title="Show the Mirror and Plane icons${used ? ' (a mirror or another plane is in use)' : ''}" aria-expanded="false"><b>Mirror<br>&amp; Plane</b></button></div>`;
    if (html !== this.modHtml || !lbl.isConnected) {
      lbl.remove();
      row.innerHTML = html;
      this.modHtml = html;
      (this.mpOpen ? byId('planeSub') : row.querySelector('.mpr'))?.append(lbl);
    }
    byId('planeSub').classList.toggle('off', !this.mpOpen);
  }

  renderPlane(): void {
    const st = this.host.state;
    const all = this.host.allPlanes();
    setHtml(
      byId('planeSeg'),
      (['XY', 'XZ', 'YZ'] as const)
        .map((p) => {
          const off = !all && p !== 'XY';
          return `<button class="gbtn ${st.plane === p ? 'on' : ''}" data-p="${p}" ${off ? 'disabled' : ''} title="${off ? 'Only the XY layer plane on this grid (as the original’s grid editor)' : `Edit the ${p} plane`}" aria-pressed="${st.plane === p}">${G.plane(p, 30)}<span>${p}</span></button>`;
        })
        .join('') +
        '<button class="gbtn mpx" data-mp="0" title="Hide Mirror and Plane icons" aria-expanded="true"><b>&lt;</b></button>',
    );
  }

  renderLayers(): void {
    const st = this.host.state;
    const p = this.host.plane();
    const el = byId('lstrip');
    if (p === null) {
      setHtml(el, '');
      setText(byId('layerLbl'), '');
      this.layerShown = '';
      return;
    }
    const has = this.host.layersWithCells();
    // the label stays at the top; only the numbers scroll
    let h = `<div class="lh" style="color:var(--a${p.la.toLowerCase()})">${p.la} layer</div><div class="lchips">`;
    for (let l = p.layers - 1; l >= 0; l--) {
      h += `<button class="lchip ${l === st.layer ? 'on' : ''} ${has.has(l) ? 'has' : ''}" data-l="${l}" title="Layer ${l + 1}">${l + 1}</button>`;
    }
    const drawn = setHtml(el, h + '</div>');
    setText(byId('layerLbl'), `Layer ${st.layer + 1} of ${p.layers}`);
    // scrolled to only when the layer chosen changed (scrollIntoView lays the page out)
    const shown = `${p.la}:${String(p.layers)}:${String(st.layer)}`;
    if (drawn && shown !== this.layerShown) {
      this.layerShown = shown;
      el.querySelector('.lchip.on')?.scrollIntoView({ block: 'nearest' });
    }
  }

  renderSelbar(): void {
    const st = this.host.state;
    const box = st.box;
    const n =
      box === null ? 0 : Number(byId('garea').querySelector('svg')?.dataset.selected ?? '0');
    const b = byId('selbar');
    // a strip of its own while selecting, so it never covers the grid
    b.classList.toggle('show', st.boxMode);
    const off = n === 0 ? 'disabled' : '';
    // under Erase, Fill is what Clear does: only Clear is offered
    const fill =
      st.voxel === 'erase'
        ? ''
        : `<button class="btn sm" data-x="fill" title="Fill the box with ${st.voxel} voxels in the drawing color (mirror and span apply)" ${off}>Fill</button>`;
    setHtml(
      b,
      `<span><b>${n}</b> cell${n === 1 ? '' : 's'} selected</span><span class="sp"></span>
  ${fill}<button class="btn sm" data-x="clear" title="Erase the voxels of the box (mirror and span apply)" ${off}>Clear</button><button class="ib" data-x="x" title="Deselect (Esc)" ${box === null ? 'disabled' : ''}>${ic('close', 14)}</button>`,
    );
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
              // a size typed below 1 is refused: the field shows the size again
              return stepperHtml({
                key: String(i),
                value: v,
                min: 1,
                less: tip('Decrease'),
                more: tip('Increase'),
                label: `Size ${a}`,
                tip: tip('Type the size'),
                axis: a,
                strict: true,
              });
            })
            .join(''),
    );
    // a grid of 20 or more cells either way is small to work in: the focus makes it larger
    const p = this.host.plane();
    byId('gridTip').hidden = p === null || (p.cols < 20 && p.rows < 20);
    const sw = byId('applyAll');
    sw.classList.toggle('on', st.applyAll);
    sw.setAttribute('aria-checked', String(st.applyAll));
    this.renderBigTip();
  }

  /**
   * The prompt under the focus button while the shape is wider or taller
   * than BIG_GRID in the grid's units: once a document (a size taken below
   * and back above it does not bring it again; opening a document does).
   */
  renderBigTip(): void {
    const tip = byId('bigTip');
    const s = this.shape;
    const u = this.host.unit();
    const big = s !== null && [0, 1].some((i) => Math.ceil((s.size[i] ?? 1) / u) > BIG_GRID);
    // the small editor on screen (not the focus, a collapsed card, stacking or another workspace)
    const room =
      big &&
      tab === 'entities' &&
      layout.focus === '' &&
      !layout.rc &&
      !byId('stage').classList.contains('stk');
    if (!tip.hidden) {
      tip.hidden = !room;
    } else if (room && this.bigTipFor !== this.host.store.generation) {
      this.bigTipFor = this.host.store.generation;
      tip.hidden = false;
    }
    if (!tip.hidden) {
      // under the button, the arrow at its middle
      const card = byId('right').getBoundingClientRect();
      const b = byId('btnF2D').getBoundingClientRect();
      const right = Math.max(8, card.right - b.right - 8);
      tip.style.top = `${String(b.bottom - card.top + 8)}px`;
      tip.style.right = `${String(right)}px`;
      tip.style.setProperty('--ar', `${String(card.right - (b.left + b.right) / 2 - right - 6)}px`);
    }
  }

  renderColors(): void {
    const st = this.host.state;
    const cols = this.colors;
    const s = this.shape;
    const own = this.ownHex();
    const chex = this.drawingHex();
    const cur = st.color === null ? undefined : cols.find((c) => c.id === st.color);
    const def = cur === undefined;
    // the shape's own color is Default until it is given one (Custom)
    const name = cur?.label ?? (s?.color == null ? 'Default' : 'Custom');
    const what = def ? 'Shape color' : 'Restriction color';
    const paint = st.tool === 'paint' && !st.boxMode;
    // stacking has no constraint colors to add or remove
    const btns =
      (this.ownOnly
        ? ''
        : `<button class="ib" id="cAdd" title="Add color">${ic('plus', 22)}</button>`) +
      `<button class="ib" id="cEdit" title="${def ? 'Change this shape’s own color' : 'Change this restriction color'}">${ic('edit', 22)}</button>` +
      (this.imageable
        ? `<button class="ib ${(s?.images.length ?? 0) === 0 ? '' : 'on'}" id="cImage" title="${(s?.images.length ?? 0) === 0 ? `Apply an image from this computer to the ${this.imageNoun}’s cells` : `Change or remove the ${this.imageNoun}’s image`}" aria-pressed="${String((s?.images.length ?? 0) > 0)}">${ic('image', 22)}</button>`
        : '') +
      (this.ownOnly
        ? ''
        : `<button class="ib" id="cRm" ${def ? 'disabled' : ''} title="${def ? 'Choose a restriction color to remove it from the puzzle' : 'Remove this restriction color from the puzzle'}">${ic('trash', 22)}</button>`) +
      // the Paint tool, with a restriction color to paint
      (def
        ? ''
        : `<button class="ib ${paint ? 'on' : ''}" id="cPaint" title="Paint: give existing voxels this restriction color  (4)" aria-pressed="${String(paint)}">${ic('brush', 22)}</button>`);
    // Default and the constraint colors on rows below, once there is one
    const rows =
      cols.length === 0
        ? ''
        : `<div class="colrow"><div class="palv" id="palView"><div class="pal"><button class="sw ${def ? 'on' : ''} def" data-c="" style="background:${own}" title="The shape’s own color (no restriction)"></button>${cols
            .map(
              (c) =>
                `<button class="sw c ${c.id === st.color ? 'on' : ''}" data-c="${c.id}" style="background:${c.rgb};color:${contrast(fromHex(c.rgb))}" title="${esc(c.label)}">${esc(c.label)}</button>`,
            )
            .join(
              '',
            )}</div></div><div class="palnav" id="palNav"><button class="ib" data-pr="-1" title="Previous row of colors">${ic('chevU', 12)}</button><button class="ib" data-pr="1" title="Next row of colors">${ic('chevD', 12)}</button></div></div>`;
    const drawn = setHtml(
      byId('colFoot'),
      `<div class="colhead"><div class="bigsw" style="background:${chex}"></div><div class="colmeta"><small>${what}</small><b>${esc(name)}</b></div><div class="colbtns">${btns}</div></div>${rows}`,
    );
    setHtml(
      byId('rrail'),
      `<button class="ib" id="btnRE" title="Expand Voxel editor">${ic('panelR')}</button><div class="bigsw" style="background:${chex};width:28px;height:28px;border-radius:8px"></div><span class="vt2">Voxel editor</span>`,
    );
    // another document starts on the first row; a color chosen (or added) shows its own
    const gen = this.host.store.generation;
    const seen = this.palSeen;
    this.palSeen = { color: st.color, generation: gen };
    if (seen?.generation !== gen) {
      this.palRow = 0;
      this.showPalRow(false);
    } else if (drawn || seen.color !== st.color) {
      // the rows are measured (a layout) only when the palette was drawn again
      this.showPalRow(seen.color !== st.color && st.color !== null);
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

  private colorMenu(): string {
    const def = this.host.state.color === null;
    const img = this.imageable
      ? `<button data-k="img" title="Apply an image from this computer to the ${this.imageNoun}’s cells">${(this.shape?.images.length ?? 0) === 0 ? 'Apply image…' : 'Change image…'}</button><button data-k="rmimg" ${(this.shape?.images.length ?? 0) === 0 ? 'disabled' : ''} title="Take the image off this ${this.imageNoun}">Remove image</button>`
      : '';
    return `<button data-k="edit" title="${def ? 'Change this shape’s own color' : 'Change this restriction color'}">Edit color…</button><button data-k="rm" ${def ? 'disabled' : ''} title="Remove this restriction color from the puzzle">Remove color</button>${img}`;
  }

  private colorAction(k: string): void {
    const id = this.host.state.color;
    if (id === null) {
      // Default is the shape's own color: edit that
      const s = this.shape;
      if (k === 'img') {
        this.host.editImage();
        return;
      }
      if (k === 'rmimg') {
        if (s !== null) {
          this.host.clearImage(s.id);
        }
        return;
      }
      if (k !== 'edit') {
        return;
      }
      if (s === null) {
        flash('Add a shape first: Default is the shape’s own color');
        return;
      }
      openColorPicker(byId('cEdit'), {
        rgb: this.ownHex(),
        onPick: (rgb) => {
          this.host.setShapeColor(s.id, rgb);
        },
        onDefault: () => {
          this.host.setShapeColor(s.id, null);
        },
        isDefault: s.color === null,
      });
      return;
    }
    if (k === 'edit') {
      openColorPicker(byId('cEdit'), {
        rgb: this.colors.find((c) => c.id === id)?.rgb ?? '#000000',
        onPick: (rgb) => {
          this.host.editColor(id, rgb);
        },
      });
    } else if (k === 'rm') {
      this.host.deleteColor(id);
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
    // the Mirror & Plane button opens them, the < after the planes hides them again
    byId('edCtl').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('button[data-mp]');
      if (b !== null) {
        this.mpOpen = b.dataset.mp === '1';
        this.renderMod();
        this.renderPlane();
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
      const b = (e.target as Element).closest<HTMLButtonElement>('button[data-x]');
      if (b === null || b.disabled) {
        return;
      }
      if (b.dataset.x === 'x') {
        st.box = null;
        this.host.changed();
      } else {
        this.host.fillBox(b.dataset.x === 'clear');
      }
    });
    // a size stepped or typed: drawn again after the resize, even one refused
    wireSteppers(byId('sizeSteppers'), {
      set: (k, v, typed) => {
        const s = this.shape;
        if (s === null) {
          return;
        }
        const axis = Number(k);
        if (typed) {
          forgetHtml(byId('sizeSteppers'));
        }
        this.host.resize(axis, v - Math.ceil((s.size[axis] ?? 1) / this.host.unit()));
      },
      restore: () => {
        forgetHtml(byId('sizeSteppers'));
        this.renderSize();
      },
    });
    const toggleAll = (): void => {
      st.applyAll = !st.applyAll;
      this.host.changed();
    };
    byId('applyAll').addEventListener('click', toggleAll);
    const foot = byId('colFoot');
    foot.addEventListener('click', (e) => {
      const t = e.target as Element;
      const sw = t.closest<HTMLElement>('.sw');
      if (sw !== null) {
        st.color = sw.dataset.c === '' || sw.dataset.c === undefined ? null : sw.dataset.c;
        this.host.changed();
      } else if (t.closest('#cAdd') !== null) {
        this.host.addColor();
      } else if (t.closest('#cEdit') !== null) {
        this.colorAction('edit');
      } else if (t.closest('#cImage') !== null) {
        this.host.editImage();
      } else if (t.closest('#cRm') !== null) {
        this.colorAction('rm');
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
      st.color = sw.dataset.c === '' || sw.dataset.c === undefined ? null : sw.dataset.c;
      this.host.changed();
      openPopAt(e.clientX, e.clientY, this.colorMenu(), (k) => {
        this.colorAction(k);
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
    byId('bigTip').addEventListener('click', (e) => {
      const b = (e.target as Element).closest<HTMLElement>('button[data-x]');
      if (b === null) {
        return;
      }
      byId('bigTip').hidden = true;
      if (b.dataset.x === 'go') {
        layout.focus = '2d';
        applyLayout();
      }
    });
    // the focus, a collapsed card or another workspace hides it (it counts as shown)
    onLayout(() => {
      this.renderBigTip();
    });
    byId('btnF2D').innerHTML = ic('focus');
  }
}
