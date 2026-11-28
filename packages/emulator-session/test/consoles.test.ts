import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import {
  DTM_HEADER_SIZE,
  type LaunchOptions,
  dolphinOptions,
  dolphinPaths,
  executableGameId,
  launchEmulator,
  parseDtmHeader,
  readDtmHeader,
  siDeviceSettings,
  xemuOptions,
} from '../src/index.js';
import { fakeEmulator, scratch } from './support.js';

interface Recorded {
  argv: string[];
  env: Record<string, string | null>;
}

async function launchRecorded(options: LaunchOptions, envKeys = '') {
  const record = path.join(await scratch(), 'record.json');
  const session = await launchEmulator({
    ...options,
    executable: process.execPath,
    commandArgs: [fakeEmulator],
    env: { ...(options.env ?? {}), FAKE_ARGS_OUT: record, FAKE_ENV_KEYS: envKeys },
  });
  for (let attempt = 0; attempt < 100; attempt += 1) {
    try {
      const recorded = JSON.parse(await readFile(record, 'utf8')) as Recorded;
      await session.stop();
      return { session, recorded };
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  await session.stop();
  throw new Error('the fake emulator never wrote its record');
}

function dtm(controllers: number, wii = false, gameId = 'GFLD01'): Uint8Array {
  const bytes = new Uint8Array(DTM_HEADER_SIZE + 8);
  bytes.set([0x44, 0x54, 0x4d, 0x1a], 0);
  bytes.set([...gameId].map((char) => char.charCodeAt(0)), 4);
  bytes[10] = wii ? 1 : 0;
  bytes[11] = controllers;
  const view = new DataView(bytes.buffer);
  view.setBigUint64(13, 600n, true);
  view.setBigUint64(21, 1200n, true);
  return bytes;
}

async function file(name: string, contents: string | Uint8Array = 'DATA'): Promise<string> {
  const target = path.join(await scratch(), name);
  await writeFile(target, contents);
  return target;
}

describe('DTM headers', () => {
  it('reads the game id, the controller bits and the counts', () => {
    expect(parseDtmHeader(dtm(0b0101))).toEqual({ gameId: 'GFLD01', wii: false, controllers: 5, fromSaveState: false, frames: 600n, inputs: 1200n });
  });

  it('refuses a short file and a file that is not a DTM', async () => {
    expect(() => parseDtmHeader(new Uint8Array(12))).toThrow('256 bytes');
    const bad = dtm(1);
    bad[3] = 0;
    expect(() => parseDtmHeader(bad)).toThrow('not a DTM');
    await expect(readDtmHeader(path.join(await scratch(), 'missing.dtm'))).rejects.toMatchObject({ code: 'MOVIE_NOT_FOUND' });
  });

  it('plugs a controller into each port the movie uses and nothing into the others', () => {
    expect(siDeviceSettings(0b1010)).toEqual({
      'Dolphin.Core.SIDevice0': 0,
      'Dolphin.Core.SIDevice1': 6,
      'Dolphin.Core.SIDevice2': 0,
      'Dolphin.Core.SIDevice3': 6,
    });
  });
});

describe('Dolphin', () => {
  it('starts muted and backgrounded in its own user folder', async () => {
    const dol = await file('game.dol');
    const profileDir = path.join(await scratch(), 'user');
    const { session, recorded } = await launchRecorded({ profile: 'dolphin', profileDir, content: [dol] }, 'SDL_AUDIODRIVER');
    expect(recorded.argv).toEqual([
      '--user', profileDir,
      '--batch',
      '--config', 'Dolphin.Interface.ConfirmStop=False',
      '--config', 'Dolphin.DSP.Backend=No Audio Output',
      '--config', 'Dolphin.DSP.Volume=0',
      '--config', 'Dolphin.Interface.PauseOnFocusLost=False',
      '--config', 'Dolphin.Input.BackgroundInput=True',
      '--exec', dol,
    ]);
    expect(recorded.env.SDL_AUDIODRIVER).toBe('dummy');
    expect(session.muted).toBe(true);
    expect(session.background).toBe(true);
  });

  it('writes the graphics and log files, turns on the dumps and plays a movie on the ports it names', async () => {
    const dol = await file('game.dol');
    const movie = await file('input.dtm', dtm(0b0011, false, 'ID-gam'));
    const profileDir = path.join(await scratch(), 'user');
    const options = await dolphinOptions({
      profileDir,
      content: [dol],
      mute: false,
      graphics: { 'Settings/AspectRatio': 1 },
      osReport: true,
      dumpAudio: true,
      dumpFrames: true,
      movie,
      settings: { 'Dolphin.Core.SIDevice1': 0 },
    });
    const { recorded } = await launchRecorded(options, 'SDL_AUDIODRIVER');
    expect(recorded.argv).toEqual([
      '--user', profileDir,
      '--batch',
      '--config', 'Dolphin.Interface.ConfirmStop=False',
      '--config', 'Dolphin.DSP.Backend=Cubeb',
      '--config', 'Dolphin.DSP.Volume=100',
      '--config', 'Dolphin.Interface.PauseOnFocusLost=False',
      '--config', 'Dolphin.Input.BackgroundInput=True',
      '--config', 'Dolphin.DSP.DumpAudio=True',
      '--config', 'Dolphin.DSP.DumpAudioSilent=True',
      '--config', 'Dolphin.Movie.DumpFrames=True',
      '--config', 'Dolphin.Movie.DumpFramesSilent=True',
      '--config', 'Dolphin.Core.SIDevice0=6',
      '--config', 'Dolphin.Core.SIDevice1=0',
      '--config', 'Dolphin.Core.SIDevice2=0',
      '--config', 'Dolphin.Core.SIDevice3=0',
      '--movie', movie,
      '--exec', dol,
    ]);
    expect(recorded.env.SDL_AUDIODRIVER).toBeNull();
    expect(await readFile(path.join(profileDir, 'Config', 'GFX.ini'), 'utf8')).toBe('[Settings]\nAspectRatio = 1\n');
    const logger = await readFile(path.join(profileDir, 'Config', 'Logger.ini'), 'utf8');
    expect(logger).toContain('WriteToFile = True');
    expect(logger).toContain('[Logs]\nOSREPORT = True\nOSREPORT_HLE = True\n');
    expect(dolphinPaths(profileDir).log).toBe(path.join(profileDir, 'Logs', 'dolphin.log'));
  });

  it('replaces the graphics file on every launch', async () => {
    const profileDir = path.join(await scratch(), 'user');
    await mkdir(path.join(profileDir, 'Config'), { recursive: true });
    await writeFile(path.join(profileDir, 'Config', 'GFX.ini'), '[Settings]\nAspectRatio = 3\nOld = 1\n');
    await launchRecorded(await dolphinOptions({ profileDir, content: [await file('game.dol')], graphics: { 'Settings/AspectRatio': 2 } }));
    expect(await readFile(path.join(profileDir, 'Config', 'GFX.ini'), 'utf8')).toBe('[Settings]\nAspectRatio = 2\n');
  });

  it('refuses a movie made for another game id, unless the check is off', async () => {
    expect(executableGameId('/builds/shapes-demo.dol')).toBe('ID-sha');
    expect(executableGameId('ab.elf')).toBe('ID-ab');
    const profileDir = await scratch();
    const dol = await file('shapes.dol');
    const movie = await file('other.dtm', dtm(1, false, 'GFLD01'));
    await expect(dolphinOptions({ profileDir, content: [dol], movie })).rejects.toThrow('"ID-sha"');
    const options = await dolphinOptions({ profileDir, content: [dol], movie, checkMovieGameId: false });
    expect(options.extraArgs).toEqual(['--movie', movie]);
    const disc = await dolphinOptions({ profileDir, content: [await file('game.iso')], movie });
    expect(disc.extraArgs).toEqual(['--movie', movie]);
  });

  it('refuses anything but one file to boot, and a Wii movie', async () => {
    const profileDir = await scratch();
    await expect(dolphinOptions({ profileDir })).rejects.toMatchObject({ code: 'CONTENT_REQUIRED' });
    await expect(dolphinOptions({ profileDir, content: ['a.dol', 'b.dol'] })).rejects.toMatchObject({ code: 'CONTENT_REQUIRED' });
    await expect(dolphinOptions({ profileDir, content: ['a.dol'], movie: await file('wii.dtm', dtm(0x10, true)) })).rejects.toMatchObject({ code: 'BAD_MOVIE' });
  });
});

describe('xemu', () => {
  it('seeds the fixture images, writes xemu.toml pointing at them and boots the XISO', async () => {
    const iso = await file('game.iso');
    const flash = await file('bios.bin', 'FLASH');
    const hdd = await file('hdd.qcow2', 'HDD');
    const eeprom = await file('eeprom.bin', 'EEPROM');
    const profileDir = path.join(await scratch(), 'xbox');
    const options = xemuOptions({ profileDir, content: [iso], flash, hdd, eeprom, aspectRatio: 'native', windowSize: '640x480' });
    const { session, recorded } = await launchRecorded(options, 'SDL_AUDIODRIVER,CFFIXED_USER_HOME');
    expect(recorded.argv).toEqual([
      '-config_path', `${profileDir}/xemu.toml`,
      '-snapshot',
      '-device', 'lpc47m157',
      '-serial', `file:${path.join(profileDir, 'serial.log')}`,
      '-dvd_path', iso,
    ]);
    expect(recorded.env).toEqual({ SDL_AUDIODRIVER: 'dummy', CFFIXED_USER_HOME: profileDir });
    expect(await readFile(path.join(profileDir, 'flash.bin'), 'utf8')).toBe('FLASH');
    expect(await readFile(path.join(profileDir, 'xbox_hdd.qcow2'), 'utf8')).toBe('HDD');
    expect(await readFile(path.join(profileDir, 'eeprom.bin'), 'utf8')).toBe('EEPROM');
    const toml = await readFile(path.join(profileDir, 'xemu.toml'), 'utf8');
    const quoted = (name: string): string => JSON.stringify(path.join(profileDir, name));
    expect(toml).toBe(
      [
        '[general]',
        'show_welcome = false',
        'skip_boot_anim = true',
        '',
        '[audio]',
        'volume_limit = 0',
        '',
        '[input]',
        'background_input_capture = true',
        '',
        '[display.ui]',
        'fit = "scale"',
        'aspect_ratio = "native"',
        '',
        '[sys]',
        'mem_limit = "64"',
        '',
        '[sys.files]',
        'bootrom_path = ""',
        `flashrom_path = ${quoted('flash.bin')}`,
        `hdd_path = ${quoted('xbox_hdd.qcow2')}`,
        `eeprom_path = ${quoted('eeprom.bin')}`,
        '',
        '[display.window]',
        'startup_size = "640x480"',
        '',
      ].join('\n'),
    );
    expect(session.muted).toBe(true);
  });

  it('keeps the profile\'s own images and edits the settings xemu wrote back', async () => {
    const profileDir = path.join(await scratch(), 'xbox');
    await mkdir(profileDir, { recursive: true });
    await writeFile(path.join(profileDir, 'xbox_hdd.qcow2'), 'PLAYED');
    await writeFile(path.join(profileDir, 'xemu.toml'), "[general]\nshow_welcome = false\n\n[audio]\nvolume_limit = 0\n\n[display.window]\nlast_width = 1280\n");
    const options = xemuOptions({
      profileDir,
      content: [await file('game.iso')],
      flash: await file('bios.bin'),
      hdd: await file('hdd.qcow2', 'CLEAN'),
      mcpx: await file('mcpx.bin', 'MCPX'),
      mute: false,
      serialLog: false,
      snapshot: false,
    });
    const { recorded } = await launchRecorded(options, 'SDL_AUDIODRIVER');
    expect(recorded.argv).toEqual(['-config_path', `${profileDir}/xemu.toml`, '-dvd_path', options.content?.[0]]);
    expect(recorded.env.SDL_AUDIODRIVER).toBeNull();
    expect(await readFile(path.join(profileDir, 'xbox_hdd.qcow2'), 'utf8')).toBe('PLAYED');
    const toml = await readFile(path.join(profileDir, 'xemu.toml'), 'utf8');
    expect(toml).toContain('[audio]\nvolume_limit = 1\n');
    expect(toml).toContain('last_width = 1280');
    expect(toml).toContain(`bootrom_path = ${JSON.stringify(path.join(profileDir, 'mcpx.bin'))}`);
  });

  it('refuses anything but one XISO', () => {
    expect(() => xemuOptions({ profileDir: '/p', flash: 'f', hdd: 'h' })).toThrow('only entry in content');
  });
});
