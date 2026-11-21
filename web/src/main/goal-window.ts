// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Play tab's goal picture in a window of its own (BtApp.goalWindow):
// one for each app window, above it and closed with it, which the user
// moves and sizes. It has no preload and loads nothing but its page, a
// data: URL (engine/app-bridge.ts's goalPage) whose one image is the
// drawing the page sent; the request filter lets data: URLs through and
// the window opens nothing and goes nowhere else.

import { BrowserWindow } from 'electron';

import { goalPage, goalWindowSize } from '../engine/app-bridge.ts';
import type { GoalPicture } from '../engine/app-bridge.ts';
import { windowOptions } from './security.ts';

/** The goal windows, by their app window's web contents' id. */
export class GoalWindows {
  private readonly open = new Map<number, BrowserWindow>();

  /**
   * Shows `picture` in `owner`'s goal window: `raise` opens it, or brings
   * it forward; else only a window open already shows it.
   */
  show(owner: BrowserWindow, picture: GoalPicture, raise: boolean): void {
    const id = owner.webContents.id;
    let win = this.open.get(id);
    if (win?.isDestroyed() === true) {
      this.open.delete(id);
      win = undefined;
    }
    if (win === undefined) {
      if (!raise) {
        return;
      }
      win = this.create(owner, picture);
      this.open.set(id, win);
      const made = win;
      made.on('closed', () => {
        if (this.open.get(id) === made) {
          this.open.delete(id);
        }
      });
    }
    void win.loadURL(`data:text/html;base64,${Buffer.from(goalPage(picture)).toString('base64')}`);
    if (raise) {
      if (win.isMinimized()) {
        win.restore();
      }
      win.show();
      win.focus();
    }
  }

  private create(owner: BrowserWindow, picture: GoalPicture): BrowserWindow {
    const size = goalWindowSize(picture);
    // beside the app window's right edge, near its top
    const b = owner.getBounds();
    const win = new BrowserWindow({
      ...windowOptions(null, picture.title, {
        ...size,
        x: Math.round(b.x + b.width - size.width - 40),
        y: Math.round(b.y + 80),
      }),
      parent: owner,
      useContentSize: true,
      minWidth: 120,
      minHeight: 90,
      minimizable: false,
      fullscreenable: false,
      backgroundColor: picture.background.startsWith('#') ? picture.background : undefined,
    });
    win.setMenuBarVisibility(false);
    const contents = win.webContents;
    // the page never leaves itself and opens nothing
    contents.on('will-navigate', (e) => {
      e.preventDefault();
    });
    contents.setWindowOpenHandler(() => ({ action: 'deny' }));
    win.once('ready-to-show', () => {
      win.show();
    });
    return win;
  }
}
