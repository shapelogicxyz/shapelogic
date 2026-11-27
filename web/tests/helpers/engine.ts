// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real engine for the engine tests: build/release/engine/shapelogic_engine (or
// SHAPELOGIC_ENGINE), run from the repository's root so fixture paths are relative.

import { EngineClient } from '../../src/engine/client.ts';
import type { ClientProblem } from '../../src/engine/client.ts';
import type { EventName, EventParams, Params } from '../../src/engine/client-types.ts';
import { openAssemblyExplorer } from '../../src/engine/explore-open.ts';
import type { AssemblyOpened } from '../../src/engine/explore-open.ts';
import { NodeTransport } from '../../src/engine/node-transport.ts';

import { ROOT, testEngine } from './engine-path.ts';

export { ROOT };
export const FIXTURES = 'tests/data/puzzles';

/**
 * The engine the tests run, or null (the tests are skipped with a message):
 * the run's copy of it (global-setup.ts).
 */
export const ENGINE: string | null = testEngine();

/**
 * An assembly explorer of a problem, once the engine has prepared it
 * (explore.openAssembly, then explore.ready).
 */
export async function opened(
  client: EngineClient,
  params: Params<'explore.openAssembly'>,
): Promise<AssemblyOpened> {
  const explorer = await openAssemblyExplorer(client, params).done;
  if (explorer === null) {
    throw new Error('the opening was canceled');
  }
  return explorer;
}

/** One thing the client delivered, in delivery order. */
export interface Seen {
  seq: number;
  event: string;
  params: Record<string, unknown>;
}

/** A client of a fresh engine, with every event and problem kept in order. */
export class Session {
  readonly client: EngineClient;
  readonly transport: NodeTransport;
  readonly events: Seen[] = [];
  readonly problems: ClientProblem[] = [];
  private seq = 0;
  private waiters: (() => void)[] = [];

  private constructor(transport: NodeTransport, client: EngineClient) {
    this.transport = transport;
    this.client = client;
    client.onEvent((event, params) => {
      this.events.push({ seq: this.next(), event, params: params as Record<string, unknown> });
      for (const w of this.waiters.splice(0)) {
        w();
      }
    });
    client.onProblem((p) => this.problems.push(p));
  }

  static async start(): Promise<Session> {
    if (ENGINE === null) {
      throw new Error('no engine');
    }
    const transport = await NodeTransport.start(ENGINE, { cwd: ROOT });
    const client = new EngineClient(transport);
    const session = new Session(transport, client);
    await client.hello();
    return session;
  }

  /** A sequence number in the order of deliveries (events and the code after awaits share it). */
  next(): number {
    return this.seq++;
  }

  /** The first event from index `from` that fits; waits for it. */
  async waitFor<E extends EventName>(
    event: E,
    pred: (p: EventParams<E>) => boolean = () => true,
    from = 0,
    timeoutMs = 60_000,
  ): Promise<EventParams<E> & { seq: number; index: number }> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      for (let i = from; i < this.events.length; i++) {
        const e = this.events[i];
        if (e?.event === event && pred(e.params as EventParams<E>)) {
          return { ...(e.params as EventParams<E>), seq: e.seq, index: i };
        }
      }
      const left = deadline - Date.now();
      if (left <= 0) {
        throw new Error(`no ${event} within ${timeoutMs} ms`);
      }
      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, left);
        this.waiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
  }

  /** Waits until no event has come for `quietMs`. */
  async drain(quietMs = 200): Promise<void> {
    for (;;) {
      const n = this.events.length;
      await new Promise((r) => setTimeout(r, quietMs));
      if (this.events.length === n) {
        return;
      }
    }
  }

  /** Ends the engine through its stdin and waits for it. */
  async end(): Promise<void> {
    if (this.client.closed) {
      return;
    }
    await new Promise<void>((resolve) => {
      this.client.onExit(() => {
        resolve();
      });
      this.client.close();
    });
  }
}
