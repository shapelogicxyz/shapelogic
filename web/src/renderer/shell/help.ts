// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Help menu's dialogs (stage 6): the keyboard shortcuts (F1, Help ▸
// Keyboard Shortcuts), printed from the one table (keys.ts) by its groups
// as the settings dialog's page prints it (the mockup's `HELP`, `kbRow`,
// with its search), and About, with the app's and the engine's versions
// (engine.info), the latest release once an update check has seen it
// (updates.ts), and the credits of AUTHORS (the original's cb_About named
// its authors and licence). The notes (Edit ▸ Notes…: document.setNotes,
// allowed while solving; the mockup's "Edit comment") are here too.

import type { Store } from '../state/store.ts';
import { closeDialog, openDialog, topDialog } from './dialog.ts';
import { esc } from './dom.ts';
import { keyGroups } from './keys.ts';
import { kbRowHtml } from './settings.ts';
import { flash, reportError } from './status.ts';
import { latestKnown } from './updates.ts';

function keysHtml(query: string): string {
  const q = query.trim().toLowerCase();
  const html = keyGroups()
    .map((g) => {
      const rows = g.rows.filter(
        (r) => q === '' || `${r.keys} ${r.does} ${g.group}`.toLowerCase().includes(q),
      );
      return rows.length === 0
        ? ''
        : `<div class="kbgrp">${esc(g.group)}${g.sub === undefined ? '' : `<small>${esc(g.sub)}</small>`}</div>` +
            rows.map(kbRowHtml).join('');
    })
    .join('');
  return html === ''
    ? `<div class="mute" style="padding:30px 0;text-align:center">No shortcut matches “${esc(query)}”</div>`
    : html;
}

/** Help ▸ Keyboard Shortcuts (F1): the one table, searchable. */
export function openKeys(): void {
  if (topDialog()?.id === 'helpKeys') {
    return;
  }
  void openDialog({
    id: 'helpKeys',
    title: 'Keyboard shortcuts',
    wide: true,
    body: `<div class="kbnote">Grouped by workspace. Keys act when the focus is not in a text field (the File menu’s keys act there too).</div><input class="kbsearch" title="Type part of a key or an action to find its shortcut" placeholder="Search shortcuts" autocomplete="off" data-focus aria-label="Search shortcuts"><div class="kblist">${keysHtml('')}</div>`,
    buttons: [{ key: 'close', label: 'Close', kind: 'pri', default: true }],
  });
  const el = topDialog();
  const input = el?.querySelector<HTMLInputElement>('.kbsearch');
  const list = el?.querySelector<HTMLElement>('.kblist');
  input?.addEventListener('input', () => {
    if (list != null) {
      list.innerHTML = keysHtml(input.value);
    }
  });
}

/** Help ▸ About: the versions and the credits. */
export function openAbout(store: Store, appVersion: string): void {
  const info = store.info;
  // the stack: the engine's language, the window's, and what joins them
  const backend =
    info === null ? 'the engine is not running' : `C++ (${esc(info.name)} ${esc(info.version)})`;
  const link = info === null ? 'JSON' : `JSON (protocol ${String(info.protocol)})`;
  const known = latestKnown();
  const latest =
    known === null
      ? ''
      : `<br>Latest release: ${esc(known.latest)}${known.newer ? ' (update available)' : ''}`;
  void openDialog({
    id: 'about',
    title: 'About ShapeLogic',
    wide: true,
    body: `<p><b>ShapeLogic ${esc(appVersion)}</b>${latest}<br>Backend: ${backend}<br>Frontend: Electron (TypeScript, three.js for the 3D view)<br>Linking language: ${link}</p>
<p>A fork of BurrTools, the puzzle solver of Andreas Röver (2003–2011), with thanks, from its AUTHORS file, to Bill Cutler, André van Kamen and Don E. Knuth (algorithms), Ronald Kint-Bruynseels, Dic Sonneveld, Derek Bosch, Markus Götz, Lee Krasnow, Volker Grabsch, George Bell and Barry Downes.</p>
<p>The interface is built on Girish Sharma’s mockup of a BurrTools web GUI.</p>
<p>This program is free software under the GNU General Public License, version 3 or later; it comes with no warranty.</p>
<p>Created and actively managed by Colin Deisenroth.<br><a href="#" data-help="author" style="color:var(--accent)" title="Open the author’s GitHub page in your browser">github.com/hypnoz</a><br>Contact: shapelogic.xyz@gmail.com</p>`,
    buttons: [{ key: 'close', label: 'Close', kind: 'pri', default: true }],
  });
  // the link goes through the app's list of addresses, never a page of its own
  topDialog()
    ?.querySelector<HTMLAnchorElement>('a[data-help]')
    ?.addEventListener('click', (e) => {
      e.preventDefault();
      window.btApp.openHelp('author');
    });
}

/** Edit ▸ Notes…: the puzzle's comment (allowed while solving). */
export function openNotes(store: Store): void {
  const snap = store.snapshot;
  if (snap === null) {
    return;
  }
  const gen = store.generation;
  void openDialog({
    id: 'notes',
    title: 'Notes',
    wide: true,
    body: `<p class="mute">The puzzle’s notes, saved with it.</p><textarea class="notes" rows="12" title="The puzzle’s notes: its name, designer and anything else, saved with it" data-focus aria-label="Notes">${esc(snap.notes)}</textarea>`,
    buttons: [
      { key: 'cancel', label: 'Cancel' },
      {
        key: 'save',
        label: 'Save notes',
        kind: 'pri',
        tip: 'Save the current notes with the puzzle',
      },
    ],
    onAnswer: (k, el) => {
      if (k !== 'save') {
        return true;
      }
      const text = el.querySelector<HTMLTextAreaElement>('textarea')?.value ?? '';
      if (store.generation !== gen) {
        flash('Another puzzle was opened: the notes were not saved', 5000);
        return true;
      }
      if (text === store.snapshot?.notes) {
        return true;
      }
      store.engine.call('document.setNotes', { text }).then(
        async (r) => {
          await store.whenRevision(r.revision);
          closeDialog('notes');
          flash('Notes saved');
        },
        (e: unknown) => {
          // kept open: the text the file cannot hold is the engine's message
          reportError(e, 'Notes');
        },
      );
      return false;
    },
  });
}
