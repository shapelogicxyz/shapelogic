// SPDX-License-Identifier: GPL-3.0-or-later
//
// The board of the Play workspace (decision 0039): a sliding problem's
// board seen from above as an SVG drawing, one layer at a time, y up as
// the 2D grids draw it. The floor's tiles in the board's chequer
// (entities/grid2d.ts's tray), corridors dashed, walls left out; each goal
// as an outline in its piece's colour; each piece one group that moves
// whole, with its name and its picture (decision 0038) as the 2D grid
// draws them. A piece is dragged with the pointer: when the drag begins
// the host asks the engine where one move can take it, and the piece then
// follows the pointer over those places (follow.ts), so the page works
// out no move of its own. A move made again (Undo, Redo, the playback) is
// drawn along its route.

import type { ShapeImage } from '../../engine/api.generated.ts';
import { esc } from '../shell/dom.ts';
import { compose, pictureToCells } from '../shell/picture-math.ts';
import type { Affine } from '../shell/picture-math.ts';
import { boardColour, contrast, dark, hex, light } from '../shell/piece-colours.ts';
import type { Rgb3 } from '../shell/piece-colours.ts';
import { isDarkTheme } from '../shell/theme.ts';
import { aim, alongRoute, follow, groupFor, routeLength, slide, stopsIn } from './follow.ts';
import type { Has } from './follow.ts';
import type { Cell, Move } from './game.ts';
import { roundedOutline } from './outline.ts';
import type { Square } from './outline.ts';
import { boardRoom, insetBoard } from './room.ts';
import type { BoardRoom } from './room.ts';

/** A piece as the board draws it. */
export interface BoardPiece {
  /** The cells it fills, from its lowest corner. */
  cells: Cell[];
  /** That corner in the shape's own grid (its picture's box is in those cells). */
  origin: Cell;
  colour: Rgb3;
  /** The piece's name, written on it; '' for none. */
  name: string;
  /** Its picture, once decoded. */
  picture: { image: ShapeImage; dataUrl: string; width: number; height: number } | null;
}

/** What the board draws. */
export interface BoardModel {
  size: Cell;
  /** The board's own colour, or null for the theme's. */
  colour: Rgb3 | null;
  floor: { at: Cell; variable: boolean }[];
  /** The board's picture on its floor, under the goals and the pieces, once decoded. */
  picture?: BoardPiece['picture'];
  goals: { colour: Rgb3; cells: Cell[] }[];
  pieces: BoardPiece[];
}

/** Pieces that move as one and where one move can take them (sliding.playReach). */
export interface ReachGroup {
  movers: number[];
  stops: { by: number[]; solved: boolean }[];
}

/** What the board asks of the workspace. */
export interface BoardHost {
  /** Where one move can take a piece now; null when no piece can be moved now. */
  reach(piece: number): Promise<ReachGroup[] | null>;
  /** A drag ended somewhere new: the move made, and whether it gives the goal. */
  moved(move: Move, solved: boolean): void;
}

const CELL_MIN = 14;
const CELL_MAX = 120;

/** The tray's measures at a cell size, in pixels. */
interface TrayLook {
  /** Its rim's width. */
  rim: number;
  /** How far its sides drop to the lower right. */
  depth: number;
  /** Where the cells begin, from the drawing's top left corner. */
  at: number;
}

// with `flat`, Simple Pieces: no sides
function trayLook(sc: number, flat: boolean): TrayLook {
  const rim = Math.max(8, Math.min(30, Math.round(sc * 0.28)));
  const depth = flat ? 0 : Math.max(3, Math.min(14, Math.round(sc * 0.11)));
  // room for the outer line
  return { rim, depth, at: rim + 2 };
}

/** A tile's measures at a cell size, in pixels. */
interface TileLook {
  /** The gap round a tile, on each side. */
  gap: number;
  /** How far its sides drop to the lower right (and the tray's inner walls). */
  lift: number;
  /** Its corners' radius. */
  radius: number;
}

// with `flat`, Simple Pieces: square tiles with no sides
function tileLook(sc: number, flat: boolean): TileLook {
  if (flat) {
    return { gap: Math.max(1, Math.round(sc * 0.02)), lift: 0, radius: 0 };
  }
  return {
    gap: Math.max(1, Math.round(sc * 0.03)),
    lift: Math.max(2, Math.min(8, Math.round(sc * 0.07))),
    radius: Math.max(2, Math.round(sc * 0.13)),
  };
}

/** A colour darkened, for the sides. */
function shade(c: Rgb3, f: number): Rgb3 {
  return [c[0] * f, c[1] * f, c[2] * f];
}

const fmt = (v: number): string => String(Math.round(v * 100) / 100);

interface Drag {
  token: number;
  piece: number;
  pointer: number;
  x0: number;
  y0: number;
  /** Shift held: carry what is nested before moving alone. */
  carry: boolean;
  /** Where the pointer went, in cells, kept until the engine's answer came. */
  waiting: [number, number][];
  groups: ReachGroup[] | null;
  has: Has[];
  chosen: number;
  /** The stop the pieces are at or nearest to. */
  shift: [number, number];
  /** How far past it they are drawn, towards the pointer (under half a cell). */
  slide: [number, number];
  ended: boolean;
}

export class PlayBoard {
  private readonly root: HTMLElement;
  /** What the board has room in: the root hugs its drawing, so Undo and Redo sit right beside it. */
  private readonly frame: HTMLElement;
  private readonly host: BoardHost;
  private model: BoardModel | null = null;
  private places: Cell[] = [];
  private layer = 0;
  private sc = 0;
  /** Where the cells begin in the drawing, in pixels. */
  private at = 0;
  private groups: SVGGElement[] = [];
  /** The pieces' names, each in its own group, by piece. */
  private labels: SVGGElement[] = [];
  private drag: Drag | null = null;
  private tokens = 0;
  private raf = 0;
  private endAnimation: (() => void) | null = null;
  /** The root changed size while a piece was dragged or a move drawn: drawn again after. */
  private resized = false;
  /** Simple Pieces is on. */
  private simple = false;

  constructor(root: HTMLElement, frame: HTMLElement, host: BoardHost) {
    this.root = root;
    this.frame = frame;
    this.host = host;
    new ResizeObserver(() => {
      // never under a drag or a move being drawn: the pieces would be other elements
      if (this.busy) {
        this.resized = true;
      } else if (this.cellSize() !== this.sc) {
        this.draw();
      } else {
        this.inset();
      }
    }).observe(frame);
    root.addEventListener('pointerdown', (e) => {
      this.down(e);
    });
    root.addEventListener('pointermove', (e) => {
      this.move(e);
    });
    root.addEventListener('pointerup', (e) => {
      this.up(e, false);
    });
    root.addEventListener('pointercancel', (e) => {
      this.up(e, true);
    });
  }

  /** Simple Pieces: flat square pieces and a flat square tray. */
  setSimple(on: boolean): void {
    this.root.classList.toggle('simple', on);
    if (on !== this.simple) {
      this.simple = on;
      this.stop();
      this.cancelDrag();
      this.draw();
    }
  }

  /** A piece is being dragged, or a move is being drawn. */
  get busy(): boolean {
    return this.drag !== null || this.endAnimation !== null;
  }

  /** Shows a board (null: none) with its pieces at `places`, layer `layer`. */
  show(model: BoardModel | null, places: readonly Cell[], layer: number): void {
    this.stop();
    this.cancelDrag();
    this.model = model;
    this.places = places.map((c): Cell => [c[0], c[1], c[2]]);
    this.layer = layer;
    this.draw();
  }

  /** Puts the pieces at `places` (the board shown stays). */
  place(places: readonly Cell[]): void {
    this.places = places.map((c): Cell => [c[0], c[1], c[2]]);
    this.groups.forEach((g, i) => {
      this.put(g, this.places[i] ?? [0, 0, 0], [0, 0]);
    });
    this.root.dataset.places = this.places.map((c) => c.join(',')).join(' ');
  }

  /**
   * Draws a move along its route, from the pieces' places before it (the
   * board's now) or with `back` from its end to them; the caller places
   * the pieces when it is done. Resolves false when it was cut short.
   */
  animate(movers: readonly number[], route: readonly number[][], back: boolean): Promise<boolean> {
    this.stop();
    const cells = routeLength(route);
    if (this.model === null || cells === 0 || this.sc === 0) {
      return Promise.resolve(true);
    }
    // the places before the move: with `back` the board shows those after it
    const end = route.at(-1) ?? [0, 0, 0];
    const base = movers.map((m): Cell => {
      const p = this.places[m] ?? [0, 0, 0];
      return back ? [p[0] - (end[0] ?? 0), p[1] - (end[1] ?? 0), p[2] - (end[2] ?? 0)] : p;
    });
    const ms = Math.max(180, Math.min(620, 150 + 70 * cells));
    const t0 = performance.now();
    this.root.dataset.anim = '1';
    return new Promise<boolean>((resolve) => {
      const done = (whole: boolean): void => {
        cancelAnimationFrame(this.raf);
        this.endAnimation = null;
        delete this.root.dataset.anim;
        this.catchUp();
        resolve(whole);
      };
      this.endAnimation = () => {
        done(false);
      };
      const tick = (): void => {
        const t = Math.min(1, (performance.now() - t0) / ms);
        // ease in and out: a move starts and ends gently
        const e = t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
        const by = alongRoute(route, back ? 1 - e : e);
        movers.forEach((m, k) => {
          const g = this.groups[m];
          if (g !== undefined) {
            this.put(g, base[k] ?? [0, 0, 0], [by[0], by[1]]);
          }
        });
        if (t >= 1) {
          done(true);
        } else {
          this.raf = requestAnimationFrame(tick);
        }
      };
      this.raf = requestAnimationFrame(tick);
    });
  }

  /** Cuts a move being drawn short. */
  stop(): void {
    this.endAnimation?.();
  }

  // the size changed meanwhile: drawn again, once the caller has placed the pieces
  private catchUp(): void {
    if (this.resized) {
      this.resized = false;
      window.setTimeout(() => {
        if (this.busy) {
          this.resized = true;
        } else {
          this.draw();
        }
      }, 0);
    }
  }

  // the cells' size the root has room for, in pixels (0: no board)
  private cellSize(): number {
    const m = this.model;
    if (m === null) {
      return 0;
    }
    const { w, h } = this.room();
    // the largest cells the board fits at, with its tray round them
    const [X, Y] = m.size;
    let sc = Math.min(CELL_MAX, Math.floor(Math.min(w / X, h / Y)));
    for (; sc > CELL_MIN; sc--) {
      const t = trayLook(sc, this.simple);
      const room = 2 * t.at + t.depth;
      if (X * sc + room <= w && Y * sc + room <= h) {
        break;
      }
    }
    return Math.max(CELL_MIN, sc);
  }

  // the board's room in its frame (room.ts)
  private room(): BoardRoom {
    return boardRoom(this.frame, this.root);
  }

  // the board at its distance from the card's left, and at the foot of its room
  private inset(): void {
    insetBoard(this.frame, this.root);
  }

  // a piece's group at its place plus a shift, in cells
  private put(g: SVGGElement, at: Cell, shift: readonly [number, number]): void {
    const Y = this.model?.size[1] ?? 0;
    const x = this.at + (at[0] + shift[0]) * this.sc;
    const y = this.at + (Y - at[1] - shift[1]) * this.sc;
    g.style.transform = `translate(${x.toFixed(2)}px,${y.toFixed(2)}px)`;
    g.dataset.at = at.join(',');
    // its name, over the goals' outlines, goes with it
    const label = this.labels[Number(g.dataset.i)];
    if (label !== undefined) {
      label.style.transform = g.style.transform;
      label.classList.toggle('drag', g.classList.contains('drag'));
    }
  }

  // the whole drawing, at the size the root has room for
  private draw(): void {
    const m = this.model;
    const root = this.root;
    if (m === null) {
      root.innerHTML = '';
      root.style.marginLeft = '';
      root.style.marginBottom = '';
      this.labels = [];
      this.groups = [];
      this.sc = 0;
      delete root.dataset.places;
      return;
    }
    const [X, Y] = m.size;
    const sc = this.cellSize();
    this.sc = sc;
    this.resized = false;
    const look = trayLook(sc, this.simple);
    const at = look.at;
    this.at = at;
    const z = this.layer;
    // the tray in one colour: the chequer's lighter one in the Dark theme
    // (a light grey board there), its darker one in the Light theme
    const base = m.colour ?? boardColour();
    const trayColour = isDarkTheme() ? light(base) : dark(base);
    const lightTray = 3 * trayColour[0] + 6 * trayColour[1] + trayColour[2] > 5;
    const tray = this.traySvg(X * sc, Y * sc, look, trayColour);
    let floor = '';
    let grid = '';
    const floorAt = new Set(
      m.floor.filter((c) => c.at[2] === z).map((c) => `${String(c.at[0])},${String(c.at[1])}`),
    );
    const onFloor = (x: number, y: number): boolean => floorAt.has(`${String(x)},${String(y)}`);
    const squares: Square[] = [];
    for (const c of m.floor) {
      if (c.at[2] !== z) {
        continue;
      }
      const x = at + c.at[0] * sc;
      const y = at + (Y - 1 - c.at[1]) * sc;
      squares.push([c.at[0], Y - 1 - c.at[1]]);
      // the grid: a line between two cells of the floor, none round its edge
      if (onFloor(c.at[0] + 1, c.at[1])) {
        grid += `M${String(x + sc)} ${String(y)}v${String(sc)}`;
      }
      if (onFloor(c.at[0], c.at[1] - 1)) {
        grid += `M${String(x)} ${String(y + sc)}h${String(sc)}`;
      }
      floor += `<rect class="pl-cell${c.variable ? ' var' : ''}" data-at="${c.at.join(',')}" x="${String(x)}" y="${String(y)}" width="${String(sc)}" height="${String(sc)}" fill="${hex(trayColour)}"/>`;
    }
    // the floor sunk into the rim, its corners those of a tile with the
    // gap round it; its inner walls show on its upper and left edges,
    // dropping as the tiles' sides do, and its bottom is the rest
    const tile = tileLook(sc, this.simple);
    const floorRadius = tile.radius + tile.gap;
    const outlineOf = (cells: readonly Square[], inset: number, radius: number): string =>
      roundedOutline(cells, { scale: sc, at: [at, at], inset: () => inset, radius });
    const hollow = outlineOf(squares, 0, floorRadius);
    const wall = tile.lift;
    const fall = Math.max(1.5, sc * 0.03);
    // each goal: its cells tinted in the piece's colour on the bottom,
    // under the pieces, and its outline over them, so a goal shows
    // whatever stands on it; its corners round as a tile's
    let goals = '';
    let goalClips = '';
    let marks = '';
    // the outline: the top face of a piece standing on the goal, drawn
    // inside it by half the line's width so it never lies on the sides
    const line = 1.5;
    const markStyle = {
      scale: sc,
      at: [at, at] as [number, number],
      inset: (into: readonly [number, number]): number =>
        (into[0] < 0 || into[1] < 0 ? tile.gap + tile.lift : tile.gap) + line,
      radius: Math.max(1, tile.radius - line),
    };
    m.goals.forEach((g, k) => {
      const cells = g.cells.filter((c) => c[2] === z);
      if (cells.length === 0) {
        return;
      }
      const own = cells.map((c): Square => [c[0], Y - 1 - c[1]]);
      const id = `pl-goal${String(k)}`;
      goalClips += `<clipPath id="${id}"><path d="${outlineOf(own, 0, floorRadius)}"/></clipPath>`;
      let rects = '';
      for (const c of cells) {
        const x = at + c[0] * sc;
        const y = at + (Y - 1 - c[1]) * sc;
        rects += `<rect class="pl-goal" data-at="${c.join(',')}" x="${String(x)}" y="${String(y)}" width="${String(sc)}" height="${String(sc)}" fill="${hex(g.colour)}"/>`;
      }
      goals += `<g clip-path="url(#${id})">${rects}</g>`;
      // a solid line in the piece's colour, edged dark so it shows over other pieces
      const outline = roundedOutline(own, markStyle);
      marks += `<path class="pl-mark under" d="${outline}"/><path class="pl-mark" d="${outline}" stroke="${hex(g.colour)}"/>`;
    });
    const drawn = m.pieces.map((p, i) => this.pieceSvg(p, i));
    const pieces = drawn.map((d) => d.body).join('');
    const labels = drawn.map((d) => d.label).join('');
    const w = X * sc + 2 * at + look.depth;
    const h = Y * sc + 2 * at + look.depth;
    const defs =
      // a fine grain of light and dark specks
      `<filter id="pl-grain" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.85" numOctaves="3" seed="7" stitchTiles="stitch" result="n"/>` +
      `<feColorMatrix in="n" type="matrix" values="0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 0 0 2.6 0 -1.25" result="w"/>` +
      `<feColorMatrix in="n" type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -2.6 0 0 1.25" result="k"/>` +
      `<feMerge><feMergeNode in="w"/><feMergeNode in="k"/></feMerge></filter>` +
      `<filter id="pl-soft"><feGaussianBlur stdDeviation="${fmt(fall * 0.9)}"/></filter>` +
      `<linearGradient id="pl-sheen" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".3"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".08"/></linearGradient>` +
      `<clipPath id="pl-hollow"><path d="${hollow}"/></clipPath>` +
      `<clipPath id="pl-bottom"><path d="${hollow}" transform="translate(${String(wall)} ${String(wall)})"/></clipPath>` +
      // the walls: the floor less its bottom
      `<mask id="pl-walls" maskUnits="userSpaceOnUse" x="0" y="0" width="${String(w)}" height="${String(h)}"><path d="${hollow}" fill="#fff"/><path d="${hollow}" transform="translate(${String(wall)} ${String(wall)})" fill="#000"/></mask>` +
      goalClips;
    const gridColour = lightTray ? 'rgba(0,0,0,.16)' : 'rgba(255,255,255,.1)';
    root.innerHTML =
      `<svg class="pl-svg" width="${String(w)}" height="${String(h)}" viewBox="0 0 ${String(w)} ${String(h)}" data-cell="${String(sc)}" style="--pl-grid:${gridColour}"><defs>${defs}</defs>` +
      tray.under +
      // the floor's bottom with its grid, and the bottom's edge: the foot of
      // the walls, its corners rounded as the floor's are
      `<g class="pl-floor" clip-path="url(#pl-hollow)"><g clip-path="url(#pl-bottom)">${floor}${this.floorPicture(m, at, sc)}<path class="pl-grid" d="${grid}"/></g>` +
      `<path class="pl-grid" d="${hollow}" transform="translate(${String(wall)} ${String(wall)})"/></g>${tray.over}` +
      `<g class="pl-goals" clip-path="url(#pl-hollow)"><g clip-path="url(#pl-bottom)">${goals}</g></g>` +
      // the walls: the band between the floor's edge and its bottom, and
      // their shadow a little way onto the bottom
      (wall > 0
        ? `<g class="pl-sunk" clip-path="url(#pl-hollow)"><path class="pl-shadow" d="${hollow}" transform="translate(${fmt(wall + fall / 2)} ${fmt(wall + fall / 2)})" stroke-width="${fmt(2 * fall)}" filter="url(#pl-soft)"/>` +
          `<path class="pl-wall" d="${hollow}" mask="url(#pl-walls)"/></g>`
        : '') +
      `<path class="pl-hollow" d="${hollow}"/>` +
      `<g class="pl-pieces">${pieces}</g><g class="pl-marks">${marks}</g><g class="pl-labels">${labels}</g></svg>`;
    this.groups = [...root.querySelectorAll<SVGGElement>('.pl-piece')];
    this.labels = [...root.querySelectorAll<SVGGElement>('.pl-label')];
    this.place(this.places);
    this.inset();
  }

  // the tray round cells w x h pixels: what goes under the floor (its
  // sides, its body) and over it (its grain, its outer lines)
  private traySvg(
    w: number,
    h: number,
    look: TrayLook,
    colour: Rgb3,
  ): { under: string; over: string } {
    const { rim, depth, at } = look;
    const x = at - rim;
    const y = at - rim;
    const ow = w + 2 * rim;
    const oh = h + 2 * rim;
    // square corners for Simple Pieces
    const r = this.simple ? 0 : Math.round(rim * 1.3 + this.sc * 0.06);
    const rect = (dx: number, inflate: number, extra: string): string =>
      `<rect x="${fmt(x + dx - inflate)}" y="${fmt(y + dx - inflate)}" width="${fmt(ow + 2 * inflate)}" height="${fmt(oh + 2 * inflate)}" rx="${fmt(r === 0 ? 0 : Math.max(0, r + inflate))}"${extra}/>`;
    // the sides: the tray drawn again a pixel further down and right each
    // time, the lowest with its outline
    let sides = depth > 0 ? rect(depth, 0, ' class="pl-trayfoot"') : '';
    for (let k = depth - 1; k >= 1; k--) {
      sides += rect(k, 0, '');
    }
    const under = `<g class="pl-tray"><g fill="${hex(shade(colour, 0.6))}">${sides}</g>${rect(0, 0, ` fill="${hex(colour)}"`)}</g>`;
    const over =
      `<g class="pl-grain" clip-path="url(#pl-trayclip)"><clipPath id="pl-trayclip">${rect(0, 0, '')}</clipPath>${rect(0, 0, ' filter="url(#pl-grain)"')}</g>` +
      rect(0, -1, ' class="pl-trayedge"') +
      rect(0, 0.5, ' class="pl-trayline"');
    return { under, over };
  }

  // one piece: a tile over its cells of the layer shown (filled, or its
  // picture) with its sides dropping to the lower right, and its name
  // the board's picture over its floor, '' for none: its pixels to the
  // board's cells, then to the drawing (y down, the floor's corner at `at`);
  // the floor's bottom clips it, so it shows on no wall
  private floorPicture(m: BoardModel, at: number, sc: number): string {
    const p = m.picture ?? null;
    if (p === null) {
      return '';
    }
    const toBoard: Affine = [sc, 0, 0, -sc, at, at + m.size[1] * sc];
    const t = compose(toBoard, pictureToCells(p.image))
      .map((n) => n.toFixed(4))
      .join(' ');
    const [sx, sy, sw, sh] = p.image.src;
    return `<g class="pl-picture" pointer-events="none"><clipPath id="pl-floorpic"><rect x="${String(sx)}" y="${String(sy)}" width="${String(sw)}" height="${String(sh)}"/></clipPath><g transform="matrix(${t})"><image href="${p.dataUrl}" width="${String(p.width)}" height="${String(p.height)}" preserveAspectRatio="none" clip-path="url(#pl-floorpic)"/></g></g>`;
  }

  private pieceSvg(p: BoardPiece, i: number): { body: string; label: string } {
    const sc = this.sc;
    const at = this.places[i] ?? [0, 0, 0];
    const cells = p.cells.filter((c) => at[2] + c[2] === this.layer);
    const id = `plc${String(i)}`;
    // a small gap between tiles; the top face sits up and left of the
    // cells by its sides' depth
    const { gap, lift, radius } = tileLook(sc, this.simple);
    const top = roundedOutline(
      cells.map((c): Square => [c[0], -(c[1] + 1)]),
      {
        scale: sc,
        at: [0, 0],
        inset: (into) => (into[0] < 0 || into[1] < 0 ? gap + lift : gap),
        radius,
      },
    );
    // where the pointer takes the piece: its cells whole, gaps and all
    let hit = '';
    for (const c of cells) {
      hit += `<rect x="${String(c[0] * sc)}" y="${String(-(c[1] + 1) * sc)}" width="${String(sc)}" height="${String(sc)}"/>`;
    }
    let sides = '';
    for (let k = lift; k >= 1; k--) {
      sides += `<use href="#${id}t" x="${String(k)}" y="${String(k)}"/>`;
    }
    let picture = '';
    if (p.picture !== null && cells.length > 0) {
      // the picture's pixels to the shape's cells, then to this group's
      // drawing: the piece's corner at the origin, y down
      const toGroup: Affine = [sc, 0, 0, -sc, -p.origin[0] * sc, p.origin[1] * sc];
      const t = compose(toGroup, pictureToCells(p.picture.image))
        .map((n) => n.toFixed(4))
        .join(' ');
      const [sx, sy, sw, sh] = p.picture.image.src;
      picture = `<g clip-path="url(#${id})"><clipPath id="${id}s"><rect x="${String(sx)}" y="${String(sy)}" width="${String(sw)}" height="${String(sh)}"/></clipPath><g transform="matrix(${t})"><image href="${p.picture.dataUrl}" width="${String(p.picture.width)}" height="${String(p.picture.height)}" preserveAspectRatio="none" clip-path="url(#${id}s)"/></g></g>`;
    }
    let name = '';
    if (p.name !== '' && cells.length > 0) {
      // at the bottom centre of its lowest cells' top face, as the 2D grids write it
      const low = Math.min(...cells.map((c) => c[1]));
      const row = cells.filter((c) => c[1] === low).map((c) => c[0]);
      const x = ((Math.min(...row) + Math.max(...row) + 1) / 2) * sc - lift / 2;
      const size = Math.max(9, Math.min(15, Math.round(sc * 0.26)));
      // clear of the goal's outline along the top face's lower edge
      name = `<text class="pl-name" x="${x.toFixed(1)}" y="${String(-low * sc - lift - gap - Math.round(sc * 0.13))}" font-size="${String(size)}" fill="${contrast(p.colour)}">${esc(p.name)}</text>`;
    }
    return {
      body:
        `<g class="pl-piece" data-i="${String(i)}"><defs><path id="${id}t" d="${top}"/><clipPath id="${id}"><path d="${top}"/></clipPath></defs>` +
        `<g class="pl-hit">${hit}</g><g class="pl-body"><g fill="${hex(shade(p.colour, 0.62))}">${sides}</g>` +
        `<path d="${top}" fill="${hex(p.colour)}"/>${picture}<path class="pl-face" d="${top}"/></g></g>`,
      // its name in a layer of its own over the goals' outlines, moved with it
      label: `<g class="pl-label" data-i="${String(i)}">${name}</g>`,
    };
  }

  // where the pointer is from where the drag began, in cells (not whole ones)
  private pointerAt(d: Drag, e: PointerEvent): [number, number] {
    return [(e.clientX - d.x0) / this.sc, (d.y0 - e.clientY) / this.sc];
  }

  private down(e: PointerEvent): void {
    if (e.button !== 0 || this.drag !== null || this.endAnimation !== null || this.sc === 0) {
      return;
    }
    const g = (e.target as Element).closest<SVGGElement>('.pl-piece');
    const piece = Number(g?.dataset.i ?? -1);
    if (g === null || piece < 0) {
      return;
    }
    e.preventDefault();
    this.root.focus({ preventScroll: true });
    this.root.setPointerCapture(e.pointerId);
    const d: Drag = {
      token: ++this.tokens,
      piece,
      pointer: e.pointerId,
      x0: e.clientX,
      y0: e.clientY,
      carry: e.shiftKey,
      waiting: [],
      groups: null,
      has: [],
      chosen: -1,
      shift: [0, 0],
      slide: [0, 0],
      ended: false,
    };
    this.drag = d;
    g.classList.add('drag');
    // drawn last: over the pieces it passes, and its name over theirs
    g.parentNode?.appendChild(g);
    const label = this.labels[piece];
    label?.parentNode?.appendChild(label);
    this.root.dataset.drag = String(piece);
    void this.host.reach(piece).then(
      (groups) => {
        if (this.drag !== d) {
          return;
        }
        if (groups === null) {
          this.cancelDrag();
          return;
        }
        d.groups = groups;
        d.has = groups.map((x) => stopsIn(x.stops));
        // what the pointer did meanwhile, in order: a quick flick is not lost
        for (const target of d.waiting) {
          this.step(d, target);
        }
        d.waiting = [];
        if (d.ended) {
          this.finish(d);
        }
      },
      () => {
        if (this.drag === d) {
          this.cancelDrag();
        }
      },
    );
  }

  private move(e: PointerEvent): void {
    const d = this.drag;
    if (d?.pointer !== e.pointerId || d.ended) {
      return;
    }
    const target = this.pointerAt(d, e);
    if (d.groups === null) {
      d.waiting.push(target);
    } else {
      this.step(d, target);
    }
  }

  private up(e: PointerEvent, cancelled: boolean): void {
    const d = this.drag;
    if (d?.pointer !== e.pointerId) {
      return;
    }
    if (cancelled) {
      this.cancelDrag();
      return;
    }
    const target = this.pointerAt(d, e);
    d.ended = true;
    if (d.groups === null) {
      d.waiting.push(target);
    } else {
      this.step(d, target);
      this.finish(d);
    }
  }

  // the pieces follow the pointer, as far as they can go: to the stop
  // nearest it, and from there part of the way to the next towards it
  private step(d: Drag, pointer: [number, number]): void {
    const groups = d.groups ?? [];
    // twice at most: a group back where it began lets the pointer choose again
    for (let pass = 0; pass < 2; pass++) {
      if (d.chosen < 0) {
        // which pieces move is settled by the way the pointer first goes:
        // the piece alone when it can go that way, else with what it
        // carries (Shift: the other way round)
        const toward = aim(pointer);
        const order = groups.map((_, i) => i);
        d.chosen =
          toward === null ? -1 : groupFor(d.has, d.carry ? order.reverse() : order, toward);
        if (d.chosen < 0) {
          return;
        }
      }
      const has = d.has[d.chosen];
      const movers = groups[d.chosen]?.movers ?? [];
      if (has === undefined) {
        return;
      }
      d.shift = follow(d.shift, [Math.round(pointer[0]), Math.round(pointer[1])], has);
      d.slide = slide(d.shift, pointer, has);
      const by: [number, number] = [d.shift[0] + d.slide[0], d.shift[1] + d.slide[1]];
      for (const m of movers) {
        const g = this.groups[m];
        if (g !== undefined) {
          g.classList.add('drag');
          this.put(g, this.places[m] ?? [0, 0, 0], by);
        }
      }
      if (by[0] !== 0 || by[1] !== 0) {
        return;
      }
      // back where it began: the pointer chooses again
      for (const m of movers) {
        if (m !== d.piece) {
          this.groups[m]?.classList.remove('drag');
        }
      }
      d.chosen = -1;
    }
  }

  // the drag is over: a move, when the pieces ended somewhere new
  private finish(d: Drag): void {
    const group = d.chosen < 0 ? undefined : d.groups?.[d.chosen];
    const stop = group?.stops.find(
      (s) => s.by[0] === d.shift[0] && s.by[1] === d.shift[1] && (s.by[2] ?? 0) === 0,
    );
    this.endDrag();
    if (group !== undefined && stop !== undefined) {
      this.host.moved({ movers: [...group.movers], by: [d.shift[0], d.shift[1], 0] }, stop.solved);
    } else {
      this.place(this.places);
    }
  }

  private endDrag(): void {
    const d = this.drag;
    if (d === null) {
      return;
    }
    this.drag = null;
    delete this.root.dataset.drag;
    if (this.root.hasPointerCapture(d.pointer)) {
      this.root.releasePointerCapture(d.pointer);
    }
    for (const g of this.groups) {
      g.classList.remove('drag');
    }
    this.catchUp();
  }

  // the drag is dropped: the pieces go back
  private cancelDrag(): void {
    if (this.drag !== null) {
      this.endDrag();
      this.place(this.places);
    }
  }
}
