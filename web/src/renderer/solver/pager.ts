// SPDX-License-Identifier: GPL-3.0-or-later
//
// A problem's saved solutions, paged and sorted by the engine
// (solution.page: a view, the stored order stays; decision 0012,
// "Solutions are paged as a view"). Pages of PAGE rows are asked for as
// the list scrolls to them (`want`), each with the problem's
// solutionsVersion: pages read at one version belong together, so a page
// of a newer version makes every other page old, to be read again; rows
// of an old page stay shown until their new page comes. The list is read
// again when the engine says it changed: solve.solutionsAdded with a
// version other than the pages' (one with the same is no news and is
// ignored), or a document.changed naming the problem (`refresh`).
// Platform-free (no DOM), for the unit tests and the fake engine.

import type { SolutionRow } from '../../engine/api.generated.ts';
import type { EngineApi } from '../../engine/client-types.ts';
import { EngineError } from '../../engine/errors.ts';

/** The rows a page holds. */
export const PAGE = 100;

/** How the list is sorted: the stored order, or one of the original's sort buttons. */
export type SortKey = 'list' | 'assembly' | 'level' | 'moves' | 'pieces';

interface Page {
  version: number;
  rows: SolutionRow[];
}

export class SolutionPager {
  readonly problem: string;
  readonly sortBy: SortKey;
  readonly descending: boolean;
  /** The number of rows (null until a page came). */
  total: number | null = null;
  /** The rows with a disassembly (null until a page came); kept assemblies are the rest. */
  disassembled: number | null = null;
  /** The version the newest page was read at (null before one). */
  version: number | null = null;
  /** Calls made (solution.page), for the tests. */
  requests = 0;
  private readonly engine: Pick<EngineApi, 'call'>;
  private readonly pages = new Map<number, Page>();
  private readonly loading = new Map<number, number>();
  private wanted: [number, number] = [0, PAGE];
  private closed = false;
  private readonly listeners = new Set<() => void>();
  private failed: ((e: unknown) => void) | null = null;

  constructor(
    engine: Pick<EngineApi, 'call'>,
    problem: string,
    sortBy: SortKey,
    descending: boolean,
  ) {
    this.engine = engine;
    this.problem = problem;
    this.sortBy = sortBy;
    this.descending = descending;
  }

  /** Listens to new rows; returns the unsubscribe. */
  onChange(listener: () => void): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  /** Where failed reads are told (a read a newer one replaced, or of a gone problem, is not). */
  onError(handler: (e: unknown) => void): void {
    this.failed = handler;
  }

  /** The row at a place of the sorted list, if its page came (of any version). */
  row(i: number): SolutionRow | undefined {
    return this.pages.get(Math.floor(i / PAGE))?.rows[i % PAGE];
  }

  /** The place of a solution among the rows that came, or -1. */
  indexOf(id: string): number {
    for (const [n, p] of this.pages) {
      const k = p.rows.findIndex((r) => r.id === id);
      if (k >= 0) {
        return n * PAGE + k;
      }
    }
    return -1;
  }

  /** Whether every row of [from, to) came at the newest version. */
  current(from: number, to: number): boolean {
    for (let n = Math.floor(from / PAGE); n * PAGE < to; n++) {
      if (this.pages.get(n)?.version !== this.version) {
        return false;
      }
    }
    return this.version !== null;
  }

  /** Asks for the rows [from, to) (the part of the list shown) where they are missing or old. */
  want(from: number, to: number): void {
    this.wanted = [Math.max(0, from), Math.max(from + 1, to)];
    this.fill();
  }

  /**
   * solve.solutionsAdded told a version: pages read at another are old (a
   * version the pages already have is no news).
   */
  added(version: number): void {
    if (version !== this.version) {
      this.refresh();
    }
  }

  /** The list may have changed (a document.changed naming the problem): read the pages shown again. */
  refresh(): void {
    for (const p of this.pages.values()) {
      p.version = -1;
    }
    this.fill(true);
  }

  /** No more reads (another problem or sort). */
  close(): void {
    this.closed = true;
    this.listeners.clear();
  }

  private fill(again = false): void {
    if (this.closed) {
      return;
    }
    const [from, to] = this.wanted;
    const last = this.total === null ? to : Math.min(to, Math.max(this.total, 1));
    for (let n = Math.floor(from / PAGE); n * PAGE < last; n++) {
      const have = this.pages.get(n);
      if (this.version !== null && have?.version === this.version) {
        continue;
      }
      if (this.loading.has(n) && !again) {
        continue;
      }
      this.load(n);
    }
    if (this.total !== null) {
      // pages past the end are gone
      for (const n of [...this.pages.keys()]) {
        if (n * PAGE >= this.total && n > 0) {
          this.pages.delete(n);
        }
      }
    }
  }

  private load(n: number): void {
    const ticket = (this.loading.get(n) ?? 0) + 1;
    this.loading.set(n, ticket);
    this.requests++;
    this.engine
      .call(
        'solution.page',
        {
          problem: this.problem,
          sortBy: this.sortBy,
          descending: this.descending,
          offset: n * PAGE,
          limit: PAGE,
        },
        { coalesce: `page:${this.problem}:${String(n)}` },
      )
      .then(
        (r) => {
          if (this.closed || this.loading.get(n) !== ticket) {
            return;
          }
          this.loading.delete(n);
          const newer = this.version === null || r.solutionsVersion !== this.version;
          this.version = r.solutionsVersion;
          this.total = r.total;
          this.disassembled = r.disassembled;
          this.pages.set(n, { version: r.solutionsVersion, rows: r.rows });
          if (newer) {
            // the list changed: the other pages are of an older one
            for (const [k, p] of this.pages) {
              if (k !== n && p.version !== r.solutionsVersion) {
                p.version = -1;
              }
            }
          }
          this.fill();
          for (const l of [...this.listeners]) {
            l();
          }
        },
        (e: unknown) => {
          if (this.loading.get(n) === ticket) {
            this.loading.delete(n);
          }
          if (
            this.closed ||
            (e instanceof EngineError && (e.errc === 'superseded' || e.errc === 'not_found'))
          ) {
            return;
          }
          this.failed?.(e);
        },
      );
  }
}
