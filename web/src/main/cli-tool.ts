// SPDX-License-Identifier: GPL-3.0-or-later
//
// The command line tool (`shapelogic_cli`, the original's burrTxt and
// burrTxt2) as the app ships it: a program in the app's resources beside
// the engine, which the "Command Line Tool…" dialog can link into the
// shell's path (macOS: /usr/local/bin/shapelogic_cli, a symbolic link
// that follows the app if it is updated in place). Only this module knows
// the paths; the renderer names an operation and gets the state back
// (app-bridge.ts: CliToolInfo), so a page cannot make the app link or
// remove anything else. Where the link's folder needs the administrator
// (it usually does on macOS), the system's own password dialog is asked
// through osascript. Free of Electron.

import { execFile } from 'node:child_process';
import { existsSync, lstatSync, mkdirSync, readlinkSync, symlinkSync, unlinkSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

import type { CliToolInfo, CliToolResult } from '../engine/app-bridge.ts';
import type { EngineSearch } from './engine-path.ts';

/** The tool's name in the shell. */
export const CLI_NAME = 'shapelogic_cli';

/** The tool's file name on a platform. */
export function cliFile(platform: NodeJS.Platform): string {
  return platform === 'win32' ? `${CLI_NAME}.exe` : CLI_NAME;
}

/**
 * The tool to run or link; null when none is built (or the packaged app
 * lacks its own). A packaged app has the copy in its resources and nothing
 * else; development takes the repository's dev build, else its release.
 */
export function findCli(s: Omit<EngineSearch, 'engine'>): string | null {
  const exe = cliFile(s.platform ?? process.platform);
  if (s.packaged) {
    const path = join(s.resources, exe);
    return existsSync(path) ? path : null;
  }
  for (const preset of ['dev', 'release']) {
    const path = resolve(s.webDir, '..', 'build', preset, 'cli', exe);
    if (existsSync(path)) {
      return path;
    }
  }
  return null;
}

/** Where Install links the tool; null where this platform has no Install yet. */
export function cliLink(platform: NodeJS.Platform): string | null {
  return platform === 'darwin' ? `/usr/local/bin/${CLI_NAME}` : null;
}

/** What is at the link, as CliToolInfo.state says. */
export function linkState(path: string | null, link: string | null): CliToolInfo['state'] {
  if (link === null) {
    return 'none';
  }
  let target: string;
  try {
    if (!lstatSync(link).isSymbolicLink()) {
      return 'other';
    }
    target = readlinkSync(link);
  } catch (e) {
    return (e as NodeJS.ErrnoException).code === 'ENOENT' ? 'none' : 'other';
  }
  if (path !== null && target === path) {
    return 'linked';
  }
  // a link to a copy of the tool somewhere else is ours to replace or remove
  return target.endsWith(`/${CLI_NAME}`) ? 'stale' : 'other';
}

/** The tool's state for the dialog. */
export function cliInfo(path: string | null, link: string | null): CliToolInfo {
  return { path, link, state: linkState(path, link) };
}

/** A string as one shell word. */
export function shellQuote(s: string): string {
  return `'${s.replaceAll("'", "'\\''")}'`;
}

/** A string as an AppleScript string literal. */
export function appleScriptString(s: string): string {
  return `"${s.replaceAll('\\', '\\\\').replaceAll('"', '\\"')}"`;
}

/** Runs a shell command as the administrator, asking for the password in the system's dialog. */
export type Privileged = (command: string) => Promise<void>;

/** The macOS way: osascript's `with administrator privileges`. */
export const macPrivileged: Privileged = (command) =>
  new Promise((done, fail) => {
    const script = `do shell script ${appleScriptString(command)} with administrator privileges`;
    execFile('/usr/bin/osascript', ['-e', script], (err, _out, stderr) => {
      if (err === null) {
        done();
      } else {
        fail(new Error(stderr.trim() || err.message));
      }
    });
  });

/** The user closed the system's password dialog (osascript error -128). */
function wasCancelled(e: unknown): boolean {
  return e instanceof Error && /-128|User canceled/i.test(e.message);
}

function permissionDenied(e: unknown): boolean {
  const code = (e as NodeJS.ErrnoException).code;
  return code === 'EACCES' || code === 'EPERM';
}

function done(path: string | null, link: string | null): CliToolResult {
  return { ok: true, info: cliInfo(path, link) };
}

function failed(e: unknown): CliToolResult {
  return wasCancelled(e)
    ? {
        ok: false,
        error: 'Cancelled: the administrator’s password was not given.',
        cancelled: true,
      }
    : { ok: false, error: e instanceof Error ? e.message : String(e) };
}

/**
 * Links the tool at `link`. Refuses when there is no tool, no link on this
 * platform, or something that is not a link to a copy of the tool at the
 * link. The folder is made and the link written as the user where that
 * works, else by `privileged`.
 */
export async function installCli(
  path: string | null,
  link: string | null,
  privileged: Privileged,
): Promise<CliToolResult> {
  if (path === null || link === null) {
    return { ok: false, error: 'This build has no command line tool to install.' };
  }
  const state = linkState(path, link);
  if (state === 'other') {
    return { ok: false, error: `${link} exists and is not a link to the tool: it was left alone.` };
  }
  if (state === 'linked') {
    return done(path, link);
  }
  try {
    mkdirSync(dirname(link), { recursive: true });
    if (state === 'stale') {
      unlinkSync(link);
    }
    symlinkSync(path, link);
  } catch (e) {
    if (!permissionDenied(e)) {
      return failed(e);
    }
    try {
      await privileged(
        `/bin/mkdir -p ${shellQuote(dirname(link))} && /bin/ln -sfn ${shellQuote(path)} ${shellQuote(link)}`,
      );
    } catch (e2) {
      return failed(e2);
    }
  }
  return done(path, link);
}

/** Removes the link Install made (a link to a copy of the tool); anything else is left alone. */
export async function uninstallCli(
  path: string | null,
  link: string | null,
  privileged: Privileged,
): Promise<CliToolResult> {
  if (link === null) {
    return { ok: false, error: 'This platform has no installed link to remove.' };
  }
  const state = linkState(path, link);
  if (state === 'none') {
    return done(path, link);
  }
  if (state === 'other') {
    return { ok: false, error: `${link} is not a link to the tool: it was left alone.` };
  }
  try {
    unlinkSync(link);
  } catch (e) {
    if (!permissionDenied(e)) {
      return failed(e);
    }
    try {
      await privileged(`/bin/rm ${shellQuote(link)}`);
    } catch (e2) {
      return failed(e2);
    }
  }
  return done(path, link);
}
