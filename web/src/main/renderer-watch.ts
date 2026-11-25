// SPDX-License-Identifier: GPL-3.0-or-later
//
// A window's page that crashed or hangs (cleanup item 7.1). The engine
// still holds the window's document and any solve running, and a page that
// loads again attaches to both (renderer/shell/rescue.ts: a reload's dirty
// document is the engine's own): so a page whose process ended for any
// reason but a clean exit is logged and reloaded, at most RELOADS_MAX times
// in RELOADS_WINDOW_MS; past that the window asks natively, Reload or
// Close. A page that stops answering is asked about natively, Wait or
// Reload; the question closes by itself once the page answers again.
// Reloading a hung page ends its process first (Electron's
// forcefullyCrashRenderer, then reload: the reload then runs in a new
// process), and that end is not counted as a crash. Free of Electron
// (main/index.ts gives the window's calls), so the unit tests run it.

/** The reloads a window's page gets by itself before the window asks. */
export const RELOADS_MAX = 3;
/** The time those reloads are counted in. */
export const RELOADS_WINDOW_MS = 60_000;

/** How soon after its kill a page's end is taken for that kill's. */
const KILL_TOLD_MS = 5_000;

/** How a page's process ended (Electron's RenderProcessGoneDetails). */
export interface GoneDetails {
  reason: string;
  exitCode: number;
}

/** What the watch needs of its window. */
export interface WatchedWindow {
  /** Loads the page again. */
  reload(): void;
  /** Ends the page's process (a hung page; then reload()). */
  kill(): void;
  /** Closes the window, without the renderer's question. */
  close(): void;
  /** Whether the window is closing or gone: then nothing is done. */
  closing(): boolean;
  /** The question after too many crashes: Reload or Close. */
  askGone(details: GoneDetails): Promise<'reload' | 'close'>;
  /** The question of a hung page, closed (answered Wait) when `signal` aborts. */
  askHung(signal: AbortSignal): Promise<'wait' | 'reload'>;
  log(level: 'ERROR' | 'WARN' | 'INFO', event: string, text: string): void;
  /** The clock (the tests set it). */
  now?: () => number;
}

/** Watches one window's page. */
export class RendererWatch {
  private readonly w: WatchedWindow;
  private readonly now: () => number;
  /** When the page was reloaded by itself, the newest last. */
  private reloads: number[] = [];
  /** The hung page's question, while it is open. */
  private hung: AbortController | null = null;
  /** When the page's process was ended for a reload: its end, soon after, is no crash. */
  private killedAt: number | null = null;
  /** The question after too many crashes is open. */
  private asking = false;

  constructor(w: WatchedWindow) {
    this.w = w;
    this.now = w.now ?? (() => Date.now());
  }

  /** The page's process ended (`render-process-gone`). */
  gone(details: GoneDetails): void {
    this.closeHung();
    if (this.killedAt !== null && this.now() - this.killedAt < KILL_TOLD_MS) {
      this.killedAt = null;
      this.w.log('INFO', 'Renderer Gone', `ended to reload the page (${describe(details)})`);
      return;
    }
    if (details.reason === 'clean-exit') {
      return;
    }
    this.w.log('ERROR', 'Renderer Gone', describe(details));
    if (this.w.closing()) {
      // the window goes anyway (a close waiting for the page asks natively)
      return;
    }
    const t = this.now();
    this.reloads = this.reloads.filter((at) => t - at < RELOADS_WINDOW_MS);
    if (this.reloads.length < RELOADS_MAX) {
      this.reloads.push(t);
      this.w.log(
        'INFO',
        'Renderer Reloaded',
        `reload ${String(this.reloads.length)} of ${String(RELOADS_MAX)} in a minute; the page attaches to the engine's document`,
      );
      this.w.reload();
      return;
    }
    if (this.asking) {
      return;
    }
    this.asking = true;
    void this.w.askGone(details).then(
      (answer) => {
        this.asking = false;
        if (this.w.closing()) {
          return;
        }
        this.w.log(
          'INFO',
          'Renderer Gone',
          `the user chose ${answer === 'reload' ? 'Reload' : 'Close'}`,
        );
        if (answer === 'reload') {
          this.w.reload();
        } else {
          this.w.close();
        }
      },
      () => {
        this.asking = false;
      },
    );
  }

  /** The page stopped answering (`unresponsive`). */
  unresponsive(): void {
    if (this.hung !== null || this.w.closing()) {
      return;
    }
    this.w.log(
      'WARN',
      'Renderer Unresponsive',
      'the page does not answer; the window asks: Wait or Reload',
    );
    const asked = new AbortController();
    this.hung = asked;
    void this.w.askHung(asked.signal).then(
      (answer) => {
        if (this.hung === asked) {
          this.hung = null;
        }
        if (answer === 'reload' && !asked.signal.aborted && !this.w.closing()) {
          this.w.log(
            'INFO',
            'Renderer Reloaded',
            'the user chose Reload for the page that did not answer',
          );
          this.killedAt = this.now();
          this.w.kill();
          this.w.reload();
        }
      },
      () => {
        if (this.hung === asked) {
          this.hung = null;
        }
      },
    );
  }

  /** The page answers again (`responsive`): its question goes. */
  responsive(): void {
    if (this.hung !== null) {
      this.w.log('INFO', 'Renderer Responsive', 'the page answers again');
      this.closeHung();
    }
  }

  private closeHung(): void {
    const h = this.hung;
    this.hung = null;
    h?.abort();
  }
}

function describe(d: GoneDetails): string {
  return `the page's process ended: ${d.reason}, exit code ${String(d.exitCode)}`;
}
