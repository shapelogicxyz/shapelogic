// SPDX-License-Identifier: GPL-3.0-or-later
//
// The command line tool's install (src/main/cli-tool.ts): where the app
// finds the tool, what is at the link, Install and Remove as the user and
// through the administrator's dialog (a stand-in here), and the quoting of
// the one command that dialog runs.

import {
  chmodSync,
  existsSync,
  lstatSync,
  mkdirSync,
  mkdtempSync,
  readlinkSync,
  rmSync,
  symlinkSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import {
  appleScriptString,
  cliFile,
  cliInfo,
  cliLink,
  findCli,
  installCli,
  linkState,
  shellQuote,
  uninstallCli,
} from '../../src/main/cli-tool.ts';

let dir: string;
let tool: string;

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), 'bt-cli-'));
  tool = join(dir, 'Resources', 'shapelogic_cli');
  mkdirSync(join(dir, 'Resources'));
  writeFileSync(tool, '#!/bin/sh\n');
});

afterEach(() => {
  // a folder the tests made read-only
  try {
    chmodSync(join(dir, 'locked'), 0o755);
  } catch {
    /* none */
  }
  rmSync(dir, { recursive: true, force: true });
});

/** A stand-in for the administrator's dialog: records the command and does nothing. */
function asked(answer: 'ok' | 'cancel' | 'fail' = 'ok'): {
  commands: string[];
  run: (c: string) => Promise<void>;
} {
  const commands: string[] = [];
  return {
    commands,
    run: (c) => {
      commands.push(c);
      return answer === 'ok'
        ? Promise.resolve()
        : Promise.reject(
            new Error(answer === 'cancel' ? 'execution error: User canceled. (-128)' : 'boom'),
          );
    },
  };
}

describe('finding the tool', () => {
  it('is the copy in the resources when packaged, and nothing else', () => {
    const search = { packaged: true, resources: join(dir, 'Resources'), webDir: join(dir, 'web') };
    expect(findCli(search)).toBe(tool);
    expect(findCli({ ...search, resources: join(dir, 'nowhere') })).toBeNull();
    // a build in the repository is not a packaged app's
    mkdirSync(join(dir, 'build', 'release', 'cli'), { recursive: true });
    writeFileSync(join(dir, 'build', 'release', 'cli', 'shapelogic_cli'), '');
    expect(findCli({ ...search, resources: join(dir, 'nowhere') })).toBeNull();
  });

  it('is the repository’s dev build, else its release build, in development', () => {
    const web = join(dir, 'web');
    mkdirSync(web);
    const dev = { packaged: false, resources: '', webDir: web };
    expect(findCli(dev)).toBeNull();
    mkdirSync(join(dir, 'build', 'release', 'cli'), { recursive: true });
    writeFileSync(join(dir, 'build', 'release', 'cli', 'shapelogic_cli'), '');
    expect(findCli(dev)).toBe(join(dir, 'build', 'release', 'cli', 'shapelogic_cli'));
    mkdirSync(join(dir, 'build', 'dev', 'cli'), { recursive: true });
    writeFileSync(join(dir, 'build', 'dev', 'cli', 'shapelogic_cli'), '');
    expect(findCli(dev)).toBe(join(dir, 'build', 'dev', 'cli', 'shapelogic_cli'));
  });

  it('is named per platform, and linked only on macOS for now', () => {
    expect(cliFile('darwin')).toBe('shapelogic_cli');
    expect(cliFile('linux')).toBe('shapelogic_cli');
    expect(cliFile('win32')).toBe('shapelogic_cli.exe');
    expect(cliLink('darwin')).toBe('/usr/local/bin/shapelogic_cli');
    expect(cliLink('win32')).toBeNull();
    expect(cliLink('linux')).toBeNull();
  });
});

describe('what is at the link', () => {
  it('is none, linked, stale or other', () => {
    const link = join(dir, 'bin', 'shapelogic_cli');
    expect(linkState(tool, link)).toBe('none');
    mkdirSync(join(dir, 'bin'));
    expect(linkState(tool, link)).toBe('none');
    symlinkSync(tool, link);
    expect(linkState(tool, link)).toBe('linked');
    rmSync(link);
    // a link to another copy of the tool (the app was moved)
    symlinkSync('/Applications/Old.app/Contents/Resources/shapelogic_cli', link);
    expect(linkState(tool, link)).toBe('stale');
    rmSync(link);
    // a link to something else, and a plain file, are not ours
    symlinkSync('/bin/ls', link);
    expect(linkState(tool, link)).toBe('other');
    rmSync(link);
    writeFileSync(link, 'mine');
    expect(linkState(tool, link)).toBe('other');
    expect(linkState(tool, null)).toBe('none');
    expect(cliInfo(tool, link)).toEqual({ path: tool, link, state: 'other' });
  });
});

describe('Install', () => {
  it('makes the folder and the link as the user where it may, without the administrator', async () => {
    const link = join(dir, 'new', 'bin', 'shapelogic_cli');
    const admin = asked();
    const r = await installCli(tool, link, admin.run);
    expect(r).toEqual({ ok: true, info: { path: tool, link, state: 'linked' } });
    expect(readlinkSync(link)).toBe(tool);
    expect(admin.commands).toEqual([]);
    // again: nothing to do
    expect((await installCli(tool, link, admin.run)).ok).toBe(true);
  });

  it('replaces a link to another copy, and leaves a file or another link alone', async () => {
    mkdirSync(join(dir, 'bin'));
    const link = join(dir, 'bin', 'shapelogic_cli');
    symlinkSync('/Applications/Old.app/Contents/Resources/shapelogic_cli', link);
    expect((await installCli(tool, link, asked().run)).ok).toBe(true);
    expect(readlinkSync(link)).toBe(tool);

    rmSync(link);
    writeFileSync(link, 'a program of the user’s');
    const r = await installCli(tool, link, asked().run);
    expect(r.ok).toBe(false);
    expect(lstatSync(link).isSymbolicLink()).toBe(false);

    rmSync(link);
    symlinkSync('/bin/ls', link);
    expect((await installCli(tool, link, asked().run)).ok).toBe(false);
    expect(readlinkSync(link)).toBe('/bin/ls');
  });

  it('asks the administrator, with the paths quoted, where the folder is not the user’s', async () => {
    if (process.getuid?.() === 0) {
      return;
    }
    mkdirSync(join(dir, 'locked'));
    chmodSync(join(dir, 'locked'), 0o555);
    const link = join(dir, 'locked', "it's here", 'shapelogic_cli');
    const admin = asked();
    const r = await installCli(tool, link, admin.run);
    expect(r.ok).toBe(true);
    expect(admin.commands).toHaveLength(1);
    expect(admin.commands[0]).toBe(
      `/bin/mkdir -p ${shellQuote(join(dir, 'locked', "it's here"))} && /bin/ln -sfn ${shellQuote(tool)} ${shellQuote(link)}`,
    );
    expect(existsSync(link)).toBe(false);
  });

  it('reports a closed password dialog as cancelled, and any other failure as it is', async () => {
    if (process.getuid?.() === 0) {
      return;
    }
    mkdirSync(join(dir, 'locked'));
    chmodSync(join(dir, 'locked'), 0o555);
    const link = join(dir, 'locked', 'shapelogic_cli');
    const cancelled = await installCli(tool, link, asked('cancel').run);
    expect(cancelled).toMatchObject({ ok: false, cancelled: true });
    const failed = await installCli(tool, link, asked('fail').run);
    expect(failed).toEqual({ ok: false, error: 'boom' });
  });

  it('refuses without a tool or a link on this platform', async () => {
    expect((await installCli(null, join(dir, 'l'), asked().run)).ok).toBe(false);
    expect((await installCli(tool, null, asked().run)).ok).toBe(false);
  });
});

describe('Remove', () => {
  it('removes a link to the tool, and nothing else', async () => {
    mkdirSync(join(dir, 'bin'));
    const link = join(dir, 'bin', 'shapelogic_cli');
    symlinkSync(tool, link);
    const r = await uninstallCli(tool, link, asked().run);
    expect(r).toEqual({ ok: true, info: { path: tool, link, state: 'none' } });
    expect(existsSync(link)).toBe(false);
    expect(existsSync(tool)).toBe(true);
    // nothing there: fine
    expect((await uninstallCli(tool, link, asked().run)).ok).toBe(true);
    // a file of the user’s is not ours to remove
    writeFileSync(link, 'mine');
    expect((await uninstallCli(tool, link, asked().run)).ok).toBe(false);
    expect(existsSync(link)).toBe(true);
    expect((await uninstallCli(tool, null, asked().run)).ok).toBe(false);
  });

  it('asks the administrator where the folder is not the user’s', async () => {
    if (process.getuid?.() === 0) {
      return;
    }
    mkdirSync(join(dir, 'locked'));
    const link = join(dir, 'locked', 'shapelogic_cli');
    symlinkSync(tool, link);
    chmodSync(join(dir, 'locked'), 0o555);
    const admin = asked();
    expect((await uninstallCli(tool, link, admin.run)).ok).toBe(true);
    expect(admin.commands).toEqual([`/bin/rm ${shellQuote(link)}`]);
  });
});

describe('quoting', () => {
  it('keeps a path one shell word and one AppleScript string', () => {
    expect(shellQuote('/Applications/Shape Logic.app/x')).toBe("'/Applications/Shape Logic.app/x'");
    expect(shellQuote("a'b; rm -rf ~ #")).toBe("'a'\\''b; rm -rf ~ #'");
    expect(appleScriptString('a "b" \\c')).toBe('"a \\"b\\" \\\\c"');
  });
});
