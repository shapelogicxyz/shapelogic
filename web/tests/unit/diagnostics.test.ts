// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help ▸ Copy Diagnostics (cleanup item 2.4): the event filter (fake
// wevtutil output with and without ShapeLogic's files), the home folder
// written as ~, the checks' answers (an access denied, the system's own
// tools refused), the report's sections; and that no system tool runs
// before the user asks: execFile is spied on while the start's reports are
// made, and only the Copy Diagnostics path in main runs the checks.

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { describe, expect, it, vi } from 'vitest';

const spawned: string[] = [];
vi.mock('node:child_process', async (original) => {
  const real = await original<typeof import('node:child_process')>();
  return {
    ...real,
    execFile: (...args: unknown[]) => {
      spawned.push(String(args[0]));
      return (real.execFile as (...a: unknown[]) => unknown)(...args);
    },
    spawn: (...args: unknown[]) => {
      spawned.push(String(args[0]));
      return (real.spawn as (...a: unknown[]) => unknown)(...args);
    },
  };
});

const { diagnosticsText, filterEvents, macChecks, runChecks, shortenHome, windowsChecks } =
  await import('../../src/main/diagnostics.ts');
const { healthReport } = await import('../../src/main/health.ts');
const { failureText } = await import('../../src/engine/start-diagnosis.ts');
type RunTool = import('../../src/main/diagnostics.ts').RunTool;

const EVENTS = `Event[0]
  Log Name: Microsoft-Windows-Windows Defender/Operational
  Event ID: 1117
  Description:
Microsoft Defender Antivirus has taken action to protect this machine from malware.
 Path: file:_C:\\Users\\ann\\AppData\\Local\\Programs\\ShapeLogic\\resources\\shapelogic_engine.exe

Event[1]
  Log Name: Microsoft-Windows-Windows Defender/Operational
  Event ID: 1116
  Description:
 Path: file:_C:\\Users\\ann\\Downloads\\other.exe

Event[2]
  Event ID: 1121
  Description: blocked C:\\Users\\ann\\AppData\\Local\\Programs\\ShapeLogic\\ShapeLogic.exe
`;

describe('the event filter', () => {
  it("keeps the events that name ShapeLogic's files, in any case", () => {
    const kept = filterEvents(EVENTS, ['shapelogic']);
    expect(kept).toHaveLength(2);
    expect(kept[0]).toContain('Event ID: 1117');
    expect(kept[1]).toContain('Event ID: 1121');
    expect(filterEvents(EVENTS.toUpperCase(), ['shapelogic'])).toHaveLength(2);
  });

  it('keeps the events that name the install folder', () => {
    expect(filterEvents(EVENTS, ['C:\\Users\\ann\\Downloads'])).toHaveLength(1);
  });

  it('keeps none without them', () => {
    expect(
      filterEvents(EVENTS.replace(/ShapeLogic|shapelogic_engine/gi, 'x'), ['shapelogic']),
    ).toEqual([]);
    expect(filterEvents('', ['shapelogic'])).toEqual([]);
  });
});

describe('the home folder', () => {
  it('is written as ~, with either slash and any case on Windows', () => {
    expect(shortenHome('C:\\Users\\Ann\\x and c:/users/ann/y', 'C:\\Users\\Ann', 'win32')).toBe(
      '~\\x and ~/y',
    );
  });

  it('only as a whole folder name', () => {
    expect(shortenHome('/home/ann/a /home/anna/b', '/home/ann', 'linux')).toBe('~/a /home/anna/b');
    expect(shortenHome('/Users/Ann/a', '/Users/ann', 'darwin')).toBe('/Users/Ann/a');
  });
});

describe('the checks', () => {
  it('run each system tool with a 5-second limit, never with a bypass or an encoded command', () => {
    for (const c of [...windowsChecks(), ...macChecks('/Applications/ShapeLogic.app')]) {
      expect(c.args.join(' ')).not.toMatch(/ExecutionPolicy|EncodedCommand|-enc\b/i);
    }
    const ps = windowsChecks().filter((c) => c.file === 'powershell');
    expect(ps.length).toBe(2);
    for (const c of ps) {
      expect(c.args.slice(0, 3)).toEqual(['-NoProfile', '-NonInteractive', '-Command']);
    }
  });

  it('keep what names ShapeLogic, record a denied log, and say when the tools themselves are refused', async () => {
    const limits: number[] = [];
    const run: RunTool = (file, args, timeoutMs) => {
      limits.push(timeoutMs);
      if (file === 'wevtutil' && args[1]?.includes('Defender') === true) {
        return Promise.resolve({ ok: true, stdout: EVENTS });
      }
      if (file === 'wevtutil' && args[1]?.includes('AppLocker') === true) {
        return Promise.resolve({ ok: false, code: 'exit 5', message: 'Access is denied.' });
      }
      if (file === 'powershell') {
        return Promise.resolve({ ok: false, code: 'UNKNOWN', message: 'spawn UNKNOWN' });
      }
      return Promise.resolve({
        ok: true,
        stdout: 'VerifiedAndReputablePolicyState    REG_DWORD    0x1\n',
      });
    };
    const out = await runChecks(windowsChecks(), run, ['shapelogic']);
    expect(limits.every((l) => l === 5000)).toBe(true);
    expect(out[0]).toMatch(/^Microsoft Defender.*: 2 about ShapeLogic\n/);
    expect(out[1]).toMatch(/none about ShapeLogic$/);
    expect(out[2]).toBe(
      'AppLocker blocks (8004): access is denied (an administrator can read this)',
    );
    expect(out.some((l) => l.includes('REG_DWORD    0x1'))).toBe(true);
    expect(out.at(-1)).toMatch(/2 of the system's own tools could not be started.*restricted/);
  });
});

describe('the report', () => {
  it('has its sections, with the home folder as ~', () => {
    const text = diagnosticsText({
      version: '2.2.0',
      platform: 'win32',
      time: new Date('2026-11-24T12:00:00Z'),
      home: 'C:\\Users\\ann',
      health: [
        'engine: C:\\Users\\ann\\AppData\\Local\\Programs\\ShapeLogic\\resources\\shapelogic_engine.exe: 31.4 MB',
      ],
      engines: [
        `window 1: ${failureText('spawn UNKNOWN', { which: 'version', code: 'UNKNOWN', message: 'spawn UNKNOWN', errno: -4094, afterMs: 3 }, 'win32')}`,
      ],
      update: 'HTTP 200; the latest release is v2.2',
      crashDumps: [new Date('2026-11-23T08:00:00Z')],
      system: ['Controlled Folder Access (0 off, 1 on, 2 audit): 1'],
      log: '2026-11-24 12:00:00.000 INFO [app] App Started: ShapeLogic 2.2.0\n',
    });
    for (const head of [
      '== Health',
      '== Engine',
      '== Update check',
      '== Crash dumps',
      '== The system',
      "== The log's newest 200 lines",
    ]) {
      expect(text).toContain(head);
    }
    expect(text).toContain('engine: ~\\AppData\\Local\\Programs');
    expect(text).not.toContain('C:\\Users\\ann');
    expect(text).toContain('Likely cause: Windows refused to start the engine');
    expect(text).toContain('1, the newest 2026-11-23T08:00:00.000Z');
  });
});

describe('no system tool runs unasked', () => {
  it("the start's reports run none", () => {
    spawned.length = 0;
    healthReport({
      platform: 'win32',
      version: '2.2.0',
      versions: {},
      os: { type: 'Windows_NT', release: '10.0', version: 'Windows 11', arch: 'x64' },
      arch: 'x64',
      translated: false,
      locale: 'en-US',
      machine: '',
      execPath: 'C:\\x\\ShapeLogic.exe',
      resourcesPath: 'C:\\x\\resources',
      packaged: true,
      env: {},
      engine: null,
      cli: null,
      folders: [],
      gpu: {},
      fs: {
        existsSync: () => false,
        statSync: () => {
          throw new Error('no');
        },
        statfsSync: () => {
          throw new Error('no');
        },
        readHead: () => new Uint8Array(),
        readFileSync: () => '',
        mkdirSync: () => undefined,
        writeFileSync: () => undefined,
        rmSync: () => undefined,
      },
    });
    windowsChecks();
    macChecks('/Applications/ShapeLogic.app');
    expect(spawned).toEqual([]);
  });

  it('main runs the checks only for Copy Diagnostics', () => {
    const main = readFileSync(resolve(import.meta.dirname, '../../src/main/index.ts'), 'utf8');
    const calls = [...main.matchAll(/runChecks\(/g)].map((m) => m.index);
    expect(calls.length).toBeGreaterThan(0);
    const report = main.indexOf('async function diagnosticsReport(');
    const end = main.indexOf('\n}\n', report);
    // every call is inside diagnosticsReport, which only Copy and Save Diagnostics call
    expect(calls.every((i) => i > report && i < end)).toBe(true);
    const callers = [...main.matchAll(/await diagnosticsReport\(\)/g)].length;
    expect(callers).toBe(2);
  });
});
