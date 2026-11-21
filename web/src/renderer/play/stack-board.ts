// SPDX-License-Identifier: GPL-3.0-or-later
//
// The board of the Play workspace for a stacking puzzle (decision 0042):
// the rods seen from the front as an SVG drawing (stack-look.ts: its
// measures and its markup), each disc a bar as wide as its size, resting
// at the height the engine gave it.
//
// A top disc is moved with the pointer: dragged to another rod, or clicked
// (it lifts) and the rod it goes to clicked after. When a disc is taken
// hold of the host asks the engine which transfers the position allows, so
// the page works out no move of its own; the rods the disc may go to are
// marked meanwhile. A move made again (Undo, Redo, the playback) is drawn
// as a solution's transfer is: lifted above the rods, across, and down.

import { boardRoom, insetBoard } from './room.ts';
import type { Stacks } from './stack-game.ts';
import { discAt, levelY, LIFT, rodAt, stackLook, stackSvg } from './stack-look.ts';
import type { StackLook, StackModel } from './stack-look.ts';

/** A transfer the position allows (stacking.playMoves). */
export interface Allowed {
  from: number;
  to: number;
  z: number;
  solved: boolean;
}

/** What the board asks of the workspace. */
export interface StackBoardHost {
  /** The transfers the position shown allows; null when no disc can be moved now. */
  moves(): Promise<Allowed[] | null>;
  /** A disc was put on another rod. */
  moved(move: Allowed): void;
  /** A disc was let go over a rod it may not go onto. */
  refused(from: number, to: number): void;
}

interface Drag {
  pointer: number;
  /** The rod the disc is taken from, and the disc. */
  rod: number;
  disc: number;
  /** The pointer and the disc's group when it was taken hold of. */
  x0: number;
  y0: number;
  gx: number;
  gy: number;
  /** The pointer went somewhere: a drag, not a click. */
  went: boolean;
  /** The transfers the position allows, once the engine answered. */
  allowed: Allowed[] | null;
  /** Let go before the answer came: over this rod. */
  ended: number | null;
}

/** How far the pointer goes before a press is a drag, in pixels. */
const DRAG_FROM = 5;

export class StackBoard {
  private readonly root: HTMLElement;
  /** What the board has room in: the root hugs its drawing, so the buttons sit right beside it. */
  private readonly frame: HTMLElement;
  private readonly host: StackBoardHost;
  private model: StackModel | null = null;
  private stacks: Stacks = [];
  private look: StackLook | null = null;
  /** The discs' groups, by disc. */
  private groups: (SVGGElement | undefined)[] = [];
  private drag: Drag | null = null;
  /** A disc clicked: lifted over its rod until the rod it goes to is clicked. */
  private held: { rod: number; disc: number; allowed: Allowed[] | null } | null = null;
  private raf = 0;
  private endAnimation: (() => void) | null = null;
  /** The frame changed size while a disc was dragged or a move drawn: drawn again after. */
  private resized = false;

  constructor(root: HTMLElement, frame: HTMLElement, host: StackBoardHost) {
    this.root = root;
    this.frame = frame;
    this.host = host;
    new ResizeObserver(() => {
      if (this.model === null) {
        return;
      }
      // never under a drag or a move being drawn: the discs would be other elements
      if (this.busy) {
        this.resized = true;
      } else if (!this.sameLook()) {
        this.draw();
      } else {
        insetBoard(this.frame, this.root);
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

  /** A disc is being dragged, or a move is being drawn. */
  get busy(): boolean {
    return this.drag !== null || this.endAnimation !== null;
  }

  /** Shows a board (null: none) with its discs at `stacks`. */
  show(model: StackModel | null, stacks: Stacks): void {
    this.stop();
    this.cancelDrag();
    this.held = null;
    this.model = model;
    this.stacks = stacks;
    this.draw();
  }

  /** Puts the discs at `stacks` (the board shown stays); a disc held is put down. */
  place(stacks: Stacks): void {
    this.stacks = stacks;
    this.held = null;
    this.settle();
  }

  /**
   * Draws a transfer: disc `disc` from where it rests on one rod, up over
   * the rods, across and down to where it rests on another; the caller
   * places the discs when it is done. Resolves false when it was cut short.
   */
  animate(
    disc: number,
    from: { rod: number; z: number },
    to: { rod: number; z: number },
  ): Promise<boolean> {
    this.stop();
    const m = this.model;
    const look = this.look;
    const g = this.groups[disc];
    const size = m?.discs[disc]?.size;
    if (m === null || look === null || g === undefined || size === undefined) {
      return Promise.resolve(true);
    }
    this.putDown();
    const a = discAt(m, look, size, from.rod, from.z);
    const b = discAt(m, look, size, to.rod, to.z);
    const lift = levelY(look, look.levels - 1 + LIFT);
    // the three runs of the way: up, across, down
    const runs: [number, number, number] = [
      Math.abs(a[1] - lift),
      Math.abs(b[0] - a[0]),
      Math.abs(b[1] - lift),
    ];
    const whole = runs[0] + runs[1] + runs[2];
    if (whole === 0) {
      return Promise.resolve(true);
    }
    const at = (f: number): [number, number] => {
      let d = f * whole;
      if (d <= runs[0]) {
        return [a[0], a[1] + (lift - a[1]) * (runs[0] === 0 ? 1 : d / runs[0])];
      }
      d -= runs[0];
      if (d <= runs[1]) {
        return [a[0] + (b[0] - a[0]) * (runs[1] === 0 ? 1 : d / runs[1]), lift];
      }
      d -= runs[1];
      return [b[0], lift + (b[1] - lift) * (runs[2] === 0 ? 1 : d / runs[2])];
    };
    const ms = Math.max(240, Math.min(560, 200 + 0.55 * whole));
    const t0 = performance.now();
    this.root.dataset.anim = '1';
    // over the discs it passes
    g.parentNode?.appendChild(g);
    return new Promise<boolean>((resolve) => {
      const done = (all: boolean): void => {
        cancelAnimationFrame(this.raf);
        this.endAnimation = null;
        delete this.root.dataset.anim;
        this.catchUp();
        resolve(all);
      };
      this.endAnimation = () => {
        done(false);
      };
      const tick = (): void => {
        const t = Math.min(1, (performance.now() - t0) / ms);
        // ease in and out: a move starts and ends gently
        const e = t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
        const [x, y] = at(e);
        g.style.transform = `translate(${x.toFixed(2)}px,${y.toFixed(2)}px)`;
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

  // the size changed meanwhile: drawn again, once the caller has placed the discs
  private catchUp(): void {
    if (this.resized) {
      this.resized = false;
      window.setTimeout(() => {
        if (this.busy) {
          this.resized = true;
        } else if (this.model !== null) {
          this.draw();
        }
      }, 0);
    }
  }

  // the measures the frame has room for
  private measure(): StackLook | null {
    const m = this.model;
    if (m === null) {
      return null;
    }
    const { w, h } = boardRoom(this.frame, this.root);
    return stackLook(m, w, h);
  }

  private sameLook(): boolean {
    const now = this.measure();
    const had = this.look;
    return now !== null && had !== null && now.width === had.width && now.height === had.height;
  }

  // the whole drawing, at the size the frame has room for
  private draw(): void {
    const m = this.model;
    const root = this.root;
    this.resized = false;
    const look = this.measure();
    this.look = look;
    if (m === null || look === null) {
      root.innerHTML = '';
      root.style.marginLeft = '';
      root.style.marginBottom = '';
      this.groups = [];
      delete root.dataset.stacks;
      delete root.dataset.held;
      return;
    }
    root.innerHTML = stackSvg(m, this.stacks, look);
    this.groups = [];
    for (const g of root.querySelectorAll<SVGGElement>('.sk-disc')) {
      this.groups[Number(g.dataset.i)] = g;
    }
    insetBoard(this.frame, root);
    this.settle();
  }

  // every disc where the stacks have it, the one held lifted over its rod
  private settle(): void {
    const m = this.model;
    const look = this.look;
    const root = this.root;
    if (m === null || look === null) {
      return;
    }
    this.stacks.forEach((rod, r) => {
      rod.forEach((d, h) => {
        const g = this.groups[d.disc];
        const size = m.discs[d.disc]?.size;
        if (g === undefined || size === undefined) {
          return;
        }
        const up = this.held?.disc === d.disc;
        const [x, y] = discAt(m, look, size, r, up ? look.levels - 1 + LIFT : d.z);
        g.style.transform = `translate(${x.toFixed(2)}px,${y.toFixed(2)}px)`;
        g.dataset.rod = String(r);
        g.dataset.z = String(d.z);
        g.classList.toggle('top', h === rod.length - 1);
        g.classList.toggle('held', up);
        g.classList.remove('drag');
      });
    });
    root.dataset.stacks = this.stacks.map((rod) => rod.map((d) => d.disc).join(',')).join('|');
    if (this.held === null) {
      delete root.dataset.held;
    } else {
      root.dataset.held = String(this.held.rod);
    }
    this.mark(this.held?.rod ?? null, this.held?.allowed ?? null);
  }

  // the rods a disc taken from `from` may go to, marked (null: none marked)
  private mark(from: number | null, allowed: readonly Allowed[] | null): void {
    // on the rod's strip (the pointer's shape) and on its glow
    for (const e of this.root.querySelectorAll<SVGElement>('.sk-col, .sk-glow')) {
      const rod = Number(e.dataset.rod);
      e.classList.toggle('has', (this.stacks[rod]?.length ?? 0) > 0);
      e.classList.toggle(
        'ok',
        from !== null && allowed?.some((a) => a.from === from && a.to === rod) === true,
      );
    }
  }

  // the rod under the pointer
  private rodUnder(e: PointerEvent): number | null {
    const m = this.model;
    const look = this.look;
    const svg = this.root.firstElementChild;
    if (m === null || look === null || svg === null) {
      return null;
    }
    return rodAt(m, look, e.clientX - svg.getBoundingClientRect().left);
  }

  // the disc held goes back on its rod
  private putDown(): void {
    if (this.held !== null) {
      this.held = null;
      this.settle();
    }
  }

  private down(e: PointerEvent): void {
    if (e.button !== 0 || this.busy || this.look === null) {
      return;
    }
    const rod = this.rodUnder(e);
    if (rod === null) {
      return;
    }
    e.preventDefault();
    this.root.focus({ preventScroll: true });
    const held = this.held;
    if (held !== null) {
      // the rod the lifted disc goes to (its own rod: it is put down)
      this.held = null;
      if (rod === held.rod) {
        this.settle();
        return;
      }
      void this.finish(held.rod, rod, held.allowed);
      return;
    }
    const top = this.stacks[rod]?.at(-1);
    const g = top === undefined ? undefined : this.groups[top.disc];
    if (top === undefined || g === undefined) {
      return;
    }
    this.root.setPointerCapture(e.pointerId);
    const where = /translate\(([-\d.]+)px,\s*([-\d.]+)px\)/.exec(g.style.transform);
    const d: Drag = {
      pointer: e.pointerId,
      rod,
      disc: top.disc,
      x0: e.clientX,
      y0: e.clientY,
      gx: Number(where?.[1] ?? 0),
      gy: Number(where?.[2] ?? 0),
      went: false,
      allowed: null,
      ended: null,
    };
    this.drag = d;
    g.classList.add('drag');
    // drawn last: over the discs it passes
    g.parentNode?.appendChild(g);
    this.root.dataset.drag = String(top.disc);
    this.mark(rod, null);
    void this.host.moves().then(
      (allowed) => {
        if (this.drag !== d) {
          return;
        }
        if (allowed === null) {
          this.cancelDrag();
          return;
        }
        d.allowed = allowed;
        this.mark(rod, allowed);
        if (d.ended !== null) {
          this.release(d, d.ended);
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
    const look = this.look;
    if (d?.pointer !== e.pointerId || d.ended !== null || look === null) {
      return;
    }
    const dx = e.clientX - d.x0;
    const dy = e.clientY - d.y0;
    if (!d.went && Math.hypot(dx, dy) < DRAG_FROM) {
      return;
    }
    d.went = true;
    // the disc with the pointer, kept inside the drawing
    const g = this.groups[d.disc];
    const w = (2 * (this.model?.discs[d.disc]?.size ?? 1) + 1) * look.u;
    const x = Math.max(0, Math.min(look.width - w, d.gx + dx));
    const y = Math.max(0, Math.min(look.height - look.pad - look.base - look.v, d.gy + dy));
    if (g !== undefined) {
      g.style.transform = `translate(${x.toFixed(2)}px,${y.toFixed(2)}px)`;
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
    const rod = this.rodUnder(e) ?? d.rod;
    if (d.allowed === null) {
      // the engine has not answered yet: settled when it does
      d.ended = rod;
    } else {
      this.release(d, rod);
    }
  }

  // the disc let go over a rod: a click lifts it, a drag puts it there
  private release(d: Drag, rod: number): void {
    const allowed = d.allowed;
    this.endDrag();
    if (!d.went || rod === d.rod) {
      // a click: the disc waits lifted for the rod it goes to; dragged
      // back over its own rod, it is put down
      this.held = d.went ? null : { rod: d.rod, disc: d.disc, allowed };
      this.settle();
      return;
    }
    void this.finish(d.rod, rod, allowed);
  }

  // the top disc of `from` onto `to`, when the position allows it
  private async finish(from: number, to: number, known: Allowed[] | null): Promise<void> {
    const stacks = this.stacks;
    let allowed = known;
    if (allowed === null) {
      try {
        allowed = await this.host.moves();
      } catch {
        allowed = null;
      }
    }
    if (this.stacks !== stacks) {
      // the position is another meanwhile
      return;
    }
    const move = allowed?.find((a) => a.from === from && a.to === to);
    this.settle();
    if (move !== undefined) {
      this.host.moved(move);
    } else if (allowed !== null) {
      this.host.refused(from, to);
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
    this.catchUp();
  }

  // the drag is dropped: the disc goes back
  private cancelDrag(): void {
    if (this.drag !== null) {
      this.endDrag();
      this.settle();
    }
  }
}
