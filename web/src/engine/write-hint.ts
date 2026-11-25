// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes that Windows refuses say why (cleanup item 2.5): Microsoft
// Defender's Controlled Folder Access (Windows Security ▸ Ransomware
// protection) keeps programs it does not know, an unsigned app among them,
// from writing into Documents, Desktop, Pictures and other folders, and the
// write then fails as access denied: EACCES or EPERM in the main process,
// "Access is denied (5)" or errno 13 in the engine's text. Platform-free:
// the renderer's messages and the main process's log use it.

/** The words added to a failed save or export that Windows may have blocked. */
export const CONTROLLED_FOLDER_HINT =
  'Windows may be blocking ShapeLogic from writing to this folder (Controlled Folder Access). ' +
  'Allow ShapeLogic in Windows Security ▸ Virus & threat protection ▸ Ransomware protection, ' +
  'or save to another folder.';

/** Whether an error's text says the system denied access. */
export function accessDenied(text: string): boolean {
  return /\b(EACCES|EPERM)\b|Access is denied|Permission denied|Operation not permitted|\(5\)|\(errno (1|13)\)/i.test(
    text,
  );
}

/** The hint for a failed write's text on this platform, or null. */
export function writeHint(platform: string, text: string): string | null {
  return platform === 'win32' && accessDenied(text) ? CONTROLLED_FOLDER_HINT : null;
}
