// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo page's bridge while tools/record-demo.ts records it: calls go
// to the recorder's real engine through a Playwright binding
// (window.__btDemoSend), and its messages come back through
// window.__btDemoDeliver, a frame's bytes as base64.

import type { BridgeMessage, BridgeRequest, BtBridge } from '../engine/bridge.ts';
import { fromBase64 } from '../engine/recorder.ts';
import type { WireMessage } from './session.ts';

interface LiveWindow {
  __btDemoSend?: (request: BridgeRequest) => void;
  __btDemoDeliver?: (message: WireMessage) => void;
}

/** Whether the recorder drives the page (it defines the binding before the page's scripts). */
export function liveRecording(): boolean {
  return typeof (window as LiveWindow).__btDemoSend === 'function';
}

/** The recorder's engine as the preload's bridge. */
export class LiveBridge implements BtBridge {
  private readonly handlers = new Set<(message: BridgeMessage) => void>();

  constructor() {
    (window as LiveWindow).__btDemoDeliver = (m) => {
      const message: BridgeMessage =
        m.kind === 'reply' && m.frame !== undefined
          ? {
              ...m,
              frame: { desc: m.frame.desc, data: fromBase64(m.frame.data).buffer as ArrayBuffer },
            }
          : (m as BridgeMessage);
      for (const h of [...this.handlers]) {
        h(message);
      }
    };
  }

  send(request: BridgeRequest): void {
    (window as LiveWindow).__btDemoSend?.(request);
  }

  onMessage(handler: (message: BridgeMessage) => void): () => void {
    this.handlers.add(handler);
    return () => {
      this.handlers.delete(handler);
    };
  }
}
