// SPDX-License-Identifier: GPL-3.0-or-later
//
// WebGL2 at start-up (Linux). The 3D views need WebGL2, and Chromium's GPU
// blocklist turns it off on some drivers and in most virtual machines, where
// the app would start with a broken interface. main/index.ts asks before it
// makes a window: it starts again once with the flags that override the
// blocklist (software rendering when need be), and when that does not help
// either, says so and points to the page on the website.

/** The Chromium switches of the second try: the blocklist ignored, software WebGL allowed. */
export const GPU_FALLBACK_SWITCHES: readonly string[] = [
  '--ignore-gpu-blocklist',
  '--enable-unsafe-swiftshader',
];

/** The switch the second try is started with, so it is not tried a third time. */
export const GPU_RETRY_FLAG = '--shapelogic-gpu-retry';

/** What to do about WebGL2 when the app starts. */
export type GpuAction = 'continue' | 'retry' | 'explain';

/** Whether Chromium reports WebGL2 as usable ('enabled', 'enabled_force', …). */
export function webgl2Usable(status: string | undefined): boolean {
  return status?.startsWith('enabled') === true;
}

/**
 * What to do, given `app.getGPUFeatureStatus().webgl2` and the command line.
 * Only a packaged app on Linux is checked: macOS and Windows have a working
 * WebGL2 wherever the app runs, and development and the tests have their own
 * switches.
 */
export function gpuAction(
  status: string | undefined,
  argv: readonly string[],
  platform: NodeJS.Platform,
  packaged: boolean,
): GpuAction {
  if (platform !== 'linux' || !packaged || webgl2Usable(status)) {
    return 'continue';
  }
  // already started with the overrides, by the app's retry or by the user
  const overridden =
    argv.includes(GPU_RETRY_FLAG) || argv.some((a) => a.startsWith('--ignore-gpu-blocklist'));
  return overridden ? 'explain' : 'retry';
}

/** The command line of the second try: this one's arguments, the overrides and the retry flag. */
export function retryArgs(argv: readonly string[]): string[] {
  return [...argv.slice(1), ...GPU_FALLBACK_SWITCHES, GPU_RETRY_FLAG];
}
