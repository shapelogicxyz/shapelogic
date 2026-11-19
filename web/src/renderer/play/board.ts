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
import { alongRoute, follow, groupFor, routeLength, stopsIn } from './follow.ts';
import type { Has } from './follow.ts';
import type { Cell, Move } from './game.ts';

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

const MARGIN = 10;
const CELL_MIN = 14;
const CELL_MAX = 120;

interface Drag {
  token: number;
  piece: number;
  pointer: number;
  x0: number;
  y0: number;
  /** Shift held: carry what is nested before moving alone. */
  carry: boolean;
  /** The pointer's cells, kept until the engine's answer came. */
  waiting: [number, number][];
  groups: ReachGroup[] | null;
  has: Has[];
  chosen: number;
  shift: [number, number];
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
  private groups: SVGGElement[] = [];
  private drag: Drag | null = null;
  private tokens = 0;
  private raf = 0;
  private endAnimation: (() => void) | null = null;
  /** The root changed size while a piece was dragged or a move drawn: drawn again after. */
  private resized = false;

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
    // the frame's room, less its padding and what else is in it (the buttons beside the board)
    const frame = this.frame;
    const style = getComputedStyle(frame);
    const px = (v: string): number => Number.parseFloat(v) || 0;
    let w = frame.clientWidth - px(style.paddingLeft) - px(style.paddingRight);
    const h = frame.clientHeight - px(style.paddingTop) - px(style.paddingBottom);
    for (const e of frame.children) {
      if (e !== this.root && e instanceof HTMLElement) {
        w -= e.offsetWidth + px(style.columnGap);
      }
    }
    return Math.max(
      CELL_MIN,
      Math.min(
        CELL_MAX,
        Math.floor(Math.min((w - 2 * MARGIN) / m.size[0], (h - 2 * MARGIN) / m.size[1])),
      ),
    );
  }

  // a piece's group at its place plus a shift, in cells
  private put(g: SVGGElement, at: Cell, shift: readonly [number, number]): void {
    const Y = this.model?.size[1] ?? 0;
    const x = MARGIN + (at[0] + shift[0]) * this.sc;
    const y = MARGIN + (Y - at[1] - shift[1]) * this.sc;
    g.style.transform = `translate(${x.toFixed(2)}px,${y.toFixed(2)}px)`;
    g.dataset.at = at.join(',');
  }

  // the whole drawing, at the size the root has room for
  private draw(): void {
    const m = this.model;
    const root = this.root;
    if (m === null) {
      root.innerHTML = '';
      this.groups = [];
      this.sc = 0;
      delete root.dataset.places;
      return;
    }
    const [X, Y] = m.size;
    const sc = this.cellSize();
    this.sc = sc;
    this.resized = false;
    const z = this.layer;
    const base = m.colour ?? boardColour();
    let floor = '';
    for (const c of m.floor) {
      if (c.at[2] !== z) {
        continue;
      }
      const odd = ((c.at[0] + c.at[1] + c.at[2]) & 1) === 1;
      const x = MARGIN + c.at[0] * sc;
      const y = MARGIN + (Y - 1 - c.at[1]) * sc;
      floor += `<rect class="pl-cell${c.variable ? ' var' : ''}" data-at="${c.at.join(',')}" x="${String(x)}" y="${String(y)}" width="${String(sc)}" height="${String(sc)}" fill="${hex(odd ? dark(base) : light(base))}"/>`;
    }
    // each goal: its cells tinted in the piece's colour under the pieces,
    // and its outline over them, so a goal shows whatever stands on it
    let goals = '';
    let marks = '';
    const inset = Math.max(3, Math.round(sc * 0.07));
    for (const g of m.goals) {
      const cells = g.cells.filter((c) => c[2] === z);
      const has = new Set(cells.map((c) => `${String(c[0])},${String(c[1])}`));
      let outline = '';
      for (const c of cells) {
        const x = MARGIN + c[0] * sc;
        const y = MARGIN + (Y - 1 - c[1]) * sc;
        goals += `<rect class="pl-goal" data-at="${c.join(',')}" x="${String(x)}" y="${String(y)}" width="${String(sc)}" height="${String(sc)}" fill="${hex(g.colour)}"/>`;
        // an edge where the goal ends, drawn a little inside it
        const edge = (dx: number, dy: number): boolean =>
          !has.has(`${String(c[0] + dx)},${String(c[1] + dy)}`);
        const l = x + (edge(-1, 0) ? inset : 0);
        const r = x + sc - (edge(1, 0) ? inset : 0);
        const t = y + (edge(0, 1) ? inset : 0);
        const b = y + sc - (edge(0, -1) ? inset : 0);
        if (edge(-1, 0)) {
          outline += `M${String(l)} ${String(t)}V${String(b)}`;
        }
        if (edge(1, 0)) {
          outline += `M${String(r)} ${String(t)}V${String(b)}`;
        }
        if (edge(0, 1)) {
          outline += `M${String(l)} ${String(t)}H${String(r)}`;
        }
        if (edge(0, -1)) {
          outline += `M${String(l)} ${String(b)}H${String(r)}`;
        }
      }
      if (outline !== '') {
        marks += `<path class="pl-mark under" d="${outline}"/><path class="pl-mark" d="${outline}"/>`;
      }
    }
    const pieces = m.pieces.map((p, i) => this.pieceSvg(p, i)).join('');
    const w = X * sc + 2 * MARGIN;
    const h = Y * sc + 2 * MARGIN;
    root.innerHTML = `<svg class="pl-svg" width="${String(w)}" height="${String(h)}" viewBox="0 0 ${String(w)} ${String(h)}" data-cell="${String(sc)}"><g class="pl-floor">${floor}</g><g class="pl-goals">${goals}</g><g class="pl-pieces">${pieces}</g><g class="pl-marks">${marks}</g></svg>`;
    this.groups = [...root.querySelectorAll<SVGGElement>('.pl-piece')];
    this.place(this.places);
  }

  // one piece: its cells of the layer shown (filled, or its picture), its outline and its name
  private pieceSvg(p: BoardPiece, i: number): string {
    const sc = this.sc;
    const at = this.places[i] ?? [0, 0, 0];
    const cells = p.cells.filter((c) => at[2] + c[2] === this.layer);
    const has = new Set(cells.map((c) => `${String(c[0])},${String(c[1])}`));
    const fill = hex(p.colour);
    let rects = '';
    let outline = '';
    for (const c of cells) {
      const x = c[0] * sc;
      const y = -(c[1] + 1) * sc;
      rects += `<rect x="${String(x)}" y="${String(y)}" width="${String(sc)}" height="${String(sc)}"/>`;
      // an edge where the piece ends
      const side = (dx: number, dy: number, d: string): void => {
        if (!has.has(`${String(c[0] + dx)},${String(c[1] + dy)}`)) {
          outline += d;
        }
      };
      side(-1, 0, `M${String(x)} ${String(y)}v${String(sc)}`);
      side(1, 0, `M${String(x + sc)} ${String(y)}v${String(sc)}`);
      side(0, 1, `M${String(x)} ${String(y)}h${String(sc)}`);
      side(0, -1, `M${String(x)} ${String(y + sc)}h${String(sc)}`);
    }
    const id = `plc${String(i)}`;
    let picture = '';
    if (p.picture !== null && cells.length > 0) {
      // the picture's pixels to the shape's cells, then to this group's
      // drawing: the piece's corner at the origin, y down
      const toGroup: Affine = [sc, 0, 0, -sc, -p.origin[0] * sc, p.origin[1] * sc];
      const t = compose(toGroup, pictureToCells(p.picture.image))
        .map((n) => n.toFixed(4))
        .join(' ');
      const [sx, sy, sw, sh] = p.picture.image.src;
      picture = `<clipPath id="${id}s"><rect x="${String(sx)}" y="${String(sy)}" width="${String(sw)}" height="${String(sh)}"/></clipPath><g transform="matrix(${t})"><image href="${p.picture.dataUrl}" width="${String(p.picture.width)}" height="${String(p.picture.height)}" preserveAspectRatio="none" clip-path="url(#${id}s)"/></g>`;
    }
    let name = '';
    if (p.name !== '' && cells.length > 0) {
      // at the bottom centre of its lowest cells, as the 2D grids write it
      const low = Math.min(...cells.map((c) => c[1]));
      const row = cells.filter((c) => c[1] === low).map((c) => c[0]);
      const x = ((Math.min(...row) + Math.max(...row) + 1) / 2) * sc;
      const size = Math.max(9, Math.min(15, Math.round(sc * 0.26)));
      name = `<text class="pl-name" x="${x.toFixed(1)}" y="${String(-low * sc - Math.round(sc * 0.12))}" font-size="${String(size)}" fill="${contrast(p.colour)}">${esc(p.name)}</text>`;
    }
    return `<g class="pl-piece" data-i="${String(i)}"><clipPath id="${id}">${rects}</clipPath><g clip-path="url(#${id})"><g fill="${fill}">${rects}</g>${picture}</g><path class="pl-edge" d="${outline}"/>${name}</g>`;
  }

  // the pointer's cell from where the drag began
  private cellOf(d: Drag, e: PointerEvent): [number, number] {
    return [Math.round((e.clientX - d.x0) / this.sc), Math.round((d.y0 - e.clientY) / this.sc)];
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
      ended: false,
    };
    this.drag = d;
    g.classList.add('drag');
    // drawn last: over the pieces it passes
    g.parentNode?.appendChild(g);
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
    const target = this.cellOf(d, e);
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
    const target = this.cellOf(d, e);
    d.ended = true;
    if (d.groups === null) {
      d.waiting.push(target);
    } else {
      this.step(d, target);
      this.finish(d);
    }
  }

  // the pieces follow the pointer to its cell, as far as they can go
  private step(d: Drag, target: [number, number]): void {
    const groups = d.groups ?? [];
    if (d.chosen < 0) {
      // which pieces move is settled by the first step: the piece alone
      // when it can go that way, else with what it carries (Shift: the
      // other way round)
      const order = groups.map((_, i) => i);
      d.chosen = groupFor(d.has, d.carry ? order.reverse() : order, target);
      if (d.chosen < 0) {
        return;
      }
    }
    const has = d.has[d.chosen];
    const movers = groups[d.chosen]?.movers ?? [];
    if (has === undefined) {
      return;
    }
    d.shift = follow(d.shift, target, has);
    for (const m of movers) {
      const g = this.groups[m];
      if (g !== undefined) {
        g.classList.add('drag');
        this.put(g, this.places[m] ?? [0, 0, 0], d.shift);
      }
    }
    if (d.shift[0] === 0 && d.shift[1] === 0) {
      // back where it began: the next step chooses again
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
