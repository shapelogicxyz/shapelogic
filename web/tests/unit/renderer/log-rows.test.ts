// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Log File Manager's rows (src/renderer/shell/log-rows.ts): a line in
// its parts for the Pretty view (the date, the time to the minute, the
// level, the source, the event and the rest), and the search's text marked
// in it, any case, escaped.

import { describe, expect, it } from 'vitest';

import { markHtml, partsOf, rowHtml } from '../../../src/renderer/shell/log-rows.ts';

const CONTINUED =
  '2026-11-16 14:39:55.123 INFO  [window 2] Solve Continued: problem P1 "Box" of "/a/b.xmpuzzle", options {}';

describe('a log line in its parts', () => {
  it('has the date, the time without its seconds, the level, the source and the event', () => {
    expect(partsOf(CONTINUED)).toEqual({
      date: '2026-11-16',
      time: '14:39',
      stamp: '2026-11-16 14:39:55.123',
      level: 'INFO',
      source: 'window 2',
      event: 'Solve Continued',
      text: 'problem P1 "Box" of "/a/b.xmpuzzle", options {}',
    });
  });

  it('has the event alone, or before a text with colons and quotes of its own', () => {
    expect(partsOf('2026-11-16 14:39:55.123 INFO  [app] Debug Logging Off')).toMatchObject({
      event: 'Debug Logging Off',
      text: '',
    });
    expect(
      partsOf('2026-11-16 14:39:55.123 ERROR [window 1] File Open Failed: "/a: b" (io_error): no'),
    ).toMatchObject({ event: 'File Open Failed', text: '"/a: b" (io_error): no' });
  });

  it('has no event in a line from before the log had them, unless it starts with one', () => {
    expect(partsOf('2026-11-16 14:39:55.123 INFO  [window 1] Solve continued: p1')?.event).toBe(
      'Solve continued',
    );
    const opened = partsOf('2026-11-16 14:39:55.123 INFO  [app] Opened "/a/b: c.xmpuzzle"');
    expect(opened).toMatchObject({ event: '', text: 'Opened "/a/b: c.xmpuzzle"' });
    const failed = partsOf(
      '2026-11-16 14:39:55.123 ERROR [app] Could not open "/x" (io_error): it is not a puzzle',
    );
    expect(failed).toMatchObject({ level: 'ERROR', event: '' });
    expect(partsOf('2026-11-16 14:39:55.123 WARN  [app] disk full')).toMatchObject({
      level: 'WARN',
      event: '',
      text: 'disk full',
    });
  });

  it('is none for a line not in the format', () => {
    expect(partsOf('something else')).toBeNull();
  });
});

describe('the search marked', () => {
  it('marks each place, any case, and escapes the rest', () => {
    expect(markHtml('Ball <b>ball</b> BALL', 'ball')).toBe(
      '<mark>Ball</mark> &#60;b&#62;<mark>ball</mark>&#60;/b&#62; <mark>BALL</mark>',
    );
  });

  it('takes the search trimmed, its signs as they are', () => {
    expect(markHtml('a (1) b', ' (1) ')).toBe('a <mark>(1)</mark> b');
    expect(markHtml('a.b', '  ')).toBe('a.b');
  });

  it('marks it in the rows of both views', () => {
    const entry = { level: 'INFO' as const, line: CONTINUED };
    expect(rowHtml(entry, 'raw', 'box')).toContain('<mark>Box</mark>');
    const row = rowHtml(entry, 'pretty', 'continued');
    expect(row).toContain('<td class="lf-ev">Solve <mark>Continued</mark></td>');
    expect(row).toContain('<td class="lf-time" title="2026-11-16 14:39:55.123">14:39</td>');
    expect(row).toContain('<span class="lf-lv-info">INFO</span>');
  });
});
