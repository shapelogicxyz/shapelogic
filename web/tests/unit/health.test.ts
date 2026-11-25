// SPDX-License-Identifier: GPL-3.0-or-later
//
// The health report at every start (cleanup item 2.1): its lines from a
// fake file system and system: an engine missing, a path with characters
// outside ASCII, a Mark of the Web, a folder that cannot be written; and
// the engine's hash against the release's record.

import type { Stats, StatsFs } from 'node:fs';

import { describe, expect, it } from 'vitest';

import {
  engineHashLine,
  headerKind,
  healthReport,
  installKind,
  isInside,
  zoneId,
} from '../../src/main/health.ts';
import type { HealthFs, HealthInput, HealthLine } from '../../src/main/health.ts';
import { startupText } from '../../src/main/startup-line.ts';

const ENGINE =
  'C:\\Users\\u\\AppData\\Local\\Programs\\ShapeLogic\\resources\\shapelogic_engine.exe';

/** A file system of these files (path: contents) and folders; `readOnly` folders refuse writes. */
function fakeFs(
  files: Record<string, string>,
  folders: string[],
  readOnly: string[] = [],
): HealthFs & { written: string[] } {
  const enoent = (path: string): Error =>
    Object.assign(new Error(`ENOENT: ${path}`), { code: 'ENOENT' });
  const written: string[] = [];
  return {
    written,
    existsSync: (p) => p in files || folders.includes(p),
    statSync: (p) => {
      const f = files[p];
      if (f === undefined) {
        throw enoent(p);
      }
      return { size: 31_400_000, mtime: new Date('2026-11-20T10:00:00Z') } as unknown as Stats;
    },
    statfsSync: () => ({ bavail: 2_000_000, bsize: 4096 }) as unknown as StatsFs,
    readHead: (p, n) => {
      const f = files[p];
      if (f === undefined) {
        throw enoent(p);
      }
      return new TextEncoder().encode(f).subarray(0, n);
    },
    readFileSync: (p) => {
      const f = files[p];
      if (f === undefined) {
        throw enoent(p);
      }
      return f;
    },
    mkdirSync: (p) => {
      folders.push(p);
      return undefined;
    },
    writeFileSync: (p) => {
      if (readOnly.some((r) => p.startsWith(r))) {
        throw Object.assign(new Error('EACCES'), { code: 'EACCES' });
      }
      written.push(p);
    },
    rmSync: () => undefined,
  };
}

function input(over: Partial<HealthInput> = {}): HealthInput {
  return {
    platform: 'win32',
    version: '2.2.0',
    versions: { electron: '39.0.0', chrome: '142.0', node: '22.20.0' },
    os: { type: 'Windows_NT', release: '10.0.26100', version: 'Windows 11 Home', arch: 'x64' },
    arch: 'x64',
    translated: false,
    locale: 'en-US',
    machine: 'Windows_NT 10.0.26100 x64, 8 cores (cpu), 16.0 GB memory (8.0 GB free)',
    execPath: 'C:\\Users\\u\\AppData\\Local\\Programs\\ShapeLogic\\ShapeLogic.exe',
    resourcesPath: 'C:\\Users\\u\\AppData\\Local\\Programs\\ShapeLogic\\resources',
    packaged: true,
    env: {
      LOCALAPPDATA: 'C:\\Users\\u\\AppData\\Local',
      ProgramFiles: 'C:\\Program Files',
      TEMP: 'C:\\Users\\u\\AppData\\Local\\Temp',
      USERPROFILE: 'C:\\Users\\u',
    },
    engine: ENGINE,
    cli: null,
    folders: [{ name: 'user data', path: 'C:\\Users\\u\\AppData\\Roaming\\ShapeLogic' }],
    gpu: { webgl: 'enabled', webgl2: 'enabled', gpu_compositing: 'enabled' },
    fs: fakeFs({ [ENGINE]: 'MZ\x90\x00' }, ['C:\\Users\\u\\AppData\\Roaming\\ShapeLogic']),
    ...over,
  };
}

const find = (lines: HealthLine[], start: string): HealthLine | undefined =>
  lines.find((l) => l.text.startsWith(start));

describe('the health report', () => {
  it('says the versions, the system, the install, the engine, the folders and the GPU', () => {
    const lines = healthReport(input());
    expect(lines.map((l) => l.text.split(':')[0])).toEqual([
      'versions',
      'system',
      'install',
      'engine',
      'folder user data',
      'gpu',
    ]);
    expect(lines.every((l) => l.level === 'info')).toBe(true);
    expect(find(lines, 'versions')?.text).toBe(
      'versions: ShapeLogic 2.2.0, Electron 39.0.0, Chromium 142.0, Node 22.20.0',
    );
    expect(find(lines, 'system')?.text).toContain('Windows_NT 10.0.26100 (Windows 11 Home)');
    expect(find(lines, 'install')?.text).toContain('per-user (under %LOCALAPPDATA%\\Programs)');
    expect(find(lines, 'engine')?.text).toBe(
      `engine: ${ENGINE}: 31.4 MB, modified 2026-11-20T10:00:00.000Z, readable, a Windows program (MZ), no Mark of the Web`,
    );
    expect(find(lines, 'folder')?.text).toMatch(/exists, writable, 8\.2 GB free$/);
    expect(find(lines, 'gpu')?.text).toBe(
      'gpu: webgl enabled, webgl2 enabled, gpu_compositing enabled',
    );
  });

  it('warns of an engine that is missing', () => {
    const line = find(healthReport(input({ fs: fakeFs({}, []) })), 'engine');
    expect(line).toEqual({ level: 'warn', text: `engine: ${ENGINE}: missing (ENOENT)` });
    expect(find(healthReport(input({ engine: null })), 'engine')).toEqual({
      level: 'warn',
      text: 'engine: none found',
    });
  });

  it('warns of a path with characters outside ASCII, a long one, and one in a temporary folder', () => {
    const odd = 'C:\\Users\\Zoë\\AppData\\Local\\Temp\\Rar$EX0\\ShapeLogic.exe';
    const lines = healthReport(
      input({
        execPath: odd,
        env: { ...input().env, TEMP: 'C:\\Users\\Zoë\\AppData\\Local\\Temp' },
      }),
    );
    const warns = lines.filter((l) => l.level === 'warn').map((l) => l.text);
    expect(warns).toContain(`install: the app's path has characters outside ASCII: ${odd}`);
    expect(warns.some((w) => w.includes('runs from a temporary folder'))).toBe(true);
    const long = `C:\\${'x'.repeat(200)}\\ShapeLogic.exe`;
    expect(
      healthReport(input({ execPath: long })).some((l) => l.text.includes('characters long')),
    ).toBe(true);
    expect(
      healthReport(input({ execPath: 'C:\\Users\\u\\Downloads\\ShapeLogic\\ShapeLogic.exe' })).some(
        (l) => l.text === 'install: the app runs from the Downloads folder',
      ),
    ).toBe(true);
  });

  it('reads the Mark of the Web on Windows: its zone, never its URLs', () => {
    const fs = fakeFs(
      {
        [ENGINE]: 'MZ',
        [`${ENGINE}:Zone.Identifier`]:
          '[ZoneTransfer]\r\nZoneId=3\r\nReferrerUrl=https://example.com/x\r\nHostUrl=https://example.com/y\r\n',
      },
      [],
    );
    const text = find(healthReport(input({ fs })), 'engine')?.text ?? '';
    expect(text).toContain('Mark of the Web: ZoneId 3 (the internet)');
    expect(text).not.toContain('example.com');
    // not looked for elsewhere
    expect(
      find(healthReport(input({ fs, platform: 'darwin', engine: ENGINE })), 'engine')?.text,
    ).not.toContain('Mark of the Web');
  });

  it('warns of a folder that cannot be written, and makes one that is not there', () => {
    const fs = fakeFs({ [ENGINE]: 'MZ' }, ['C:\\data'], ['C:\\data']);
    const lines = healthReport(
      input({
        fs,
        folders: [
          { name: 'user data', path: 'C:\\data' },
          { name: 'sliding', path: 'C:\\cache\\sliding' },
          { name: 'Panex', path: '' },
        ],
      }),
    );
    expect(find(lines, 'folder user data')).toEqual({
      level: 'warn',
      text: 'folder user data: C:\\data: exists, not writable (EACCES), 8.2 GB free',
    });
    expect(find(lines, 'folder sliding')?.text).toBe(
      'folder sliding: C:\\cache\\sliding: made, writable, 8.2 GB free',
    );
    expect(find(lines, 'folder Panex')?.text).toBe('folder Panex: none');
  });

  it('warns of an engine file that is not a program', () => {
    const fs = fakeFs({ [ENGINE]: '<html>' }, []);
    expect(find(healthReport(input({ fs })), 'engine')?.level).toBe('warn');
  });
});

describe('its pieces', () => {
  it('knows the program headers', () => {
    expect(headerKind(new Uint8Array([0x4d, 0x5a, 0, 0]))).toBe('a Windows program (MZ)');
    expect(headerKind(new Uint8Array([0x7f, 0x45, 0x4c, 0x46]))).toBe('an ELF program');
    expect(headerKind(new Uint8Array([0xcf, 0xfa, 0xed, 0xfe]))).toBe('a Mach-O program');
    expect(headerKind(new Uint8Array([0xca, 0xfe, 0xba, 0xbe]))).toBe('a Mach-O program');
    expect(headerKind(new Uint8Array())).toBe('an empty file');
  });

  it('reads a ZoneId', () => {
    expect(zoneId('[ZoneTransfer]\nZoneId=3\n')).toBe(3);
    expect(zoneId('[ZoneTransfer]\n')).toBeNull();
  });

  it('tells where the app is installed', () => {
    expect(installKind(input({ execPath: 'C:\\Program Files\\ShapeLogic\\ShapeLogic.exe' }))).toBe(
      'packaged, per-machine (Program Files)',
    );
    expect(installKind(input({ packaged: false }))).toBe('development (not packaged)');
    expect(isInside('C:\\PROGRAM FILES\\x', 'C:\\Program Files', 'win32')).toBe(true);
    expect(isInside('/Applications2/x', '/Applications', 'darwin')).toBe(false);
  });
});

describe("the engine's hash against the release's", () => {
  const hex = 'ab'.repeat(32);
  const record = `${hex}  shapelogic_engine.exe\n${'cd'.repeat(32)}  shapelogic_cli.exe\n`;

  it('matches, or says the file was changed', () => {
    expect(engineHashLine('shapelogic_engine.exe', hex, record)).toEqual({
      level: 'info',
      text: `engine file matches the release (SHA-256 ${hex})`,
    });
    const other = engineHashLine('shapelogic_engine.exe', 'ef'.repeat(32), record);
    expect(other?.level).toBe('warn');
    expect(other?.text).toContain(
      'does not match the release: damaged or changed by other software',
    );
  });

  it('is nothing without a record, and a warning when the record lacks the engine', () => {
    expect(engineHashLine('shapelogic_engine.exe', hex, null)).toBeNull();
    expect(engineHashLine('shapelogic_engine', hex, record)?.level).toBe('warn');
  });
});

describe('the Startup line (cleanup item 2.7)', () => {
  it('counts each moment from the start, and says what was not reached', () => {
    expect(
      startupText(1000, { shown: 1420, loaded: 1610, spawned: 1500, hello: 1580, document: 1900 }),
    ).toBe(
      'window shown 420 ms, page loaded 610 ms, engine spawned 500 ms, engine hello 580 ms, first document shown 900 ms',
    );
    expect(
      startupText(1000, { shown: 1420, loaded: 1610, spawned: null, hello: null, document: null }),
    ).toBe(
      'window shown 420 ms, page loaded 610 ms, engine spawned not yet, engine hello not yet, first document shown not yet',
    );
  });
});
