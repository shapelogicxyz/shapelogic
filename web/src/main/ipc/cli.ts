// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.slApp's command line tool (a group of ../app-ipc.ts, cleanup item
// 13.4; cli-tool.ts): the renderer names an operation, never a path.

import { cliInfo, installCli, macPrivileged, uninstallCli } from '../cli-tool.ts';
import type { Registrar } from './register.ts';

/** What the command line tool's channels need of the main process. */
export interface CliIpc {
  /** The command line tool in this install (cli-tool.ts), or null when there is none. */
  cliPath: string | null;
  /** Where Install links it, or null where this platform has no Install. */
  cliLink: string | null;
}

/** Registers the command line tool's channels. */
export function installCliIpc(r: Registrar, h: CliIpc): void {
  const notOurs = { ok: false, error: 'not a ShapeLogic window' } as const;
  r.handle('sl:cli-info', notOurs, () => ({ ok: true, info: cliInfo(h.cliPath, h.cliLink) }));
  r.handle('sl:cli-install', notOurs, () => installCli(h.cliPath, h.cliLink, macPrivileged));
  r.handle('sl:cli-uninstall', notOurs, () => uninstallCli(h.cliPath, h.cliLink, macPrivileged));
}
