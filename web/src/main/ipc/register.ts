// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the main process registers the channels of app-bridge.ts's table
// (cleanup item 12.2), shared by the groups of ../app-ipc.ts (cleanup item
// 13.4): `handle`, `on` and `sync` each check once that the sender is one
// of the app's windows (a window that is not is answered the channel's
// `refused`, or not at all) and are typed by the table. Beside them what
// several groups need: `post` (main's messages to a renderer), where a
// dialog starts, and the log line of a write Windows refused.

import { app, ipcMain } from 'electron';
import type { BrowserWindow, IpcMainEvent, IpcMainInvokeEvent, WebContents } from 'electron';
import { existsSync } from 'node:fs';
import { dirname } from 'node:path';

import type { ChannelArgs, ChannelOf, ChannelResult } from '../../engine/app-bridge.ts';
import { writeHint } from '../../engine/write-hint.ts';
import type { AppLog } from '../app-log.ts';
import type { StateStore } from '../state-store.ts';

/** What a handler is handed of a channel's arguments: as many, each unchecked (the renderer is not trusted). */
export type Unchecked<A extends readonly unknown[]> = { [K in keyof A]-?: unknown };

/** The windows the channels answer. */
export interface IpcWindows {
  /** The app's window of a renderer, or null (not ours: not answered). */
  window(sender: WebContents): BrowserWindow | null;
}

/** The three ways a channel is registered, each checking the sender once. */
export interface Registrar {
  /** An invoke channel: a window that is not the app's is answered `refused`. */
  handle<C extends ChannelOf<'invoke'>>(
    channel: C,
    refused: NoInfer<ChannelResult<C>>,
    fn: NoInfer<
      (
        e: IpcMainInvokeEvent,
        win: BrowserWindow,
        ...args: Unchecked<ChannelArgs<C>>
      ) => ChannelResult<C> | Promise<ChannelResult<C>>
    >,
  ): void;
  /** A send channel: a window that is not the app's is not heard. */
  on<C extends ChannelOf<'send'>>(
    channel: C,
    fn: (e: IpcMainEvent, win: BrowserWindow, ...args: Unchecked<ChannelArgs<C>>) => void,
  ): void;
  /** A sync channel: a window that is not the app's gets `refused`. */
  sync<C extends ChannelOf<'sync'>>(
    channel: C,
    refused: ChannelResult<C>,
    fn: (e: IpcMainEvent, win: BrowserWindow) => ChannelResult<C>,
  ): void;
}

/** The registrar over ipcMain that answers only `w`'s windows. */
export function registrar(w: IpcWindows): Registrar {
  const handle: Registrar['handle'] = <C extends ChannelOf<'invoke'>>(
    channel: C,
    refused: ChannelResult<C>,
    fn: (
      e: IpcMainInvokeEvent,
      win: BrowserWindow,
      ...args: Unchecked<ChannelArgs<C>>
    ) => ChannelResult<C> | Promise<ChannelResult<C>>,
  ): void => {
    ipcMain.handle(channel, (e, ...args: unknown[]) => {
      const win = w.window(e.sender);
      return win === null ? refused : fn(e, win, ...(args as Unchecked<ChannelArgs<C>>));
    });
  };
  const on: Registrar['on'] = <C extends ChannelOf<'send'>>(
    channel: C,
    fn: (e: IpcMainEvent, win: BrowserWindow, ...args: Unchecked<ChannelArgs<C>>) => void,
  ): void => {
    ipcMain.on(channel, (e, ...args: unknown[]) => {
      const win = w.window(e.sender);
      if (win !== null) {
        fn(e, win, ...(args as Unchecked<ChannelArgs<C>>));
      }
    });
  };
  const sync: Registrar['sync'] = <C extends ChannelOf<'sync'>>(
    channel: C,
    refused: ChannelResult<C>,
    fn: (e: IpcMainEvent, win: BrowserWindow) => ChannelResult<C>,
  ): void => {
    ipcMain.on(channel, (e) => {
      const win = w.window(e.sender);
      e.returnValue = win === null ? refused : fn(e, win);
    });
  };
  return { handle, on, sync };
}

/** Sends a window's renderer a message of a main-to-renderer channel (app-bridge.ts's table). */
export function post<C extends ChannelOf<'event'>>(
  contents: Pick<WebContents, 'send'>,
  channel: C,
  ...args: ChannelArgs<C>
): void {
  contents.send(channel, ...args);
}

/**
 * Where a dialog starts: the folder a dialog gave last, kept across runs
 * (the store), unless it is gone since; else the documents folder.
 */
export function startDir(store: Pick<StateStore, 'lastDir'>): string {
  const d = store.lastDir;
  return d !== undefined && existsSync(d) ? d : defaultDir();
}

function defaultDir(): string {
  try {
    return app.getPath('documents');
  } catch {
    return '';
  }
}

/**
 * A write Windows refused as access denied: the log says where, and that
 * Controlled Folder Access may be why (cleanup item 2.5).
 */
export function logRefusedWrite(log: Pick<AppLog, 'warn'>, path: string, message: string): void {
  const hint = writeHint(process.platform, message);
  if (hint !== null) {
    log.warn('app', 'Write Refused', `${message} in the folder ${dirname(path)}. ${hint}`);
  }
}
