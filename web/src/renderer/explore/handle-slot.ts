// SPDX-License-Identifier: GPL-3.0-or-later
//
// One explorer handle (an assembler's for Placements or Step), opened once
// however often it is asked for, and given back when it is dropped: a
// handle whose reply comes after the drawer was closed (or the slot reset)
// is closed at once rather than kept, so the engine does not hold that
// puzzle copy for the rest of the session (cleanup item 6.7). An opening
// under way is told when the slot is dropped (cleanup item 18.1: the
// engine prepares an assembly explorer for as long as its matrix takes,
// and a drawer that closed must end that). Free of the page; explorer.ts
// holds two.

/** A handle being opened. */
export interface Opening {
  /** The handle, or null when it could not be opened or the opening was canceled. */
  handle: Promise<string | null>;
  /**
   * The slot was dropped while this was under way: stop it (`close`: and
   * close what the engine holds for it; without, the engine dropped it
   * already).
   */
  cancel(close: boolean): void;
}

export class HandleSlot {
  /** The handle open now, or null. */
  handle: string | null = null;
  private opening: Promise<string | null> | null = null;
  private cancelOpening: ((close: boolean) => void) | null = null;
  private readonly release: (handle: string) => void;

  /** `release` closes a handle no longer wanted (explore.close). */
  constructor(release: (handle: string) => void) {
    this.release = release;
  }

  /** The handle, opened by `open` when there is none and none is being opened. */
  ensure(open: () => Opening): Promise<string | null> {
    if (this.handle !== null) {
      return Promise.resolve(this.handle);
    }
    if (this.opening === null) {
      const o = open();
      const p: Promise<string | null> = o.handle.then((h) => {
        if (this.opening !== p) {
          // dropped while it was opened: not kept
          if (h !== null) {
            this.release(h);
          }
          return null;
        }
        // one that failed stays failed until dropped (asked again, it is not retried)
        this.cancelOpening = null;
        if (h !== null) {
          this.opening = null;
          this.handle = h;
        }
        return h;
      });
      this.opening = p;
      this.cancelOpening = (close) => {
        o.cancel(close);
      };
    }
    return this.opening;
  }

  /**
   * Gives the handle up (`close`: closed, else forgotten, as when the
   * engine dropped it with its document); one still being opened is told
   * to stop, and closed if it comes all the same.
   */
  drop(close = true): void {
    const h = this.handle;
    const cancel = this.cancelOpening;
    this.handle = null;
    this.opening = null;
    this.cancelOpening = null;
    cancel?.(close);
    if (close && h !== null) {
      this.release(h);
    }
  }
}
