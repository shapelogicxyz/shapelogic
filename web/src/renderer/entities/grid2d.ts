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
// Shift fixed). A sliding puzzle's board (docs/webui-spec.md, sections
// 8.1 and 8.2) is drawn as the original's grideditor.cpp drew it: dark
// tiles; in the Puzzle workspace's Start and goal grids (stamping) the
// marked cells in their pieces' colors, the piece to place outlined under
// the mouse (`drawStampPreview`), a click placing it and a right click
// taking off the copy under the mouse. The cells of the layer next to the
// one drawn (below or above, the Voxel hint layer setting) show faintly
// under its empty cells, each in its own layer's outline: grideditor.cpp's
// dimmed squares of the layer below, a sixth of the shape's color. A
// copy's name, when the grid is given it, is written at the bottom center
// of the copy's cells in this layer, on a rounded box in the theme's
// opposite (black on white in the dark theme, white on black in the light).

import type { Grid } from '../../engine/api.generated.ts';
import { cursorFor } from '../shell/cursors.ts';
import type { Tool } from '../shell/cursors.ts';
import { esc } from '../shell/dom.ts';
import { boardColor, dark, hex, light } from '../shell/piece-colors.ts';
import type { Rgb3 } from '../shell/piece-colors.ts';
import { isDarkTheme } from '../shell/theme.ts';
import { key } from './editor-state.ts';
import { effectiveTool, modsOf } from './tools.ts';
import type { Mods } from './tools.ts';
import type { Box, Cell, EditorState, PlaneMap } from './editor-state.ts';
import type { LayerGeometry } from './layer-geometry.ts';
import type { CellValue } from './shape-cells.ts';
import { compose } from '../shell/picture-math.ts';
import type { Affine } from '../shell/picture-math.ts';
import type { PieceImage } from '../shell/piece-images.ts';
import type { StampOutline, TrayName } from './tray.ts';

/** What the grid shows. */
export interface GridInput {
  grid: Grid;
  geo: LayerGeometry;
  plane: PlaneMap;
  layer: number;
  /** The layer's cells by key. */
  cells: ReadonlyMap<string, CellValue>;
  /** The shape's own color, #rrggbb. */
  shapeColor: string;
  /** The constraint colors by id, #rrggbb. */
  palette: ReadonlyMap<string, string>;
  /**
   * A sliding start/goal shape: its tiles, the marks shown (pieces' colors
   * by cell key), and the copies' names written on them; null for any other shape.
   */
  tray?: {
    marks: ReadonlyMap<string, Rgb3>;
    own?: Rgb3 | null;
    names?: readonly TrayName[];
  } | null;
  /**
   * A sliding piece's or board's picture (decision 0038): drawn on its filled cells, the
   * map from its pixels to the grid's cells (y up) and the part of it shown
   * ([x, y, width, height] in pixels); null for none.
   */
  image?: { picture: PieceImage; toCells: Affine; src: readonly number[] } | null;
  /** The layer hinted at under this one: its drawing and its cells (null: none). */
  hint?: { geo: LayerGeometry; cells: ReadonlyMap<string, CellValue> } | null;
  /**
   * A Tilted Square puzzle's piece: whether the board's cell at (x, y) is
   * whole. The others are drawn gray and take no pointer: a piece's cells
   * lie on whole cells (decision 0045). Absent on other shapes.
   */
  whole?: ((x: number, y: number) => boolean) | null;
  /**
   * A Tilted Square puzzle's piece: the tray, in the grid's own units (the
   * board's grid): its corners and its walls' squares. The grid is drawn
   * turned so the tray stands straight, clipped to it, with its outline
   * and walls over the cells (decision 0045, stage D). Absent: not turned.
   */
  tilt?: {
    degrees: number;
    corners: readonly P2[];
    walls: readonly (readonly P2[])[];
  } | null;
}

/** How much of the shape's color a hinted cell shows (the original's sixth). */
const HINT_OPACITY = 1 / 6;
/** The room around a copy's name inside its box, and between the box and the copy's bottom edge, in pixels. */
const NAME_PAD_X = 4;
const NAME_PAD_Y = 1.5;
const NAME_GAP = 3;

/** What the grid asks of the editor. */
export interface GridHost {
  state: EditorState;
  /** Whether editing is refused now (a solve runs); a press then says so. */
  frozen(): boolean;
  /** The drawing color, #rrggbb, or null for Default (the cursor's badge). */
  drawingColor(): string | null;
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
  /** A grid where starts or goals are placed on a board: clicks place, nothing is drawn. */
  stamping(): boolean;
  /** A click while stamping: the left button places, the right takes off. */
  stampClick(cell: Cell, right: boolean): void;
  /** The cell under the mouse while stamping (null: none), for the outline. */
  stampHover(cell: Cell | null): void;
  /** Whether the 2D editor has the focus: the grid then takes all its room, however few its cells. */
  focused(): boolean;
}

/** The space round the grid for its rulers (spaceGridSvg's), in pixels. */
const M = 44;

type P2 = [number, number];

/** Numbers the grids, so each picture pattern's id is the page's only one. */
let grids = 0;

export class Grid2D {
  private readonly area: HTMLElement;
  /** This grid's picture pattern id: several grids are on the page at once. */
  private readonly patternId = `pimg${String(++grids)}`;
  /** What the picture pattern was last drawn from, and the defs it is in. */
  private patternKey = '';
  private patternDefs: Element | null = null;
  private readonly host: GridHost;
  /** The space round the grid, in pixels. */
  private readonly margin: number;
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

  /** A grid in an element; `margin` the space round it (a small grid takes less). */
  constructor(area: HTMLElement, host: GridHost, margin = M) {
    this.area = area;
    this.host = host;
    this.margin = margin;
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
    const tilt = input.tilt == null ? '' : JSON.stringify(input.tilt);
    const shape = `${aw}x${ah}:${input.grid}:${input.plane.ca}${input.plane.ra}:${input.geo.width}x${input.geo.height}:${input.geo.cells.length}:${input.layer}:${input.geo.cells[0]?.at.join(',') ?? ''}:${tilt}`;
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
    c: { polygon?: P2[]; circle?: { center: P2; radius: number } },
    h: number,
    attrs: string,
  ): string {
    if (c.circle !== undefined) {
      const [cx, cy] = c.circle.center;
      return `<circle ${attrs} cx="${this.X(cx).toFixed(1)}" cy="${this.Y(cy, h).toFixed(1)}" r="${(c.circle.radius * this.sc).toFixed(1)}"/>`;
    }
    const pts = (c.polygon ?? [])
      .map((q) => `${this.X(q[0]).toFixed(1)},${this.Y(q[1], h).toFixed(1)}`)
      .join(' ');
    return `<polygon ${attrs} points="${pts}"/>`;
  }

  // the SVG for a layer drawing at a size (spaceGridSvg's fit)
  private build(input: GridInput, aw: number, ah: number): void {
    if (input.tilt != null) {
      this.buildTilted(input, input.tilt, aw, ah);
      return;
    }
    const { width: ex, height: ey } = input.geo;
    // every grid shrinks to fit its area instead of being cut off (a very
    // large one is better edited in the focus: the card offers it); in the
    // focus there is no most
    const least = 2;
    const most = this.host.focused() ? Infinity : 110;
    this.sc = Math.max(
      least,
      Math.min(
        most,
        Math.floor(Math.min((aw - 2 * this.margin) / ex, (ah - 2 * this.margin) / ey)),
      ),
    );
    this.ox = (aw - ex * this.sc) / 2;
    this.oy = (ah - ey * this.sc) / 2;
    let b = '<defs class="pdefs"></defs><g class="hint" pointer-events="none"></g>';
    input.geo.cells.forEach((c, i) => {
      b += this.shapeOf(c, ey, `class="cell sp" data-i="${i}" data-at="${c.at.join(',')}"`);
    });
    b += '<g class="cmarks" pointer-events="none"></g>';
    const axis = (a: 'X' | 'Y' | 'Z'): string => `var(--a${a.toLowerCase()})`;
    const { flipped, rowsFlipped } = input.plane;
    // the axes meet at the origin: on the right where the columns are
    // flipped, at the top where the rows are
    // along the outer edge of the cells (a sphere at the edge reaches past
    // the grid's box), from corner to corner of what they cover
    let lx = 0;
    let hx = ex;
    let ly = 0;
    let hy = ey;
    for (const c of input.geo.cells) {
      if (c.circle !== undefined) {
        const [cx, cy] = c.circle.center;
        const r = c.circle.radius;
        lx = Math.min(lx, cx - r);
        hx = Math.max(hx, cx + r);
        ly = Math.min(ly, cy - r);
        hy = Math.max(hy, cy + r);
      }
    }
    const X0 = this.X(lx).toFixed(1);
    const XO = this.X(flipped ? hx : lx).toFixed(1);
    const YO = this.Y(rowsFlipped ? hy : ly, ey).toFixed(1);
    b += `<line x1="${X0}" y1="${YO}" x2="${this.X(hx).toFixed(1)}" y2="${YO}" stroke="${axis(input.plane.ca)}" stroke-width="3" pointer-events="none"/><line x1="${XO}" y1="${this.Y(ly, ey).toFixed(1)}" x2="${XO}" y2="${this.Y(hy, ey).toFixed(1)}" stroke="${axis(input.plane.ra)}" stroke-width="3" pointer-events="none"/>`;
    // the cube editor's rulers: the column and row numbers, the columns' along the origin's edge
    if (input.grid === 'bricks' || input.grid === 'sliding' || input.grid === 'stacking') {
      let t = '';
      // cells too small for a number each: every second, fifth… one
      const every = Math.ceil(14 / this.sc);
      // counted from 1 as the layers are (the cells' coordinates, the
      // file's and the API's, still count from 0): 1, then each multiple
      // of the step (1, 5, 10); at a step of 2, 1 would touch 2, so 2, 4, 6
      const shown = (n: number): boolean => n % every === 0 || (n === 1 && every !== 2);
      const cy = rowsFlipped ? this.Y(ey, ey) - 7 : this.Y(0, ey) + 15;
      for (let c = 0; c < ex; c++) {
        const n = (flipped ? ex - 1 - c : c) + 1;
        if (shown(n)) {
          t += `<text x="${this.X(c + 0.5).toFixed(1)}" y="${cy.toFixed(1)}">${n}</text>`;
        }
      }
      for (let r = 0; r < ey; r++) {
        const n = (rowsFlipped ? ey - 1 - r : r) + 1;
        if (shown(n)) {
          t += `<text x="${(this.X(0) - 9).toFixed(1)}" y="${(this.Y(r + 0.5, ey) + 4).toFixed(1)}">${n}</text>`;
        }
      }
      b += `<g class="rul" text-anchor="middle" font-size="11" fill="var(--muted)" pointer-events="none">${t}</g>`;
    }
    b += '<g class="names" pointer-events="none"></g>';
    b += '<g class="ovl" pointer-events="none"></g>';
    this.area.innerHTML = `<svg width="${aw}" height="${ah}" style="display:block" data-grid="${esc(input.grid)}">${b}</svg>`;
    this.svg = this.area.querySelector('svg');
    this.cellEls = [...this.area.querySelectorAll<SVGElement>('.cell')];
    this.overlay = this.area.querySelector<SVGGElement>('.ovl');
  }

  /**
   * A Tilted Square puzzle's piece (decision 0045): the grid turned by the
   * tray's angle about the tray's center so the tray stands straight,
   * fitted on the tray's width and height and clipped to it, with the tray's walls and
   * outline over the cells. The cells keep their places in the grid's units
   * (X, Y) inside the turned group, so the cursor, strokes and the box
   * selection work as on a straight grid: the pointer finds a cell through
   * elementFromPoint, which follows the turn. No axes or rulers: the grid's
   * axes are not the tray's.
   */
  private buildTilted(
    input: GridInput,
    tilt: NonNullable<GridInput['tilt']>,
    aw: number,
    ah: number,
  ): void {
    const ey = input.geo.height;
    // the tray's sides, in the grid's units: across (corner 0 to 1) and up
    // (corner 0 to 3), as it stands once the grid is turned
    const [c0, c1, , c3] = tilt.corners;
    const sideTo = (c: P2 | undefined): number =>
      c0 === undefined || c === undefined ? 1 : Math.hypot(c[0] - c0[0], c[1] - c0[1]);
    const wide = sideTo(c1);
    const high = sideTo(c3);
    const cx = tilt.corners.reduce((s, p) => s + p[0], 0) / Math.max(1, tilt.corners.length);
    const cy = tilt.corners.reduce((s, p) => s + p[1], 0) / Math.max(1, tilt.corners.length);
    const most = this.host.focused() ? Infinity : 110;
    this.sc = Math.max(
      2,
      Math.min(most, Math.min((aw - 2 * this.margin) / wide, (ah - 2 * this.margin) / high)),
    );
    // the tray's center at the area's center
    this.ox = aw / 2 - cx * this.sc;
    this.oy = ah / 2 - (ey - cy) * this.sc;
    const poly = (pts: readonly P2[]): string =>
      pts.map((q) => `${this.X(q[0]).toFixed(1)},${this.Y(q[1], ey).toFixed(1)}`).join(' ');
    // SVG turns clockwise on the screen, y down: the grid's counter-clockwise turn is negative
    const turn = `rotate(${(-tilt.degrees).toFixed(4)} ${(aw / 2).toFixed(2)} ${(ah / 2).toFixed(2)})`;
    const clip = `${this.patternId}t`;
    let b = `<defs class="pdefs"></defs><defs><clipPath id="${clip}"><polygon points="${poly(tilt.corners)}"/></clipPath></defs>`;
    b += `<g class="tturn" transform="${turn}"><g clip-path="url(#${clip})">`;
    b += '<g class="hint" pointer-events="none"></g>';
    input.geo.cells.forEach((c, i) => {
      b += this.shapeOf(c, ey, `class="cell sp" data-i="${i}" data-at="${c.at.join(',')}"`);
    });
    b += '<g class="cmarks" pointer-events="none"></g>';
    b += `<g class="twalls" pointer-events="none">${tilt.walls.map((w) => `<polygon points="${poly(w)}"/>`).join('')}</g>`;
    b += '</g>';
    b += `<polygon class="tedge" pointer-events="none" points="${poly(tilt.corners)}"/>`;
    b += '<g class="names" pointer-events="none"></g>';
    b += '<g class="ovl" pointer-events="none"></g>';
    b += '</g>';
    this.area.innerHTML = `<svg width="${aw}" height="${ah}" style="display:block" data-grid="${esc(input.grid)}" data-tilt="${tilt.degrees.toFixed(2)}">${b}</svg>`;
    this.svg = this.area.querySelector('svg');
    this.cellEls = [...this.area.querySelectorAll<SVGElement>('.cell')];
    this.overlay = this.area.querySelector<SVGGElement>('.ovl');
  }

  // a quarter of a cell at its top left (the cell shrunk to half toward
  // that corner: a square's top-left quarter, and inside any other cell)
  private corner(
    c: { polygon?: P2[]; circle?: { center: P2; radius: number } },
    h: number,
    attrs: string,
  ): string {
    if (c.circle !== undefined) {
      const r = c.circle.radius * this.sc;
      const d = r / 2 / Math.SQRT2;
      const cx = this.X(c.circle.center[0]) - d;
      const cy = this.Y(c.circle.center[1], h) - d;
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
  // cell half filled with a dashed border, in the shape's color; a
  // constraint color as a box in the top-left quarter)
  private paint(input: GridInput): void {
    // the hinted layer's cells under this one's, which leave their empty
    // cells clear (still painted, so they take the pointer)
    const hint = input.hint ?? null;
    let hinted = '';
    if (hint !== null) {
      for (const c of hint.geo.cells) {
        if (hint.cells.has(key(c.at))) {
          hinted += this.shapeOf(c, input.geo.height, 'class="hcell"');
        }
      }
    }
    const hg = this.svg?.querySelector('.hint');
    if (hg != null) {
      hg.setAttribute('fill', input.shapeColor);
      hg.setAttribute('fill-opacity', HINT_OPACITY.toFixed(3));
      hg.innerHTML = hinted;
    }
    const picture = this.paintPicture(input);
    let marks = '';
    input.geo.cells.forEach((c, i) => {
      const el = this.cellEls[i];
      if (el === undefined) {
        return;
      }
      const v = input.cells.get(key(c.at));
      const vr = v?.state === 'variable';
      let col = v === undefined ? 'var(--panel)' : input.shapeColor;
      const cc = v?.color == null ? undefined : input.palette.get(v.color);
      if (cc !== undefined && input.tray == null) {
        marks += this.corner(
          c,
          input.geo.height,
          `class="cmark" data-at="${c.at.join(',')}" fill="${cc}" stroke="var(--line2)" stroke-width="1"`,
        );
      }
      const tray = input.tray;
      const mark = tray?.marks.get(key(c.at));
      if (tray != null && v !== undefined && (mark !== undefined || picture === null)) {
        // the tray's chequer, a marked cell in its piece's (grideditor.cpp);
        // a board with a picture shows it on the cells no piece stands on
        const odd = ((c.at[0] + c.at[1] + c.at[2]) & 1) === 1;
        const base = mark ?? tray.own ?? boardColor();
        col = hex(odd ? dark(base) : light(base));
      } else if (picture !== null && v !== undefined) {
        col = picture;
      }
      // a tilted puzzle's piece: the cells the tray cuts take nothing
      const cut = input.whole != null && !input.whole(c.at[0], c.at[1]);
      if (cut && v === undefined) {
        col = 'var(--line2)';
      }
      el.classList.toggle('tcut', cut);
      el.setAttribute('fill', col);
      el.setAttribute(
        'fill-opacity',
        vr ? '0.5' : v === undefined && hinted !== '' && !cut ? '0' : '1',
      );
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
    this.paintNames(input);
  }

  // the piece's or board's picture as a pattern over the whole drawing, the shape's
  // color where the picture does not reach: the fill of its filled cells,
  // or null when it has none (the XY plane only, the one a sliding puzzle
  // is drawn in)
  private paintPicture(input: GridInput): string | null {
    const defs = this.svg?.querySelector('.pdefs');
    const im = input.image ?? null;
    if (defs == null) {
      return null;
    }
    if (im === null || input.plane.ca !== 'X' || input.plane.ra !== 'Y') {
      if (defs.hasChildNodes()) {
        defs.innerHTML = '';
      }
      this.patternKey = '';
      return null;
    }
    const { width: ex, height: ey } = input.geo;
    const { flipped, rowsFlipped } = input.plane;
    // grid cells to pixels: X(x) and Y(y), each axis maybe flipped
    const toScreen: Affine = [
      flipped ? -this.sc : this.sc,
      0,
      0,
      rowsFlipped ? this.sc : -this.sc,
      this.ox + (flipped ? ex * this.sc : 0),
      this.oy + (rowsFlipped ? 0 : ey * this.sc),
    ];
    const t = compose(toScreen, im.toCells)
      .map((n) => n.toFixed(4))
      .join(' ');
    const [sx = 0, sy = 0, sw = 0, sh = 0] = im.src;
    const w = this.svg?.getAttribute('width') ?? '0';
    const h = this.svg?.getAttribute('height') ?? '0';
    // the pattern again only when the picture or its place changed: its
    // data URL can be megabytes (the drawing built again has new defs)
    const k = `${im.picture.hash}:${String(im.picture.width)}x${String(im.picture.height)}:${t}:${im.src.join(',')}:${w}x${h}:${input.shapeColor}`;
    if (k === this.patternKey && defs === this.patternDefs && defs.hasChildNodes()) {
      return `url(#${this.patternId})`;
    }
    this.patternKey = k;
    this.patternDefs = defs;
    defs.innerHTML = `<clipPath id="${this.patternId}Clip"><rect x="${sx}" y="${sy}" width="${sw}" height="${sh}"/></clipPath><pattern id="${this.patternId}" patternUnits="userSpaceOnUse" x="0" y="0" width="${w}" height="${h}"><rect width="${w}" height="${h}" fill="${input.shapeColor}"/><g transform="matrix(${t})"><image href="${im.picture.dataUrl}" width="${im.picture.width}" height="${im.picture.height}" preserveAspectRatio="none" clip-path="url(#${this.patternId}Clip)"/></g></pattern>`;
    return `url(#${this.patternId})`;
  }

  // each copy's name at the bottom center of its cells in this layer: the
  // middle of its lowest cells on the screen, the box just above their
  // bottom edge
  private paintNames(input: GridInput): void {
    const g = this.svg?.querySelector<SVGGElement>('.names');
    if (g == null) {
      return;
    }
    const names = input.tray?.names ?? [];
    const h = input.geo.height;
    const geoAt = new Map(input.geo.cells.map((c) => [key(c.at), c]));
    let text = '';
    for (const n of names) {
      // each cell's span on the screen
      const spans: { x0: number; x1: number; y1: number }[] = [];
      for (const at of n.cells) {
        const c = geoAt.get(key([at[0], at[1], at[2]]));
        if (c === undefined) {
          continue;
        }
        const pts: P2[] =
          c.circle !== undefined
            ? [
                [c.circle.center[0] - c.circle.radius, c.circle.center[1] - c.circle.radius],
                [c.circle.center[0] + c.circle.radius, c.circle.center[1] + c.circle.radius],
              ]
            : (c.polygon ?? []);
        const xs = pts.map((q) => this.X(q[0]));
        const ys = pts.map((q) => this.Y(q[1], h));
        if (xs.length > 0) {
          spans.push({ x0: Math.min(...xs), x1: Math.max(...xs), y1: Math.max(...ys) });
        }
      }
      if (spans.length === 0) {
        continue;
      }
      const bottom = Math.max(...spans.map((q) => q.y1));
      const low = spans.filter((q) => q.y1 > bottom - 0.5);
      const x = (Math.min(...low.map((q) => q.x0)) + Math.max(...low.map((q) => q.x1))) / 2;
      text += `<text x="${x.toFixed(1)}" y="${(bottom - NAME_GAP - NAME_PAD_Y).toFixed(1)}">${esc(n.text)}</text>`;
    }
    const night = isDarkTheme();
    g.setAttribute('fill', night ? '#000000' : '#ffffff');
    g.setAttribute('text-anchor', 'middle');
    g.setAttribute('dominant-baseline', 'text-after-edge');
    g.setAttribute('style', 'font:600 calc(11px + var(--fs)) system-ui, sans-serif');
    g.innerHTML = text;
    // each name's box, measured as drawn, under it
    for (const t of g.querySelectorAll<SVGTextElement>('text')) {
      if (typeof t.getBBox !== 'function') {
        break;
      }
      const b = t.getBBox();
      const r = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
      r.setAttribute('x', (b.x - NAME_PAD_X).toFixed(1));
      r.setAttribute('y', (b.y - NAME_PAD_Y).toFixed(1));
      r.setAttribute('width', (b.width + 2 * NAME_PAD_X).toFixed(1));
      r.setAttribute('height', (b.height + 2 * NAME_PAD_Y).toFixed(1));
      r.setAttribute('rx', '4');
      r.setAttribute('fill', night ? '#ffffff' : '#000000');
      t.before(r);
    }
    g.dataset.names = String(g.querySelectorAll('text').length);
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
    // the box: its cells' edges toward cells outside it, dashed
    b += this.outline(new Set(sel), 'stroke-dasharray="5 3"');
    // the cursor: its cells' edges toward cells not under it
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
          b += this.shapeOf(c, h, `fill="none" stroke="${st.color}" stroke-width="3"`);
        }
      }
    }
    if (this.svg !== null) {
      this.svg.dataset.stamp =
        st === null ? '' : `${st.color};${st.cells.map((q) => q.join(',')).join(' ')}`;
    }
    g.innerHTML = b;
  }

  // the edges of a set of cells toward cells not in it (circles whole)
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
      const nb = c.neighbors ?? [];
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
          : cursorFor(tool, this.host.drawingColor());
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
