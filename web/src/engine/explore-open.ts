// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opening an assembly explorer (cleanup item 18.1): explore.openAssembly
// answers at once with the handle, the engine builds the matrix on a
// thread of its own, and explore.ready tells when the handle can be used or
// why it cannot. This is the one place that puts the three together, for
// the renderer's explorers and the tests: the call, the wait for
// explore.ready (heard from before the call, so one that comes with the
// reply is not missed), the progress meanwhile, and a cancel that ends the
// engine's build (explore.close).

import { ERROR_CODES } from './api.generated.ts';
import type { EngineApi, EventParams, Params } from './client-types.ts';
import { EngineError } from './errors.ts';

/** An assembly explorer that can be used. */
export interface AssemblyOpened {
  handle: string;
  pieces: number;
  placementsSupported: boolean;
  finished: number;
}

/** An assembly explorer being opened. */
export interface AssemblyOpening {
  /**
   * The explorer once explore.ready came; null when cancel() ended the
   * wait. Fails with the call's own EngineError, or with one of
   * `invalid_argument` when the matrix could not be made (explore.ready's
   * message: the pieces' units, a piece that fits nowhere).
   */
  readonly done: Promise<AssemblyOpened | null>;
  /**
   * Ends the wait: `done` resolves with null. With `close` the engine's
   * build is ended and its handle closed (explore.close), now or as soon as
   * the reply gives the handle; without, nothing is sent (the engine
   * dropped the handle with its document, or is another engine by now,
   * whose handles count from 1 again). After `done` resolved with an
   * explorer it does nothing: that handle is the caller's to close.
   */
  cancel(close: boolean): void;
}

/**
 * Opens an assembly explorer of a problem. `onPreparing` hears
 * explore.preparing while the matrix is built (at most four times a second,
 * and not at all for a build that ends sooner).
 */
export function openAssemblyExplorer(
  engine: Pick<EngineApi, 'call' | 'on'>,
  params: Params<'explore.openAssembly'>,
  onPreparing?: (told: EventParams<'explore.preparing'>) => void,
): AssemblyOpening {
  type Ready = EventParams<'explore.ready'>;
  let handle: string | null = null;
  // what cancel() asked for (an object: read again after each wait)
  const asked = { canceled: false, closeOnReply: false };
  /** Ends the wait for explore.ready, once it has begun. */
  let finish: ((ready: Ready | null) => void) | null = null;
  // an explore.ready that comes before the reply's continuation has run
  const early: Ready[] = [];
  const offReady = engine.on('explore.ready', (ready) => {
    if (handle === null) {
      early.push(ready);
    } else if (ready.handle === handle) {
      finish?.(ready);
    }
  });
  const offPreparing = engine.on('explore.preparing', (told) => {
    if (!asked.canceled && told.handle === handle) {
      onPreparing?.(told);
    }
  });
  const close = (h: string): void => {
    engine.call('explore.close', { handle: h }).catch(() => {
      // gone already (another document)
    });
  };
  const done = (async (): Promise<AssemblyOpened | null> => {
    try {
      const opened = await engine.call('explore.openAssembly', params);
      handle = opened.handle;
      if (asked.canceled) {
        if (asked.closeOnReply) {
          close(opened.handle);
        }
        return null;
      }
      const ready =
        early.find((r) => r.handle === opened.handle) ??
        (await new Promise<Ready | null>((resolve) => {
          finish = resolve;
        }));
      finish = null;
      if (ready === null) {
        return null;
      }
      if (!ready.ready) {
        throw new EngineError(
          ERROR_CODES.invalid_argument[0],
          'invalid_argument',
          ready.message ?? 'the assembler could not be prepared',
          'explore.openAssembly',
        );
      }
      return {
        handle: opened.handle,
        pieces: opened.pieces,
        placementsSupported: ready.placementsSupported,
        finished: ready.finished,
      };
    } finally {
      offReady();
      offPreparing();
    }
  })();
  return {
    done,
    cancel: (withClose: boolean): void => {
      if (asked.canceled) {
        return;
      }
      asked.canceled = true;
      if (handle === null) {
        // the reply is still to come
        asked.closeOnReply = withClose;
      } else if (finish !== null) {
        if (withClose) {
          close(handle);
        }
        finish(null);
      }
    },
  };
}
