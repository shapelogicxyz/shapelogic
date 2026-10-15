// SPDX-License-Identifier: GPL-3.0-or-later
//
// The packaged app (stage 8, `just web-dist`): electron-builder over the
// build in out/ (electron-vite), with shapelogic_engine from the release preset in
// the bundle's resources and one version for both, CMakeLists.txt's.
//
//   node tools/dist.ts [--dir] [--arch x64|arm64]
//
// Builds for the platform it runs on (the engine is built natively: no
// cross-compiling), for this machine's architecture unless --arch says
// otherwise; on macOS --arch x64 takes build/release-x64's engine (the
// `release-x64` preset). --dir makes the unpacked app only (the smoke
// test's), else the installers: macOS a .dmg and a .zip, Windows an NSIS
// installer, Linux an AppImage and a .deb. Before packing it checks that the
// engine is there, built for that architecture, and says the project's
// version (`shapelogic_engine --version`), and that package.json says it too.
//
// Signing (docs/handoff/web-8.md, "Signing and notarisation"): on macOS the
// app is signed ad hoc unless CSC_NAME or CSC_LINK names a Developer ID
// certificate, which electron-builder then uses with the hardened runtime
// (and notarises when APPLE_API_KEY, APPLE_API_KEY_ID and APPLE_API_ISSUER
// are set); on Windows it is unsigned unless CSC_LINK (or WIN_CSC_LINK)
// names a certificate.

import { execFileSync } from 'node:child_process';
import { existsSync, openSync, readFileSync, readSync, closeSync } from 'node:fs';
import { resolve } from 'node:path';
import { parseArgs } from 'node:util';

import { Arch, build, Platform } from 'electron-builder';
import type { Configuration } from 'electron-builder';

import { projectVersion, REPO_ROOT } from './project-version.ts';

const WEB = resolve(import.meta.dirname, '..');

/** The architecture a binary was built for, read from its header; null when not known. */
export function binaryArch(path: string): 'x64' | 'arm64' | null {
  const b = Buffer.alloc(0x200);
  const fd = openSync(path, 'r');
  try {
    readSync(fd, b, 0, b.length, 0);
  } finally {
    closeSync(fd);
  }
  // Mach-O, 64 bits, little-endian: the CPU type after the magic
  if (b.readUInt32LE(0) === 0xfeedfacf) {
    const cpu = b.readUInt32LE(4);
    return cpu === 0x0100000c ? 'arm64' : cpu === 0x01000007 ? 'x64' : null;
  }
  // ELF: e_machine
  if (b.readUInt32BE(0) === 0x7f454c46) {
    const machine = b.readUInt16LE(18);
    return machine === 62 ? 'x64' : machine === 183 ? 'arm64' : null;
  }
  // PE: the COFF header's machine, after "PE\0\0"
  if (b.toString('latin1', 0, 2) === 'MZ') {
    const pe = b.readUInt32LE(0x3c);
    if (pe + 6 <= b.length && b.readUInt32LE(pe) === 0x4550) {
      const machine = b.readUInt16LE(pe + 4);
      return machine === 0x8664 ? 'x64' : machine === 0xaa64 ? 'arm64' : null;
    }
  }
  return null;
}

/** The release engine for a platform and architecture. */
export function releaseEngine(platform: NodeJS.Platform, arch: 'x64' | 'arm64'): string {
  const exe = platform === 'win32' ? 'shapelogic_engine.exe' : 'shapelogic_engine';
  // the macOS host builds its own architecture in `release`, Intel in `release-x64`
  const preset = platform === 'darwin' && arch !== process.arch ? `release-${arch}` : 'release';
  return resolve(REPO_ROOT, 'build', preset, 'engine', exe);
}

/** The command line tool beside an engine in the same build (`build/<preset>/cli/`). */
export function releaseCli(engine: string): string {
  const exe = engine.endsWith('.exe') ? 'shapelogic_cli.exe' : 'shapelogic_cli';
  return resolve(engine, '..', '..', 'cli', exe);
}

/** A program is there, of that architecture and version; else why not. */
function checkEngine(
  path: string,
  arch: 'x64' | 'arm64',
  version: string,
  program = 'shapelogic_engine',
): void {
  const preset = path.split(/[\\/]/).at(-3) ?? 'release';
  if (!existsSync(path)) {
    throw new Error(`no ${program} at ${path}: run \`just build ${preset}\` first`);
  }
  const built = binaryArch(path);
  if (built !== arch) {
    throw new Error(`${path} is built for ${built ?? 'an unknown architecture'}, not ${arch}`);
  }
  const said = execFileSync(path, ['--version'], { encoding: 'utf8', timeout: 10_000 }).trim();
  if (said !== `${program} ${version}`) {
    throw new Error(
      `${path} says "${said}", not "${program} ${version}" (CMakeLists.txt): build it again`,
    );
  }
}

/** electron-builder's configuration for this version and engine. */
export function distConfig(
  version: string,
  engine: string,
  platform: NodeJS.Platform,
  cli: string = releaseCli(engine),
): Configuration {
  const exe = platform === 'win32' ? 'shapelogic_engine.exe' : 'shapelogic_engine';
  const cliExe = platform === 'win32' ? 'shapelogic_cli.exe' : 'shapelogic_cli';
  const signed = (process.env.CSC_NAME ?? '') !== '' || (process.env.CSC_LINK ?? '') !== '';
  return {
    appId: 'xyz.shapelogic.app',
    productName: 'ShapeLogic',
    copyright: 'GPL-3.0-or-later; a fork of BurrTools by Andreas Röver and others',
    directories: { output: 'dist', buildResources: 'packaging' },
    // the bundles electron-vite made, and nothing else: three.js is in the
    // renderer's bundle, the main process needs no package
    files: ['out/**/*', 'packaging/icon.png', 'package.json', '!**/*.map'],
    extraMetadata: { version, main: './out/main/index.js' },
    asar: true,
    npmRebuild: false,
    publish: null,
    // the engine, and the command line tool the app can link into the path
    // (src/main/cli-tool.ts)
    extraResources: [
      { from: engine, to: exe },
      { from: cli, to: cliExe },
    ],
    fileAssociations: [
      {
        ext: 'xmpuzzle',
        name: 'ShapeLogic Puzzle',
        description: 'ShapeLogic Puzzle',
        role: 'Editor',
        icon: 'document',
      },
      {
        ext: 'slsolve',
        name: 'ShapeLogic Paused Solve',
        description: 'ShapeLogic Paused Solve',
        role: 'Editor',
        icon: 'document',
      },
    ],
    // Electron's fuses (the security checklist): no ELECTRON_RUN_AS_NODE,
    // no NODE_OPTIONS, no --inspect, the app only from its app.asar, and
    // that checked against the hash the build wrote; no extra privileges
    // for file: pages (the page is on the app's own scheme, security.ts)
    electronFuses: {
      runAsNode: false,
      enableCookieEncryption: true,
      enableNodeOptionsEnvironmentVariable: false,
      enableNodeCliInspectArguments: false,
      enableEmbeddedAsarIntegrityValidation: true,
      onlyLoadAppFromAsar: true,
      grantFileProtocolExtraPrivileges: false,
    },
    mac: {
      target: ['dmg', 'zip'],
      category: 'public.app-category.puzzle-games',
      icon: 'packaging/icon.icns',
      // ad hoc unless a certificate is named (then hardened and notarised)
      // with the project's own entitlements (allow-jit only), for the app and its helpers
      ...(signed
        ? {
            hardenedRuntime: true,
            entitlements: 'packaging/entitlements.mac.plist',
            entitlementsInherit: 'packaging/entitlements.mac.plist',
          }
        : { identity: '-', hardenedRuntime: false }),
    },
    dmg: { title: 'ShapeLogic ${version}' },
    win: { target: 'nsis', icon: 'packaging/icon.png' },
    nsis: { oneClick: false, perMachine: false, allowToChangeInstallationDirectory: true },
    linux: {
      target: ['AppImage', 'deb'],
      executableName: 'shapelogic',
      category: 'Game',
      icon: 'packaging/icon.png',
      maintainer: 'ShapeLogic',
      synopsis: 'Design and solve interlocking puzzles',
    },
  };
}

async function main(): Promise<void> {
  const { values } = parseArgs({
    options: { dir: { type: 'boolean', default: false }, arch: { type: 'string' } },
  });
  const arch = values.arch ?? process.arch;
  if (arch !== 'x64' && arch !== 'arm64') {
    throw new Error(`--arch ${arch}: x64 or arm64`);
  }
  const platform = process.platform;
  const version = projectVersion();
  const pkg = JSON.parse(readFileSync(resolve(WEB, 'package.json'), 'utf8')) as {
    version: string;
  };
  if (pkg.version !== version) {
    throw new Error(`web/package.json says ${pkg.version}, CMakeLists.txt ${version}`);
  }
  if (!existsSync(resolve(WEB, 'out/main/index.js'))) {
    throw new Error('no build in out/: run `npm run build` first');
  }
  const engine = releaseEngine(platform, arch);
  checkEngine(engine, arch, version);
  checkEngine(releaseCli(engine), arch, version, 'shapelogic_cli');
  const target =
    platform === 'darwin' ? Platform.MAC : platform === 'win32' ? Platform.WINDOWS : Platform.LINUX;
  const config = distConfig(version, engine, platform);
  console.log(`dist: ShapeLogic ${version} for ${platform} ${arch}, with ${engine}`);
  await build({
    projectDir: WEB,
    targets: target.createTarget(
      values.dir ? ['dir'] : null,
      arch === 'x64' ? Arch.x64 : Arch.arm64,
    ),
    config,
    publish: 'never',
  });
}

if (import.meta.filename === resolve(process.argv[1] ?? '')) {
  main().catch((e: unknown) => {
    console.error(`dist: ${e instanceof Error ? e.message : String(e)}`);
    process.exitCode = 1;
  });
}
