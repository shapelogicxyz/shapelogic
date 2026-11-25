// SPDX-License-Identifier: GPL-3.0-or-later
//
// Base64 without Node (cleanup item 12.11: one copy for the engine's
// recordings, the documents sent as bytes, the pictures and the goal
// window). btoa and atob work on strings of bytes, so bytes go in slices.

/** Base64 of bytes. */
export function toBase64(bytes: Uint8Array): string {
  let text = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    text += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(text);
}

/** The bytes of base64 text. */
export function fromBase64(text: string): Uint8Array<ArrayBuffer> {
  const s = atob(text);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) {
    out[i] = s.charCodeAt(i);
  }
  return out;
}
