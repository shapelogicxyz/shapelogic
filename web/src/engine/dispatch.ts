// SPDX-License-Identifier: GPL-3.0-or-later
//
// The order a front end sees (decision 0012): a request's reply, then its
// document.changed, then the poll's events. Settling a reply's promise only
// queues the code awaiting it; the events behind the reply wait for a
// macrotask, so that code (and every microtask it starts) runs first. Used
// by the client and by the renderer's proxy, which see the same order.

/** Runs `fn` after the current macrotask (and its microtasks). */
export type Defer = (fn: () => void) => void;

/** What makeDefer looks for on the global object. */
export interface DeferGlobals {
  setImmediate?: (fn: () => void) => unknown;
  MessageChannel?: new () => {
    port1: { onmessage: ((event: unknown) => void) | null; unref?: () => void };
    port2: { postMessage: (value: unknown) => void; unref?: () => void };
  };
  setTimeout: (fn: () => void, ms: number) => unknown;
}

/**
 * The deferral for a global object: setImmediate where there is one (Node,
 * Electron's main process); else a MessageChannel (a browser's renderer:
 * setTimeout(0) is held to 4 ms once timers nest five deep, so a burst of
 * replies would be delivered 4 ms apart); else setTimeout.
 */
export function makeDefer(g: DeferGlobals): Defer {
  const setImmediate = g.setImmediate;
  if (setImmediate !== undefined) {
    return (fn) => {
      setImmediate(fn);
    };
  }
  const Channel = g.MessageChannel;
  if (Channel !== undefined) {
    const channel = new Channel();
    const queue: (() => void)[] = [];
    channel.port1.onmessage = () => {
      queue.shift()?.();
    };
    // Node's ports hold the process open; a browser's have no unref
    channel.port1.unref?.();
    channel.port2.unref?.();
    return (fn) => {
      queue.push(fn);
      channel.port2.postMessage(null);
    };
  }
  return (fn) => {
    g.setTimeout(fn, 0);
  };
}

/** The deferral of this platform. */
export const defaultDefer: Defer = makeDefer(globalThis as unknown as DeferGlobals);

/** Delivers items in order, yielding a macrotask after each settled reply when more wait. */
export class OrderedInbox<T> {
  private readonly items: T[] = [];
  private head = 0;
  private running = false;
  private scheduled = false;
  // a reply was settled and nothing has waited a macrotask since
  private replied = false;
  private readonly deliver: (item: T) => 'reply' | 'other';
  private readonly defer: Defer;

  /** `deliver` handles one item and says whether it settled a reply. */
  constructor(deliver: (item: T) => 'reply' | 'other', defer: Defer = defaultDefer) {
    this.deliver = deliver;
    this.defer = defer;
  }

  /** Items not delivered yet. */
  get waiting(): number {
    return this.items.length - this.head;
  }

  push(item: T): void {
    this.items.push(item);
    if (this.running || this.scheduled) {
      return;
    }
    if (this.replied) {
      // the item came in the same task as a reply (the parser hands over a
      // chunk's messages one by one): it waits for the awaiting code
      this.schedule();
    } else {
      this.drain();
    }
  }

  private schedule(): void {
    this.scheduled = true;
    this.defer(() => {
      this.drain();
    });
  }

  private clearReplied(): void {
    if (!this.running && !this.scheduled) {
      this.replied = false;
    }
  }

  private drain(): void {
    this.scheduled = false;
    this.replied = false;
    this.running = true;
    let handedOn = false;
    try {
      while (this.head < this.items.length) {
        const item = this.items[this.head] as T;
        this.head++;
        if (this.deliver(item) === 'reply') {
          this.replied = true;
          if (this.head < this.items.length) {
            handedOn = true;
            this.schedule();
            return;
          }
          // a macrotask clears the mark, so an item of a later task need not wait
          this.defer(() => {
            this.clearReplied();
          });
        }
      }
    } finally {
      this.running = false;
      if (!handedOn && this.head < this.items.length) {
        // `deliver` threw: carry on with the rest later rather than stall
        this.schedule();
      } else if (this.head === this.items.length) {
        this.items.length = 0;
        this.head = 0;
      } else if (this.head > 1024 && this.head * 2 > this.items.length) {
        // a steady stream never drains to empty: drop what was delivered
        this.items.splice(0, this.head);
        this.head = 0;
      }
    }
  }
}
