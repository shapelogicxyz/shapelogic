// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window says when its engine cannot start (cleanup item 2.3): a
// dialog with the likely cause in plain words (engine/start-diagnosis.ts),
// what to try, and Try Again, Copy Details, View Logs and Close. It opens
// when the engine's status is failed, or when the first document has not
// come 15 seconds after the page loaded; once per failure, and again
// whenever a document command (Open, New, ...) comes while the engine has
// failed: without an engine they would do nothing at all.

import type { SlApp } from '../../engine/app-bridge.ts';
import type { EngineStatus } from '../../engine/client-types.ts';
import { likelyCause } from '../../engine/start-diagnosis.ts';
import type { Store } from '../state/store.ts';
import { closeDialog, openDialog, VIEW_LOGS } from './dialog.ts';
import { esc } from './dom.ts';

/** How long the page waits for its first document before it says the engine has not started. */
export const FIRST_DOCUMENT_WAIT_MS = 15_000;

const ID = 'engineFailure';

/** The dialog's cause and details, in plain words; `waited`: no failure, the engine has not answered. */
export function failureWords(
  status: EngineStatus | null,
  platform: string,
): { cause: string; detail: string } {
  if (status?.state !== 'failed') {
    return {
      cause: `the engine has not answered for ${String(FIRST_DOCUMENT_WAIT_MS / 1000)} seconds; security software may be holding it.`,
      detail: status === null ? 'the engine has not reported yet' : `the engine is ${status.state}`,
    };
  }
  const f = status.failure;
  const cause = f !== undefined ? likelyCause(f, platform) : null;
  return {
    cause: cause ?? 'the engine stopped soon after it started, more than once.',
    detail: f !== undefined ? `${f.code}: ${f.message}` : status.reason,
  };
}

/** What to try, by platform. */
export function whatToTry(platform: string): string[] {
  if (platform === 'win32') {
    return [
      'Open Windows Security ▸ Virus & threat protection ▸ Protection history. If ShapeLogic or shapelogic_engine.exe was quarantined or blocked, restore it and allow it.',
      'In another antivirus program, allow ShapeLogic in the same way.',
      'Smart App Control blocks programs that are not signed; while it is on, ShapeLogic may not start.',
      'Install ShapeLogic again if its engine file is missing.',
    ];
  }
  if (platform === 'darwin') {
    return [
      'Move ShapeLogic to the Applications folder and open it from there.',
      'Install it again if its engine file is missing.',
    ];
  }
  return ['Install ShapeLogic again if its engine file is missing.'];
}

/** The engine's dialog for one window. */
export class EngineFailure {
  private readonly store: Store;
  private readonly app: SlApp;
  /** The failure the dialog was shown for (once per failure). */
  private shownFor: EngineStatus | 'waited' | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;

  constructor(store: Store, app: SlApp) {
    this.store = store;
    this.app = app;
    store.subscribe((c) => {
      if (c.kind === 'document' && store.snapshot !== null) {
        this.clearTimer();
      }
      if (c.kind !== 'status') {
        return;
      }
      const s = store.status;
      if (s?.state === 'failed' && this.shownFor !== s) {
        this.show();
      } else if (s?.state === 'running') {
        // a new failure may show the dialog again
        this.shownFor = null;
        closeDialog(ID);
      }
    });
    this.timer = setTimeout(() => {
      this.timer = null;
      if (store.snapshot === null && this.shownFor === null) {
        this.show();
      }
    }, FIRST_DOCUMENT_WAIT_MS);
  }

  /** Whether the engine has failed (a document command is not held then). */
  get failed(): boolean {
    return this.store.status?.state === 'failed';
  }

  /** Shows the dialog (again, when a command asks while the engine has failed). */
  show(): void {
    const s = this.store.status;
    this.shownFor = s?.state === 'failed' ? s : 'waited';
    const { cause, detail } = failureWords(s, this.app.platform);
    const tries = whatToTry(this.app.platform)
      .map((t) => `<li>${esc(t)}</li>`)
      .join('');
    const body =
      `<p class="engine-cause">${esc(cause.charAt(0).toUpperCase() + cause.slice(1))}</p>` +
      `<p class="dlg-detail">${esc(detail)}</p>` +
      `<p>What to try:</p><ul>${tries}</ul>` +
      `<p>Copy Details puts a report on the clipboard to send with a question (Help ▸ Copy Diagnostics does the same). <a href="#" data-help="troubleshooting">When ShapeLogic does not start</a> explains more.</p>` +
      `<p class="engine-copied" hidden></p>`;
    void openDialog({
      id: ID,
      title: 'The engine could not start',
      body,
      wide: true,
      buttons: [
        {
          key: 'retry',
          label: 'Try Again',
          kind: 'pri',
          default: true,
          tip: 'Start the engine again',
        },
        {
          key: 'copy',
          label: 'Copy Details',
          tip: 'Put a report for a question about this on the clipboard',
        },
        VIEW_LOGS,
        { key: 'close', label: 'Close' },
      ],
      onAnswer: (key, el) => {
        if (key === 'copy') {
          const note = el.querySelector<HTMLElement>('.engine-copied');
          void this.app.copyDiagnostics().then((r) => {
            if (note !== null) {
              note.hidden = false;
              note.textContent = r.ok
                ? 'The details are on the clipboard: paste them into an email or a GitHub issue.'
                : `The details could not be copied: ${r.error}`;
            }
          });
          return false;
        }
        return undefined;
      },
    }).then((key) => {
      if (key === 'retry') {
        // the next failure shows the dialog again
        this.shownFor = null;
        this.app.retryEngine();
      }
    });
    const link = document.querySelector<HTMLElement>(`#${ID} [data-help]`);
    link?.addEventListener('click', (e) => {
      e.preventDefault();
      this.app.openHelp('troubleshooting');
    });
  }

  private clearTimer(): void {
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  }
}
