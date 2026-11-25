// SPDX-License-Identifier: GPL-3.0-or-later
//
// The menus' document commands before the first document (cleanup item
// 2.3, renderer/shell/command-gate.ts): held while the engine starts and
// run once the document comes; not held once the engine has failed, but
// the engine's dialog shown instead; the log told of each held or dropped.
// Under an open dialog they wait for it to close, one at a time (cleanup
// item 6.2: ids restart per document, so the dialog would act on the new
// document's shape of the same id).

import { describe, expect, it } from 'vitest';

import type { AppCommand } from '../../../src/engine/app-bridge.ts';
import { CommandGate } from '../../../src/renderer/shell/command-gate.ts';

function gate(): {
  g: CommandGate;
  state: { document: boolean; failed: boolean; dialogs: number; opens: boolean };
  ran: string[];
  held: string[];
  shown: () => number;
  closeDialog: () => void;
} {
  const state = { document: false, failed: false, dialogs: 0, opens: false };
  const ran: string[] = [];
  const held: string[] = [];
  let closed: (() => void)[] = [];
  let shown = 0;
  const g = new CommandGate({
    hasDocument: () => state.document,
    engineFailed: () => state.failed,
    run: (c: AppCommand) => {
      ran.push(c.cmd === 'openPath' ? `openPath ${c.path}` : c.cmd);
      // an open of a changed document asks about its changes first
      if (state.opens) {
        state.dialogs++;
      }
    },
    held: (cmd, dropped) => held.push(`${dropped ? 'dropped' : 'held'} ${cmd}`),
    showFailure: () => {
      shown++;
    },
    dialogOpen: () => state.dialogs > 0,
    whenDialogsClosed: (cb) => {
      if (state.dialogs === 0) {
        cb();
      } else {
        closed.push(cb);
      }
    },
  });
  const closeDialog = (): void => {
    state.dialogs--;
    if (state.dialogs === 0) {
      const waiting = closed;
      closed = [];
      for (const cb of waiting) {
        cb();
      }
    }
  };
  return { g, state, ran, held, shown: () => shown, closeDialog };
}

describe('document commands before the first document', () => {
  it('wait while the engine starts, and run once the document comes', () => {
    const t = gate();
    t.g.command({ cmd: 'openPath', path: '/x/a.xmpuzzle' });
    // a command that does not replace the document runs at once
    t.g.command({ cmd: 'keys' });
    expect(t.ran).toEqual(['keys']);
    expect(t.held).toEqual(['held openPath']);
    t.state.document = true;
    t.g.changed();
    expect(t.ran).toEqual(['keys', 'openPath /x/a.xmpuzzle']);
  });

  it('are dropped, and the dialog shown, once the engine has failed', () => {
    const t = gate();
    t.g.command({ cmd: 'openPath', path: '/x/a.xmpuzzle' });
    t.state.failed = true;
    t.g.changed();
    // the one that waited is dropped (the dialog opens for the failure itself)
    expect(t.held).toEqual(['held openPath', 'dropped openPath']);
    t.g.command({ cmd: 'open' });
    expect(t.held.at(-1)).toBe('dropped open');
    expect(t.shown()).toBe(1);
    t.g.command({ cmd: 'new' });
    expect(t.shown()).toBe(2);
    // nothing ran, even when a document comes later (an engine started again)
    t.state.failed = false;
    t.state.document = true;
    t.g.changed();
    expect(t.ran).toEqual([]);
  });
});

describe('document commands under an open dialog', () => {
  it('wait until it closes; the others run at once', () => {
    const t = gate();
    t.state.document = true;
    t.state.dialogs = 1;
    t.g.command({ cmd: 'openPath', path: '/x/a.xmpuzzle' });
    t.g.command({ cmd: 'importScad' });
    t.g.command({ cmd: 'exportStl' });
    expect(t.ran).toEqual(['exportStl']);
    // the log's "held" is the engine's start: not told
    expect(t.held).toEqual([]);
    t.closeDialog();
    expect(t.ran).toEqual(['exportStl', 'openPath /x/a.xmpuzzle', 'importScad']);
  });

  it('run one at a time: one that opens a dialog leaves the next waiting for it', () => {
    const t = gate();
    t.state.document = true;
    t.state.dialogs = 1;
    t.g.command({ cmd: 'openPath', path: '/x/a.xmpuzzle' });
    t.g.command({ cmd: 'openPath', path: '/x/b.xmpuzzle' });
    t.state.opens = true;
    t.closeDialog();
    // a's question about unsaved changes is open now
    expect(t.ran).toEqual(['openPath /x/a.xmpuzzle']);
    t.state.opens = false;
    t.closeDialog();
    expect(t.ran).toEqual(['openPath /x/a.xmpuzzle', 'openPath /x/b.xmpuzzle']);
    // with nothing open and nothing waiting, a command runs at once
    t.g.command({ cmd: 'new' });
    expect(t.ran.at(-1)).toBe('new');
  });
});
