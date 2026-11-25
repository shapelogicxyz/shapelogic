// SPDX-License-Identifier: GPL-3.0-or-later
//
// The menus' document commands before the first document (stage 8,
// cleanup item 2.3): a command that replaces the document (New, Open,
// Open Recent, the imports) and comes before the first document is shown
// waits for it, while the engine starts; once the engine has failed no
// document will come, so the command is not held but the engine's dialog
// is shown instead, and the commands still waiting are dropped. The log
// hears of each one held or dropped. Free of the page (the unit tests
// drive it); menus.ts uses it.

import type { AppCommand, AppCommandName } from '../../engine/app-bridge.ts';

/** The commands that replace the document: before the first one is shown, they wait for it. */
export const DOCUMENT_COMMANDS: ReadonlySet<AppCommandName> = new Set([
  'new',
  'open',
  'openPath',
  'importPaused',
  'importPs3d',
  'importScad',
]);

/** What the gate asks and tells. */
export interface GateHost {
  /** The first document is shown. */
  hasDocument(): boolean;
  /** The engine could not start: no document will come. */
  engineFailed(): boolean;
  run(c: AppCommand): void;
  /** The log hears of a command held (or dropped). */
  held(command: AppCommandName, dropped: boolean): void;
  /** The engine's dialog (a command came while it has failed). */
  showFailure(): void;
}

/** Holds, runs or drops the document commands that come before the first document. */
export class CommandGate {
  private readonly h: GateHost;
  private readonly waiting: AppCommand[] = [];

  constructor(host: GateHost) {
    this.h = host;
  }

  /** A menu command: run, held, or (the engine failed) dropped with the dialog shown. */
  command(c: AppCommand): void {
    if (this.h.hasDocument() || !DOCUMENT_COMMANDS.has(c.cmd)) {
      this.h.run(c);
      return;
    }
    if (this.h.engineFailed()) {
      // no document will come: say why instead of doing nothing
      this.h.held(c.cmd, true);
      this.h.showFailure();
      return;
    }
    // the first document is not shown yet: the engine is starting
    this.h.held(c.cmd, false);
    this.waiting.push(c);
  }

  /** The document or the engine's status changed: run what waited, or drop it. */
  changed(): void {
    if (this.h.hasDocument()) {
      for (const w of this.waiting.splice(0)) {
        this.h.run(w);
      }
    } else if (this.h.engineFailed()) {
      // the commands held for an engine that failed: the dialog says why
      for (const w of this.waiting.splice(0)) {
        this.h.held(w.cmd, true);
      }
    }
  }
}
