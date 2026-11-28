import path from 'node:path';
import { type EmulatorProfile, LaunchError, type SettingValue, mergeSettings } from './profile.js';
import type { LaunchOptions, SeedFile } from './session.js';

/** The names the fixture images get inside the profile folder. */
export const XEMU_FILES = { flash: 'flash.bin', hdd: 'xbox_hdd.qcow2', eeprom: 'eeprom.bin', mcpx: 'mcpx.bin' } as const;

export interface XemuOptions extends Omit<LaunchOptions, 'profile' | 'profileDir'> {
  /** The folder for this run. The fixture images are copied into it and `xemu.toml` lives in it. */
  profileDir: string;
  /** The profile to start from. Defaults to the built-in `xemu`. */
  profile?: EmulatorProfile;
  /** The flash (BIOS) image. Copied to `flash.bin` unless the profile already has one. */
  flash: string;
  /** The hard disk image. Copied to `xbox_hdd.qcow2` unless the profile already has one. */
  hdd: string;
  /** The EEPROM image. Copied to `eeprom.bin` when given; otherwise xemu makes one there. */
  eeprom?: string;
  /** The MCPX boot ROM. Copied to `mcpx.bin` when given; otherwise the boot ROM path is left empty. */
  mcpx?: string;
  /** `display.ui/aspect_ratio`: `native`, `auto`, `4x3` or `16x9`. */
  aspectRatio?: string;
  /** `display.window/startup_size`, such as `640x480` or `1280x720`. */
  windowSize?: string;
  /** `display.ui/fit`: how the picture fills the window. Defaults to `scale`. */
  fit?: string;
  /** `sys/mem_limit`: `64` or `128` (MiB). Defaults to `64`, a retail console. */
  memory?: '64' | '128';
  /**
   * Where the game's serial port output goes: a file path, or false for none. Defaults to `serial.log` in the profile
   * folder. It is the SuperIO serial port (`-device lpc47m157`).
   */
  serialLog?: string | false;
  /** Keep the hard disk image unchanged by the run (`-snapshot`). Defaults to true. */
  snapshot?: boolean;
}

/**
 * Builds the launch options for an xemu run: the `xemu` profile, the fixture images seeded into the profile folder,
 * `xemu.toml` pointed at them, the display settings, and the serial port and snapshot arguments. Everything else in
 * `options` is passed through, and `settings` given here win over the ones this adds. Throws `LaunchError` for a
 * content list that is not one XISO.
 */
export function xemuOptions(options: XemuOptions): LaunchOptions {
  const { flash, hdd, eeprom, mcpx, aspectRatio, windowSize, fit, memory, serialLog, snapshot, profile, ...rest } = options;
  if (rest.content === undefined || rest.content.length !== 1) {
    throw new LaunchError('CONTENT_REQUIRED', 'xemu boots one XISO with -dvd_path; pass it as the only entry in content');
  }
  const profileDir = path.resolve(options.profileDir);
  const inside = (name: string): string => path.join(profileDir, name);
  const seeds: SeedFile[] = [
    { from: flash, to: XEMU_FILES.flash },
    { from: hdd, to: XEMU_FILES.hdd },
    ...(eeprom === undefined ? [] : [{ from: eeprom, to: XEMU_FILES.eeprom }]),
    ...(mcpx === undefined ? [] : [{ from: mcpx, to: XEMU_FILES.mcpx }]),
  ];
  const added: Record<string, SettingValue> = {
    'display.ui/fit': fit ?? 'scale',
    'sys/mem_limit': memory ?? '64',
    'sys.files/bootrom_path': mcpx === undefined ? '' : inside(XEMU_FILES.mcpx),
    'sys.files/flashrom_path': inside(XEMU_FILES.flash),
    'sys.files/hdd_path': inside(XEMU_FILES.hdd),
    'sys.files/eeprom_path': inside(XEMU_FILES.eeprom),
  };
  if (aspectRatio !== undefined) added['display.ui/aspect_ratio'] = aspectRatio;
  if (windowSize !== undefined) added['display.window/startup_size'] = windowSize;

  const extraArgs = [...(rest.extraArgs ?? [])];
  if (snapshot ?? true) extraArgs.push('-snapshot');
  if (serialLog !== false) extraArgs.push('-device', 'lpc47m157', '-serial', `file:${path.resolve(serialLog ?? inside('serial.log'))}`);

  return {
    ...rest,
    profile: profile ?? 'xemu',
    profileDir,
    seedFiles: [...(rest.seedFiles ?? []), ...seeds],
    settings: mergeSettings(added, rest.settings),
    extraArgs,
  };
}
