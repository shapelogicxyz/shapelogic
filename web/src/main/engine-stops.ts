// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engines being ended, and the quit that waits for them (cleanup item
// 15.1). A window that closes ends its engine: the engine's input closes,
// and the engine pauses and saves its solves before it goes (decision
// 0012), which can take seconds. The app used to quit meanwhile: the
// engine lived on with no reader for its output and nobody to end it after
// the grace time, and one that hung kept its solves' locks (decision
// 0035). The quit now waits until every engine has ended, up to a cap.

/** What ends an engine: EngineHost.stop, which ends it by force after its grace time. */
export interface Stoppable {
  stop(): Promise<void>;
}

/** The longest a quit waits for the engines (past the hosts' own 10 s grace). */
export const QUIT_WAIT_MS = 15_000;

/** The engines being ended; the quit goes on once they are, or after the cap. */
export class EngineStops {
  private readonly pending = new Set<Promise<void>>();
  private readonly quit: () => void;
  private readonly capMs: number;
  /** A quit is held: `quit` is called once the engines have ended or the cap has passed. */
  private holding = false;
  /** The held quit was let go (or given up): the next one passes. */
  private released = false;
  private timer: ReturnType<typeof setTimeout> | null = null;

  /** `quit` asks for the quit again (app.quit); `capMs` is the longest it is held. */
  constructor(quit: () => void, capMs = QUIT_WAIT_MS) {
    this.quit = quit;
    this.capMs = capMs;
  }

  /** The engines still ending. */
  get size(): number {
    return this.pending.size;
  }

  /** Ends a host's engine and keeps it until it has ended (a stop that fails has ended too). */
  stop(host: Stoppable): void {
    const done: Promise<void> = host
      .stop()
      .catch(() => undefined)
      .then(() => {
        this.pending.delete(done);
        if (this.pending.size === 0) {
          this.release();
        }
      });
    this.pending.add(done);
  }

  /**
   * before-quit's question, asked when no window is left: true when the
   * quit must wait (the caller prevents it). The quit is then asked for
   * again once every engine has ended, or after the cap; that one, and any
   * quit with no engine ending, passes.
   */
  holdQuit(): boolean {
    if (this.released || this.pending.size === 0) {
      this.released = false;
      return false;
    }
    if (!this.holding) {
      this.holding = true;
      this.timer = setTimeout(() => {
        this.timer = null;
        this.release();
      }, this.capMs);
    }
    return true;
  }

  /**
   * The app goes on after all (a window was opened while the quit was
   * held): the held quit is dropped, and the engines end as they would
   * have. Whether a quit was held.
   */
  cancelQuit(): boolean {
    const held = this.holding;
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
    }
    this.holding = false;
    this.released = false;
    return held;
  }

  private release(): void {
    if (!this.holding) {
      return;
    }
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
    }
    this.holding = false;
    this.released = true;
    this.quit();
  }
}
