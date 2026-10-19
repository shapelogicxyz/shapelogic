// SPDX-License-Identifier: GPL-3.0-or-later
//
// The packaging's parts that run without packing (stage 8): one version for
// the app and the engine (CMakeLists.txt's, package.json's the same, the
// release engine's --version), electron-builder's configuration (the
// bundles only, the engine in the resources, the fuses), the architecture
// read from a binary's header, and where the app finds its engine
// (packaged: its own copy only; in development BT_ENGINE, then the builds).

import { execFileSync } from 'node:child_process';
import {
  chmodSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import { afterEach, describe, expect, it } from 'vitest';

import { engineFile, findEngine } from '../../src/main/engine-path.ts';
import { binaryArch, distConfig, releaseCli, releaseEngine } from '../../tools/dist.ts';
import { projectVersion, REPO_ROOT } from '../../tools/project-version.ts';

const WEB = resolve(import.meta.dirname, '../..');

describe('the version', () => {
  it('is CMakeLists.txt’s, and package.json says the same', () => {
    const v = projectVersion();
    expect(v).toMatch(/^\d+\.\d+\.\d+$/);
    const cmake = readFileSync(resolve(REPO_ROOT, 'CMakeLists.txt'), 'utf8');
    expect(cmake).toContain(`VERSION ${v}`);
    const pkg = JSON.parse(readFileSync(resolve(WEB, 'package.json'), 'utf8')) as {
      version: string;
    };
    expect(pkg.version).toBe(v);
  });

  it('is what the release engine says, when it is built', () => {
    const engine = releaseEngine(process.platform, process.arch === 'arm64' ? 'arm64' : 'x64');
    if (!existsSync(engine)) {
      return;
    }
    expect(execFileSync(engine, ['--version'], { encoding: 'utf8' }).trim()).toBe(
      `shapelogic_engine ${projectVersion()}`,
    );
    expect(binaryArch(engine)).toBe(process.arch);
  });

  it('is what the release command line tool says, when it is built', () => {
    const engine = releaseEngine(process.platform, process.arch === 'arm64' ? 'arm64' : 'x64');
    const cli = releaseCli(engine);
    expect(cli).toBe(
      resolve(
        engine,
        '..',
        '..',
        'cli',
        process.platform === 'win32' ? 'shapelogic_cli.exe' : 'shapelogic_cli',
      ),
    );
    if (!existsSync(cli)) {
      return;
    }
    expect(execFileSync(cli, ['--version'], { encoding: 'utf8' }).trim()).toBe(
      `shapelogic_cli ${projectVersion()}`,
    );
    expect(binaryArch(cli)).toBe(process.arch);
  });

  it('is put into the main process at build', () => {
    const main = resolve(WEB, 'out/main/index.js');
    if (!existsSync(main)) {
      return;
    }
    const text = readFileSync(main, 'utf8');
    expect(text).not.toContain('__BT_VERSION__');
    expect(text).toContain(`const VERSION = ${JSON.stringify(projectVersion())}`);
  });

  it('is read from project(), whatever else the file says', () => {
    const dir = mkdtempSync(join(tmpdir(), 'bt-version-'));
    try {
      writeFileSync(
        join(dir, 'CMakeLists.txt'),
        'cmake_minimum_required(VERSION 3.28)\nproject(Some\n  VERSION 2.10.3\n  LANGUAGES CXX)\n',
      );
      expect(projectVersion(dir)).toBe('2.10.3');
      writeFileSync(join(dir, 'CMakeLists.txt'), 'cmake_minimum_required(VERSION 3.28)\n');
      expect(() => projectVersion(dir)).toThrow(/no project/);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe('the macOS signing', () => {
  const was = { name: process.env.CSC_NAME, link: process.env.CSC_LINK };
  const put = (key: 'CSC_NAME' | 'CSC_LINK', v: string | undefined): void => {
    if (v === undefined) {
      Reflect.deleteProperty(process.env, key);
    } else {
      process.env[key] = v;
    }
  };
  afterEach(() => {
    put('CSC_NAME', was.name);
    put('CSC_LINK', was.link);
  });

  it('is ad hoc without a certificate, and hardened with allow-jit only with one', () => {
    put('CSC_NAME', undefined);
    put('CSC_LINK', undefined);
    const adhoc = distConfig('1.2.3', '/b/release/engine/shapelogic_engine', 'darwin').mac;
    expect(adhoc).toMatchObject({ identity: '-', hardenedRuntime: false });
    expect(adhoc).not.toHaveProperty('entitlements');
    process.env.CSC_NAME = 'Developer ID Application: Someone (ABCDE12345)';
    const signed = distConfig('1.2.3', '/b/release/engine/shapelogic_engine', 'darwin').mac;
    expect(signed).toMatchObject({
      hardenedRuntime: true,
      entitlements: 'packaging/entitlements.mac.plist',
      entitlementsInherit: 'packaging/entitlements.mac.plist',
    });
    const plist = readFileSync(resolve(WEB, 'packaging/entitlements.mac.plist'), 'utf8');
    expect([...plist.matchAll(/<key>([^<]+)<\/key>/g)].map((m) => m[1])).toEqual([
      'com.apple.security.cs.allow-jit',
    ]);
  });
});

describe('the configuration', () => {
  const c = distConfig('1.2.3', '/b/release/engine/shapelogic_engine', 'darwin');

  it('packs the bundles and the version, with the engine beside them', () => {
    expect(c.files).toEqual(['out/**/*', 'packaging/icon.png', 'package.json', '!**/*.map']);
    expect(c.extraMetadata).toEqual({
      version: '1.2.3',
      main: './out/main/index.js',
      desktopName: 'shapelogic.desktop',
      description: 'Design and solve interlocking puzzles',
    });
    // the engine, and the command line tool of the same build beside it
    expect(c.extraResources).toEqual([
      { from: '/b/release/engine/shapelogic_engine', to: 'shapelogic_engine' },
      { from: '/b/release/cli/shapelogic_cli', to: 'shapelogic_cli' },
    ]);
    expect(
      distConfig('1.2.3', 'C:\\e\\engine\\shapelogic_engine.exe', 'win32').extraResources,
    ).toEqual([
      { from: 'C:\\e\\engine\\shapelogic_engine.exe', to: 'shapelogic_engine.exe' },
      {
        from: resolve(
          'C:\\e\\engine\\shapelogic_engine.exe',
          '..',
          '..',
          'cli',
          'shapelogic_cli.exe',
        ),
        to: 'shapelogic_cli.exe',
      },
    ]);
    expect(c.asar).toBe(true);
    expect(c.publish).toBeNull();
    // the main process needs no package: three.js is in the renderer's bundle
    const pkg = JSON.parse(readFileSync(resolve(WEB, 'package.json'), 'utf8')) as {
      dependencies?: Record<string, string>;
    };
    expect(pkg.dependencies ?? {}).toEqual({});
  });

  it('turns off what lets the environment run code in the app, and checks its archive', () => {
    expect(c.electronFuses).toEqual({
      runAsNode: false,
      enableCookieEncryption: true,
      enableNodeOptionsEnvironmentVariable: false,
      enableNodeCliInspectArguments: false,
      enableEmbeddedAsarIntegrityValidation: true,
      onlyLoadAppFromAsar: true,
      grantFileProtocolExtraPrivileges: false,
    });
  });

  it('opens the puzzle files', () => {
    expect(c.fileAssociations).toEqual([
      expect.objectContaining({ ext: 'xmpuzzle' }),
      expect.objectContaining({ ext: 'slsolve' }),
    ]);
  });
});

describe('the binary’s architecture', () => {
  const dir = mkdtempSync(join(tmpdir(), 'bt-arch-'));
  const file = (name: string, bytes: number[]): string => {
    const b = Buffer.alloc(0x200);
    Buffer.from(bytes).copy(b);
    const p = join(dir, name);
    writeFileSync(p, b);
    return p;
  };

  it('Mach-O, ELF and PE headers', () => {
    expect(binaryArch(file('m1', [0xcf, 0xfa, 0xed, 0xfe, 0x0c, 0, 0, 0x01]))).toBe('arm64');
    expect(binaryArch(file('m2', [0xcf, 0xfa, 0xed, 0xfe, 0x07, 0, 0, 0x01]))).toBe('x64');
    const elf = [0x7f, 0x45, 0x4c, 0x46, ...Array<number>(14).fill(0)];
    expect(binaryArch(file('e1', [...elf, 62, 0]))).toBe('x64');
    expect(binaryArch(file('e2', [...elf, 183, 0]))).toBe('arm64');
    const pe = Array<number>(0x90).fill(0);
    pe[0] = 0x4d;
    pe[1] = 0x5a;
    pe[0x3c] = 0x80;
    pe.splice(0x80, 6, 0x50, 0x45, 0, 0, 0x64, 0x86);
    expect(binaryArch(file('p1', pe))).toBe('x64');
    expect(binaryArch(file('z', [1, 2, 3, 4]))).toBeNull();
    rmSync(dir, { recursive: true, force: true });
  });
});

describe('where the app finds its engine', () => {
  const dir = mkdtempSync(join(tmpdir(), 'bt-find-'));
  const resources = join(dir, 'Resources');
  const web = join(dir, 'repo', 'web');
  const dev = join(dir, 'repo', 'build', 'dev', 'engine', engineFile(process.platform));
  const release = join(dir, 'repo', 'build', 'release', 'engine', engineFile(process.platform));
  const put = (p: string): void => {
    mkdirSync(resolve(p, '..'), { recursive: true });
    writeFileSync(p, '');
    chmodSync(p, 0o755);
  };

  it('packaged: its own copy, never BT_ENGINE or a build', () => {
    put(dev);
    expect(
      findEngine({ packaged: true, resources, webDir: web, engine: '/x/shapelogic_engine' }),
    ).toBeNull();
    put(join(resources, engineFile(process.platform)));
    expect(
      findEngine({ packaged: true, resources, webDir: web, engine: '/x/shapelogic_engine' }),
    ).toBe(join(resources, engineFile(process.platform)));
    expect(findEngine({ packaged: true, resources, webDir: web, platform: 'win32' })).toBeNull();
  });

  it('in development: BT_ENGINE, else the dev build, else the release build', () => {
    expect(
      findEngine({ packaged: false, resources, webDir: web, engine: '/x/shapelogic_engine' }),
    ).toBe('/x/shapelogic_engine');
    expect(findEngine({ packaged: false, resources, webDir: web })).toBe(dev);
    rmSync(dev);
    put(release);
    expect(findEngine({ packaged: false, resources, webDir: web, engine: '' })).toBe(release);
    rmSync(dir, { recursive: true, force: true });
  });
});
