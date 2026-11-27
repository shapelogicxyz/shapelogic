// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.slApp's side of the window's engine (a group of ../app-ipc.ts,
// cleanup item 13.4): the engine's bridge (`sl:call`, to the window's
// engine, which main/index.ts serves), the page's first document (the
// Startup line), Try Again, a document command held or dropped while the
// engine starts, and Copy and Save Diagnostics (diagnostics.ts).

import type { WebContents } from 'electron';

import { isAppCommandName } from '../../engine/app-bridge.ts';
import type {
  AppCommandName,
  DiagnosticsResult,
  LogExportResult,
} from '../../engine/app-bridge.ts';
import type { Registrar } from './register.ts';

/** What the window's engine channels need of the main process. */
export interface EngineIpc {
  /** A message of a window's engine bridge (`sl:call`): to its engine (main/index.ts's server). */
  engineCall(sender: WebContents, request: unknown): void;
  /** The page showed its first document (the Startup line). */
  ready(sender: WebContents): void;
  /** Try Again: the window's engine is started again. */
  retryEngine(sender: WebContents): void;
  /** A document command was held for the first document, or dropped (the engine failed). */
  commandHeld(sender: WebContents, command: AppCommandName, dropped: boolean): void;
  /** Copy Diagnostics (diagnostics.ts): the report on the clipboard. */
  copyDiagnostics(sender: WebContents): Promise<DiagnosticsResult>;
  /** The report saved where the save dialog says. */
  saveDiagnostics(sender: WebContents): Promise<LogExportResult>;
}

/** Registers the window's engine channels. */
export function installEngineIpc(r: Registrar, h: EngineIpc): void {
  // the engine's bridge: a window's calls go to its engine (main/index.ts serves it)
  r.on('sl:call', (e, _win, request) => {
    h.engineCall(e.sender, request);
  });

  r.on('sl:engine-ready', (e) => {
    h.ready(e.sender);
  });
  r.on('sl:engine-retry', (e) => {
    h.retryEngine(e.sender);
  });
  r.on('sl:command-held', (e, _win, command, dropped) => {
    if (isAppCommandName(command) && typeof dropped === 'boolean') {
      h.commandHeld(e.sender, command, dropped);
    }
  });

  r.handle('sl:diagnostics-copy', null, (e) => h.copyDiagnostics(e.sender));
  r.handle('sl:diagnostics-save', null, (e) => h.saveDiagnostics(e.sender));
}
