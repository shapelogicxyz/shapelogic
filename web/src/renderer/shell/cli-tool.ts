// SPDX-License-Identifier: GPL-3.0-or-later
//
// The "Command Line Tool" dialog (ShapeLogic ▸ Install Command Line Tool…):
// says what `shapelogic_cli` is (the original's burrTxt and burrTxt2), its
// main options, where it is in this install and what to type, and below
// that Install and Cancel. Install links the tool into the shell's path
// (the main process does it, and macOS asks for the administrator's
// password); once linked, the dialog offers to remove the link. The
// renderer never sees a path it chose: the main process names them.

import type { BtApp, CliToolInfo } from '../../engine/app-bridge.ts';
import { closeDialog, openDialog } from './dialog.ts';
import type { DialogButton } from './dialog.ts';
import { esc } from './dom.ts';
import { flash, reportError } from './status.ts';

const ID = 'cliTool';
const NAME = 'shapelogic_cli';

/** The main options, as `shapelogic_cli -h` prints them (cli/options.cpp). */
const OPTIONS: readonly (readonly [string, string])[] = [
  [
    '-d',
    'solve: take the assemblies apart (brick), find the slide path (sliding) or the rod transfers (stacking)',
  ],
  ['-p', 'print the moves of each solution'],
  ['-q', 'be quiet: print only the statistics'],
  ['-o n  |  -o all', 'solve problem n of the file (counting from 0), or all of them'],
  ['-t n', 'worker threads (0: all cores)'],
  [
    '--solver TYPE',
    'the solver; the choices depend on the puzzle type (see -h brick, -h sliding, -h stacking)',
  ],
  ['--json', 'one machine-readable result, for scripts'],
  ['--save FILE', 'solve as the app does and write the solved puzzle to FILE'],
  ['-h [TYPE]', 'all the options, or those of one puzzle type'],
];

function optionRows(): string {
  return OPTIONS.map(
    ([flag, what]) =>
      `<tr><td style="white-space:nowrap;padding:2px 14px 2px 0;vertical-align:top"><code>${esc(flag)}</code></td><td style="padding:2px 0">${esc(what)}</td></tr>`,
  ).join('');
}

function where(info: CliToolInfo): string {
  if (info.path === null) {
    return `<p>This copy of ShapeLogic has no command line tool (a development build without <code>just build</code>).</p>`;
  }
  const lines = [`<p><b>Where it is:</b><br><code>${esc(info.path)}</code></p>`];
  if (info.link === null) {
    lines.push(
      `<p class="mute">Installing it from the app is not available on this system yet: run it by its full path, as below.</p>`,
    );
  } else if (info.state === 'linked') {
    lines.push(
      `<p><b>Installed:</b> <code>${esc(info.link)}</code> points to it, so <code>${NAME}</code> works in any new terminal window.</p>`,
    );
  } else if (info.state === 'other') {
    lines.push(
      `<p><b>Not installed:</b> <code>${esc(info.link)}</code> already exists and is not a link to the tool, so it is left alone. Remove or rename it yourself to install.</p>`,
    );
  } else {
    lines.push(
      `<p><b>Install</b> makes <code>${esc(info.link)}</code> a link to it, so <code>${NAME}</code> works in any new terminal window.${info.state === 'stale' ? ' (There is a link there to another copy: Install replaces it.)' : ''} macOS asks for your password, because that folder belongs to the system.</p>`,
    );
  }
  return lines.join('');
}

function command(info: CliToolInfo): string {
  const run = info.state === 'linked' || info.path === null ? NAME : `"${info.path}"`;
  return (
    `<p><b>To run it:</b></p><pre style="margin:4px 0 0;white-space:pre-wrap;word-break:break-all"><code>${esc(run)} -h\n${esc(run)} -d -q puzzle.xmpuzzle</code></pre>` +
    (run === NAME || info.link === null
      ? ''
      : `<p class="mute">After Install, just <code>${NAME}</code>.</p>`)
  );
}

function body(info: CliToolInfo): string {
  return (
    `<p><b>${NAME}</b> solves puzzle files from a terminal or a script, with no window. It is ShapeLogic’s version of BurrTools’ <code>burrTxt</code> and <code>burrTxt2</code>: it assembles and takes apart brick puzzles and solves sliding and stacking (Panex) puzzles, printing counts, moves or JSON.</p>` +
    `<table style="border-collapse:collapse;margin:0 0 10px">${optionRows()}</table>` +
    where(info) +
    command(info) +
    `<p class="mute" data-cli-error style="color:var(--danger,#c0392b)" hidden></p>`
  );
}

function buttons(info: CliToolInfo): DialogButton[] {
  const installable = info.path !== null && info.link !== null && info.state !== 'other';
  if (!installable) {
    return [{ key: 'close', label: 'Close', kind: 'pri', default: true }];
  }
  if (info.state === 'linked') {
    return [
      { key: 'uninstall', label: `Remove ${info.link ?? 'the link'}` },
      { key: 'close', label: 'Close', kind: 'pri', default: true },
    ];
  }
  return [
    { key: 'cancel', label: 'Cancel' },
    { key: 'install', label: 'Install', kind: 'pri', default: true },
  ];
}

/** Runs one operation of the dialog; false keeps the dialog open (it failed or waits). */
async function run(
  op: 'install' | 'uninstall',
  app: BtApp,
  el: HTMLElement,
  info: CliToolInfo,
): Promise<void> {
  const err = el.querySelector<HTMLElement>('[data-cli-error]');
  if (err !== null) {
    err.hidden = true;
  }
  const buttonsNow = el.querySelectorAll<HTMLButtonElement>('button');
  buttonsNow.forEach((b) => {
    b.disabled = true;
  });
  try {
    const r = await (op === 'install' ? app.installCliTool() : app.uninstallCliTool());
    if (r.ok) {
      closeDialog(ID);
      flash(
        op === 'install'
          ? `Installed: ${info.link ?? NAME} (open a new terminal window to use ${NAME})`
          : `Removed ${info.link ?? NAME}`,
        5000,
      );
      return;
    }
    if (err !== null) {
      err.textContent = r.error;
      err.hidden = false;
    }
  } catch (e) {
    reportError(e, 'Command line tool');
  }
  buttonsNow.forEach((b) => {
    b.disabled = false;
  });
}

/** ShapeLogic ▸ Install Command Line Tool…: explains the tool, installs or removes its link. */
export async function openCliTool(app: BtApp): Promise<void> {
  let info: CliToolInfo;
  try {
    info = await app.cliToolInfo();
  } catch (e) {
    reportError(e, 'Command line tool');
    return;
  }
  void openDialog({
    id: ID,
    title: 'Command line tool',
    wide: true,
    body: body(info),
    buttons: buttons(info),
    onAnswer: (key, el) => {
      if (key === 'install' || key === 'uninstall') {
        void run(key, app, el, info);
        return false;
      }
      return true;
    },
  });
}
