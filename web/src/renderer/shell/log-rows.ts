// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Log File Manager's rows (log-files.ts): a line of the log as Raw
// shows it (as the file has it) or as Pretty does (a table row: the date,
// the time to the minute, the level, the source, the event and the rest),
// the search's text marked in it. Free of the DOM (its own escape, as
// release-notes.ts), so the unit tests run it under Node.

import type { LogEntry } from '../../engine/app-bridge.ts';

/** Text as HTML: dom.ts's `esc`, which needs the DOM's types. */
const esc = (text: string): string =>
  text.replace(/[&<>"']/g, (c) => `&#${String(c.charCodeAt(0))};`);

/** How the list shows the lines: a table (Pretty), or as the file has them (Raw). */
export type LogView = 'pretty' | 'raw';

/** Text, escaped, with each place the search's text is in it (any case) marked. */
export function markHtml(text: string, search: string): string {
  const needle = search.trim();
  if (needle === '') {
    return esc(text);
  }
  const re = new RegExp(needle.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi');
  let out = '';
  let at = 0;
  for (const m of text.matchAll(re)) {
    out += `${esc(text.slice(at, m.index))}<mark>${esc(m[0])}</mark>`;
    at = m.index + m[0].length;
  }
  return out + esc(text.slice(at));
}

/** A line of the log in its parts, as main/app-log.ts's formatLogLine writes it. */
export interface LogParts {
  /** `2026-11-16` */
  date: string;
  /** `14:39`: the seconds and milliseconds left out. */
  time: string;
  /** `2026-11-16 14:39:55.123`: the time in full, for the tooltip. */
  stamp: string;
  level: string;
  /** `app`, `window 2`. */
  source: string;
  /** What happened, when the text starts with it: `Solve continued`; else empty. */
  event: string;
  /** The rest of the text. */
  text: string;
}

const PARTS =
  /^(\d{4}-\d\d-\d\d) (\d\d:\d\d)(:\d\d\.\d{3}) (DEBUG|INFO|WARN|ERROR) +\[([^\]]*)\] ?(.*)$/s;
/**
 * The event's name (main/app-log.ts's EVENT_NAME, the engine's too): words
 * of letters and digits, the first capitalised, before the text's `: ` or
 * alone; a line from before the log had events may have none.
 */
const EVENT = /^[A-Z][A-Za-z0-9]*(?: [A-Za-z0-9]+)*$/;
/** An event's name is short; a longer lead-in is a sentence of the text. */
const EVENT_MAX = 40;

/** A line's parts, or null for one not in the log's format. */
export function partsOf(line: string): LogParts | null {
  const m = PARTS.exec(line.trimEnd());
  if (m === null) {
    return null;
  }
  const [, date = '', time = '', rest = '', level = '', source = '', body = ''] = m;
  const colon = body.indexOf(': ');
  const lead = colon < 0 ? body : body.slice(0, colon);
  const event = lead.length <= EVENT_MAX && EVENT.test(lead) ? lead : '';
  return {
    date,
    time,
    stamp: `${date} ${time}${rest}`,
    level,
    source,
    event,
    text: event === '' ? body : colon < 0 ? '' : body.slice(colon + 2),
  };
}

/** The Pretty view's table, its rows to come in its body. */
export function tableHtml(rows: string): string {
  return (
    `<table class="lf-table"><thead><tr><th>Date</th><th>Time</th><th>Level</th><th>Source</th><th>Event</th><th>Message</th></tr></thead>` +
    `<tbody>${rows}</tbody></table>`
  );
}

/** A line of the log as its row shows it, marked by its level, the search's text marked in it. */
export function rowHtml(entry: LogEntry, view: LogView = 'raw', search = ''): string {
  const level = entry.level.toLowerCase();
  if (view === 'raw') {
    return `<div class="lf-row lf-${level}">${markHtml(entry.line, search)}</div>`;
  }
  const p = partsOf(entry.line);
  if (p === null) {
    return `<tr class="lf-row lf-${level}"><td colspan="6" class="lf-msg">${markHtml(entry.line, search)}</td></tr>`;
  }
  const cell = (cls: string, text: string): string =>
    `<td class="${cls}">${markHtml(text, search)}</td>`;
  return (
    `<tr class="lf-row lf-${level}">` +
    cell('lf-date', p.date) +
    `<td class="lf-time" title="${esc(p.stamp)}">${markHtml(p.time, search)}</td>` +
    `<td class="lf-lv"><span class="lf-lv-${p.level.toLowerCase()}">${markHtml(p.level, search)}</span></td>` +
    cell('lf-src', p.source) +
    cell('lf-ev', p.event) +
    cell('lf-msg', p.text) +
    `</tr>`
  );
}
