// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the two boards of the Play workspace share (decisions 0039 and
// 0042): a sliding puzzle's (board.ts) and a stacking puzzle's
// (stack-board.ts). The frame's size followed, but never under a drag or a
// move being drawn (the pieces would be other elements then): drawn again
// once that is over. The pointer wired to the board's down, move and up. A
// drag's end and its dropping. The frame loop that draws a move, eased in
// and out. And the buttons beside the board that toggle what it shows.

import { byId } from '../shell/dom.ts';
import { ic } from '../shell/icons.ts';
import type { IconName } from '../shell/icons.ts';

/** The style transform that puts a piece's or a disc's group at a point of the drawing, in pixels. */
export function translate(x: number, y: number): string {
  return `translate(${x.toFixed(2)}px,${y.toFixed(2)}px)`;
}

/** A board of the Play workspace, with its drag of type D (the pointer that drags). */
export abstract class BoardShell<D extends { pointer: number }> {
  protected readonly root: HTMLElement;
  /** What the board has room in: the root hugs its drawing, so the buttons sit right beside it. */
  protected readonly frame: HTMLElement;
  protected drag: D | null = null;
  /** The frame changed size while something was dragged or a move drawn: drawn again after. */
  protected resized = false;
  private raf = 0;
  private endAnimation: (() => void) | null = null;

  protected constructor(root: HTMLElement, frame: HTMLElement) {
    this.root = root;
    this.frame = frame;
    new ResizeObserver(() => {
      if (this.busy) {
        this.resized = true;
      } else {
        this.fit();
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

  /** The frame changed size, with nothing dragged or drawn: the drawing fitted to it. */
  protected abstract fit(): void;
  /** The frame changed size under a drag or a move, now over: the whole board drawn again. */
  protected abstract redraw(): void;
  /** A drag was dropped: what it moved goes back. */
  protected abstract dropped(): void;
  protected abstract down(e: PointerEvent): void;
  protected abstract move(e: PointerEvent): void;
  protected abstract up(e: PointerEvent, canceled: boolean): void;

  /** Something is being dragged, or a move is being drawn. */
  get busy(): boolean {
    return this.drag !== null || this.endAnimation !== null;
  }

  /** Cuts a move being drawn short. */
  stop(): void {
    this.endAnimation?.();
  }

  /**
   * Draws a move for `ms` milliseconds: `at` puts what moves at a part of
   * its way (0 to 1, eased in and out: a move starts and ends gently), once
   * a frame. Resolves false when it was cut short (stop, or another move).
   */
  protected animateFor(ms: number, at: (part: number) => void): Promise<boolean> {
    this.stop();
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
        at(t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2);
        if (t >= 1) {
          done(true);
        } else {
          this.raf = requestAnimationFrame(tick);
        }
      };
      this.raf = requestAnimationFrame(tick);
    });
  }

  /** The drag is over: its pointer let go, and the board drawn again if the frame changed size meanwhile. */
  protected endDrag(): void {
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

  /** The drag is dropped: what it moved goes back. */
  protected cancelDrag(): void {
    if (this.drag !== null) {
      this.endDrag();
      this.dropped();
    }
  }

  // the size changed meanwhile: drawn again, once the caller has placed what moved
  private catchUp(): void {
    if (this.resized) {
      this.resized = false;
      window.setTimeout(() => {
        if (this.busy) {
          this.resized = true;
        } else {
          this.redraw();
        }
      }, 0);
    }
  }
}

/** What a toggle button beside the board shows in one of its states: what a click will do. */
export interface ToggleLook {
  icon: IconName;
  label: string;
  /** The tooltip after the label. */
  does: string;
}

/**
 * Draws toggle button `id` beside the board: pressed (aria-pressed) or
 * not, its icon, words and tooltip those of the state it is in.
 */
export function toggleButton(
  id: string,
  pressed: boolean,
  looks: { pressed: ToggleLook; released: ToggleLook },
): void {
  const look = pressed ? looks.pressed : looks.released;
  const b = byId(id);
  b.innerHTML = `${ic(look.icon, 22)}<span>${look.label}</span>`;
  b.title = `${look.label}: ${look.does}`;
  b.setAttribute('aria-pressed', String(pressed));
}
