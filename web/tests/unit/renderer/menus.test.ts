// SPDX-License-Identifier: GPL-3.0-or-later
//
// The menus' document commands before the first document (cleanup item
// 2.3, renderer/shell/command-gate.ts): held while the engine starts and
// run once the document comes; not held once the engine has failed, but
// the engine's dialog shown instead; the log told of each held or dropped.

import { describe, expect, it } from 'vitest';

import type { AppCommand } from '../../../src/engine/app-bridge.ts';
import { CommandGate } from '../../../src/renderer/shell/command-gate.ts';

function gate(): {
  g: CommandGate;
  state: { document: boolean; failed: boolean };
  ran: string[];
  held: string[];
  shown: () => number;
} {
  const state = { document: false, failed: false };
  const ran: string[] = [];
  const held: string[] = [];
  let shown = 0;
  const g = new CommandGate({
    hasDocument: () => state.document,
    engineFailed: () => state.failed,
    run: (c: AppCommand) => ran.push(c.cmd === 'openPath' ? `openPath ${c.path}` : c.cmd),
    held: (cmd, dropped) => held.push(`${dropped ? 'dropped' : 'held'} ${cmd}`),
    showFailure: () => {
      shown++;
    },
  });
  return { g, state, ran, held, shown: () => shown };
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
