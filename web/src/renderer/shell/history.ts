// SPDX-License-Identifier: GPL-3.0-or-later
//
// Undo and redo (Ctrl+Z, Shift+Ctrl+Z): the engine's history, one step per
// command or edit group, named by the engine (`edit.history`); the status
// bar says what was undone or redone. Refused while a solve or an analysis
// runs ("pause the solve first", never sent: the engine would answer
// invalid_state) and while a drag is under way (its step is not finished);
// a drag just released is waited for (its group closes first). The mockup had no undo; the original's Edit menu
// named the steps the same way (stage 6 adds the menu items).

import type { Store } from '../state/store.ts';
import { onKey } from './keyboard.ts';
import { flash, reportError } from './status.ts';

/**
 * What an edit group of the workspace is doing: none open, a drag under
 * way, or one closing (resolved when its edit.end is answered).
 */
export type GroupState = 'none' | 'drawing' | Promise<unknown>;

/** Undoes or redoes one step and says which. */
export async function undoRedo(
  store: Store,
  redo: boolean,
  group: () => GroupState,
): Promise<void> {
  const g = group();
  if (g === 'drawing') {
    return;
  }
  if (g !== 'none') {
    await g;
  }
  const snap = store.snapshot;
  if (snap === null) {
    return;
  }
  if (store.solving) {
    flash('Pause the solve first', 4000);
    return;
  }
  if (!(redo ? snap.canRedo : snap.canUndo)) {
    flash(redo ? 'Nothing to redo' : 'Nothing to undo');
    return;
  }
  try {
    const h = await store.engine.call('edit.history');
    const label = redo ? h.redo[0] : h.undo.at(-1);
    const r = await store.engine.call(redo ? 'edit.redo' : 'edit.undo');
    await store.whenRevision(r.revision);
    flash(`${redo ? 'Redone' : 'Undone'}: ${label ?? 'a step'}`);
  } catch (e) {
    reportError(e, redo ? 'Redo' : 'Undo');
  }
}

/** The keys of undo and redo. */
export function installHistory(store: Store, group: () => GroupState): void {
  onKey('undo', () => {
    void undoRedo(store, false, group);
    return true;
  });
  onKey('redo', () => {
    void undoRedo(store, true, group);
    return true;
  });
}
