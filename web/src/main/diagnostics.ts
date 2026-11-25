// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help ▸ Copy Diagnostics (cleanup item 2.4): a text report for a user to
// send when the app cannot work: the health report (health.ts), each
// window's last engine failure with its likely cause (start-diagnosis.ts),
// the last update check, the crash dumps Electron kept, the newest lines
// of the app's log and, on request only, the system's own view: on
// Windows what Microsoft Defender, code integrity (Smart App Control),
// AppLocker and Windows Error Reporting recorded about ShapeLogic's files,
// Smart App Control's state, the antivirus products and Controlled Folder
// Access; on macOS the quarantine attribute and Gatekeeper's assessment.
//
// No system tool runs before the user asks (an unsigned program that
// starts PowerShell is what security software watches for); each runs
// with a 5-second limit, never with an execution-policy bypass or an
// encoded command. The report names only ShapeLogic's own files, the
// system's version and the security products' names and states; the home
// folder is shortened to `~`. Free of Electron: the main process passes in
// what it knows and how to run a program (the tests a fake one).

/** How a program is run: its output, or its failure (Node's error code, `TIMEOUT` past the limit). */
export type RunTool = (
  file: string,
  args: readonly string[],
  timeoutMs: number,
) => Promise<{ ok: true; stdout: string } | { ok: false; code: string; message: string }>;

/** The limit of each system tool. */
export const TOOL_TIMEOUT_MS = 5_000;

/** The newest log lines the report holds. */
export const DIAGNOSTICS_LOG_LINES = 200;

/** One check of the system: a program and what to keep of its answer. */
export interface SystemCheck {
  title: string;
  file: string;
  args: string[];
  /** Keep only the events that name ShapeLogic's files (wevtutil's text output). */
  events?: boolean;
}

const WEVTUTIL = 'wevtutil';
const query = (ids: number[]): string =>
  `/q:*[System[(${ids.map((id) => `EventID=${String(id)}`).join(' or ')})]]`;

/** The Windows checks, in the report's order. */
export function windowsChecks(): SystemCheck[] {
  const powershell = (command: string): string[] => [
    '-NoProfile',
    '-NonInteractive',
    '-Command',
    command,
  ];
  return [
    {
      title: 'Microsoft Defender: detections, actions, blocks (1116, 1117, 1118, 1121, 1123)',
      file: WEVTUTIL,
      args: [
        'qe',
        'Microsoft-Windows-Windows Defender/Operational',
        query([1116, 1117, 1118, 1121, 1123]),
        '/c:50',
        '/rd:true',
        '/f:text',
      ],
      events: true,
    },
    {
      title: 'Code integrity and Smart App Control blocks (3033, 3076, 3077)',
      file: WEVTUTIL,
      args: [
        'qe',
        'Microsoft-Windows-CodeIntegrity/Operational',
        query([3077, 3076, 3033]),
        '/c:50',
        '/rd:true',
        '/f:text',
      ],
      events: true,
    },
    {
      title: 'AppLocker blocks (8004)',
      file: WEVTUTIL,
      args: [
        'qe',
        'Microsoft-Windows-AppLocker/EXE and DLL',
        query([8004]),
        '/c:20',
        '/rd:true',
        '/f:text',
      ],
      events: true,
    },
    {
      title: 'Crashes Windows recorded (Application Error 1000, Windows Error Reporting 1001)',
      file: WEVTUTIL,
      args: ['qe', 'Application', query([1000, 1001]), '/c:100', '/rd:true', '/f:text'],
      events: true,
    },
    {
      title: "Smart App Control's state (0 off, 1 on, 2 evaluation)",
      file: 'reg',
      args: [
        'query',
        'HKLM\\SYSTEM\\CurrentControlSet\\Control\\CI\\Policy',
        '/v',
        'VerifiedAndReputablePolicyState',
      ],
    },
    {
      title: 'Antivirus products and their states',
      file: 'powershell',
      args: powershell(
        "Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntivirusProduct | ForEach-Object { $_.displayName + ' ' + $_.productState }",
      ),
    },
    {
      title: 'Controlled Folder Access (0 off, 1 on, 2 audit)',
      file: 'powershell',
      args: powershell('(Get-MpPreference).EnableControlledFolderAccess'),
    },
  ];
}

/** The macOS checks of the app's bundle. */
export function macChecks(bundle: string): SystemCheck[] {
  return [
    {
      title: 'The quarantine attribute',
      file: 'xattr',
      args: ['-p', 'com.apple.quarantine', bundle],
    },
    {
      title: "Gatekeeper's assessment",
      file: 'spctl',
      args: ['--assess', '--type', 'execute', '-vv', bundle],
    },
  ];
}

/**
 * The events of wevtutil's text output (each begins `Event[n]`) that name
 * one of `needles` (any case): ShapeLogic's files and its install folder.
 */
export function filterEvents(text: string, needles: readonly string[]): string[] {
  const wanted = needles.filter((n) => n !== '').map((n) => n.toLowerCase());
  const events = text
    .split(/\r?\n(?=Event\[\d+\])/i)
    .map((e) => e.trim())
    .filter((e) => /^Event\[\d+\]/i.test(e));
  return events.filter((e) => {
    const low = e.toLowerCase();
    return wanted.some((n) => low.includes(n));
  });
}

/** A text with the home folder written as `~` (both slashes, any case on Windows). */
export function shortenHome(text: string, home: string, platform: NodeJS.Platform): string {
  if (home === '' || home === '/' || home.length < 3) {
    return text;
  }
  const variants = new Set([home, home.replace(/\\/g, '/'), home.replace(/\//g, '\\')]);
  let out = text;
  for (const v of variants) {
    const escaped = v.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    // the folder itself, not a longer name that starts with it (C:\Users\Ann, not C:\Users\Anna)
    out = out.replace(new RegExp(`${escaped}(?![\\w-])`, platform === 'win32' ? 'gi' : 'g'), '~');
  }
  return out;
}

/** Whether a tool's failure says the system kept it from starting (the app as a whole may be restricted). */
function blockedStart(code: string): boolean {
  return code === 'UNKNOWN' || code === 'EPERM' || code === 'EACCES';
}

/** Runs the checks one after another; each section's text. */
export async function runChecks(
  checks: readonly SystemCheck[],
  run: RunTool,
  needles: readonly string[],
): Promise<string[]> {
  const out: string[] = [];
  let blocked = 0;
  for (const c of checks) {
    const r = await run(c.file, c.args, TOOL_TIMEOUT_MS);
    if (!r.ok) {
      if (blockedStart(r.code)) {
        blocked++;
      }
      const denied = /access is denied|requires elevation|administrator/i.test(r.message);
      out.push(
        `${c.title}: ${denied ? 'access is denied (an administrator can read this)' : `could not be read (${r.code}: ${r.message.trim()})`}`,
      );
      continue;
    }
    if (c.events === true) {
      const kept = filterEvents(r.stdout, needles);
      out.push(
        kept.length === 0
          ? `${c.title}: none about ShapeLogic`
          : `${c.title}: ${String(kept.length)} about ShapeLogic\n${kept.join('\n\n')}`,
      );
    } else {
      const answer = r.stdout.trim();
      out.push(`${c.title}: ${answer === '' ? '(no answer)' : answer}`);
    }
  }
  if (blocked > 0) {
    out.push(
      `${String(blocked)} of the system's own tools could not be started (spawn UNKNOWN or EPERM): ` +
        'a strong sign the app as a whole is being restricted.',
    );
  }
  return out;
}

/** What the report holds, gathered by the main process. */
export interface DiagnosticsInput {
  version: string;
  platform: NodeJS.Platform;
  time: Date;
  home: string;
  /** The health report's lines (health.ts), and the engine hash's when it was checked. */
  health: readonly string[];
  /** Each window's last engine failure, in words (start-diagnosis.ts's failureText), or none. */
  engines: readonly string[];
  /** The last update check, in words, or null when none ran. */
  update: string | null;
  /** The crash dumps' modification times. */
  crashDumps: readonly Date[];
  /** The system's own checks (runChecks), when they ran. */
  system: readonly string[] | null;
  /** The newest lines of the app's log. */
  log: string;
}

/** The report's text. */
export function diagnosticsText(d: DiagnosticsInput): string {
  const section = (title: string, lines: readonly string[]): string =>
    `== ${title}\n${lines.length === 0 ? '(none)' : lines.join('\n')}\n`;
  const dumps =
    d.crashDumps.length === 0
      ? ['none']
      : [
          `${String(d.crashDumps.length)}, the newest ${d.crashDumps
            .map((t) => t.toISOString())
            .sort()
            .reverse()
            .slice(0, 5)
            .join(', ')}`,
        ];
  const text = [
    `ShapeLogic ${d.version} diagnostics, ${d.time.toISOString()} (${d.platform})\n`,
    section('Health', d.health),
    section('Engine', d.engines.length === 0 ? ['every window has an engine running'] : d.engines),
    section('Update check', [d.update ?? 'none ran']),
    section('Crash dumps', dumps),
    ...(d.system !== null ? [section('The system', d.system)] : []),
    section(
      `The log's newest ${String(DIAGNOSTICS_LOG_LINES)} lines`,
      d.log.trim() === '' ? [] : [d.log.trimEnd()],
    ),
  ].join('\n');
  return shortenHome(text, d.home, d.platform);
}
