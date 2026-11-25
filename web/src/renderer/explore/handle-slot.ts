// SPDX-License-Identifier: GPL-3.0-or-later
//
// One explorer handle (an assembler's for Placements or Step), opened once
// however often it is asked for, and given back when it is dropped: a
// handle whose reply comes after the drawer was closed (or the slot reset)
// is closed at once rather than kept, so the engine does not hold that
// puzzle copy for the rest of the session (cleanup item 6.7). Free of the
// page; explorer.ts holds two.

export class HandleSlot {
  /** The handle open now, or null. */
  handle: string | null = null;
  private opening: Promise<string | null> | null = null;
  private readonly release: (handle: string) => void;

  /** `release` closes a handle no longer wanted (explore.close). */
  constructor(release: (handle: string) => void) {
    this.release = release;
  }

  /** The handle, opened by `open` when there is none and none is being opened. */
  ensure(open: () => Promise<string | null>): Promise<string | null> {
    if (this.handle !== null) {
      return Promise.resolve(this.handle);
    }
    if (this.opening === null) {
      const p: Promise<string | null> = open().then((h) => {
        if (this.opening !== p) {
          // dropped while it was opened: not kept
          if (h !== null) {
            this.release(h);
          }
          return null;
        }
        // one that failed stays failed until dropped (asked again, it is not retried)
        if (h !== null) {
          this.opening = null;
          this.handle = h;
        }
        return h;
      });
      this.opening = p;
    }
    return this.opening;
  }

  /**
   * Gives the handle up (`close`: closed, else forgotten, as when the
   * engine dropped it with its document); one still being opened is
   * closed when it comes.
   */
  drop(close = true): void {
    const h = this.handle;
    this.handle = null;
    this.opening = null;
    if (close && h !== null) {
      this.release(h);
    }
  }
}
