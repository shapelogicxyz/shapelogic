// SPDX-License-Identifier: GPL-3.0-or-later
//
// The handlers of the engine's events, statuses and problems, one registry
// for the three that deliver them (the client, the main process's engine
// host, the renderer's proxy; cleanup item 12.4). Each handler is called
// on its own: one that throws does not keep the value from the others, and
// what it threw goes to the owner's callback. Platform-free.

import type { EventName, EventParams } from './client-types.ts';

/** What the owner hears of a handler that threw: which handler, and the error in words. */
export type HandlerThrew = (detail: string) => void;

/** The handlers of one kind of value (a status, a problem). */
export class Listeners<T> {
  private readonly set = new Set<(value: T) => void>();
  private readonly threw: HandlerThrew;
  private readonly what: string;

  /** `what` names the handlers in the callback's words ("a status handler"). */
  constructor(what: string, threw: HandlerThrew) {
    this.what = what;
    this.threw = threw;
  }

  /** How many listen. */
  get size(): number {
    return this.set.size;
  }

  /** Adds a handler; returns its removal. */
  on(handler: (value: T) => void): () => void {
    this.set.add(handler);
    return () => {
      this.set.delete(handler);
    };
  }

  /** Calls every handler with the value, each guarded (one added meanwhile waits for the next). */
  emit(value: T): void {
    for (const h of [...this.set]) {
      try {
        h(value);
      } catch (e) {
        this.threw(`${this.what} threw: ${String(e)}`);
      }
    }
  }
}

/** The handlers of the engine's events: by event, and of every event. */
export class EventHandlers {
  private readonly byEvent = new Map<string, Set<(params: never) => void>>();
  private readonly any = new Set<(event: string, params: unknown) => void>();
  private readonly threw: HandlerThrew;

  constructor(threw: HandlerThrew) {
    this.threw = threw;
  }

  /** Adds a handler of one event; returns its removal. */
  on<E extends EventName>(event: E, handler: (params: EventParams<E>) => void): () => void {
    let set = this.byEvent.get(event);
    if (set === undefined) {
      set = new Set();
      this.byEvent.set(event, set);
    }
    const h = handler as (params: never) => void;
    set.add(h);
    return () => {
      set.delete(h);
      if (set.size === 0 && this.byEvent.get(event) === set) {
        this.byEvent.delete(event);
      }
    };
  }

  /** Adds a handler of every event; returns its removal. */
  onEvent(handler: (event: string, params: unknown) => void): () => void {
    this.any.add(handler);
    return () => {
      this.any.delete(handler);
    };
  }

  /** Calls the event's handlers, then those of every event, each guarded. */
  emit(event: string, params: unknown): void {
    for (const h of [...(this.byEvent.get(event) ?? [])]) {
      try {
        (h as (p: unknown) => void)(params);
      } catch (e) {
        this.threw(`a ${event} handler threw: ${String(e)}`);
      }
    }
    for (const h of [...this.any]) {
      try {
        h(event, params);
      } catch (e) {
        this.threw(`an event handler threw on ${event}: ${String(e)}`);
      }
    }
  }
}
