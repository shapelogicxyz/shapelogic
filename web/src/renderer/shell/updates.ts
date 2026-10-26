// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whether a newer ShapeLogic is out (the main process asks GitHub,
// main/updates.ts): a few seconds after startup when the setting is on, and
// at any time from Check for Updates… (Help; the application menu on
// macOS). A newer release is offered in a dialog: Download opens its page,
// Skip This Version keeps the startup check quiet about it, Later does
// nothing. About shows the latest release once a check has seen it.

import type { BtApp, UpdateResult } from '../../engine/app-bridge.ts';
import { openDialog } from './dialog.ts';
import { esc } from './dom.ts';
import { prefs } from './prefs.ts';

/** How long after startup the check waits, so it never slows the window's first moments. */
const STARTUP_DELAY_MS = 5000;

/** The last answer a check had, for About. */
let last: UpdateResult | null = null;

/** The latest release a check has seen, and whether it is newer; null before one answered. */
export function latestKnown(): { latest: string; newer: boolean } | null {
  return last === null || last.status === 'error'
    ? null
    : { latest: last.latest, newer: last.status === 'newer' };
}

/** The app's version as the tags say it: 1.3.0 is v1.3, 1.3.2 is v1.3.2. */
function shown(version: string): string {
  return `v${version.replace(/^(\d+\.\d+)\.0$/, '$1')}`;
}

/** Offers a newer release. */
export async function showUpdate(app: BtApp, r: UpdateResult & { status: 'newer' }): Promise<void> {
  const key = await openDialog({
    id: 'update',
    title: 'Update available',
    body: `<p><b>ShapeLogic ${esc(r.latest)}</b> is available. You have ${esc(shown(r.current))}.</p>
<p>Download opens the release page, where you can read what is new and get the installer for your system.</p>`,
    buttons: [
      { key: 'later', label: 'Later', tip: 'Ask again the next time ShapeLogic starts' },
      {
        key: 'skip',
        label: 'Skip This Version',
        tip: `Do not mention ${r.latest} at startup again`,
      },
      {
        key: 'download',
        label: 'Download',
        kind: 'pri',
        default: true,
        tip: 'Open the release page in your browser',
      },
    ],
  });
  if (key === 'download') {
    app.openReleases();
  } else if (key === 'skip') {
    app.skipUpdate(r.latest);
  }
}

/** Check for Updates…: asks now, and always says what it found. */
export async function checkNow(app: BtApp): Promise<void> {
  const r = await app.checkForUpdates(false);
  if (r === null) {
    return;
  }
  last = r;
  if (r.status === 'newer') {
    await showUpdate(app, r);
    return;
  }
  await openDialog({
    id: 'update',
    title: r.status === 'current' ? 'No update available' : 'Could not check for updates',
    body:
      r.status === 'current'
        ? `<p>You have the latest version of ShapeLogic, ${esc(shown(r.current))}.</p>`
        : `<p>ShapeLogic could not reach GitHub to see whether a newer version is out (${esc(r.error)}). Check your internet connection and try again later.</p>`,
    buttons: [{ key: 'close', label: 'Close', kind: 'pri', default: true }],
  });
}

/** The check at startup: when the setting is on, a moment after the window is up; quiet unless a newer release is out. */
export function autoCheck(app: BtApp): void {
  if (!prefs.updates) {
    return;
  }
  window.setTimeout(() => {
    void app.checkForUpdates(true).then((r) => {
      if (r !== null) {
        last = r;
        if (r.status === 'newer') {
          void showUpdate(app, r);
        }
      }
    });
  }, STARTUP_DELAY_MS);
}
