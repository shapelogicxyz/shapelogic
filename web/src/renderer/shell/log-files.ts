// SPDX-License-Identifier: GPL-3.0-or-later
//
// View ▸ Log File Manager… (not in the mockup or the original): the app's
// log file (main/app-log.ts), newest line on top, a hundred lines at a
// time with Load more messages… for the next hundred. Errors and warnings
// shows those alone; the search box keeps the lines with its text in them, any
// case, once typing pauses (SEARCH_WAIT_MS), the text marked where it is in
// each line. Pretty (at the top right) shows the lines as a table: the
// date, the time to the minute, the level in its color, the source, the
// event (`Solve continued`) and the rest; Raw shows them as the file has
// them. Export… offers the last 100 lines or all of the log (the older,
// compressed file's lines too), each with its size, and writes the one
// picked to a plain text file the save dialog gives; Clear All Log Data…
// deletes both files, after asking; Refresh reads the file again. At the
// bottom left, where to report a problem: GitHub Issues (opened in the
// browser) or an email. The main process reads the file: the renderer asks
// for a page by its filters and how many lines it shows, and tells it when
// the dialog closes (it drops the lines it read).

import './log-files.css';
import type {
  SlApp,
  LogEntry,
  LogExportSizes,
  LogPage,
  LogQuery,
} from '../../engine/app-bridge.ts';
import { sizeText } from './cache-text.ts';
import { askChoice, openDialog } from './dialog.ts';
import { esc } from './dom.ts';
import { exportChoices, REPORT_HTML } from './log-export.ts';
import { rowHtml, tableHtml } from './log-rows.ts';
import type { LogView } from './log-rows.ts';
import { closePop, focusPop, openPop, popOpenFor } from './popover.ts';
import { flash, reportError } from './status.ts';

const ID = 'logFiles';
/** The lines a page holds (the main process's LOG_PAGE_LINES). */
export const PAGE_LINES = 100;
/** How long typing in the search box pauses before the log is searched (cleanup item 10.9). */
export const SEARCH_WAIT_MS = 200;

/** The view the dialog opens in: the last one picked while the app runs. */
let lastView: LogView = 'pretty';

/** What the list says when no line matches. */
export function emptyHtml(query: LogQuery, lines: number): string {
  if (lines === 0) {
    return `<div class="lf-empty">The log is empty.</div>`;
  }
  const what = query.errorsOnly ? 'error or warning' : 'line';
  return query.search.trim() === ''
    ? `<div class="lf-empty">No ${what} in the log.</div>`
    : `<div class="lf-empty">No ${what} has “${esc(query.search.trim())}” in it.</div>`;
}

/** The line under the filters: how many lines match, and the file. */
export function summaryHtml(page: LogPage, query: LogQuery, shown: number): string {
  const filtered = query.errorsOnly || query.search.trim() !== '';
  const count = filtered
    ? `${page.matches.toLocaleString()} of ${page.lines.toLocaleString()} lines match`
    : `${page.lines.toLocaleString()} ${page.lines === 1 ? 'line' : 'lines'}`;
  const showing = page.matches > shown ? `, the newest ${shown.toLocaleString()} shown` : '';
  const older = page.archived ? ' An older log, compressed, is beside it: Export includes it.' : '';
  const debug = page.debug ? ' · debug logging on' : '';
  return `${count}${showing} · ${esc(sizeText(page.bytes))}${debug} · <code>${esc(page.file)}</code>${older}`;
}

/** The dialog's state and its parts. */
class LogFilesView {
  private readonly app: SlApp;
  private readonly el: HTMLElement;
  private query: LogQuery = { offset: 0, errorsOnly: false, search: '' };
  private view: LogView = lastView;
  /** The lines the list shows, kept to draw them again in the other view. */
  private entries: LogEntry[] = [];
  /** The page the list shows them from (its counts, for the empty list). */
  private page: LogPage | null = null;
  private shown = 0;
  /** Each read's number: a reply to an older one (a letter typed since) is dropped. */
  private asked = 0;
  private busy = false;
  /** Export…'s sizes are being read (its choices not shown yet). */
  private choosing = false;
  /** The search waiting for typing to pause. */
  private searchTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(app: SlApp, el: HTMLElement) {
    this.app = app;
    this.el = el;
    const search = this.part<HTMLInputElement>('.lf-search');
    search.addEventListener('input', () => {
      this.query = { ...this.query, search: search.value };
      this.stopSearch();
      this.searchTimer = setTimeout(() => {
        this.searchTimer = null;
        void this.load(false);
      }, SEARCH_WAIT_MS);
    });
    const errors = this.part<HTMLInputElement>('.lf-errors');
    errors.addEventListener('change', () => {
      this.query = { ...this.query, errorsOnly: errors.checked };
      void this.load(false);
    });
    this.part('.lf-more').addEventListener('click', () => {
      void this.load(true);
    });
    for (const b of this.el.querySelectorAll<HTMLButtonElement>('.lf-view button')) {
      b.addEventListener('click', () => {
        this.setView(b.dataset.v === 'raw' ? 'raw' : 'pretty');
      });
    }
    this.markView();
  }

  /** Drops the search waiting for typing to pause (a read now, or the dialog closed). */
  stopSearch(): void {
    if (this.searchTimer !== null) {
      clearTimeout(this.searchTimer);
      this.searchTimer = null;
    }
  }

  /** Pretty or Raw: the lines shown drawn again in it, the scroll kept. */
  setView(view: LogView): void {
    if (view === this.view) {
      return;
    }
    this.view = view;
    lastView = view;
    this.markView();
    const list = this.part('.lf-list');
    const top = list.scrollTop;
    this.draw();
    list.scrollTop = top;
  }

  private markView(): void {
    for (const b of this.el.querySelectorAll<HTMLButtonElement>('.lf-view button')) {
      const on = b.dataset.v === this.view;
      b.classList.toggle('on', on);
      b.setAttribute('aria-pressed', String(on));
    }
    this.part('.lf-list').classList.toggle('pretty', this.view === 'pretty');
  }

  private rows(entries: readonly LogEntry[]): string {
    return entries.map((e) => rowHtml(e, this.view, this.query.search)).join('');
  }

  /** The list drawn from the lines it shows, in the view picked. */
  private draw(): void {
    const list = this.part('.lf-list');
    if (this.entries.length === 0) {
      list.innerHTML = emptyHtml(this.query, this.page?.lines ?? 0);
    } else {
      const rows = this.rows(this.entries);
      list.innerHTML = this.view === 'pretty' ? tableHtml(rows) : rows;
    }
  }

  private part<T extends HTMLElement = HTMLElement>(sel: string): T {
    const p = this.el.querySelector<T>(sel);
    if (p === null) {
      throw new Error(`the log dialog has no ${sel}`);
    }
    return p;
  }

  private error(text: string | null): void {
    const e = this.part('.lf-err');
    e.textContent = text ?? '';
    e.hidden = text === null;
  }

  /** Reads the first page again (the filters changed, Refresh), or the next one (`more`). */
  async load(more: boolean): Promise<void> {
    this.stopSearch();
    const n = ++this.asked;
    const offset = more ? this.shown : 0;
    let page: LogPage | null;
    try {
      page = await this.app.logPage({ ...this.query, offset, limit: PAGE_LINES });
    } catch (e) {
      if (n === this.asked) {
        this.error(e instanceof Error ? e.message : String(e));
      }
      return;
    }
    if (n !== this.asked) {
      return;
    }
    if (page === null) {
      this.error('There is no log here.');
      return;
    }
    this.show(page, more);
  }

  /** Shows a page: in place of the list, or below it (`more`). */
  show(page: LogPage, more: boolean): void {
    const list = this.part('.lf-list');
    this.page = page;
    if (more && this.entries.length > 0) {
      this.entries.push(...page.entries);
      const body = this.view === 'pretty' ? list.querySelector('tbody') : list;
      body?.insertAdjacentHTML('beforeend', this.rows(page.entries));
    } else {
      this.entries = [...page.entries];
      this.draw();
      if (!more) {
        list.scrollTop = 0;
      }
    }
    this.shown = this.entries.length;
    this.part('.lf-sum').innerHTML = summaryHtml(page, this.query, this.shown);
    this.part('.lf-more').hidden = !page.more;
    this.error(page.error === null ? null : `The log could not be written: ${page.error}`);
  }

  /**
   * Export…: its choices, the last lines or all of the log, in the popover
   * over the button (a menu of the dialog: the arrow keys, Tab and Esc are
   * its first, popover.ts).
   */
  async chooseExport(): Promise<void> {
    if (this.busy) {
      return;
    }
    const button = this.part('[data-x="export"]');
    if (popOpenFor(button)) {
      // a second press closes them
      closePop();
      return;
    }
    if (this.choosing) {
      return;
    }
    this.choosing = true;
    let sizes: LogExportSizes | null;
    try {
      sizes = await this.app.logExportSizes();
    } catch (e) {
      this.error(`The log could not be read: ${e instanceof Error ? e.message : String(e)}`);
      return;
    } finally {
      this.choosing = false;
    }
    if (sizes === null || !this.el.isConnected) {
      return;
    }
    openPop(
      button,
      `<div role="menu" aria-label="Export the log">${exportChoices(sizes)
        .map(
          (c) =>
            `<button type="button" role="menuitem" data-k="${c.lines === undefined ? 'all' : String(c.lines)}">${esc(c.label)}</button>`,
        )
        .join('')}</div>`,
      (k) => {
        button.focus();
        void this.exportLines(k === 'all' ? undefined : Number(k));
      },
      true,
    );
    focusPop();
  }

  /** Writes the log, all of it or its newest `lines` lines, to a file the save dialog gives. */
  async exportLines(lines: number | undefined): Promise<void> {
    if (this.busy) {
      return;
    }
    this.busy = true;
    try {
      const r = await this.app.exportLog(lines);
      if (r.ok) {
        flash(`Log exported to ${r.path}`, 5000);
      } else if (!('canceled' in r)) {
        this.error(`The log could not be exported: ${r.error}`);
      }
    } catch (e) {
      reportError(e, 'Export the log');
    } finally {
      this.busy = false;
    }
  }

  /** Clear All Log Data…: both files deleted, after asking. */
  async clearAll(): Promise<void> {
    if (this.busy) {
      return;
    }
    const answer = await askChoice(
      'logFilesClear',
      'Clear all log data?',
      'The log file and the older, compressed one are deleted. This cannot be undone.',
      [
        { key: 'cancel', label: 'Cancel', default: true },
        { key: 'clear', label: 'Clear', kind: 'danger' },
      ],
    );
    if (answer !== 'clear') {
      return;
    }
    this.busy = true;
    try {
      await this.app.clearLog();
      flash('The log was cleared', 4000);
    } catch (e) {
      reportError(e, 'Clear the log');
    } finally {
      this.busy = false;
    }
    await this.load(false);
  }
}

/** View ▸ Log File Manager…: the log, newest first, its filters, Export and Clear. */
export async function openLogFiles(app: SlApp): Promise<void> {
  let first: LogPage | null;
  try {
    first = await app.logPage({ offset: 0, limit: PAGE_LINES, errorsOnly: false, search: '' });
  } catch (e) {
    reportError(e, 'Log File Manager');
    return;
  }
  if (first === null) {
    flash('There is no log here: the demo keeps none.', 5000);
    return;
  }
  let view: LogFilesView | null = null;
  const shown = openDialog({
    id: ID,
    title: 'Log file',
    wide: true,
    head:
      `<div class="seg lf-view" role="group" aria-label="How the log is shown">` +
      `<button type="button" data-v="pretty" title="The log as a table: date, time, level, source, event and message">Pretty</button>` +
      `<button type="button" data-v="raw" title="The log's lines as the file has them">Raw</button>` +
      `</div>`,
    body:
      `<p class="lf-intro">What ShapeLogic did and what went wrong, newest first: files opened and saved, solves started, paused, continued and aborted, and the errors.</p>` +
      `<div class="lf-bar">` +
      `<input type="search" class="lf-search" placeholder="Search the log" aria-label="Search the log" spellcheck="false" autocomplete="off" data-focus>` +
      `<label class="lf-filter"><input type="checkbox" class="lf-errors"> Errors and warnings</label>` +
      `</div>` +
      `<div class="lf-sum mute"></div>` +
      `<p class="lf-err" role="alert" hidden></p>` +
      `<div class="lf-list" role="log" aria-label="The log, newest first" tabindex="0"></div>` +
      `<button class="btn sm lf-more" hidden>Load more messages…</button>`,
    buttons: [
      { key: 'clear', label: 'Clear All Log Data…', kind: 'danger', tip: 'Delete the log files' },
      {
        key: 'export',
        label: 'Export…',
        tip: 'Save the last 100 lines or all of the log as a text file',
      },
      {
        key: 'copy',
        label: 'Copy Details',
        tip: 'Put a report for a question on the clipboard: the system, the engine, the newest lines of the log (Help ▸ Copy Diagnostics)',
      },
      { key: 'refresh', label: 'Refresh', tip: 'Read the log again' },
      { key: 'close', label: 'Close', kind: 'pri', default: true },
    ],
    foot: REPORT_HTML,
    onAnswer: (key) => {
      if (key === 'refresh') {
        void view?.load(false);
        return false;
      }
      if (key === 'export') {
        void view?.chooseExport();
        return false;
      }
      if (key === 'clear') {
        void view?.clearAll();
        return false;
      }
      if (key === 'copy') {
        // cleanup item 2.4: the same report as Help ▸ Copy Diagnostics
        void app.copyDiagnostics().then((r) => {
          flash(
            r.ok
              ? 'The details are on the clipboard: paste them into an email or a GitHub issue'
              : `The details could not be copied: ${r.error}`,
            5000,
          );
        });
        return false;
      }
      return true;
    },
  });
  const el = document.getElementById(ID);
  if (el !== null) {
    view = new LogFilesView(app, el);
    view.show(first, false);
    el.querySelector('.lf-issues')?.addEventListener('click', (e) => {
      e.preventDefault();
      app.openHelp('issues');
    });
  }
  await shown;
  closePop();
  view?.stopSearch();
  void app.logClosed().catch(() => undefined);
}
