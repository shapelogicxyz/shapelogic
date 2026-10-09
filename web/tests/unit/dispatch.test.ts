// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ordered inbox (a reply settles, and the code awaiting it runs, before
// the items behind it) and the deferral it yields with on each platform.

import { describe, expect, it } from 'vitest';

import { makeDefer, OrderedInbox } from '../../src/engine/dispatch.ts';
import type { DeferGlobals } from '../../src/engine/dispatch.ts';

const tick = (): Promise<void> => new Promise((resolve) => setImmediate(resolve));

describe('the ordered inbox', () => {
  it('yields a macrotask after each settled reply; the items queued behind it wait', async () => {
    const log: string[] = [];
    const inbox = new OrderedInbox<string>((item) => {
      log.push(item);
      return item.startsWith('r') ? 'reply' : 'other';
    });
    inbox.push('r1');
    inbox.push('r2');
    inbox.push('e1');
    inbox.push('e2');
    expect(log).toEqual(['r1']);
    expect(inbox.waiting).toBe(3);
    await Promise.resolve();
    expect(log).toEqual(['r1']);
    await tick();
    expect(log).toEqual(['r1', 'r2']);
    await tick();
    expect(log).toEqual(['r1', 'r2', 'e1', 'e2']);
    expect(inbox.waiting).toBe(0);
  });

  it('delivers events at once when no reply is in front of them', () => {
    const log: string[] = [];
    const inbox = new OrderedInbox<string>((item) => {
      log.push(item);
      return 'other';
    });
    inbox.push('e1');
    inbox.push('e2');
    expect(log).toEqual(['e1', 'e2']);
  });

  it('carries on after a delivery that threw', async () => {
    const log: string[] = [];
    const inbox = new OrderedInbox<string>((item) => {
      if (item === 'bad') {
        throw new Error('x');
      }
      log.push(item);
      return 'other';
    });
    inbox.push('a');
    expect(() => {
      inbox.push('bad');
    }).toThrow('x');
    inbox.push('b');
    await tick();
    expect(log).toEqual(['a', 'b']);
  });
});

describe('the deferral', () => {
  it('uses setImmediate where there is one', () => {
    const calls: string[] = [];
    const defer = makeDefer({
      setImmediate: (fn) => {
        calls.push('setImmediate');
        fn();
      },
      setTimeout: () => {
        calls.push('setTimeout');
      },
    });
    defer(() => calls.push('ran'));
    expect(calls).toEqual(['setImmediate', 'ran']);
  });

  it('uses a MessageChannel without one, after the microtasks and in order', async () => {
    const defer = makeDefer({ MessageChannel, setTimeout } as unknown as DeferGlobals);
    const log: string[] = [];
    const done = new Promise<void>((resolve) => {
      defer(() => {
        log.push('deferred');
        resolve();
      });
    });
    queueMicrotask(() => log.push('microtask'));
    log.push('sync');
    await done;
    expect(log).toEqual(['sync', 'microtask', 'deferred']);
    const order: number[] = [];
    await new Promise<void>((resolve) => {
      for (let i = 0; i < 5; i++) {
        defer(() => {
          order.push(i);
          if (i === 4) {
            resolve();
          }
        });
      }
    });
    expect(order).toEqual([0, 1, 2, 3, 4]);
  });

  it('falls back to setTimeout', () => {
    const calls: string[] = [];
    const defer = makeDefer({
      setTimeout: (fn, ms) => {
        calls.push(`setTimeout ${ms}`);
        fn();
      },
    });
    defer(() => calls.push('ran'));
    expect(calls).toEqual(['setTimeout 0', 'ran']);
  });
});
