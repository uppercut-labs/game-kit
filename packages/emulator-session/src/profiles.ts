import type { EmulatorProfile } from './profile.js';

/*
 * Launch recipes for the emulators that capture scripts drive, as plain data. Each one comes from a launch script
 * that ran. Nothing here names an install folder, a home folder or a drive: the caller passes `executable`,
 * `profileDir` and `installDir`.
 *
 * Where the scripts show no launch-time way to mute an emulator or let it run unfocused, the profile has no `mute`
 * or `background` and says so in `notes`; a session reports `muted: false` instead of claiming a silence it
 * cannot give.
 */

const ares: EmulatorProfile = {
  id: 'ares',
  label: 'ares',
  windowOwner: 'ares',
  args: ['--system', '{system}', '{settings}', '--no-file-prompt', '{content}'],
  settings: { kind: 'flags', flag: ['--setting', '{key}={value}'] },
  background: { 'Input/Defocus': 'Allow' },
  mute: { 'Audio/Mute': 'true' },
  unmute: { 'Audio/Mute': 'false' },
  systems: {
    n64: { name: 'Nintendo 64', settings: { 'General/HomebrewMode': 'true' } },
    n64dd: { name: 'Nintendo 64DD', settings: { 'General/HomebrewMode': 'true' } },
    '64dd': { name: 'Nintendo 64DD', settings: { 'General/HomebrewMode': 'true' } },
    '32x': { name: 'Mega 32X' },
    gba: { name: 'Game Boy Advance' },
    psx: { name: 'PlayStation' },
  },
  noise: ['Stalled compile', 'Resizing output'],
  bootLatencySec: 4.5,
  notes:
    'Homebrew mode makes the ISViewer port print to stdout. The window opens after about 3.5 to 4.5 seconds. A Mega 32X ' +
    'run only flushes its save RAM on a clean quit, so pass `quit` (a command that asks the app to quit) to launchEmulator.',
};

const ymir: EmulatorProfile = {
  id: 'ymir',
  label: 'Ymir',
  windowOwner: 'Ymir',
  args: ['-p', '{profileDir}', '-d', '{content}'],
  settings: { kind: 'none' },
  notes:
    'Takes its whole configuration from the profile folder, so seed that folder with `seedFiles` (a clean copy of ' +
    'its state files) instead of passing settings. No launch-time mute is known. The window title carries the frame ' +
    'rate, so a capture with a window finder that also prints the title records it.',
};

const flycast: EmulatorProfile = {
  id: 'flycast',
  label: 'Flycast',
  windowOwner: 'Flycast',
  args: ['{settings}', '{content}'],
  settings: { kind: 'flags', flag: ['-config', '{key}={value}'] },
  defaults: { 'window:fullscreen': 'no' },
  mute: { 'audio:backend': 'null' },
  notes:
    'A setting key is `section:key` as Flycast spells it, for example `config:Debug.SerialConsoleEnabled`. ' +
    'Set HOME in `env` to the profile folder to keep its data folder out of the real home folder.',
};

const redream: EmulatorProfile = {
  id: 'redream',
  label: 'redream',
  windowOwner: 'redream',
  args: ['{content}'],
  settings: { kind: 'file', path: 'redream.cfg', format: 'keyvalue' },
  portable: { executable: 'redream.exe', files: ['redream.exe', 'flash.bin'] },
  notes:
    'Runs from the folder it is in, so the profile folder gets its own copy of the program and its flash file from ' +
    '`installDir`, and its config is edited there. No launch-time mute or background setting is known. Windows only.',
};

const ppsspp: EmulatorProfile = {
  id: 'ppsspp',
  label: 'PPSSPP',
  windowOwner: 'PPSSPP',
  args: ['--windowed', '{settings}', '{content}'],
  settings: { kind: 'overlay-file', path: 'overlay.ini', format: 'ini', arg: '--appendconfig={file}' },
  background: { 'General/PauseOnLostFocus': 'False' },
  mute: { 'Sound/Enable': 'False' },
  unmute: { 'Sound/Enable': 'True' },
  env: { HOME: '{profileDir}', CFFIXED_USER_HOME: '{profileDir}' },
  muteEnv: { SDL_AUDIODRIVER: 'dummy' },
  notes:
    'Settings are written to an overlay file that is passed with --appendconfig, so the real configuration is not ' +
    'touched. HOME points at the profile folder, so its memory stick lives there. A remote debugger session is a ' +
    'separate WebSocket client and is not part of this package.',
};

const duckstationRegtest: EmulatorProfile = {
  id: 'duckstation-regtest',
  label: 'DuckStation regression runner',
  args: ['-frames', '{frames}', '-dumpinterval', '{frames}', '-dumpdir', '{dumpDir}', '-renderer', '{renderer}', '--', '{content}'],
  settings: { kind: 'none' },
  vars: { renderer: 'Software' },
  headless: true,
  notes:
    'Writes a PNG of the requested frame into `dumpDir` and opens no window, so there is nothing to capture with ' +
    'captureAt: read the frame file instead. Pass `frames` and `dumpDir` in `vars`. It can crash while shutting down ' +
    'after the PNG is written, so judge the run by the file. To run it in a container, pass the container command as ' +
    '`executable` and `commandArgs`, and a container removal as `onStop`; stopping only signals the client process.',
};

const pcsx2: EmulatorProfile = {
  id: 'pcsx2',
  label: 'PCSX2',
  windowOwner: 'PCSX2',
  args: ['-datapath', '{profileDir}', '-batch', '-nogui', '{extra}', '{content}'],
  settings: { kind: 'file', path: 'PCSX2/inis/PCSX2.ini', format: 'ini' },
  notes:
    'Boot with `extraArgs` such as `-elf <file>`, or `--` followed by a disc image in `content`; `-gameargs` passes ' +
    'arguments to the game. Its settings file is PCSX2/inis/PCSX2.ini under the data path. A setting key is ' +
    '`Section/Name`, for example `EmuCore/GS/OsdShowMessages`. The profile folder needs a BIOS and a seed settings ' +
    'file: copy them in with `seedFiles`. No launch-time mute is known.',
};

const dolphin: EmulatorProfile = {
  id: 'dolphin',
  label: 'Dolphin',
  windowOwner: 'Dolphin',
  args: ['--user', '{profileDir}', '--batch', '{settings}', '{extra}', '--exec', '{content}'],
  settings: { kind: 'flags', flag: ['--config', '{key}={value}'] },
  defaults: { 'Dolphin.Interface.ConfirmStop': 'False' },
  mute: { 'Dolphin.DSP.Backend': 'No Audio Output', 'Dolphin.DSP.Volume': 0 },
  unmute: { 'Dolphin.DSP.Backend': 'Cubeb', 'Dolphin.DSP.Volume': 100 },
  muteEnv: { SDL_AUDIODRIVER: 'dummy' },
  background: { 'Dolphin.Interface.PauseOnFocusLost': 'False', 'Dolphin.Input.BackgroundInput': 'True' },
  bootLatencySec: 3,
  notes:
    'The profile folder is Dolphin\'s user folder (`--user`), so its settings, logs and dumps stay in it. A setting key is ' +
    '`System.Section.Key` as Dolphin spells it, for example `GFX.Settings.AspectRatio`, passed with `--config`. ' +
    '`content` is the one file to boot with `--exec` (a DOL, an ELF or a disc image). `Interface.ConfirmStop` is off, so ' +
    'SIGTERM ends it at once instead of asking. Its log lines, OSReport included, go to stderr. `dolphinOptions` adds ' +
    'the graphics and log files, the dumps and DTM movie playback.',
};

const xemu: EmulatorProfile = {
  id: 'xemu',
  label: 'xemu',
  windowOwner: 'xemu',
  args: ['-config_path', '{profileDir}/xemu.toml', '{extra}', '-dvd_path', '{content}'],
  settings: { kind: 'file', path: 'xemu.toml', format: 'toml' },
  defaults: { 'general/show_welcome': false, 'general/skip_boot_anim': true },
  mute: { 'audio/volume_limit': 0 },
  unmute: { 'audio/volume_limit': 1 },
  muteEnv: { SDL_AUDIODRIVER: 'dummy' },
  background: { 'input/background_input_capture': true },
  env: { CFFIXED_USER_HOME: '{profileDir}' },
  bootLatencySec: 3,
  notes:
    'Settings go in `xemu.toml` in the profile folder, passed with `-config_path`. A setting key is `table/name`, for ' +
    'example `display.ui/aspect_ratio`, and values are written as TOML. `content` is one XISO for `-dvd_path`. The ' +
    'flash, hard disk and EEPROM images are the caller\'s: `xemuOptions` seeds them into the profile folder and points ' +
    'the settings at them. CFFIXED_USER_HOME points at the profile folder so xemu keeps its other files there.',
};

/** Every built-in profile, by id. */
export const profiles: Readonly<Record<string, EmulatorProfile>> = {
  ares,
  ymir,
  flycast,
  redream,
  ppsspp,
  'duckstation-regtest': duckstationRegtest,
  pcsx2,
  dolphin,
  xemu,
};

/** Looks up a built-in profile. Throws `LaunchError` for an id that is not one. */
export function getProfile(id: string): EmulatorProfile {
  const profile = profiles[id];
  if (profile === undefined) throw new Error(`unknown emulator profile "${id}"; known: ${Object.keys(profiles).join(', ')}`);
  return profile;
}
