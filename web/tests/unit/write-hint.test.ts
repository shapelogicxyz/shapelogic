// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes that Windows refuses say why (cleanup item 2.5): the Controlled
// Folder Access hint is added for an access-denied failure on Windows,
// from the main process's codes and from the engine's text, and nowhere
// else.

import { describe, expect, it } from 'vitest';

import { accessDenied, CONTROLLED_FOLDER_HINT, writeHint } from '../../src/engine/write-hint.ts';

describe('the Windows hint of a refused write', () => {
  it("is added for access denied: main's EACCES and EPERM, the engine's code 5 and errno 13", () => {
    for (const text of [
      'could not write puzzle.png (EACCES)',
      'could not write puzzle.png (EPERM)',
      'could not open C:\\Users\\a\\Documents\\p.xmpuzzle.tmp for writing: Access is denied (5)',
      'could not open /x/p.xmpuzzle.tmp for writing: Permission denied (errno 13)',
    ]) {
      expect(writeHint('win32', text), text).toBe(CONTROLLED_FOLDER_HINT);
    }
  });

  it('is not added for other failures, nor off Windows', () => {
    expect(writeHint('win32', 'could not write p.png (ENOSPC)')).toBeNull();
    expect(writeHint('win32', 'p.xmpuzzle is read-only')).toBeNull();
    expect(writeHint('darwin', 'could not write p.png (EACCES)')).toBeNull();
    expect(accessDenied('No space left on device (errno 28)')).toBe(false);
  });

  it('names where to allow ShapeLogic', () => {
    expect(CONTROLLED_FOLDER_HINT).toContain('Controlled Folder Access');
    expect(CONTROLLED_FOLDER_HINT).toContain('Ransomware protection');
  });
});
