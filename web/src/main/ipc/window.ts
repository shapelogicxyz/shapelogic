// SPDX-License-Identifier: GPL-3.0-or-later
//
// window.btApp's window (a group of ../app-ipc.ts, cleanup item 13.4): the
// menus' state, the window's close, New Window, which file the window
// opened (path-owners.ts: the first window to hold a file owns it,
// another holds it read-only; decision 0036), the Play tab's goal picture
// in a window of its own (goal-window.ts), and the help links
// (app-bridge.ts's table: the renderer names a link, never a URL).

import { shell } from 'electron';
import type { WebContents } from 'electron';

import { helpUrl, isGoalPicture, isHelpLink, isMenuState } from '../../engine/app-bridge.ts';
import type { HelpLink, MenuState } from '../../engine/app-bridge.ts';
import { GoalWindows } from '../goal-window.ts';
import type { PathGrants } from '../path-grants.ts';
import type { PathOwners } from '../path-owners.ts';
import type { Registrar } from './register.ts';

/** Opens a help page in the system's browser. */
export function openHelp(link: HelpLink): void {
  void shell.openExternal(helpUrl(link));
}

/** What the window's channels need of the main process. */
export interface WindowIpc {
  setMenu(sender: WebContents, state: MenuState): void;
  /** File ▸ New Window: another window, with an empty puzzle. */
  newWindow(): void;
  /** The files each window was handed (a window claims only those). */
  paths: Pick<PathGrants<WebContents>, 'allows'>;
  /** Which window owns each file it holds, by the web contents' id. */
  owners: Pick<PathOwners<number>, 'claim'>;
  closeTaken(sender: WebContents): void;
  closeWindow(sender: WebContents): void;
  cancelQuit(sender: WebContents): void;
  /** Whether windows are kept hidden (security.ts's BT_BACKGROUND). */
  background: boolean;
}

/** Registers the window's channels. */
export function installWindowIpc(r: Registrar, h: WindowIpc): void {
  r.on('bt:menu-state', (e, _win, state) => {
    if (isMenuState(state)) {
      h.setMenu(e.sender, state);
    }
  });

  r.on('bt:new-window', () => {
    h.newWindow();
  });

  // the window opened this file: it holds it, owned or read-only (path-owners.ts)
  r.on('bt:claim-path', (e, _win, path) => {
    if (typeof path === 'string' && h.paths.allows(e.sender, path)) {
      h.owners.claim(e.sender.id, path);
    }
  });

  r.on('bt:close-taken', (e) => {
    h.closeTaken(e.sender);
  });
  r.on('bt:close-window', (e) => {
    h.closeWindow(e.sender);
  });
  r.on('bt:close-canceled', (e) => {
    h.cancelQuit(e.sender);
  });

  // the Play tab's goal picture in its own window (goal-window.ts)
  const goals = new GoalWindows(h.background);
  r.on('bt:goal-window', (_e, win, picture, raise) => {
    if (isGoalPicture(picture)) {
      goals.show(win, picture, raise === true);
    }
  });

  r.on('bt:help', (_e, _win, link) => {
    if (isHelpLink(link)) {
      openHelp(link);
    }
  });
}
