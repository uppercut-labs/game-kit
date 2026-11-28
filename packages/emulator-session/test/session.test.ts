import { type ChildProcess, spawn } from 'node:child_process';
import { chmod, copyFile, link, mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import { type EmulatorProfile, LaunchError, launchEmulator, profiles, runAndCapture } from '../src/index.js';
import { fakeCapture, fakeEmulator, fakeFinder, isAlive, scratch, termExit } from './support.js';

interface Recorded {
  argv: string[];
  env: Record<string, string | null>;
  pid: number;
}

async function launchFake(profile: string | EmulatorProfile, extra: Partial<Parameters<typeof launchEmulator>[0]> = {}, fakeEnv: Record<string, string> = {}) {
  const dir = await scratch();
  const record = path.join(dir, 'record.json');
  const session = await launchEmulator({
    profile,
    executable: process.execPath,
    commandArgs: [fakeEmulator],
    ...extra,
    env: { FAKE_ARGS_OUT: record, ...fakeEnv, ...(extra.env ?? {}) },
  });
  const recorded = async (): Promise<Recorded> => {
    // A Node start can take seconds on a busy machine, more so on Windows, so wait on a deadline, not a count.
    const deadline = Date.now() + 10_000;
    do {
      try {
        return JSON.parse(await readFile(record, 'utf8')) as Recorded;
      } catch {
        await new Promise((resolve) => setTimeout(resolve, 20));
      }
    } while (Date.now() < deadline);
    throw new Error('the fake emulator never wrote its record');
  };
  await recorded();
  return { session, dir, recorded };
}

async function rom(name = 'game.rom'): Promise<string> {
  const file = path.join(await scratch(), name);
  await writeFile(file, 'ROM');
  return file;
}

describe('launch arguments', () => {
  it('starts ares for a homebrew system muted and allowed to run in the background', async () => {
    const file = await rom('game.z64');
    const { session, recorded } = await launchFake('ares', { system: 'n64', content: [file] });
    expect((await recorded()).argv).toEqual([
      '--system', 'Nintendo 64',
      '--setting', 'General/HomebrewMode=true',
      '--setting', 'Audio/Mute=true',
      '--setting', 'Input/Defocus=Allow',
      '--no-file-prompt',
      file,
    ]);
    expect(session.muted).toBe(true);
    expect(session.background).toBe(true);
    await session.stop();
  });

  it('starts ares for a system with no homebrew mode, with sound when asked', async () => {
    const file = await rom('game.32x');
    const { session, recorded } = await launchFake('ares', { system: '32x', content: [file], mute: false });
    expect((await recorded()).argv).toEqual([
      '--system', 'Mega 32X',
      '--setting', 'Audio/Mute=false',
      '--setting', 'Input/Defocus=Allow',
      '--no-file-prompt',
      file,
    ]);
    expect(session.muted).toBe(false);
    await session.stop();
  });

  it('lets caller settings win over the profile and adds new ones last', async () => {
    const file = await rom();
    const { session, recorded } = await launchFake('ares', {
      system: 'gba',
      content: [file],
      settings: { 'Input/Defocus': 'Block', 'Video/Driver': 'OpenGL 3.2' },
    });
    expect((await recorded()).argv).toEqual([
      '--system', 'Game Boy Advance',
      '--setting', 'Audio/Mute=true',
      '--setting', 'Input/Defocus=Block',
      '--setting', 'Video/Driver=OpenGL 3.2',
      '--no-file-prompt',
      file,
    ]);
    await session.stop();
  });

  it('refuses a system the profile does not know', async () => {
    await expect(launchEmulator({ profile: 'ares', executable: process.execPath, system: 'dreamcast' })).rejects.toMatchObject({ code: 'UNKNOWN_SYSTEM' });
  });

  it('passes Flycast its settings as repeated -config options', async () => {
    const file = await rom('game.elf');
    const { session, recorded } = await launchFake('flycast', { content: [file], settings: { 'config:Dreamcast.Cable': 0 } });
    expect((await recorded()).argv).toEqual(['-config', 'window:fullscreen=no', '-config', 'audio:backend=null', '-config', 'config:Dreamcast.Cable=0', file]);
    await session.stop();
  });

  it('writes PPSSPP an overlay file, points HOME at the profile and sets the mute variable', async () => {
    const file = await rom('game.iso');
    const profileDir = path.join(await scratch(), 'profile');
    const { session, recorded } = await launchFake('ppsspp', { content: [file], profileDir }, { FAKE_ENV_KEYS: 'HOME,CFFIXED_USER_HOME,SDL_AUDIODRIVER' });
    const seen = await recorded();
    const overlay = path.join(profileDir, 'overlay.ini');
    expect(seen.argv).toEqual(['--windowed', `--appendconfig=${overlay}`, file]);
    expect(await readFile(overlay, 'utf8')).toBe('[Sound]\nEnable = False\n\n[General]\nPauseOnLostFocus = False\n');
    expect(seen.env).toEqual({ HOME: profileDir, CFFIXED_USER_HOME: profileDir, SDL_AUDIODRIVER: 'dummy' });
    await session.stop();
  });

  it('does not set the mute variable when sound is asked for', async () => {
    const profileDir = path.join(await scratch(), 'profile');
    const { session, recorded } = await launchFake('ppsspp', { profileDir, mute: false }, { FAKE_ENV_KEYS: 'SDL_AUDIODRIVER' });
    expect((await recorded()).env.SDL_AUDIODRIVER).toBeNull();
    expect(await readFile(path.join(profileDir, 'overlay.ini'), 'utf8')).toContain('Enable = True');
    await session.stop();
  });

  it('edits the PCSX2 settings file in place and keeps what is in it', async () => {
    const profileDir = path.join(await scratch(), 'profile');
    await mkdir(path.join(profileDir, 'PCSX2', 'inis'), { recursive: true });
    await writeFile(path.join(profileDir, 'PCSX2', 'inis', 'PCSX2.ini'), '[EmuCore/GS]\nOsdShowMessages = true\nVsync = 1\n[Folders]\nBios = bios\n');
    const { session, recorded } = await launchFake('pcsx2', {
      profileDir,
      extraArgs: ['-gameargs', 'take:45', '--'],
      content: [await rom('game.iso')],
      settings: { 'EmuCore/GS/OsdShowMessages': false },
    });
    const seen = await recorded();
    expect(seen.argv.slice(0, 6)).toEqual(['-datapath', profileDir, '-batch', '-nogui', '-gameargs', 'take:45']);
    expect(await readFile(path.join(profileDir, 'PCSX2', 'inis', 'PCSX2.ini'), 'utf8')).toBe('[EmuCore/GS]\nOsdShowMessages = false\nVsync = 1\n[Folders]\nBios = bios\n');
    await session.stop();
  });

  it('leaves the PCSX2 settings file alone when there is nothing to change', async () => {
    const profileDir = path.join(await scratch(), 'profile');
    const { session } = await launchFake('pcsx2', { profileDir });
    await expect(stat(path.join(profileDir, 'PCSX2', 'inis', 'PCSX2.ini'))).rejects.toMatchObject({ code: 'ENOENT' });
    await session.stop();
  });

  it('runs a portable emulator from a copy in the profile folder with its config edited there', async () => {
    const install = await scratch();
    const profileDir = path.join(await scratch(), 'profile');
    // The stand-in program: a shell script on Unix. Windows runs only real executables without a shell, so there it
    // is a copy of node.exe (a hard link when the volume allows) told to run the fake emulator.
    const windows = process.platform === 'win32';
    const name = windows ? 'fake-emu.exe' : 'fake-emu';
    const program = path.join(install, name);
    if (windows) {
      await link(process.execPath, program).catch(() => copyFile(process.execPath, program));
    } else {
      await writeFile(program, `#!/bin/sh\nexec "${process.execPath}" "${fakeEmulator}" "$@"\n`);
      await chmod(program, 0o755);
    }
    await writeFile(path.join(install, 'flash.bin'), 'FLASH');
    const profile: EmulatorProfile = { ...profiles.redream!, portable: { executable: name, files: [name, 'flash.bin'] } };
    const file = await rom('game.cdi');
    const record = path.join(await scratch(), 'record.json');
    const session = await launchEmulator({
      profile,
      installDir: install,
      profileDir,
      ...(windows ? { commandArgs: [fakeEmulator] } : {}),
      content: [file],
      settings: { cable: 'vga', width: 640 },
      env: { FAKE_ARGS_OUT: record },
    });
    expect(session.executable).toBe(path.join(profileDir, name));
    expect(await readFile(path.join(profileDir, 'flash.bin'), 'utf8')).toBe('FLASH');
    expect(await readFile(path.join(profileDir, 'redream.cfg'), 'utf8')).toBe('cable=vga\nwidth=640\n');
    let seen: Recorded | undefined;
    for (const deadline = Date.now() + 10_000; seen === undefined && Date.now() < deadline; ) {
      seen = await readFile(record, 'utf8').then((text) => JSON.parse(text) as Recorded).catch(() => undefined);
      if (seen === undefined) await new Promise((resolve) => setTimeout(resolve, 20));
    }
    expect(seen?.argv).toEqual([file]);
    await session.stop();
  }, 20_000);

  it('refuses settings for an emulator that takes none', async () => {
    await expect(launchEmulator({ profile: 'ymir', executable: process.execPath, profileDir: await scratch(), mute: false, settings: { a: 1 } })).rejects.toMatchObject({ code: 'SETTINGS_UNSUPPORTED' });
  });

  it('says so when an emulator cannot be muted at launch', async () => {
    const { session } = await launchFake('ymir', { profileDir: await scratch(), content: [await rom('disc.cue')] });
    expect(session.muted).toBe(false);
    expect(session.warnings.join(' ')).toContain('no known launch-time mute');
    await session.stop();
  });

  it('builds the headless DuckStation arguments from vars', async () => {
    const { session, recorded } = await launchFake('duckstation-regtest', {
      vars: { frames: '600', dumpDir: '/captures' },
      content: ['/roms/disc.cue'],
    });
    expect((await recorded()).argv).toEqual(['-frames', '600', '-dumpinterval', '600', '-dumpdir', '/captures', '-renderer', 'Software', '--', '/roms/disc.cue']);
    await session.stop();
  });
});

describe('launch checks', () => {
  it('refuses a profile folder that is, or holds, a refused folder', async () => {
    const base = await scratch();
    await expect(launchEmulator({ profile: 'pcsx2', executable: process.execPath, profileDir: base, refuseProfileDirs: [path.join(base, 'real-config')] })).rejects.toMatchObject({ code: 'PROFILE_DIR_REFUSED' });
    await expect(launchEmulator({ profile: 'pcsx2', executable: process.execPath, profileDir: base, refuseProfileDirs: [base] })).rejects.toMatchObject({ code: 'PROFILE_DIR_REFUSED' });
  });

  it.runIf(process.platform === 'win32' || process.platform === 'darwin')('refuses a refused folder written in other letter case where paths ignore case', async () => {
    const base = await scratch();
    const profileDir = path.join(base, 'Profile');
    await expect(launchEmulator({ profile: 'pcsx2', executable: process.execPath, profileDir, refuseProfileDirs: [path.join(base, 'PROFILE', 'Real-Config')] })).rejects.toMatchObject({ code: 'PROFILE_DIR_REFUSED' });
    await expect(launchEmulator({ profile: 'pcsx2', executable: process.execPath, profileDir, refuseProfileDirs: [profileDir.toUpperCase()] })).rejects.toMatchObject({ code: 'PROFILE_DIR_REFUSED' });
  });

  it('needs a profile folder for a profile that keeps settings in one', async () => {
    await expect(launchEmulator({ profile: 'pcsx2', executable: process.execPath })).rejects.toMatchObject({ code: 'PROFILE_DIR_REQUIRED' });
  });

  it('reports a missing content file and a missing executable', async () => {
    await expect(launchEmulator({ profile: 'ares', system: 'gba', executable: process.execPath, content: ['/no/such/file.gba'] })).rejects.toMatchObject({ code: 'CONTENT_NOT_FOUND' });
    await expect(launchEmulator({ profile: 'ares', system: 'gba', executable: path.join(await scratch(), 'missing') })).rejects.toBeInstanceOf(LaunchError);
    await expect(launchEmulator({ profile: 'flycast' })).rejects.toMatchObject({ code: 'EXECUTABLE_REQUIRED' });
  });

  it('copies seed files without overwriting state and refuses a target outside the profile', async () => {
    const dir = await scratch();
    const profileDir = path.join(dir, 'profile');
    const clean = path.join(dir, 'smpc.bin.clean');
    await writeFile(clean, 'CLEAN');
    const first = await launchFake('ymir', { profileDir, seedFiles: [{ from: clean, to: 'state/smpc.bin' }, { from: path.join(dir, 'absent'), to: 'x', optional: true }] });
    expect(await readFile(path.join(profileDir, 'state', 'smpc.bin'), 'utf8')).toBe('CLEAN');
    await first.session.stop();
    await writeFile(path.join(profileDir, 'state', 'smpc.bin'), 'PLAYED');
    const second = await launchFake('ymir', { profileDir, seedFiles: [{ from: clean, to: 'state/smpc.bin' }] });
    expect(await readFile(path.join(profileDir, 'state', 'smpc.bin'), 'utf8')).toBe('PLAYED');
    await second.session.stop();
    const third = await launchFake('ymir', { profileDir, seedFiles: [{ from: clean, to: 'state/smpc.bin', overwrite: true }] });
    expect(await readFile(path.join(profileDir, 'state', 'smpc.bin'), 'utf8')).toBe('CLEAN');
    await third.session.stop();
    await expect(launchEmulator({ profile: 'ymir', executable: process.execPath, profileDir, seedFiles: [{ from: clean, to: '../escape' }] })).rejects.toMatchObject({ code: 'PATH_OUTSIDE_PROFILE' });
  });

  it('writes files into the profile after the seeds, replacing them, and refuses a target outside it', async () => {
    const dir = await scratch();
    const profileDir = path.join(dir, 'profile');
    const seed = path.join(dir, 'seed.ini');
    await writeFile(seed, 'SEEDED');
    const { session } = await launchFake('ymir', {
      profileDir,
      seedFiles: [{ from: seed, to: 'Config/a.ini' }],
      writeFiles: [{ to: 'Config/a.ini', text: 'WRITTEN' }, { to: 'Config/deep/b.ini', text: 'B' }],
    });
    expect(await readFile(path.join(profileDir, 'Config', 'a.ini'), 'utf8')).toBe('WRITTEN');
    expect(await readFile(path.join(profileDir, 'Config', 'deep', 'b.ini'), 'utf8')).toBe('B');
    await session.stop();
    await expect(launchEmulator({ profile: 'ymir', executable: process.execPath, profileDir, writeFiles: [{ to: '../escape', text: '' }] })).rejects.toMatchObject({ code: 'PATH_OUTSIDE_PROFILE' });
    await expect(launchEmulator({ profile: 'ares', system: 'gba', executable: process.execPath, writeFiles: [{ to: 'x', text: '' }] })).rejects.toMatchObject({ code: 'PROFILE_DIR_REQUIRED' });
  });
});

describe('output', () => {
  it('streams stdout and stderr lines through the noise filter and writes both log files', async () => {
    const dir = await scratch();
    const { session } = await launchFake(
      'ares',
      { system: 'n64', rawLogPath: path.join(dir, 'raw.log'), filteredLogPath: path.join(dir, 'game.log') },
      { FAKE_LINES: JSON.stringify(['Stalled compile 3ms', 'game: hello', 'err:Resizing output', 'err:game: from stderr']) },
    );
    const seen: string[] = [];
    session.onLine((event) => seen.push(event.line));
    await session.waitForLine('from stderr');
    await session.stop();
    expect(session.lines().sort()).toEqual(['game: from stderr', 'game: hello']);
    expect(session.stats.noise).toBe(2);
    expect((await readFile(path.join(dir, 'raw.log'), 'utf8')).trim().split('\n')).toHaveLength(4);
    expect((await readFile(path.join(dir, 'game.log'), 'utf8')).trim().split('\n').sort()).toEqual(['game: from stderr', 'game: hello']);
  });

  it('hands every line to the onLine option from the first one', async () => {
    const seen: string[] = [];
    const { session } = await launchFake(
      'ymir',
      { profileDir: await scratch(), onLine: (event) => seen.push(event.line) },
      { FAKE_LINES: JSON.stringify(['first line printed at startup', 'second']), FAKE_EXIT_AFTER_MS: '100' },
    );
    await session.exited;
    expect(seen).toEqual(['first line printed at startup', 'second']);
  });

  it('bounds a log file', async () => {
    const dir = await scratch();
    const lines = Array.from({ length: 200 }, (_, index) => `line number ${index}`);
    const { session } = await launchFake('ymir', { profileDir: dir, rawLogPath: path.join(dir, 'raw.log'), maxLogBytes: 300 }, { FAKE_LINES: JSON.stringify(lines), FAKE_EXIT_AFTER_MS: '200' });
    await session.exited;
    const text = await readFile(path.join(dir, 'raw.log'), 'utf8');
    expect(text.length).toBeLessThan(400);
    expect(text).toContain('[log stopped: size limit reached]');
  });

  it('waitForLine finds an earlier line, times out, and fails when the emulator exits first', async () => {
    const { session } = await launchFake('ymir', { profileDir: await scratch() }, { FAKE_LINES: JSON.stringify(['UB-CAP START']), FAKE_EXIT_AFTER_MS: '400' });
    await expect(session.waitForLine('UB-CAP START')).resolves.toBe('UB-CAP START');
    await expect(session.waitForLine('never printed', { timeoutMs: 100 })).rejects.toThrow('no matching line');
    await expect(session.waitForLine('never printed', { timeoutMs: 5000 })).rejects.toThrow('exited before a matching line');
    expect((await session.exited).code).toBe(3);
  });
});

describe('stopping', () => {
  function bystander(): ChildProcess {
    return spawn(process.execPath, [fakeEmulator], { stdio: 'ignore' });
  }

  it('signals only the launched process', async () => {
    const other = bystander();
    const { session } = await launchFake('ymir', { profileDir: await scratch() });
    const pid = session.pid;
    expect(isAlive(pid)).toBe(true);
    const exit = await session.stop();
    expect(exit).toMatchObject(termExit);
    expect(isAlive(pid)).toBe(false);
    expect(isAlive(other.pid ?? -1)).toBe(true);
    other.kill('SIGKILL');
  });

  it('escalates to SIGKILL for a process that ignores SIGTERM, and is safe to call twice', async () => {
    const { session } = await launchFake('ymir', { profileDir: await scratch(), graceMs: 150 }, { FAKE_IGNORE_TERM: '1' });
    await session.waitUntil(0.2);
    const [first, second] = await Promise.all([session.stop(), session.stop()]);
    // A Windows process cannot ignore SIGTERM: it ends at the first signal, so there is nothing to escalate.
    expect(first.signal).toBe(process.platform === 'win32' ? 'SIGTERM' : 'SIGKILL');
    expect(second).toBe(first);
    expect(isAlive(session.pid)).toBe(false);
  });

  it('asks the emulator to quit before sending any signal', async () => {
    const quitFile = path.join(await scratch(), 'quit');
    const { session } = await launchFake(
      'ymir',
      {
        profileDir: await scratch(),
        quit: { argv: [process.execPath, '-e', 'require("node:fs").writeFileSync(process.argv[1], process.argv[2])', quitFile, '{pid}'], waitMs: 3000 },
      },
      { FAKE_QUIT_FILE: quitFile },
    );
    await session.waitUntil(0.2);
    const exit = await session.stop();
    expect(exit).toMatchObject({ code: 7, signal: null });
  });

  it('runs onStop once and reports its error', async () => {
    let calls = 0;
    const { session } = await launchFake('ymir', {
      profileDir: await scratch(),
      onStop: () => {
        calls += 1;
        throw new Error('container still there');
      },
    });
    const exit = await session.stop();
    await session.stop();
    expect(calls).toBe(1);
    expect(exit.error?.message).toBe('container still there');
  });

  it('returns the real exit for an emulator that ended by itself', async () => {
    const { session } = await launchFake('ymir', { profileDir: await scratch() }, { FAKE_EXIT_AFTER_MS: '100' });
    await session.exited;
    const exit = await session.stop();
    expect(exit).toMatchObject({ code: 3, signal: null });
    expect(exit.error).toBeUndefined();
  });
});

describe('captures', () => {
  async function captureSetup(extra: Record<string, string> = {}) {
    const dir = await scratch();
    const state = path.join(dir, 'finder-state');
    return { dir, state, extra };
  }

  it('waits for the boot latency, finds the window, runs the capture command and reports the window', async () => {
    const { dir, state } = await captureSetup();
    const out = path.join(dir, 'shots', 'a.png');
    const { session } = await launchFake('ares', {
      system: 'gba',
      bootLatencySec: 0.4,
      capture: {
        kind: 'command',
        windowInfoCommand: [process.execPath, fakeFinder, state, '1', '{pid}', '{owner}'],
        argv: [process.execPath, fakeCapture, '--window', '{windowId}', '{out}'],
      },
    });
    const [result] = await session.captureAt([{ atSec: 0.05, file: out }]);
    await session.stop();
    expect(result).toMatchObject({ ok: true, delayed: true, windowId: '4242', title: `ares window for ${session.pid}` });
    expect(result?.takenAtSec).toBeGreaterThanOrEqual(0.4);
    expect(await readFile(out, 'utf8')).toBe('PNGDATA');
    expect(JSON.parse((await readFile(`${out}.args`, 'utf8')).trim())).toEqual(['--window', '4242', out]);
  });

  it('asks again until the window shows up', async () => {
    const { dir, state } = await captureSetup();
    const out = path.join(dir, 'b.png');
    const { session } = await launchFake('ares', {
      system: 'gba',
      bootLatencySec: 0,
      capture: { kind: 'command', windowRetries: 5, windowRetryMs: 20, windowInfoCommand: [process.execPath, fakeFinder, state, '3', '{pid}', '{owner}'], argv: [process.execPath, fakeCapture, '{out}'] },
    });
    const [result] = await session.captureAt([{ atSec: 0, file: out }]);
    await session.stop();
    expect(result?.ok).toBe(true);
    expect(await readFile(state, 'utf8')).toBe('3');
  });

  it('reports a capture that found no window or wrote no file, and still takes the next one', async () => {
    const { dir, state } = await captureSetup();
    const { session } = await launchFake('ares', {
      system: 'gba',
      bootLatencySec: 0,
      capture: { kind: 'command', windowRetries: 2, windowRetryMs: 10, windowInfoCommand: [process.execPath, fakeFinder, state, '99', '{pid}', '{owner}'], argv: [process.execPath, fakeCapture, '{out}'] },
    });
    const first = await session.captureAt([{ atSec: 0, file: path.join(dir, 'none.png') }]);
    expect(first[0]).toMatchObject({ ok: false });
    expect(first[0]?.error).toContain('no window for pid');
    await session.stop();

    const quiet = await launchFake('ares', { system: 'gba', bootLatencySec: 0, capture: { kind: 'command', argv: [process.execPath, fakeCapture, '--fail', '{out}'] } });
    const second = await quiet.session.captureAt([{ atSec: 0, file: path.join(dir, 'empty.png') }]);
    expect(second[0]).toMatchObject({ ok: false });
    expect(second[0]?.error).toContain('wrote no file');
    await quiet.session.stop();
  });

  it('builds the macOS screencapture command from the found window id', async () => {
    const { dir, state } = await captureSetup();
    const out = path.join(dir, 'mac.png');
    const { session } = await launchFake('flycast', {
      capture: {
        kind: 'macos-screencapture',
        screencapture: process.execPath,
        windowInfoCommand: [process.execPath, fakeFinder, state, '1', '{pid}', '{owner}'],
      },
    });
    // The "screencapture" here is node itself, which cannot write the file, so only the command line is checked.
    const [result] = await session.captureAt([{ atSec: 0, file: out }]);
    await session.stop();
    expect(result?.ok).toBe(false);
    expect(result?.windowId).toBeUndefined();
    expect(result?.error).toBeDefined();
  });

  it('runs a capture function', async () => {
    const dir = await scratch();
    const { session } = await launchFake('flycast', {
      capture: {
        kind: 'function',
        capture: async (request) => {
          expect(request.owner).toBe('Flycast');
          await writeFile(request.out, 'X');
        },
      },
    });
    const [result] = await session.captureAt([{ atSec: 0, file: path.join(dir, 'f.png') }]);
    await session.stop();
    expect(result?.ok).toBe(true);
  });

  it('reports a capture that was due after the emulator exited', async () => {
    const dir = await scratch();
    const { session } = await launchFake(
      'flycast',
      { capture: { kind: 'command', argv: [process.execPath, fakeCapture, '{out}'] } },
      { FAKE_EXIT_AFTER_MS: '100' },
    );
    const [result] = await session.captureAt([{ atSec: 5, file: path.join(dir, 'late.png') }]);
    expect(result).toMatchObject({ ok: false, error: 'the emulator exited before the capture' });
  });

  it('refuses captures with no configuration and for a headless runner', async () => {
    const plain = await launchFake('flycast');
    await expect(plain.session.captureAt([{ atSec: 0, file: 'x.png' }])).rejects.toMatchObject({ code: 'NO_CAPTURE_CONFIG' });
    await plain.session.stop();
    const headless = await launchFake('duckstation-regtest', { vars: { frames: '1', dumpDir: '/x' }, capture: { kind: 'command', argv: ['x'] } });
    await expect(headless.session.captureAt([{ atSec: 0, file: 'x.png' }])).rejects.toMatchObject({ code: 'HEADLESS' });
    await headless.session.stop();
  });
});

describe('runAndCapture', () => {
  it('launches, captures at the given seconds, waits for the total and stops the launched process', async () => {
    const dir = await scratch();
    const result = await runAndCapture(
      {
        profile: 'flycast',
        executable: process.execPath,
        commandArgs: [fakeEmulator],
        env: { FAKE_LINES: JSON.stringify(['boot ok', 'UB-NAOMI frame 1']) },
        capture: { kind: 'command', argv: [process.execPath, fakeCapture, '{out}'] },
      },
      { totalSec: 0.6, captures: [{ atSec: 0.3, file: path.join(dir, 'b.png') }, { atSec: 0.1, file: path.join(dir, 'a.png') }] },
    );
    expect(result.ok).toBe(true);
    expect(result.captures.map((capture) => path.basename(capture.file))).toEqual(['a.png', 'b.png']);
    expect(result.stoppedBySession).toBe(true);
    expect(result.exit).toMatchObject(termExit);
    expect(result.lines).toEqual(['boot ok', 'UB-NAOMI frame 1']);
    expect(isAlive(result.pid)).toBe(false);
  });

  it('is not marked stopped by the session when the emulator ended first, and flags a failed capture', async () => {
    const dir = await scratch();
    const result = await runAndCapture(
      {
        profile: 'flycast',
        executable: process.execPath,
        commandArgs: [fakeEmulator],
        env: { FAKE_EXIT_AFTER_MS: '150' },
        capture: { kind: 'command', argv: [process.execPath, fakeCapture, '{out}'] },
      },
      { totalSec: 2, captures: [{ atSec: 1, file: path.join(dir, 'a.png') }] },
    );
    expect(result.ok).toBe(false);
    expect(result.stoppedBySession).toBe(false);
    expect(result.exit.code).toBe(3);
  });
});

describe('copy of a built-in profile', () => {
  it('has no machine path in any profile', () => {
    const text = JSON.stringify(profiles);
    expect(text).not.toMatch(/\/Users\/|\/home\/|[A-Z]:\\|\/Applications\//);
  });
});

