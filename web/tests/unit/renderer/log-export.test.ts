// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Log File Manager's Export… choices and the line about reporting a
// problem (src/renderer/shell/log-export.ts), and the notice of the cache
// files at the start (src/renderer/shell/cache-text.ts): the items it
// names (not the rescue copies) and its tip.

import { describe, expect, it } from 'vitest';

import type { CacheItem } from '../../../src/engine/app-bridge.ts';
import { noticeHtml, noticeItems } from '../../../src/renderer/shell/cache-text.ts';
import { exportChoices, REPORT_HTML } from '../../../src/renderer/shell/log-export.ts';

describe('Export… of the log', () => {
  it('offers the last 100 lines and all of the log, each with its size', () => {
    expect(
      exportChoices({ lines: 1234, bytes: 456_000, tailLines: 100, tailBytes: 23_000 }),
    ).toEqual([
      { label: 'Last 100 lines (23 KB)', lines: 100 },
      { label: 'All 1,234 lines (456 KB)', lines: undefined },
    ]);
  });

  it('offers only all of a log of 100 lines or fewer', () => {
    expect(exportChoices({ lines: 100, bytes: 9000, tailLines: 100, tailBytes: 9000 })).toEqual([
      { label: 'All 100 lines (9.0 KB)', lines: undefined },
    ]);
    expect(exportChoices({ lines: 1, bytes: 60, tailLines: 1, tailBytes: 60 })).toEqual([
      { label: 'All 1 line (60 bytes)', lines: undefined },
    ]);
  });

  it('says where to report a problem: GitHub Issues as a link, and the address', () => {
    expect(REPORT_HTML).toContain('>GitHub Issues</a>');
    expect(REPORT_HTML).toContain('shapelogic.xyz@gmail.com');
    expect(REPORT_HTML.replace(/<[^>]+>/g, '')).toBe(
      'Copy or export relevant log lines to open an issue in GitHub Issues or as an email to shapelogic.xyz@gmail.com',
    );
  });
});

const item = (kind: CacheItem['kind'], title: string, bytes: number): CacheItem => ({
  id: `${kind}:${title}`,
  kind,
  title,
  about: '',
  bytes,
  puzzle: null,
  puzzleName: null,
  problem: null,
  created: 0,
  modified: 0,
  files: [],
  moreFiles: 0,
});

describe('the notice of the cache files at the start', () => {
  it('names every item but the rescue copies, their size in all, and the tip', () => {
    const items = noticeItems([
      item('sliding', 'Paused sliding solve of Block 15.xmpuzzle', 2_000_000),
      item('rescue', 'Autosave of Bermuda.xmpuzzle', 5000),
      item('other', 'Leftover files', 1_000_000),
    ]);
    expect(items.map((i) => i.kind)).toEqual(['sliding', 'other']);
    const html = noticeHtml(items);
    expect(html).toContain(
      'These 2 items were left in the cache folder by earlier sessions (3.0 MB in all)',
    );
    expect(html).toContain('Paused sliding solve of Block 15.xmpuzzle');
    expect(html).toContain('Sliding solve · 2.0 MB');
    expect(html).toContain(
      'Tip: These files can be managed using View ▸ Manage Cache and Autosave Files.',
    );
    expect(noticeItems([item('rescue', 'Autosave', 1)])).toEqual([]);
  });
});
