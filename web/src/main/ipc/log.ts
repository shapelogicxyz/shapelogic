// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.slApp's log (a group of ../app-ipc.ts, cleanup item 13.4): the Log
// File Manager's pages, Clear, its close, and Export to the file this
// process's own save dialog gives (the renderer never names a path).

import { dialog } from 'electron';
import { dirname, extname, join } from 'node:path';

import { isLogQuery } from '../../engine/app-bridge.ts';
import type { LogExportResult } from '../../engine/app-bridge.ts';
import type { AppLog } from '../app-log.ts';
import type { StateStore } from '../state-store.ts';
import { startDir } from './register.ts';
import type { Registrar } from './register.ts';

/** A date for an exported log's name: 2026-11-16. */
function logStamp(d: Date): string {
  const p = (n: number): string => String(n).padStart(2, '0');
  return `${String(d.getFullYear())}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

/** What the log manager needs of the main process. */
export interface LogIpc {
  /** The app's log (View ▸ Log File Manager…). */
  log: AppLog;
  /** Where the export's dialog starts, and the folder it gave last. */
  store: Pick<StateStore, 'lastDir' | 'setLastDir'>;
}

/** Registers the log manager's channels. */
export function installLogIpc(r: Registrar, h: LogIpc): void {
  r.handle('sl:log-page', null, (_e, _win, query) =>
    isLogQuery(query) ? h.log.page(query) : null,
  );
  r.handle('sl:log-clear', null, async () => {
    await h.log.clear();
    return null;
  });
  r.handle('sl:log-closed', null, () => {
    // the manager closed: the lines it read go (cleanup item 10.9)
    h.log.forget();
    return null;
  });
  r.handle('sl:log-sizes', null, () => h.log.exportSizes());
  r.handle('sl:log-export', null, async (_e, win, lines) => {
    // all of the log, or its newest lines
    if (
      lines !== undefined &&
      (typeof lines !== 'number' || !Number.isInteger(lines) || lines < 1 || lines > 1_000_000)
    ) {
      return null;
    }
    const res = await dialog.showSaveDialog(win, {
      title: 'Export Log',
      defaultPath: join(startDir(h.store), `shapelogic-log-${logStamp(new Date())}.txt`),
      filters: [
        { name: 'Text Files', extensions: ['txt', 'log'] },
        { name: 'All Files', extensions: ['*'] },
      ],
      properties: ['showOverwriteConfirmation', 'createDirectory'],
    });
    if (res.canceled || res.filePath === '') {
      return { ok: false, canceled: true } satisfies LogExportResult;
    }
    const path = extname(res.filePath) === '' ? `${res.filePath}.txt` : res.filePath;
    try {
      await h.log.exportTo(path, lines);
      h.store.setLastDir(dirname(path));
      return { ok: true, path } satisfies LogExportResult;
    } catch (err) {
      return {
        ok: false,
        error: err instanceof Error ? err.message : String(err),
      } satisfies LogExportResult;
    }
  });
}
