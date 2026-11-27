// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.slApp's rescue copy (a group of ../app-ipc.ts, cleanup item 13.4):
// each window has its own (rescue.ts's slots, decision 0036). The window's
// engine writes and opens the copy itself (document.saveRescue, cleanup
// item 10.11); only the record crosses here. The record's file and the
// copy's own are granted to the window's engine (path-grants.ts).

import type { WebContents } from 'electron';

import { isRescueInfo } from '../../engine/app-bridge.ts';
import { isEngineError } from '../../engine/errors.ts';
import type { AppLog } from '../app-log.ts';
import type { EngineHost } from '../engine-host.ts';
import type { PathGrants } from '../path-grants.ts';
import type { RescueSlots } from '../rescue.ts';
import { logRefusedWrite } from './register.ts';
import type { Registrar } from './register.ts';

/** What the rescue copy needs of the main process. */
export interface RescueIpc {
  /** The windows' rescue copies, by the web contents' id. */
  rescue: Pick<RescueSlots<number>, 'of'>;
  /** A window's engine, or null (the main process asks it to write the rescue copy). */
  engine(sender: WebContents): Pick<EngineHost, 'call'> | null;
  /** The files each window's engine may be given. */
  paths: PathGrants<WebContents>;
  /** The app's log: a write Windows refused. */
  log: Pick<AppLog, 'warn'>;
}

/** Registers the rescue copy's channels. */
export function installRescueIpc(r: Registrar, h: RescueIpc): void {
  r.handle('sl:rescue-write', null, async (e, _win, info) => {
    if (!isRescueInfo(info)) {
      return null;
    }
    const engine = h.engine(e.sender);
    if (engine === null) {
      return { ok: false, errc: 'disconnected', message: 'the window has no engine' };
    }
    // the record's file is handed to the window that reads it: only
    // one this window was handed itself, else the copy has no file
    const path = info.path !== null && h.paths.allows(e.sender, info.path) ? info.path : null;
    const store = h.rescue.of(e.sender.id);
    try {
      // the engine writes the copy itself (cleanup item 10.11). The call
      // is made before this handler first awaits, so it goes ahead of
      // the window's later calls: the copy is of the document at the
      // revision the window saw, or refused (another one is open)
      await store.write(
        (file) =>
          engine
            .call('document.saveRescue', { path: file, revision: info.revision })
            .then((res) => res.revision),
        { ...info, path },
      );
      return { ok: true };
    } catch (err) {
      if (isEngineError(err)) {
        if (err.errc === 'io_error') {
          logRefusedWrite(h.log, store.file, err.detail);
        }
        return { ok: false, errc: err.errc, message: err.detail };
      }
      const message = err instanceof Error ? err.message : String(err);
      logRefusedWrite(h.log, store.record, message);
      return { ok: false, errc: 'io_error', message };
    }
  });

  r.handle('sl:rescue-read', null, async (e) => {
    const copy = await h.rescue.of(e.sender.id).read();
    if (copy !== null) {
      // the engine opens the copy itself, as the document of its file
      h.paths.grant(e.sender, copy.file);
      if (copy.info.path !== null) {
        h.paths.grant(e.sender, copy.info.path);
      }
    }
    return copy;
  });

  r.handle('sl:rescue-clear', null, async (e) => {
    await h.rescue.of(e.sender.id).clear();
    return null;
  });
}
