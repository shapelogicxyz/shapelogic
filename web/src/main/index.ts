// SPDX-License-Identifier: GPL-3.0-or-later
//
// Electron's main process (decision 0013): a window, one engine host per
// window, the bridge between them, and (stage 6) the menus, the file
// dialogs, the store of settings, layouts and recent files, the rescue
// copy, and the window's close. The renderer is sandboxed and isolated; it
// reaches the engine through the preload's window.sl and the rest through
// window.slApp (app-ipc.ts). A main-frame navigation (a reload) gets a new
// bridge server: the new page's proxy numbers its calls from 1 again, so
// replies to the old page's calls must not reach it; any other navigation
// (a file dropped outside the page's drop zone) and every new window are
// refused.
//
// Closing a window (its button, File ▸ Close, quitting) is the renderer's
// question: the close is held and the renderer told (`close`); it asks
// about unsaved changes and answers `closeWindow`. A renderer that does
// not take the command within a few seconds (hung, or crashed) gets the
// question natively, so a window can always be closed. A page that crashed
// is reloaded (it attaches to the engine's document again), and one that
// hangs is asked about natively (renderer-watch.ts). A quit asks the
// windows one after the other (the focused first): each closes once its
// question is answered, a Cancel stops the quit, and the app quits once the
// last has closed. One app runs at a time for a data directory: a second
// start hands its files to the first, and one without files (Shift-click
// on the taskbar, the launcher again) opens a new window in it.
//
// Several windows (decision 0036): File ▸ New Window opens one, each with
// its own engine, its own rescue copy (rescue.ts's slots) and its own
// document; a new window cascades from the focused one. The first window to
// hold a puzzle file owns it; another that opens it holds it read-only
// (path-owners.ts): its engine's save over that file is refused here, as a
// save over a file another window owns.
//
// The security checklist (stage 8, security.ts): the window sandboxed and
// isolated; the page the app's own, on its own scheme (`sl-app://app/`,
// served from the renderer's directory only; a dev server only when not
// packaged); every request but the app's files canceled; no permission
// granted; no webview, no new window, no navigation; a call naming a file
// the window was not handed refused before the engine sees it
// (path-grants.ts), and so is a page's settings.set naming one of the
// engine's folders: the main process sets the searches' folders (the
// cache folder, cache-folder.ts: by default `sliding` and `panex` in the
// system's application data folder for ShapeLogic)
// on every engine before the page may call it, and again on every engine
// when Settings ▸ General changes the cache folder; exports written only
// where the dialog said (ipc/files.ts).
//
// For the e2e tests: SHAPELOGIC_USER_DATA puts the app's data (the store,
// the default cache folder's saved searches, the rescue copy) in a
// directory of the test's own; with SHAPELOGIC_CALL_LOG set every call the
// renderer makes is written to that file with how it ended
// (a JSON line: method, errc), so the tests can check that the UI never
// asked for what the engine refused; SHAPELOGIC_AUTOSAVE_SECONDS shortens the
// Solver's autosave (its floor of a minute, solver/cadence.ts), passed in the page's address. A
// packaged app takes SHAPELOGIC_USER_DATA only (security.ts's switches).

import {
  app,
  BrowserWindow,
  clipboard,
  crashReporter,
  dialog,
  Menu,
  net,
  protocol,
  screen,
  session,
  shell,
} from 'electron';
import type { WebContents } from 'electron';
import { execFile } from 'node:child_process';
import {
  appendFileSync,
  closeSync,
  existsSync,
  mkdirSync,
  openSync,
  readdirSync,
  readFileSync,
  readSync,
  rmSync,
  statfsSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import {
  arch as osArch,
  release as osRelease,
  type as osType,
  version as osVersion,
} from 'node:os';
import { basename, dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import type {
  AppCommand,
  DiagnosticsResult,
  LogExportResult,
  MenuState,
} from '../engine/app-bridge.ts';
import { NodeTransport } from '../engine/node-transport.ts';
import { EngineStartError } from '../engine/spawn-failure.ts';
import { installAppIpc } from './app-ipc.ts';
import { post } from './ipc/register.ts';
import { openHelp } from './ipc/window.ts';
import {
  AppLog,
  EngineLines,
  LOG_DEBUG_MAX_BYTES,
  LOG_KEEP_ARCHIVE_BYTES,
  LOG_KEEP_LINES,
  LOG_MAX_BYTES,
  logEngineProblems,
  machineText,
  parseEngineLine,
  tailOf,
  teeStderr,
} from './app-log.ts';
import { appDataDir, CacheFolderManager, systemCacheDir } from './cache-folder.ts';
import { CacheLinks, sweepOther } from './cache-files.ts';
import { cliLink, findCli } from './cli-tool.ts';
import {
  DIAGNOSTICS_LOG_LINES,
  diagnosticsText,
  macChecks,
  runChecks,
  windowsChecks,
} from './diagnostics.ts';
import type { RunTool } from './diagnostics.ts';
import { ENGINE_HASH_FILE, sha256File } from './engine-hash.ts';
import { findEngine, programFile } from './engine-path.ts';
import { writeFileSafely } from './export-file.ts';
import { EngineHost } from './engine-host.ts';
import { EngineStops } from './engine-stops.ts';
import { serveEngine } from '../engine/serve.ts';
import type { CallOutcome } from '../engine/serve.ts';
import { menuTemplate, NO_STATE } from './menu.ts';
import { gpuAction, probeFailure, retryArgs, WEBGL2_PROBE } from './gpu-check.ts';
import { engineHashLine, healthReport } from './health.ts';
import type { HealthFs, HealthLine } from './health.ts';
import { PathGrants, settingsRefusal } from './path-grants.ts';
import { PathOwners } from './path-owners.ts';
import { CLOSE_TAKEN_MS, RELOADS_MAX, RendererWatch } from './renderer-watch.ts';
import { RescueSlots } from './rescue.ts';
import {
  allowedRequest,
  APP_SCHEME,
  appFile,
  appUrl,
  permissionAllowed,
  showWindow,
  switches,
  SYSTEM_URLS,
  windowOptions,
} from './security.ts';
import { failureText, stderrLine } from '../engine/start-diagnosis.ts';
import { STARTUP_LINE_WAIT_MS, startupText } from './startup-line.ts';
import type { StartupTimes } from './startup-line.ts';
import { StateStore } from './state-store.ts';
import type { PrefKey } from '../renderer/shell/prefs.ts';

const APP_NAME = 'ShapeLogic';

/** The renderer's setting of the log's debug lines, read from the stored settings by name. */
const DEBUG_LOG = 'debugLog' satisfies PrefKey;
/** The project's version (CMakeLists.txt's, put in at build): the app's and its engine's. */
const VERSION = __SL_VERSION__;
/** The web/ directory, where development looks for the repository's engine builds. */
const WEB_DIR = fileURLToPath(new URL('../../', import.meta.url));
app.setName(APP_NAME);
const env = switches(app.isPackaged, process.env);
if (env.background === true && process.platform === 'darwin') {
  // the e2e tests' apps (SHAPELOGIC_BACKGROUND): no Dock icon, no place in
  // Cmd-Tab, and the app does not come to the front when it starts
  app.setActivationPolicy('accessory');
}
if (env.userData !== undefined) {
  app.setPath('userData', resolve(env.userData));
}
/** When the app's process started (the Startup lines count from it). */
const APP_START = Date.now() - Math.round(process.uptime() * 1000);
// crashes of the main, renderer and GPU processes leave minidumps in
// app.getPath('crashDumps'), sent nowhere: Copy Diagnostics counts them
// (cleanup item 2.7); the engine's crashes show by its exit code
crashReporter.start({ uploadToServer: false });

const callLog = env.callLog;
const logCall =
  callLog === undefined
    ? undefined
    : (o: CallOutcome): void => {
        appendFileSync(callLog, JSON.stringify(o) + '\n');
      };

/** The renderer's directory (out/renderer, in app.asar when packaged). */
const rendererDir = fileURLToPath(new URL('../renderer/', import.meta.url));
// the page's own scheme (security.ts), registered before the app is ready
protocol.registerSchemesAsPrivileged([
  { scheme: APP_SCHEME, privileges: { standard: true, secure: true, codeCache: true } },
]);

/** The files each window's renderer was handed: the only ones its engine may be given. */
const paths = new PathGrants<WebContents>();

/** What the main process keeps of a window. */
interface WindowState {
  win: BrowserWindow;
  menu: MenuState;
  /** The renderer answered a close: the next close goes through. */
  allowClose: boolean;
  /** A close was sent and not yet taken. */
  closeTimer: ReturnType<typeof setTimeout> | null;
  /** Its start's moments, until its Startup line is written (then null). */
  startup: StartupTimes | null;
  startupTimer: ReturnType<typeof setTimeout> | null;
}

/** The last update check's outcome, in words (updates.ts), for the diagnostics. */
let lastUpdateCheck: string | null = null;
/** The engine hash check's line, once it ran (the diagnostics repeat it). */
let engineHashText: string | null = null;

const servers = new Map<number, ReturnType<typeof serveEngine>>();
const windows = new Map<number, WindowState>();
/** Each window's engine host: a change of the cache folder reaches them all. */
const hosts = new Map<number, EngineHost>();
/** The engines of closed windows, until they have ended; a quit waits for them. */
const engineStops = new EngineStops(() => {
  app.quit();
});
let store: StateStore;
/** The app's log (app-log.ts), made at the start. */
let appLog: AppLog;
/** Debug logging (Settings ▸ General), as the settings last stored say. */
let logDebug = false;
/** The log's changes of size, one after another. */
let logDebugDone: Promise<void> = Promise.resolve();

const mb = (bytes: number): string => `${String(Math.round(bytes / 1e6))} MB`;

/**
 * Debug logging turned on or off (a window stored the settings): the log
 * takes its size, and every engine running its level of lines; one
 * started later is set up with it.
 */
function setLogDebug(on: boolean): void {
  if (on === logDebug) {
    return;
  }
  logDebug = on;
  logDebugDone = logDebugDone
    .then(() => appLog.setDebug(on))
    .then(() => {
      if (on) {
        appLog.info(
          'app',
          'Debug Logging On',
          `the log may grow to ${mb(LOG_DEBUG_MAX_BYTES)} before it is compressed. ${machineText([cache.folders.slide, cache.folders.panex])}`,
        );
      } else {
        appLog.info(
          'app',
          'Debug Logging Off',
          `the log kept its newest ${String(LOG_KEEP_LINES)} lines and the ${mb(LOG_KEEP_ARCHIVE_BYTES)} before them, compressed; it grows to ${mb(LOG_MAX_BYTES)} before it is compressed.`,
        );
      }
    });
  for (const h of hosts.values()) {
    if (h.status.state === 'running') {
      h.call('settings.set', { log: on ? 'debug' : 'info' }).catch(() => undefined);
    }
  }
}
let cache: CacheFolderManager<WebContents>;
let rescue: RescueSlots<number>;
/**
 * A quit is under way: the windows are asked to close one after the other
 * (closeNextForQuit); a Cancel ends it.
 */
let quitting = false;
/** Which window owns each puzzle file the windows hold (by web contents id). */
const owners = new PathOwners<number>((id, readOnly) => {
  const c = windows.get(id)?.win.webContents;
  if (c !== undefined && !c.isDestroyed()) {
    post(c, 'sl:read-only', readOnly);
  }
});
/** How far a new window is put from the focused one, right and down. */
const CASCADE = 24;
/**
 * Where the cache folder is by default: the system's application data
 * folder for ShapeLogic, with the earlier versions' default folders to move
 * from (`sliding` in the system's cache folder, `panex` in Electron's data
 * directory); a data directory of its own (SHAPELOGIC_USER_DATA) holds it, and
 * there is nothing earlier to move.
 */
function cacheDefaults(): { dataDir: string; legacy?: { slide: string; panex: string } } {
  const userData = app.getPath('userData');
  if (env.userData !== undefined) {
    return { dataDir: userData };
  }
  const cache = systemCacheDir(process.platform, process.env);
  return {
    dataDir: appDataDir(process.platform, process.env) || userData,
    legacy: { slide: cache === '' ? '' : join(cache, 'sliding'), panex: join(userData, 'panex') },
  };
}

/** The file system as the health report uses it (health.ts). */
const HEALTH_FS: HealthFs = {
  existsSync,
  statSync,
  statfsSync,
  readHead: (path, bytes) => {
    const fd = openSync(path, 'r');
    try {
      const buf = Buffer.alloc(bytes);
      const n = readSync(fd, buf, 0, bytes, 0);
      return buf.subarray(0, n);
    } finally {
      closeSync(fd);
    }
  },
  readFileSync: (path, encoding) => readFileSync(path, encoding),
  mkdirSync,
  writeFileSync,
  rmSync,
};

function logHealth(lines: readonly HealthLine[]): void {
  for (const l of lines) {
    appLog.write(l.level === 'warn' ? 'WARN' : 'INFO', 'app', 'App Health', l.text);
  }
}

/** The engine the app runs (the windows' engines are the same file). */
function appEngine(): string | null {
  return findEngine({
    packaged: app.isPackaged,
    resources: process.resourcesPath,
    webDir: WEB_DIR,
    engine: env.engine,
  });
}

/** The health report's lines now (health.ts). */
function healthLines(): HealthLine[] {
  const f = cache.folders;
  const userData = app.getPath('userData');
  return healthReport({
    platform: process.platform,
    version: VERSION,
    versions: process.versions,
    os: { type: osType(), release: osRelease(), version: osVersion(), arch: osArch() },
    arch: process.arch,
    translated: app.runningUnderARM64Translation,
    locale: app.getSystemLocale(),
    machine: machineText([]),
    execPath: process.execPath,
    resourcesPath: process.resourcesPath,
    packaged: app.isPackaged,
    env: {
      LOCALAPPDATA: process.env.LOCALAPPDATA,
      ProgramFiles: process.env.ProgramFiles,
      'ProgramFiles(x86)': process.env['ProgramFiles(x86)'],
      TEMP: process.env.TEMP,
      TMP: process.env.TMP,
      USERPROFILE: process.env.USERPROFILE,
      HOME: process.env.HOME,
      APPIMAGE: process.env.APPIMAGE,
    },
    engine: appEngine(),
    cli: findCli({ packaged: app.isPackaged, resources: process.resourcesPath, webDir: WEB_DIR }),
    folders: [
      { name: 'user data', path: userData },
      { name: 'logs', path: dirname(appLog.file) },
      { name: 'cache', path: f.slide === '' ? '' : dirname(f.slide) },
      { name: 'sliding', path: f.slide },
      { name: 'Panex', path: f.panex },
      { name: 'temp', path: app.getPath('temp') },
    ],
    gpu: app.getGPUFeatureStatus() as unknown as Record<string, string>,
    fs: HEALTH_FS,
  });
}

/** The "App Health" block: once, at the start, after "App Started" (cleanup item 2.1). */
function writeHealthReport(): void {
  logHealth(healthLines());
}

let engineHashChecked = false;

/**
 * The engine's SHA-256 against the release's record, once, after the first
 * window shows (the file is tens of MB): a development build has no record.
 */
function checkEngineHash(): void {
  if (engineHashChecked) {
    return;
  }
  engineHashChecked = true;
  const engine = appEngine();
  if (engine === null) {
    return;
  }
  const record = join(process.resourcesPath, ENGINE_HASH_FILE);
  const recorded = app.isPackaged && existsSync(record) ? readFileSync(record, 'utf8') : null;
  if (recorded === null) {
    return;
  }
  sha256File(engine).then(
    (hex) => {
      const line = engineHashLine(basename(engine), hex, recorded);
      if (line !== null) {
        engineHashText = line.text;
        logHealth([line]);
      }
    },
    (e: unknown) => {
      logHealth([
        { level: 'warn', text: `engine: ${engine} cannot be read to check it: ${String(e)}` },
      ]);
    },
  );
}

/** Files the system asked to open before a window could take them. */
const pendingOpen: string[] = [];
/**
 * The start-up is done (the store made, the first window asked for). Before
 * that, a second start (a file from the file manager, the launcher clicked
 * again during the WebGL2 check) only queues its files for the first window.
 */
let started = false;

/** The page's parameters: the autosave's interval when SHAPELOGIC_AUTOSAVE_SECONDS sets one. */
const query: Record<string, string> = env.autosave !== undefined ? { autosave: env.autosave } : {};

function send(w: WindowState, command: AppCommand): void {
  if (!w.win.isDestroyed() && !w.win.webContents.isDestroyed()) {
    if (command.cmd === 'openPath') {
      // a file the menus or the system opened with the app
      paths.grant(w.win.webContents, command.path);
    }
    post(w.win.webContents, 'sl:command', command);
  }
}

/** The focused window (the menus act on it), else the first not destroyed. */
function current(): WindowState | undefined {
  // a goal window (goal-window.ts) stands for the app window it belongs to
  const focused = BrowserWindow.getFocusedWindow();
  const owner = focused?.getParentWindow() ?? focused;
  const w = owner == null ? undefined : windows.get(owner.webContents.id);
  if (w !== undefined) {
    return w;
  }
  for (const x of windows.values()) {
    if (!x.win.isDestroyed()) {
      return x;
    }
  }
  return undefined;
}

/** Builds the menus from the focused window's state. */
function updateMenu(): void {
  const w = current();
  const template = menuTemplate(w?.menu ?? NO_STATE, {
    platform: process.platform,
    name: APP_NAME,
    recent: store.recent,
    dev: !app.isPackaged,
    send: (command) => {
      if (command.cmd === 'newWindow') {
        createWindow();
        return;
      }
      const target = current();
      if (target === undefined) {
        if (command.cmd === 'new' || command.cmd === 'open' || command.cmd === 'openPath') {
          // macOS: no window open: a new one takes the command
          createWindow(command);
        }
        return;
      }
      send(target, command);
    },
    openHelp,
    clearRecent: () => {
      store.clearRecent();
    },
    copyDiagnostics: () => {
      void copyDiagnosticsFromMenu();
    },
    windows: [...windows.entries()]
      .filter(([, x]) => !x.win.isDestroyed())
      .map(([id, x]) => ({ id, title: x.win.getTitle(), focused: x === w && x.win.isFocused() })),
    focusWindow: (id) => {
      const x = windows.get(id);
      if (x !== undefined && !x.win.isDestroyed()) {
        if (x.win.isMinimized()) {
          x.win.restore();
        }
        x.win.focus();
      }
    },
  });
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
  // the window may have gone while the menu was built: code run through the
  // inspector (the e2e tests' app.evaluate, which destroys the windows at a
  // test's end) can interrupt this function between two statements
  if (w !== undefined && !w.win.isDestroyed() && process.platform === 'darwin') {
    w.win.setDocumentEdited(w.menu.dirty);
    w.win.setRepresentedFilename(w.menu.path ?? '');
  }
}

/** The window could not be asked through its page: ask natively. */
async function askNatively(w: WindowState): Promise<void> {
  if (w.win.isDestroyed()) {
    return;
  }
  const { response } = await dialog.showMessageBox(w.win, {
    type: 'warning',
    buttons: ['Cancel', 'Close Anyway'],
    defaultId: 0,
    cancelId: 0,
    message: 'The window does not answer.',
    detail:
      'Close it anyway? Changes not saved are lost; the rescue copy of the last ones is offered when the app starts again.',
  });
  if (response === 1 && !w.win.isDestroyed()) {
    w.allowClose = true;
    w.win.close();
  } else {
    quitting = false;
  }
}

/** A quit's next step: the next window asked to close (the focused first), else the quit. */
function closeNextForQuit(): void {
  const next = current();
  if (next === undefined) {
    // quitting stays set: before-quit lets this one through
    app.quit();
    return;
  }
  next.win.close();
}

/** Where a new window goes: cascaded from the focused one, on its screen; null for the first. */
function cascaded(width: number, height: number): { x: number; y: number } | null {
  const from = current();
  if (from === undefined || from.win.isDestroyed()) {
    return null;
  }
  const b = from.win.getBounds();
  const area = screen.getDisplayMatching(b).workArea;
  let x = b.x + CASCADE;
  let y = b.y + CASCADE;
  if (x + width > area.x + area.width || y + height > area.y + area.height) {
    // off the screen's edge: back to its top left corner
    x = area.x;
    y = area.y;
  }
  return { x, y };
}

/** A window's Startup line, once: when its first document shows, or when the wait is over. */
function writeStartupLine(w: WindowState, host: EngineHost, source: string): void {
  const t = w.startup;
  if (t === null) {
    return;
  }
  w.startup = null;
  if (w.startupTimer !== null) {
    clearTimeout(w.startupTimer);
    w.startupTimer = null;
  }
  const launch = host.timings;
  appLog.info(
    source,
    'Startup',
    startupText(APP_START, {
      ...t,
      spawned: launch?.spawnedAt ?? null,
      hello: launch?.helloAt ?? null,
    }),
  );
}

/** How a system tool is run for the diagnostics (only when the user asks). */
const runTool: RunTool = (file, args, timeoutMs) =>
  new Promise((done) => {
    try {
      execFile(
        file,
        [...args],
        { timeout: timeoutMs, windowsHide: true, maxBuffer: 4 * 1024 * 1024 },
        (error, stdout, stderr) => {
          if (error === null) {
            done({ ok: true, stdout });
            return;
          }
          const code =
            error.killed === true
              ? 'TIMEOUT'
              : typeof error.code === 'string'
                ? error.code
                : `exit ${String(error.code ?? '?')}`;
          done({ ok: false, code, message: stderr.trim() !== '' ? stderr : error.message });
        },
      );
    } catch (e) {
      // spawn errors Node throws at once (Windows: UNKNOWN, EPERM)
      const code = (e as { code?: unknown }).code;
      done({
        ok: false,
        code: typeof code === 'string' ? code : 'UNKNOWN',
        message: e instanceof Error ? e.message : String(e),
      });
    }
  });

/** The crash dumps Electron kept: their modification times. */
function crashDumpTimes(): Date[] {
  const out: Date[] = [];
  const walk = (dir: string, depth: number): void => {
    let names: string[];
    try {
      names = readdirSync(dir);
    } catch {
      return;
    }
    for (const n of names) {
      const p = join(dir, n);
      try {
        const st = statSync(p);
        if (st.isDirectory() && depth < 3) {
          walk(p, depth + 1);
        } else if (n.endsWith('.dmp')) {
          out.push(st.mtime);
        }
      } catch {
        // gone meanwhile
      }
    }
  };
  walk(app.getPath('crashDumps'), 0);
  return out;
}

/** The Copy Diagnostics report: built now, the system's own checks run now. */
async function diagnosticsReport(): Promise<string> {
  const engine = appEngine();
  const installDir = dirname(process.execPath);
  const needles = ['shapelogic', installDir, ...(engine !== null ? [engine] : [])];
  // the app's bundle on macOS: …/ShapeLogic.app/Contents/MacOS/ShapeLogic
  const bundle = resolve(process.execPath, '..', '..', '..');
  const system =
    process.platform === 'win32'
      ? await runChecks(windowsChecks(), runTool, needles)
      : process.platform === 'darwin' && app.isPackaged
        ? await runChecks(macChecks(bundle), runTool, needles)
        : null;
  const engines = [...windows.entries()].flatMap(([id, x]) => {
    const h = hosts.get(id);
    const s = h?.status;
    if (s === undefined || s.state === 'running') {
      return [];
    }
    const title = x.win.isDestroyed()
      ? `window ${String(id)}`
      : `window ${String(id)} (${x.win.getTitle()})`;
    if (s.state === 'exited') {
      return [
        `${title}: the engine stopped (exit ${String(s.code)}, signal ${String(s.signal)}); it is started again`,
      ];
    }
    const said = stderrLine(s.failure);
    return [
      `${title}: ${failureText(s.reason, s.failure, process.platform)}` +
        (said !== null ? `\n  the engine's last words: ${said}` : ''),
    ];
  });
  const health = (() => {
    try {
      return healthLines().map((l) => (l.level === 'warn' ? `WARN ${l.text}` : l.text));
    } catch (e) {
      return [`the health report failed: ${String(e)}`];
    }
  })();
  const log = await appLog.text().then(
    (all) => tailOf(all, DIAGNOSTICS_LOG_LINES).toString('utf8'),
    () => '',
  );
  return diagnosticsText({
    version: VERSION,
    platform: process.platform,
    time: new Date(),
    home: app.getPath('home'),
    health: [...health, ...(engineHashText !== null ? [engineHashText] : [])],
    engines,
    update: lastUpdateCheck,
    crashDumps: crashDumpTimes(),
    system,
    log,
  });
}

/** Copy Diagnostics: the report on the clipboard. */
async function copyDiagnostics(): Promise<DiagnosticsResult> {
  try {
    const text = await diagnosticsReport();
    await clipboard.writeText(text);
    appLog.info('app', 'Diagnostics Copied', `${String(text.length)} characters`);
    return { ok: true, bytes: Buffer.byteLength(text) };
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : String(e) };
  }
}

/** Save Diagnostics…: the report in a text file the save dialog gives. */
async function saveDiagnostics(win: BrowserWindow | null): Promise<LogExportResult> {
  const options = {
    title: 'Save Diagnostics',
    defaultPath: join(
      store.lastDir ?? app.getPath('documents'),
      `shapelogic-diagnostics-${new Date().toISOString().slice(0, 10)}.txt`,
    ),
    filters: [{ name: 'Text Files', extensions: ['txt'] }],
    properties: ['showOverwriteConfirmation' as const, 'createDirectory' as const],
  };
  const r =
    win === null ? await dialog.showSaveDialog(options) : await dialog.showSaveDialog(win, options);
  if (r.canceled || r.filePath === '') {
    return { ok: false, canceled: true };
  }
  try {
    await writeFileSafely(r.filePath, Buffer.from(await diagnosticsReport(), 'utf8'));
    return { ok: true, path: r.filePath };
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : String(e) };
  }
}

/** Help ▸ Copy Diagnostics from the menu: works with or without a working page. */
async function copyDiagnosticsFromMenu(): Promise<void> {
  const r = await copyDiagnostics();
  const win = BrowserWindow.getFocusedWindow();
  const options = r.ok
    ? {
        type: 'info' as const,
        title: APP_NAME,
        message: 'The diagnostics are on the clipboard.',
        detail:
          'Paste them into an email or a GitHub issue. They name ShapeLogic’s own files, the system’s version and its security software, and hold the newest lines of the log; your home folder is written as ~.',
        buttons: ['OK', 'Save Diagnostics…'],
        defaultId: 0,
        cancelId: 0,
      }
    : {
        type: 'error' as const,
        title: APP_NAME,
        message: 'The diagnostics could not be made.',
        detail: r.error,
        buttons: ['OK'],
      };
  const { response } =
    win === null ? await dialog.showMessageBox(options) : await dialog.showMessageBox(win, options);
  if (r.ok && response === 1) {
    await saveDiagnostics(win);
  }
}

function createWindow(first?: AppCommand): void {
  const bounds = store.bounds;
  const icon = join(WEB_DIR, 'packaging', 'icon.png');
  const width = bounds?.width ?? 1280;
  const height = bounds?.height ?? 800;
  // the first window where the last was left; another cascaded from the focused one
  const at = windows.size > 0 ? cascaded(width, height) : null;
  const place =
    at ?? (bounds === undefined || windows.size > 0 ? {} : { x: bounds.x, y: bounds.y });
  const win = new BrowserWindow({
    ...windowOptions(fileURLToPath(new URL('../preload/index.cjs', import.meta.url)), APP_NAME, {
      width,
      height,
      ...place,
    }),
    // The window icon of Windows and Linux; macOS takes the app's.
    ...(existsSync(icon) ? { icon } : {}),
  });
  const contents = win.webContents;
  const id = contents.id;
  const w: WindowState = {
    win,
    menu: NO_STATE,
    allowClose: false,
    closeTimer: null,
    startup: { shown: null, loaded: null, spawned: null, hello: null, document: null },
    startupTimer: null,
  };
  windows.set(id, w);
  // its rescue copy's slot, kept while it lives (a reload, an engine restarted)
  rescue.of(id);
  // the log names a window's lines by its number
  const source = `window ${String(id)}`;
  const engine = appEngine();
  const host = new EngineHost({
    start: () => {
      if (engine === null) {
        const message = app.isPackaged
          ? 'no shapelogic_engine in the app'
          : 'no shapelogic_engine: build one (just build) or set SHAPELOGIC_ENGINE';
        throw new EngineStartError(message, {
          which: 'version',
          code: 'NO_ENGINE',
          message,
          path: join(process.resourcesPath, programFile('shapelogic_engine', process.platform)),
          afterMs: 0,
        });
      }
      // one version for the app and the engine (CMakeLists.txt's); its
      // stderr is the log's too, line by line
      const lines = new EngineLines((line) => {
        const told = parseEngineLine(line);
        if (told !== null) {
          appLog.write(told.level, source, told.event, told.text);
        }
      });
      return NodeTransport.start(engine, { expectVersion: VERSION }).then((t) =>
        teeStderr(
          t,
          (chunk) => {
            lines.push(chunk);
          },
          // the last words of an engine that died without their newline
          () => {
            lines.end();
          },
        ),
      );
    },
    // the searches' folders (the cache folder), before the page may call
    // the engine
    setup: async (client) => {
      await cache.start();
      const f = cache.folders;
      // the engine's lines for the log (app-log.ts)
      await client.call('settings.set', {
        slideFolder: f.slide,
        panexFolder: f.panex,
        log: logDebug ? 'debug' : 'info',
      });
    },
  });
  hosts.set(id, host);
  // a window opened while a quit waited for the engines: the app goes on
  // (the quit is over: this window's close ends nothing but itself)
  if (engineStops.cancelQuit()) {
    quitting = false;
  }
  logEngineProblems(host, appLog, source);
  host.onStatus((status) => {
    if (status.state === 'exited') {
      appLog.error(
        source,
        'Engine Stopped',
        `exit code ${String(status.code)}, signal ${String(status.signal)}; it is started again`,
      );
    } else if (status.state === 'failed') {
      appLog.error(
        source,
        'Engine Start Failed',
        failureText(status.reason, status.failure, process.platform),
      );
      const said = stderrLine(status.failure);
      if (said !== null) {
        appLog.error(source, 'Engine Start Failed', `the engine's last words: ${said}`);
      }
    } else {
      const t = host.timings;
      if (t?.helloAt != null) {
        appLog.info(
          source,
          'Engine Ready',
          `version ${status.version}, ${String(t.helloAt - t.startedAt)} ms from its start to its hello` +
            (t.spawnedAt !== null ? ` (spawned at ${String(t.spawnedAt - t.startedAt)} ms)` : ''),
        );
      }
      if (status.restarts > 0) {
        appLog.info(source, 'Engine Restarted', `restart ${String(status.restarts)}`);
      }
    }
  });
  const serve = (): ReturnType<typeof serveEngine> =>
    serveEngine(
      host,
      (message) => {
        if (!contents.isDestroyed()) {
          post(contents, 'sl:message', message);
        }
      },
      logCall,
      (method, params) =>
        paths.refusal(contents, method, params) ??
        settingsRefusal(method, params) ??
        owners.refusal(id, method, params),
    );
  servers.set(id, serve());
  contents.on('did-start-navigation', (details) => {
    if (details.isMainFrame && !details.isSameDocument) {
      servers.get(id)?.dispose();
      servers.set(id, serve());
    }
  });
  // the page never leaves itself: a file dropped outside its drop zone, a link
  contents.on('will-navigate', (e) => {
    e.preventDefault();
  });
  contents.setWindowOpenHandler(() => ({ action: 'deny' }));

  // a page that crashed is reloaded, one that hangs asked about (renderer-watch.ts)
  const watch = new RendererWatch({
    reload: () => {
      if (!contents.isDestroyed()) {
        contents.reload();
      }
    },
    kill: () => {
      if (!contents.isDestroyed()) {
        contents.forcefullyCrashRenderer();
      }
    },
    close: () => {
      if (!win.isDestroyed()) {
        w.allowClose = true;
        win.close();
      }
    },
    // a close waiting for the page's answer: its timer asks natively
    closing: () => win.isDestroyed() || w.allowClose || w.closeTimer !== null,
    askGone: async (details) => {
      const { response } = await dialog.showMessageBox(win, {
        type: 'error',
        buttons: ['Reload', 'Close Window'],
        defaultId: 0,
        cancelId: 0,
        message: 'The window keeps failing.',
        detail: `Its page ended ${String(RELOADS_MAX)} times in a minute (${details.reason}, exit code ${String(details.exitCode)}). Reload it to go on with the document, which the engine still holds, or close the window: changes not saved are then lost; the rescue copy of the last ones is offered when the app starts again.`,
      });
      return response === 1 ? 'close' : 'reload';
    },
    askHung: async (signal) => {
      const { response } = await dialog.showMessageBox(win, {
        type: 'warning',
        buttons: ['Wait', 'Reload'],
        defaultId: 0,
        cancelId: 0,
        message: 'The window does not answer.',
        detail:
          'Wait for it, or reload its page: the document and any solve are kept by the engine, and the page attaches to them again.',
        signal,
      });
      return response === 1 ? 'reload' : 'wait';
    },
    log: (level, event, text) => {
      appLog.write(level, source, event, text);
    },
  });
  contents.on('render-process-gone', (_e, details) => {
    watch.gone(details);
  });
  contents.on('unresponsive', () => {
    watch.unresponsive();
  });
  contents.on('responsive', () => {
    watch.responsive();
  });

  win.on('close', (e) => {
    if (w.allowClose) {
      return;
    }
    e.preventDefault();
    if (w.closeTimer !== null) {
      // asked already and not taken yet: the timer asks natively
      return;
    }
    if (contents.isCrashed()) {
      void askNatively(w);
      return;
    }
    send(w, { cmd: 'close' });
    w.closeTimer = setTimeout(() => {
      w.closeTimer = null;
      void askNatively(w);
    }, CLOSE_TAKEN_MS);
  });
  win.on('closed', () => {
    if (w.closeTimer !== null) {
      clearTimeout(w.closeTimer);
    }
    if (w.startupTimer !== null) {
      clearTimeout(w.startupTimer);
    }
    windows.delete(id);
    servers.get(id)?.dispose();
    servers.delete(id);
    hosts.delete(id);
    owners.release(id);
    // a copy the window left (not answered, or not saved) stays for the next window
    rescue.release(id);
    void cache.forget(contents);
    // its solves pause first: a quit waits for it (before-quit)
    engineStops.stop(host);
    updateMenu();
    // a quit goes on with the next window however this one closed (its own
    // close, while the quit waited on another's answer, does not end it)
    if (quitting) {
      closeNextForQuit();
    }
  });
  const keepBounds = (): void => {
    if (!win.isMinimized() && !win.isMaximized() && !win.isFullScreen()) {
      store.setBounds(win.getBounds());
    }
  };
  win.on('resize', keepBounds);
  win.on('move', keepBounds);
  win.on('focus', updateMenu);
  win.on('blur', updateMenu);
  // the Window menu lists the windows by their titles
  win.on('page-title-updated', () => {
    setImmediate(updateMenu);
  });
  win.once('ready-to-show', () => {
    showWindow(win, env.background === true);
    if (w.startup !== null) {
      w.startup.shown = Date.now();
    }
    checkEngineHash();
  });
  // the Startup line waits for the first document, but not for ever
  w.startupTimer = setTimeout(() => {
    writeStartupLine(w, host, source);
  }, STARTUP_LINE_WAIT_MS);
  contents.on('did-finish-load', () => {
    // a reload: the page is told again that its file is read-only
    if (owners.readOnly(id)) {
      post(contents, 'sl:read-only', true);
    }
  });
  contents.once('did-finish-load', () => {
    if (w.startup !== null) {
      w.startup.loaded = Date.now();
    }
    for (const path of pendingOpen.splice(0)) {
      send(w, { cmd: 'openPath', path });
    }
    if (first !== undefined) {
      send(w, first);
    }
  });
  host.start().catch(() => {
    // its failed status is logged (onStatus above: Engine Start Failed, with
    // the system's details and the engine's last words)
  });
  const devUrl = env.rendererUrl;
  if (devUrl !== undefined) {
    // in development the renderer's warnings and errors show in the terminal
    contents.on('console-message', (event) => {
      if (event.level === 'warning' || event.level === 'error') {
        console.log(`renderer ${event.level}: ${event.message}`);
      }
    });
    const url = new URL(devUrl);
    for (const [k, v] of Object.entries(query)) {
      url.searchParams.set(k, v);
    }
    void win.loadURL(url.toString());
  } else {
    void win.loadURL(appUrl('index.html', query));
  }
}

/** A puzzle or paused-solve file given on the command line (Windows, Linux). */
function argFiles(): string[] {
  return process.argv
    .slice(app.isPackaged ? 1 : 2)
    .filter((a) => ['.xmpuzzle', '.slsolve'].includes(extname(a).toLowerCase()) && existsSync(a))
    .map((a) => resolve(a));
}

/** Opens files the system gave the app (a window takes them, or a new one once ready). */
function openFiles(paths: readonly string[]): void {
  if (!primary) {
    return;
  }
  const w = current();
  if (w === undefined || !started) {
    pendingOpen.push(...paths);
    if (started) {
      createWindow();
    }
    return;
  }
  for (const path of paths) {
    send(w, { cmd: 'openPath', path });
  }
}

// One app at a time for a data directory: the store and the rescue copy are
// the app's. A second start (Windows and Linux start one for each file opened
// from the file manager) would offer the running app's rescue copy as left by
// a session that died, and its Discard or a clean close would remove the copy
// of a document still being edited: it hands its files to the first and quits.
const primary = app.requestSingleInstanceLock({ files: argFiles() });
if (!primary) {
  app.quit();
}
app.on('second-instance', (_e, _argv, _dir, data: unknown) => {
  const given = (data as { files?: unknown } | null)?.files;
  const files = Array.isArray(given)
    ? given.filter((f): f is string => typeof f === 'string' && f !== '')
    : [];
  if (files.length > 0) {
    openFiles(files);
  } else if (started && primary) {
    // the app started again without a file (Shift-click on the taskbar,
    // the launcher, `open -n`): another window
    createWindow();
    return;
  }
  const w = current();
  if (w !== undefined && !w.win.isDestroyed()) {
    if (w.win.isMinimized()) {
      w.win.restore();
    }
    w.win.focus();
  }
});

// macOS: a file opened with the app (Finder, the Dock)
app.on('open-file', (e, path) => {
  e.preventDefault();
  openFiles([path]);
});

// a quit asks the windows one at a time (closeNextForQuit), so a Cancel in
// one leaves the others open; with none left it goes through
app.on('before-quit', (e) => {
  if (![...windows.values()].some((w) => !w.win.isDestroyed())) {
    // The engines of the windows that closed are still pausing and saving
    // their solves: the quit waits for them (capped), then is asked for
    // again. Quitting at once left them behind, their output unread and
    // nobody to end one that hung (cleanup item 15.1).
    if (engineStops.holdQuit()) {
      e.preventDefault();
    }
    return;
  }
  e.preventDefault();
  if (!quitting) {
    quitting = true;
    closeNextForQuit();
  }
});

app.on('will-quit', () => {
  // a quit during the start-up (the WebGL2 check) has no store yet
  if (primary && started) {
    store.flush();
  }
  // the log's lines of this tick (app-log.ts writes them at its end); a
  // second start quits before it has a log
  (appLog as AppLog | undefined)?.flush();
});

// app.exit and the end of the process: the log's last lines
process.on('exit', () => {
  (appLog as AppLog | undefined)?.flush();
});

/** True while the hidden WebGL2 probe's window is the only one (its close does not quit). */
let probing = false;

/** `p`'s value, or `fallback` once `ms` have passed (a GPU process that hangs never answers). */
async function within<T>(p: Promise<T>, ms: number, fallback: T): Promise<T> {
  return Promise.race([
    p,
    new Promise<T>((done) => {
      setTimeout(() => {
        done(fallback);
      }, ms);
    }),
  ]);
}

/** The renderer of a hidden page's WebGL2 context (gpu-check.ts); null without one, or after 15 s. */
async function webgl2Renderer(): Promise<string | null> {
  probing = true;
  // the app window's options, never shown, without the preload (its
  // sl:app-info would wait on the IPC installed after this); a blank page only
  const win = new BrowserWindow({
    ...windowOptions(null, APP_NAME, { width: 64, height: 64 }),
  });
  try {
    const probe = win
      .loadURL('about:blank')
      .then(() => win.webContents.executeJavaScript(WEBGL2_PROBE) as Promise<string | null>);
    return await within(probe, 15_000, null);
  } catch {
    return null;
  } finally {
    win.destroy();
    probing = false;
  }
}

/**
 * WebGL2 is checked before the first window (gpu-check.ts): a real context
 * asked for, then a start again with the blocklist overridden, else an
 * explanation. True when the app goes on.
 */
async function webglReady(): Promise<boolean> {
  await within(
    app.getGPUInfo('basic').catch(() => null),
    5_000,
    null,
  );
  const status = app.getGPUFeatureStatus().webgl2;
  const action = gpuAction(status, process.argv, process.platform, app.isPackaged);
  if (action === 'continue') {
    return true;
  }
  const renderer = await webgl2Renderer();
  console.error(
    `ShapeLogic: WebGL2 status ${status}; context in a page: ${renderer === null ? 'no' : `yes (renderer: ${renderer})`}`,
  );
  if (renderer !== null) {
    return true;
  }
  if (probeFailure(process.argv) === 'retry') {
    // the new start can come up while this one is still exiting: holding the
    // lock, this one would take it for a second start and the new one would quit
    app.releaseSingleInstanceLock();
    // an AppImage is started again through its own file, not the unpacked binary
    const execPath = process.env.APPIMAGE;
    app.relaunch({
      ...(execPath !== undefined ? { execPath } : {}),
      args: retryArgs(process.argv),
    });
    app.exit(0);
    return false;
  }
  const { response } = await dialog.showMessageBox({
    type: 'error',
    title: APP_NAME,
    message: 'ShapeLogic cannot draw its 3D views on this computer.',
    detail:
      'WebGL2 is not available, even with the graphics blocklist overridden. ' +
      'This happens in virtual machines without 3D acceleration and with some graphics drivers. ' +
      `The page below explains what to try:\n\n${SYSTEM_URLS.linuxGraphics}\n\n` +
      `(WebGL2 status: ${status})`,
    buttons: ['Open the help page', 'Quit'],
    defaultId: 0,
    cancelId: 1,
  });
  if (response === 0) {
    await shell.openExternal(SYSTEM_URLS.linuxGraphics);
  }
  app.exit(0);
  return false;
}

void app.whenReady().then(async () => {
  if (!primary) {
    // a second start: quitting, its files handed to the first
    return;
  }
  if (!(await webglReady())) {
    return;
  }
  // Development runs Electron's own bundle, whose Dock icon is Electron's;
  // a packaged app has its icon in its bundle already.
  const dockIcon = join(WEB_DIR, 'packaging', 'icon.png');
  if (!app.isPackaged && process.platform === 'darwin' && existsSync(dockIcon)) {
    app.dock?.setIcon(dockIcon);
  }
  // a failed write of state.json: the log is made just below, before any write
  store = new StateStore(app.getPath('userData'), {
    onError: (message) => {
      appLog.warn('app', 'Settings Not Stored', message);
    },
  });
  logDebug = store.prefs?.[DEBUG_LOG] === true;
  appLog = new AppLog(join(app.getPath('userData'), 'logs'), { debug: logDebug });
  appLog.info(
    'app',
    'App Started',
    `ShapeLogic ${VERSION} (${process.platform} ${process.arch}${app.isPackaged ? '' : ', development'}${logDebug ? ', debug logging on' : ''}); the log is ${appLog.file}`,
  );
  rescue = new RescueSlots<number>(app.getPath('userData'));
  cache = new CacheFolderManager<WebContents>({
    defaults: cacheDefaults(),
    stored: () => store.cacheRoot,
    store: (root) => {
      store.setCacheRoot(root);
    },
    solving: async () => {
      const answers = await Promise.all(
        [...hosts.values()]
          .filter((h) => h.status.state === 'running')
          .map((h) =>
            h.call('document.get').then(
              (d) => d.solving,
              () => false,
            ),
          ),
      );
      return answers.includes(true);
    },
    apply: async (f) => {
      await Promise.all(
        [...hosts.values()]
          .filter((h) => h.status.state === 'running')
          .map((h) =>
            h
              .call('settings.set', { slideFolder: f.slide, panexFolder: f.panex })
              .catch(() => undefined),
          ),
      );
    },
  });
  // the stray files of the search folders a week old go, unasked
  void cache
    .start()
    // the health report goes to the log whether or not the folder could be used
    .catch(() => undefined)
    .then(() => {
      try {
        writeHealthReport();
      } catch (e) {
        appLog.write('WARN', 'app', 'App Health', `the report failed: ${String(e)}`);
      }
      if (logDebug) {
        appLog.info(
          'app',
          'Debug Logging On',
          `the log may grow to ${mb(LOG_DEBUG_MAX_BYTES)} before it is compressed. ${machineText([cache.folders.slide, cache.folders.panex])}`,
        );
      }
      return sweepOther(cache.folders);
    })
    .catch(() => undefined);
  pendingOpen.push(...argFiles());
  // the page and its files, from the renderer's directory and nowhere else
  protocol.handle(APP_SCHEME, (request) => {
    const file = appFile(request.url, rendererDir);
    const notFound = (): Response => new Response('not found', { status: 404 });
    if (file === null) {
      return notFound();
    }
    return net.fetch(pathToFileURL(file).href).catch(notFound);
  });
  // no remote content, no permission
  session.defaultSession.webRequest.onBeforeRequest((details, done) => {
    done({ cancel: !allowedRequest(details.url, { rendererDir, rendererUrl: env.rendererUrl }) });
  });
  session.defaultSession.setPermissionRequestHandler((_contents, permission, done) => {
    done(permissionAllowed(permission));
  });
  session.defaultSession.setPermissionCheckHandler((_contents, permission) =>
    permissionAllowed(permission),
  );
  installAppIpc({
    store,
    rescue,
    engine: (sender) => hosts.get(sender.id) ?? null,
    engineCall: (sender, request) => {
      servers.get(sender.id)?.receive(request);
    },
    paths,
    owners,
    version: VERSION,
    packaged: app.isPackaged,
    // not the default session: it cancels every remote request
    fetch: (url, init) => session.fromPartition('updates').fetch(url, init),
    cliPath: findCli({
      packaged: app.isPackaged,
      resources: process.resourcesPath,
      webDir: WEB_DIR,
    }),
    cliLink: cliLink(process.platform),
    cache,
    cacheLinks: new CacheLinks(app.getPath('userData')),
    log: appLog,
    background: env.background === true,
    prefsChanged: (prefs) => {
      setLogDebug(prefs[DEBUG_LOG] === true);
    },
    window: (sender) => windows.get(sender.id)?.win ?? null,
    setMenu: (sender, state) => {
      const w = windows.get(sender.id);
      if (w !== undefined) {
        w.menu = state;
        owners.hold(sender.id, state.path);
        updateMenu();
      }
    },
    newWindow: () => {
      createWindow();
    },
    others: (sender) =>
      [...windows.values()]
        .map((x) => x.win)
        .filter((x) => !x.isDestroyed() && x.webContents.id !== sender.id)
        .map((x) => x.webContents),
    closeTaken: (sender) => {
      const w = windows.get(sender.id);
      if (w?.closeTimer != null) {
        clearTimeout(w.closeTimer);
        w.closeTimer = null;
      }
    },
    closeWindow: (sender) => {
      const w = windows.get(sender.id);
      if (w !== undefined && !w.win.isDestroyed()) {
        w.allowClose = true;
        w.win.close();
      }
    },
    cancelQuit: (sender) => {
      if (windows.has(sender.id)) {
        // a window's Cancel: the quit stops, the windows not asked yet stay open
        quitting = false;
      }
    },
    updateChecked: (text) => {
      if (text !== lastUpdateCheck) {
        appLog.info('app', 'Update Check', text);
      }
      lastUpdateCheck = text;
    },
    ready: (sender) => {
      const w = windows.get(sender.id);
      const host = hosts.get(sender.id);
      if (w?.startup != null && host !== undefined) {
        w.startup.document = Date.now();
        writeStartupLine(w, host, `window ${String(sender.id)}`);
      }
    },
    retryEngine: (sender) => {
      const host = hosts.get(sender.id);
      if (host !== undefined) {
        appLog.info(`window ${String(sender.id)}`, 'Engine Try Again', 'the window asked');
        host.retry().catch(() => {
          // its failed status says why, and the log has it
        });
      }
    },
    commandHeld: (sender, command, dropped) => {
      appLog.info(
        `window ${String(sender.id)}`,
        dropped ? 'Command Dropped' : 'Command Held',
        dropped
          ? `${command}: the engine could not start; the window says so`
          : `${command}: waits for the first document (the engine is starting)`,
      );
    },
    copyDiagnostics: () => copyDiagnostics(),
    saveDiagnostics: (sender) => saveDiagnostics(windows.get(sender.id)?.win ?? null),
  });
  store.onRecent(updateMenu);
  updateMenu();
  started = true;
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

// no webview and no new window, whatever web contents asks
app.on('web-contents-created', (_e, contents) => {
  contents.on('will-attach-webview', (e) => {
    e.preventDefault();
  });
  contents.setWindowOpenHandler(() => ({ action: 'deny' }));
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin' && !probing) {
    app.quit();
  }
});
