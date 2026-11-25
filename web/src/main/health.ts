// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's health report (cleanup item 2.1): one "App Health" block in
// the log at every start, right after "App Started", so a report from a
// machine where the app cannot work tells why: the versions, the system,
// where the app is installed (and the paths that trouble Windows), the
// engine's file (there, readable, its header, the Mark of the Web), the
// folders the app writes (there, writable, their free space) and the GPU.
// Only cheap checks run here; the engine's SHA-256 is checked after the
// first window shows (engineHashLine). No system tool is run.
//
// Free of Electron: the main process passes in what Electron knows and
// the file system (the unit tests a fake one).

import type { Stats, StatsFs } from 'node:fs';
import { posix, win32 } from 'node:path';

import { ENGINE_HASH_FILE, parseHashFile } from './engine-hash.ts';

/** The file system calls the report makes. */
export interface HealthFs {
  existsSync(path: string): boolean;
  statSync(path: string): Stats;
  statfsSync(path: string): StatsFs;
  /** The first bytes of a file (throws when it cannot be opened or read). */
  readHead(path: string, bytes: number): Uint8Array;
  readFileSync(path: string, encoding: 'utf8'): string;
  mkdirSync(path: string, options: { recursive: true }): unknown;
  writeFileSync(path: string, data: string): void;
  rmSync(path: string, options: { force: true }): void;
}

/** What the report is about: Electron's and the system's answers, passed in. */
export interface HealthInput {
  platform: NodeJS.Platform;
  version: string;
  versions: { electron?: string; chrome?: string; node?: string };
  os: { type: string; release: string; version: string; arch: string };
  /** process.arch: the app's own architecture. */
  arch: string;
  /** app.runningUnderARM64Translation (Rosetta or Windows' x64 emulation on Arm). */
  translated: boolean;
  locale: string;
  /** machineText's cores and memory (app-log.ts). */
  machine: string;
  execPath: string;
  resourcesPath: string;
  packaged: boolean;
  /** The environment's LOCALAPPDATA, ProgramFiles, ProgramFiles(x86), TEMP, TMP, USERPROFILE, HOME. */
  env: Record<string, string | undefined>;
  /** The engine's path, or null when the app found none. */
  engine: string | null;
  /** The command line tool's, or null. */
  cli: string | null;
  /** The folders the app writes, by name. */
  folders: readonly { name: string; path: string }[];
  /** app.getGPUFeatureStatus(). */
  gpu: Record<string, string>;
  fs: HealthFs;
}

/** One line of the report. */
export interface HealthLine {
  level: 'info' | 'warn';
  text: string;
}

/** The longest install path that stays clear of Windows' 260-character limit with the app's own files below it. */
export const LONG_PATH = 200;

const gb = (n: number): string => `${(n / 1e9).toFixed(1)} GB`;

/** The path functions of the platform the report is about. */
function pathOf(platform: NodeJS.Platform): typeof posix {
  return platform === 'win32' ? win32 : posix;
}

/** Whether `path` is `folder` or inside it, as the platform compares paths. */
export function isInside(path: string, folder: string, platform: NodeJS.Platform): boolean {
  if (folder === '') {
    return false;
  }
  const p = pathOf(platform);
  const fold = (s: string): string => (platform === 'win32' ? s.toLowerCase() : s);
  const rel = p.relative(fold(folder), fold(path));
  return rel === '' || (!rel.startsWith('..') && !p.isAbsolute(rel));
}

/** How the app is installed: per-user, per-machine, or a folder of its own. */
export function installKind(input: HealthInput): string {
  if (!input.packaged) {
    return 'development (not packaged)';
  }
  if (input.platform === 'win32') {
    const local = input.env.LOCALAPPDATA;
    if (local !== undefined && isInside(input.execPath, win32.join(local, 'Programs'), 'win32')) {
      return 'packaged, per-user (under %LOCALAPPDATA%\\Programs)';
    }
    for (const key of ['ProgramFiles', 'ProgramFiles(x86)']) {
      const pf = input.env[key];
      if (pf !== undefined && isInside(input.execPath, pf, 'win32')) {
        return 'packaged, per-machine (Program Files)';
      }
    }
    return 'packaged, in a folder of its own';
  }
  if (input.platform === 'darwin') {
    return isInside(input.execPath, '/Applications', 'darwin')
      ? 'packaged, in /Applications'
      : 'packaged, outside /Applications';
  }
  return input.env.APPIMAGE !== undefined ? 'packaged, an AppImage' : 'packaged';
}

/** The warnings about where the app runs from. */
export function installWarnings(input: HealthInput): string[] {
  const out: string[] = [];
  const at = input.execPath;
  // eslint-disable-next-line no-control-regex -- the test is for bytes outside ASCII
  if (/[^\x00-\x7f]/.test(at)) {
    out.push(`the app's path has characters outside ASCII: ${at}`);
  }
  if (at.length > LONG_PATH) {
    out.push(
      `the app's path is ${String(at.length)} characters long (over ${String(LONG_PATH)}): ${at}`,
    );
  }
  const temps = [input.env.TEMP, input.env.TMP].filter(
    (t): t is string => t !== undefined && t !== '',
  );
  if (temps.some((t) => isInside(at, t, input.platform))) {
    out.push(
      'the app runs from a temporary folder: a zip opened without extracting it runs from there; ' +
        'install it, or extract the zip first',
    );
  }
  const home = input.env.USERPROFILE ?? input.env.HOME;
  if (
    home !== undefined &&
    isInside(at, pathOf(input.platform).join(home, 'Downloads'), input.platform)
  ) {
    out.push('the app runs from the Downloads folder');
  }
  if (input.platform === 'darwin' && at.includes('/AppTranslocation/')) {
    out.push('macOS runs the app from a translocated copy: move it to /Applications');
  }
  return out;
}

/** What a file's first bytes say it is. */
export function headerKind(head: Uint8Array): string {
  const b = (i: number): number => head[i] ?? -1;
  if (b(0) === 0x4d && b(1) === 0x5a) {
    return 'a Windows program (MZ)';
  }
  if (b(0) === 0x7f && b(1) === 0x45 && b(2) === 0x4c && b(3) === 0x46) {
    return 'an ELF program';
  }
  const magic = ((b(0) << 24) | (b(1) << 16) | (b(2) << 8) | b(3)) >>> 0;
  if ([0xcffaedfe, 0xcefaedfe, 0xfeedfacf, 0xfeedface, 0xcafebabe, 0xbebafeca].includes(magic)) {
    return 'a Mach-O program';
  }
  return head.length === 0 ? 'an empty file' : 'not a program (an unknown header)';
}

/** The ZoneId of a Windows Mark of the Web (`[ZoneTransfer]\nZoneId=3`), or null; its URLs are never read out. */
export function zoneId(text: string): number | null {
  const m = /^\s*ZoneId\s*=\s*(\d+)/im.exec(text);
  return m?.[1] !== undefined ? Number(m[1]) : null;
}

const ZONES = [
  'the computer',
  'the local network',
  'trusted sites',
  'the internet',
  'restricted sites',
];

/** One program file: there, its size and time, readable, its header, its Mark of the Web. */
export function fileLine(name: string, path: string | null, input: HealthInput): HealthLine {
  if (path === null) {
    return { level: 'warn', text: `${name}: none found` };
  }
  const { fs } = input;
  let stat: Stats;
  try {
    stat = fs.statSync(path);
  } catch (e) {
    return { level: 'warn', text: `${name}: ${path}: missing (${codeOf(e)})` };
  }
  const parts = [`${(stat.size / 1e6).toFixed(1)} MB`, `modified ${stat.mtime.toISOString()}`];
  let level: HealthLine['level'] = 'info';
  try {
    const kind = headerKind(fs.readHead(path, 4));
    parts.push(`readable, ${kind}`);
    if (kind.startsWith('not') || kind.startsWith('an empty')) {
      level = 'warn';
    }
  } catch (e) {
    parts.push(`cannot be read (${codeOf(e)})`);
    level = 'warn';
  }
  if (input.platform === 'win32') {
    // the Mark of the Web: the file came from the internet (an alternate data stream)
    try {
      const zone = zoneId(fs.readFileSync(`${path}:Zone.Identifier`, 'utf8'));
      parts.push(
        zone === null
          ? 'Mark of the Web without a zone'
          : `Mark of the Web: ZoneId ${String(zone)} (${ZONES[zone] ?? 'unknown zone'})`,
      );
    } catch {
      parts.push('no Mark of the Web');
    }
  }
  return { level, text: `${name}: ${path}: ${parts.join(', ')}` };
}

/** One folder: there or made, writable (a probe file made and removed), its free space. */
export function folderLine(name: string, path: string, input: HealthInput): HealthLine {
  if (path === '') {
    return { level: 'info', text: `folder ${name}: none` };
  }
  const { fs } = input;
  const parts: string[] = [];
  let level: HealthLine['level'] = 'info';
  try {
    if (fs.existsSync(path)) {
      parts.push('exists');
    } else {
      fs.mkdirSync(path, { recursive: true });
      parts.push('made');
    }
  } catch (e) {
    return { level: 'warn', text: `folder ${name}: ${path}: cannot be made (${codeOf(e)})` };
  }
  const probe = pathOf(input.platform).join(path, `.shapelogic-health-${String(process.pid)}`);
  try {
    fs.writeFileSync(probe, 'x');
    fs.rmSync(probe, { force: true });
    parts.push('writable');
  } catch (e) {
    parts.push(`not writable (${codeOf(e)})`);
    level = 'warn';
  }
  try {
    const s = fs.statfsSync(path);
    parts.push(`${gb(s.bavail * s.bsize)} free`);
  } catch {
    parts.push('free space unknown');
  }
  return { level, text: `folder ${name}: ${path}: ${parts.join(', ')}` };
}

function codeOf(e: unknown): string {
  if (typeof e === 'object' && e !== null && typeof (e as { code?: unknown }).code === 'string') {
    return (e as { code: string }).code;
  }
  return e instanceof Error ? e.message : String(e);
}

/** The health report's lines, in order. */
export function healthReport(input: HealthInput): HealthLine[] {
  const v = input.versions;
  const lines: HealthLine[] = [
    {
      level: 'info',
      text:
        `versions: ShapeLogic ${input.version}, Electron ${v.electron ?? '?'}, ` +
        `Chromium ${v.chrome ?? '?'}, Node ${v.node ?? '?'}`,
    },
    {
      level: 'info',
      text:
        `system: ${input.os.type} ${input.os.release} (${input.os.version}), ` +
        `the app ${input.arch} on ${input.os.arch}${input.translated ? ', translated (emulated)' : ''}, ` +
        `locale ${input.locale}; ${input.machine}`,
    },
    {
      level: 'info',
      text: `install: ${input.execPath}, resources ${input.resourcesPath}; ${installKind(input)}`,
    },
    ...installWarnings(input).map((text): HealthLine => ({
      level: 'warn',
      text: `install: ${text}`,
    })),
    fileLine('engine', input.engine, input),
    ...(input.cli !== null ? [fileLine('command line tool', input.cli, input)] : []),
    ...input.folders.map((f) => folderLine(f.name, f.path, input)),
    {
      level: 'info',
      text: `gpu: ${['webgl', 'webgl2', 'gpu_compositing']
        .map((k) => `${k} ${input.gpu[k] ?? 'unknown'}`)
        .join(', ')}`,
    },
  ];
  return lines;
}

/**
 * The log line of the engine's SHA-256 against the one recorded at
 * packaging (engine.sha256 in the resources): null when there is no
 * record (a development build).
 */
export function engineHashLine(
  engineName: string,
  actual: string,
  recorded: string | null,
): HealthLine | null {
  if (recorded === null) {
    return null;
  }
  const want = parseHashFile(recorded).get(engineName);
  if (want === undefined) {
    return { level: 'warn', text: `engine: ${ENGINE_HASH_FILE} has no line for ${engineName}` };
  }
  return want === actual
    ? { level: 'info', text: `engine file matches the release (SHA-256 ${actual})` }
    : {
        level: 'warn',
        text:
          `engine file does not match the release: damaged or changed by other software ` +
          `(SHA-256 ${actual}, the release's ${want})`,
      };
}
