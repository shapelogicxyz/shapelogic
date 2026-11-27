// SPDX-License-Identifier: GPL-3.0-or-later
//
// The main process's side of window.slApp (engine/app-bridge.ts, stage 6),
// one module per group of channels under ipc/ (cleanup item 13.4), each
// with its own small dependency interface: the dialogs and exports
// (ipc/files.ts), the store, recent files and updates (ipc/store.ts), the
// rescue copy (ipc/rescue.ts), the cache folder and manager
// (ipc/cache.ts), the log (ipc/log.ts), the command line tool
// (ipc/cli.ts), the window: menus, close, goal picture and help
// (ipc/window.ts), and the window's engine: its bridge, its start and the
// diagnostics (ipc/engine.ts). Every message is checked before it is
// used, and only the app's windows are answered: each channel of
// app-bridge.ts's table (cleanup item 12.2) is registered through
// ipc/register.ts's `handle`, `on` or `sync`, which check the sender once.
// A new channel goes in the group it belongs to; a group's new need goes
// in its interface, which AppIpcHost joins.

import type { CacheIpc } from './ipc/cache.ts';
import { installCacheIpc } from './ipc/cache.ts';
import type { CliIpc } from './ipc/cli.ts';
import { installCliIpc } from './ipc/cli.ts';
import type { EngineIpc } from './ipc/engine.ts';
import { installEngineIpc } from './ipc/engine.ts';
import type { FilesIpc } from './ipc/files.ts';
import { installFilesIpc } from './ipc/files.ts';
import type { LogIpc } from './ipc/log.ts';
import { installLogIpc } from './ipc/log.ts';
import { registrar } from './ipc/register.ts';
import type { IpcWindows } from './ipc/register.ts';
import type { RescueIpc } from './ipc/rescue.ts';
import { installRescueIpc } from './ipc/rescue.ts';
import type { StoreIpc } from './ipc/store.ts';
import { installStoreIpc } from './ipc/store.ts';
import type { WindowIpc } from './ipc/window.ts';
import { installWindowIpc } from './ipc/window.ts';

/** What every group needs of the main process: the groups' interfaces together. */
export type AppIpcHost = IpcWindows &
  FilesIpc &
  StoreIpc &
  RescueIpc &
  CacheIpc &
  LogIpc &
  CliIpc &
  WindowIpc &
  EngineIpc;

/** Installs every group's handlers, each answering only the app's windows. */
export function installAppIpc(h: AppIpcHost): void {
  const r = registrar(h);
  installEngineIpc(r, h);
  installStoreIpc(r, h);
  installFilesIpc(r, h);
  installRescueIpc(r, h);
  installWindowIpc(r, h);
  installCliIpc(r, h);
  installCacheIpc(r, h);
  installLogIpc(r, h);
}
