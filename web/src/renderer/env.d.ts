// SPDX-License-Identifier: GPL-3.0-or-later

import type { SlApp } from '../engine/app-bridge.ts';
import type { SlBridge } from '../engine/bridge.ts';

declare global {
  interface Window {
    /** The engine bridge of the preload script (src/preload/index.ts). */
    readonly sl: SlBridge;
    /** The app's menus, dialogs, store and rescue copy (stage 6). */
    readonly slApp: SlApp;
  }
}
