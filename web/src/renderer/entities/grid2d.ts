// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 2D editor's grid: ONE SVG renderer for every grid, over a layer
// drawing (layer-geometry.ts: polygons, circles on the sphere grid, the
// cell across each edge). The cursor is outlined as the original's grid
// editors outlined it (grideditor_0 to _4, drawing an edge only where the
// cell across it is not under the cursor too), the cursor being the cell
// under the mouse with its mirror and span cells of this layer, or the box
// being dragged. A drag collects every cell the pointer passes over (the
// path between two moves sampled, so a fast drag skips none) and hands
// them to the editor, which sends them as one stroke (stroke.ts).
// From docs/mockup/index.html: `renderGrid`, `spaceGridSvg` (the fit: a
// 44 px margin, 16 to 110 px a unit; the axis lines), the cube editor's
// rulers, `fillStyle` (variable cells half filled, dashed), `hover2d`,
// `clear2d`, `eff2d`, `applyCell` and the #garea pointer handlers (a
// right press erases whatever the tool; Ctrl erases, Alt or Cmd variable,
// Shift fixed). A sliding puzzle's start/goal shape (docs/webui-spec.md,
// section 8.1) is drawn as the original's grideditor.cpp drew it: dark
// tiles, and while a start or goal is placed the marked cells in their
// pieces' colours, the piece to place outlined under the mouse
// (`drawStampPreview`), a click placing it and a right click taking off
// the piece under the mouse.

import type { Grid } from '../../engine/api.generated.ts';
import { cursorFor } from '../shell/cursors.ts';
import type { Tool } from '../shell/cursors.ts';
import { esc } from '../shell/dom.ts';
import { dark, hex, light, TRAY_EDIT } from '../shell/piece-colours.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { key } from './editor-state.ts';
import { effectiveTool, modsOf } from './tools.ts';
import type { Mods } from './tools.ts';
import type { Box, Cell, EditorState, PlaneMap } from './editor-state.ts';
import type { LayerGeometry } from './layer-geometry.ts';
import type { CellValue } from './shape-cells.ts';
import type { StampOutline } from './tray.ts';

/** What the grid shows. */
export interface GridInput {
  grid: Grid;
  geo: LayerGeometry;
  plane: PlaneMap;
  layer: number;
  /** The layer's cells by key. */
  cells: ReadonlyMap<string, CellValue>;
  /** The shape's own colour, #rrggbb. */
  shapeColour: string;
  /** The constraint colours by id, #rrggbb. */
  palette: ReadonlyMap<string, string>;
  /** A sliding start/goal shape: its tiles, and the marks shown (pieces' colours by cell key); null for any other shape. */
  tray?: { marks: ReadonlyMap<string, Rgb3> } | null;
}

/** What the grid asks of the editor. */
export interface GridHost {
  state: EditorState;
  /** Whether editing is refused now (a solve runs); a press then says so. */
  frozen(): boolean;
  /** The drawing colour, #rrggbb, or null for Default (the cursor's badge). */
  drawingColour(): string | null;
  /** A stroke starts with a tool at a cell. */
  strokeStart(tool: Tool, cell: Cell): void;
  /** More cells of the stroke. */
  strokeAdd(cells: Cell[]): void;
  strokeEnd(): void;
  /** Whether the stroke under way entered a cell (it is marked until the engine answers). */
  strokeHas(cell: Cell): boolean;
  /** The cells under the cursor with mirror and span (this layer's and the others'). */
  expand(cell: Cell): Cell[];
  /** The cell under the mouse and the tool a click would use (null: none), for the 3D preview and the status bar. */
  hover(cell: Cell | null, tool: Tool | null, mods: Mods): void;
  /** The box selection changed (the selection bar). */
  boxChanged(): void;
  /** A start or goal is being placed on a tray: clicks place, nothing is drawn. */
  stamping(): boolean;
  /** A click while stamping: the left button places, the right takes off. */
  stampClick(cell: Cell, right: boolean): void;
  /** The cell under the mouse while stamping (null: none), for the outline. */
  stampHover(cell: Cell | null): void;
}

const M = 44;

type P2 = [number, number];

export class Grid2D {
  private readonly area: HTMLElement;
  private readonly host: GridHost;
  private input: GridInput | null = null;
  /** Drawing units to pixels. */
  private sc = 1;
  private ox = 0;
  private oy = 0;
  private svg: SVGSVGElement | null = null;
  private cellEls: SVGElement[] = [];
  private overlay: SVGGElement | null = null;
  private builtFor = '';
  private hovered: number | null = null;
  private lastMods: Mods = { ctrl: false, shift: false, alt: false };
  private drag: { kind: 'stroke' | 'box'; last: P2; anchor: [number, number] } | null = null;
  /** The piece to place, outlined under the mouse (stamping), or null. */
  private stamp: StampOutline | null = null;

  constructor(area: HTMLElement, host: GridHost) {
    this.area = area;
    this.host = host;
    this.install();
    new ResizeObserver(() => {
      this.builtFor = '';
      this.render();
    }).observe(area);
  }

  /** Shows a layer (null: nothing to edit). */
  show(input: GridInput | null): void {
    this.input = input;
    this.render();
  }

  /** Draws the grid again: the cells' look, the selection, the cursor. */
  render(): void {
    const input = this.input;
    if (input === null) {
      this.area.innerHTML = '';
      this.svg = null;
      this.builtFor = '';
      return;
    }
    const aw = this.area.clientWidth;
    const ah = this.area.clientHeight;
    if (aw === 0 || ah === 0) {
      return;
    }
    const shape = `${aw}x${ah}:${input.grid}:${input.plane.ca}${input.plane.ra}:${input.geo.width}x${input.geo.height}:${input.geo.cells.length}:${input.layer}:${input.geo.cells[0]?.at.join(',') ?? ''}`;
    if (shape !== this.builtFor || this.svg === null) {
      this.build(input, aw, ah);
      this.builtFor = shape;
    }
    this.paint(input);
    this.drawOverlay();
  }

  /** The piece to place, outlined (null: none). */
  setStamp(stamp: StampOutline | null): void {
    this.stamp = stamp;
    this.drawOverlay();
  }

  /** The modifiers changed while the mouse rests on the grid (the mockup's keydown/keyup hover). */
  modsChanged(m: Mods): void {
    if (this.hovered !== null && this.drag === null) {
      this.lastMods = m;
      this.hoverAt(this.hovered);
    }
  }

  /** The tool or the mirror and span changed: the cursor follows. */
  refreshHover(): void {
    if (this.hovered !== null && this.drag === null) {
      this.hoverAt(this.hovered);
    } else {
      this.drawOverlay();
    }
  }

  private X(x: number): number {
    return this.ox + x * this.sc;
  }

  private Y(y: number, h: number): number {
    return this.oy + (h - y) * this.sc;
  }

  private shapeOf(
    c: { polygon?: P2[]; circle?: { centre: P2; radius: number } },
    h: number,
    attrs: string,
  ): string {
    if (c.circle !== undefined) {
      const [cx, cy] = c.circle.centre;
      return `<circle ${attrs} cx="${this.X(cx).toFixed(1)}" cy="${this.Y(cy, h).toFixed(1)}" r="${(c.circle.radius * this.sc).toFixed(1)}"/>`;
    }
    const pts = (c.polygon ?? [])
      .map((q) => `${this.X(q[0]).toFixed(1)},${this.Y(q[1], h).toFixed(1)}`)
      .join(' ');
    return `<polygon ${attrs} points="${pts}"/>`;
  }

  // the SVG for a layer drawing at a size (spaceGridSvg's fit)
  private build(input: GridInput, aw: number, ah: number): void {
    const { width: ex, height: ey } = input.geo;
    this.sc = Math.max(
      16,
      Math.min(110, Math.floor(Math.min((aw - 2 * M) / ex, (ah - 2 * M) / ey))),
    );
    this.ox = (aw - ex * this.sc) / 2;
    this.oy = (ah - ey * this.sc) / 2;
    let b = '';
    input.geo.cells.forEach((c, i) => {
      b += this.shapeOf(c, ey, `class="cell sp" data-i="${i}" data-at="${c.at.join(',')}"`);
    });
    b += '<g class="cmarks" pointer-events="none"></g>';
    const axis = (a: 'X' | 'Y' | 'Z'): string => `var(--a${a.toLowerCase()})`;
    const { flipped, rowsFlipped } = input.plane;
    // the axes meet at the origin: on the right where the columns are
    // flipped, at the top where the rows are
    const X0 = this.X(0).toFixed(1);
    const XO = this.X(flipped ? ex : 0).toFixed(1);
    const YO = this.Y(rowsFlipped ? ey : 0, ey).toFixed(1);
    b += `<line x1="${X0}" y1="${YO}" x2="${this.X(ex).toFixed(1)}" y2="${YO}" stroke="${axis(input.plane.ca)}" stroke-width="3" pointer-events="none"/><line x1="${XO}" y1="${this.Y(0, ey).toFixed(1)}" x2="${XO}" y2="${this.Y(ey, ey).toFixed(1)}" stroke="${axis(input.plane.ra)}" stroke-width="3" pointer-events="none"/>`;
    // the cube editor's rulers: the column and row numbers, the columns' along the origin's edge
    if (input.grid === 'bricks' || input.grid === 'sliding' || input.grid === 'stacking') {
      let t = '';
      const cy = rowsFlipped ? this.Y(ey, ey) - 7 : this.Y(0, ey) + 15;
      for (let c = 0; c < ex; c++) {
        t += `<text x="${this.X(c + 0.5).toFixed(1)}" y="${cy.toFixed(1)}">${flipped ? ex - 1 - c : c}</text>`;
      }
      for (let r = 0; r < ey; r++) {
        t += `<text x="${(this.X(0) - 9).toFixed(1)}" y="${(this.Y(r + 0.5, ey) + 4).toFixed(1)}">${rowsFlipped ? ey - 1 - r : r}</text>`;
      }
      b += `<g class="rul" text-anchor="middle" font-size="11" fill="var(--muted)" pointer-events="none">${t}</g>`;
    }
    b += '<g class="ovl" pointer-events="none"></g>';
    this.area.innerHTML = `<svg width="${aw}" height="${ah}" style="display:block" data-grid="${esc(input.grid)}">${b}</svg>`;
    this.svg = this.area.querySelector('svg');
    this.cellEls = [...this.area.querySelectorAll<SVGElement>('.cell')];
    this.overlay = this.area.querySelector<SVGGElement>('.ovl');
  }

  // a quarter of a cell at its top left (the cell shrunk to half towards
  // that corner: a square's top-left quarter, and inside any other cell)
  private corner(
    c: { polygon?: P2[]; circle?: { centre: P2; radius: number } },
    h: number,
    attrs: string,
  ): string {
    if (c.circle !== undefined) {
      const r = c.circle.radius * this.sc;
      const d = r / 2 / Math.SQRT2;
      const cx = this.X(c.circle.centre[0]) - d;
      const cy = this.Y(c.circle.centre[1], h) - d;
      return `<circle ${attrs} cx="${cx.toFixed(1)}" cy="${cy.toFixed(1)}" r="${(r / 2).toFixed(1)}"/>`;
    }
    const pts = (c.polygon ?? []).map((q): P2 => [this.X(q[0]), this.Y(q[1], h)]);
    const at = pts.reduce<P2 | null>(
      (a, q) => (a === null || q[0] + q[1] < a[0] + a[1] ? q : a),
      null,
    );
    if (at === null) {
      return '';
    }
    const half = pts
      .map((q) => `${((q[0] + at[0]) / 2).toFixed(1)},${((q[1] + at[1]) / 2).toFixed(1)}`)
      .join(' ');
    return `<polygon ${attrs} points="${half}"/>`;
  }

  // each cell's look from the engine's cells (the mockup's fill: a variable
  // cell half filled with a dashed border, in the shape's colour; a
  // constraint colour as a box in the top-left quarter)
  private paint(input: GridInput): void {
    let marks = '';
    input.geo.cells.forEach((c, i) => {
      const el = this.cellEls[i];
      if (el === undefined) {
        return;
      }
      const v = input.cells.get(key(c.at));
      const vr = v?.state === 'variable';
      let col = v === undefined ? 'var(--panel)' : input.shapeColour;
      const cc = v?.colour == null ? undefined : input.palette.get(v.colour);
      if (cc !== undefined && input.tray == null) {
        marks += this.corner(
          c,
          input.geo.height,
          `class="cmark" data-at="${c.at.join(',')}" fill="${cc}" stroke="var(--line2)" stroke-width="1"`,
        );
      }
      const tray = input.tray;
      if (tray != null && v !== undefined) {
        // the tray's chequer, a marked cell in its piece's (grideditor.cpp)
        const odd = ((c.at[0] + c.at[1] + c.at[2]) & 1) === 1;
        const mark = tray.marks.get(key(c.at));
        col =
          mark === undefined
            ? odd
              ? TRAY_EDIT.dark
              : TRAY_EDIT.light
            : hex(odd ? dark(mark) : light(mark));
      }
      el.setAttribute('fill', col);
      el.setAttribute('fill-opacity', vr ? '0.5' : '1');
      el.setAttribute('stroke', vr ? 'var(--text)' : 'var(--line2)');
      el.setAttribute('stroke-width', vr ? '1.6' : '1.2');
      if (vr) {
        el.setAttribute('stroke-dasharray', '4 3');
      } else {
        el.removeAttribute('stroke-dasharray');
      }
      el.dataset.state = v?.state ?? '';
    });
    const g = this.svg?.querySelector('.cmarks');
    if (g != null) {
      g.innerHTML = marks;
    }
    if (this.svg !== null) {
      this.svg.dataset.cells = String(input.cells.size);
    }
  }

  // the box selection, the stroke's cells not answered yet, the cursor
  private drawOverlay(): void {
    const input = this.input;
    const g = this.overlay;
    if (input === null || g === null) {
      return;
    }
    const h = input.geo.height;
    let b = '';
    const box = this.host.state.box;
    const inBox = (c: Cell): boolean => {
      if (box === null) {
        return false;
      }
      const [cc, rr] = input.plane.crl(c);
      return (
        cc >= Math.min(box.c0, box.c1) &&
        cc <= Math.max(box.c0, box.c1) &&
        rr >= Math.min(box.r0, box.r1) &&
        rr <= Math.max(box.r0, box.r1)
      );
    };
    const sel: number[] = [];
    input.geo.cells.forEach((c, i) => {
      if (this.host.strokeHas(c.at)) {
        b += this.shapeOf(c, h, 'fill="var(--accent)" fill-opacity="0.18"');
      }
      if (inBox(c.at)) {
        sel.push(i);
        b += this.shapeOf(c, h, 'fill="var(--accentSoft)" fill-opacity="0.6"');
      }
    });
    if (this.svg !== null) {
      this.svg.dataset.selected = String(sel.length);
    }
    // the box: its cells' edges towards cells outside it, dashed
    b += this.outline(new Set(sel), 'stroke-dasharray="5 3"');
    // the cursor: its cells' edges towards cells not under it
    b += this.outline(this.cursorCells(), '');
    // the piece to place: each of its cells on the grid outlined (drawStampPreview)
    const st = this.host.stamping() ? this.stamp : null;
    if (st !== null) {
      // the cells on the layer shown
      for (const [x, y, z] of st.cells) {
        if (z !== input.layer) {
          continue;
        }
        const i = input.geo.index.get(key([x, y, z]));
        const c = i === undefined ? undefined : input.geo.cells[i];
        if (c !== undefined) {
          b += this.shapeOf(c, h, `fill="none" stroke="${st.colour}" stroke-width="3"`);
        }
      }
    }
    if (this.svg !== null) {
      this.svg.dataset.stamp =
        st === null ? '' : `${st.colour};${st.cells.map((q) => q.join(',')).join(' ')}`;
    }
    g.innerHTML = b;
  }

  // the edges of a set of cells towards cells not in it (circles whole)
  private outline(cur: ReadonlySet<number>, extra: string): string {
    const input = this.input;
    if (input === null) {
      return '';
    }
    const h = input.geo.height;
    let b = '';
    for (const i of cur) {
      const c = input.geo.cells[i];
      if (c === undefined) {
        continue;
      }
      if (c.circle !== undefined) {
        b += this.shapeOf(c, h, `fill="none" stroke="var(--accent)" stroke-width="2.6" ${extra}`);
        continue;
      }
      const poly = c.polygon ?? [];
      const nb = c.neighbours ?? [];
      let d = '';
      poly.forEach((p, j) => {
        const q = poly[(j + 1) % poly.length] ?? p;
        const n = nb[j] ?? -1;
        if (n < 0 || !cur.has(n)) {
          d += `M${this.X(p[0]).toFixed(1)} ${this.Y(p[1], h).toFixed(1)}L${this.X(q[0]).toFixed(1)} ${this.Y(q[1], h).toFixed(1)}`;
        }
      });
      if (d !== '') {
        b += `<path d="${d}" stroke="var(--accent)" stroke-width="2.6" stroke-linecap="round" fill="none" ${extra}/>`;
      }
    }
    return b;
  }

  // the cells of this layer under the cursor: the hovered one with its
  // mirror and span cells, or the box being dragged
  private cursorCells(): Set<number> {
    const input = this.input;
    const out = new Set<number>();
    if (input === null) {
      return out;
    }
    if (this.drag?.kind === 'box' || this.host.stamping()) {
      return out;
    }
    if (this.hovered === null) {
      return out;
    }
    const c = input.geo.cells[this.hovered];
    if (c === undefined) {
      return out;
    }
    if (this.host.state.boxMode) {
      out.add(this.hovered);
      return out;
    }
    for (const e of this.host.expand(c.at)) {
      const i = input.geo.index.get(key(e));
      if (i !== undefined) {
        out.add(i);
      }
    }
    return out;
  }

  private cellAt(x: number, y: number): number | null {
    const el = document.elementFromPoint(x, y);
    const c = el?.closest<SVGElement>('.cell');
    if (c === null || c === undefined || !this.area.contains(c)) {
      return null;
    }
    const i = Number(c.dataset.i);
    return Number.isInteger(i) ? i : null;
  }

  private hoverAt(i: number | null): void {
    this.hovered = i;
    const input = this.input;
    const c = i === null || input === null ? undefined : input.geo.cells[i];
    if (this.host.stamping()) {
      this.area.style.cursor = c === undefined ? '' : 'pointer';
      this.host.hover(c?.at ?? null, null, this.lastMods);
      this.host.stampHover(c?.at ?? null);
    } else if (c === undefined) {
      this.area.style.cursor = '';
      this.host.hover(null, null, this.lastMods);
    } else {
      const tool = effectiveTool(this.lastMods, this.host.state.tool);
      this.area.style.cursor =
        this.host.state.boxMode && this.drag === null && !this.anyMod()
          ? 'crosshair'
          : cursorFor(tool, this.host.drawingColour());
      this.host.hover(c.at, this.host.state.boxMode && !this.anyMod() ? null : tool, this.lastMods);
    }
    this.drawOverlay();
  }

  private anyMod(): boolean {
    return this.lastMods.ctrl || this.lastMods.alt || this.lastMods.shift;
  }

  // the cells along the pointer's path from the last point (fast drags skip none)
  private along(from: P2, to: P2): number[] {
    const out: number[] = [];
    const steps = Math.max(
      1,
      Math.ceil(Math.hypot(to[0] - from[0], to[1] - from[1]) / Math.max(4, this.sc / 4)),
    );
    for (let s = 1; s <= steps; s++) {
      const t = s / steps;
      const i = this.cellAt(from[0] + (to[0] - from[0]) * t, from[1] + (to[1] - from[1]) * t);
      if (i !== null && out.at(-1) !== i) {
        out.push(i);
      }
    }
    return out;
  }

  private boxOf(anchor: [number, number], i: number): Box | null {
    const input = this.input;
    const c = input?.geo.cells[i];
    if (input === null || c === undefined) {
      return null;
    }
    const [cc, rr] = input.plane.crl(c.at);
    return { c0: anchor[0], r0: anchor[1], c1: cc, r1: rr };
  }

  private install(): void {
    const area = this.area;
    area.addEventListener('contextmenu', (e) => {
      if ((e.target as Element).closest('.cell') !== null) {
        e.preventDefault();
      }
    });
    area.addEventListener('pointerdown', (e) => {
      const input = this.input;
      const i = this.cellAt(e.clientX, e.clientY);
      if (input === null || i === null || e.button === 1) {
        return;
      }
      const c = input.geo.cells[i];
      if (c === undefined) {
        return;
      }
      e.preventDefault();
      if (this.host.frozen()) {
        return;
      }
      if (this.host.stamping()) {
        if (e.button === 0 || e.button === 2) {
          this.host.stampClick(c.at, e.button === 2);
        }
        return;
      }
      this.lastMods = modsOf(e);
      const point: P2 = [e.clientX, e.clientY];
      if (this.host.state.boxMode && e.button === 0 && !this.anyMod()) {
        const [cc, rr] = input.plane.crl(c.at);
        this.drag = { kind: 'box', last: point, anchor: [cc, rr] };
        this.host.state.box = { c0: cc, r0: rr, c1: cc, r1: rr };
        this.drawOverlay();
        this.host.boxChanged();
        return;
      }
      const tool: Tool =
        e.button === 2 ? 'erase' : effectiveTool(this.lastMods, this.host.state.tool);
      this.drag = { kind: 'stroke', last: point, anchor: [0, 0] };
      if (e.button === 2) {
        area.style.cursor = cursorFor('erase', null);
      }
      this.host.strokeStart(tool, c.at);
      this.drawOverlay();
    });
    area.addEventListener('pointermove', (e) => {
      const input = this.input;
      if (input === null) {
        return;
      }
      const d = this.drag;
      const point: P2 = [e.clientX, e.clientY];
      if (d === null) {
        const i = this.cellAt(e.clientX, e.clientY);
        this.lastMods = modsOf(e);
        if (i !== this.hovered) {
          this.hoverAt(i);
        }
        return;
      }
      const passed = this.along(d.last, point);
      d.last = point;
      const last = passed.at(-1);
      if (last !== undefined && last !== this.hovered) {
        this.hovered = last;
        const c = input.geo.cells[last];
        if (c !== undefined) {
          this.host.hover(c.at, null, this.lastMods);
        }
      }
      if (d.kind === 'box') {
        if (last !== undefined) {
          this.host.state.box = this.boxOf(d.anchor, last);
          this.drawOverlay();
          this.host.boxChanged();
        }
        return;
      }
      const cells = passed.flatMap((i) => {
        const c = input.geo.cells[i];
        return c === undefined ? [] : [c.at];
      });
      if (cells.length > 0) {
        this.host.strokeAdd(cells);
        this.drawOverlay();
      }
    });
    const up = (): void => {
      const d = this.drag;
      if (d === null) {
        return;
      }
      this.drag = null;
      if (d.kind === 'stroke') {
        this.host.strokeEnd();
        this.hoverAt(this.hovered);
      } else {
        this.hoverAt(this.hovered);
        this.host.boxChanged();
      }
    };
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
    // a release outside the window may never come: losing the focus ends the drag
    window.addEventListener('blur', up);
    area.addEventListener('pointerleave', () => {
      if (this.drag === null) {
        this.hoverAt(null);
      }
    });
  }
}
