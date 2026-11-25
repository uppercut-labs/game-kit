import { type ChildProcess, spawn } from 'node:child_process';
import { createWriteStream, type WriteStream } from 'node:fs';
import { copyFile, mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import { type CaptureConfig, run, takeCapture } from './capture.js';
import { LineReader, type LineEvent, type NoiseRule } from './lines.js';
import {
  type EmulatorProfile,
  LaunchError,
  type Settings,
  applyIni,
  applyKeyValue,
  expandArgs,
  fill,
  mergeSettings,
  renderFlags,
} from './profile.js';
import { getProfile } from './profiles.js';

export interface SeedFile {
  /** The file to copy from. */
  from: string;
  /** Where it goes, relative to the profile folder. A path that leaves the folder is refused. */
  to: string;
  /** Replace a file that is already there. Defaults to false: a seed never overwrites a profile's own state. */
  overwrite?: boolean;
  /** Skip the copy when `from` does not exist. Defaults to false. */
  optional?: boolean;
}

export interface QuitCommand {
  /** A command that asks this emulator to quit cleanly. `{pid}` is filled in. Run before any signal. */
  argv: readonly string[];
  /** How long to wait for the emulator to leave afterwards, in milliseconds. Defaults to 5000. */
  waitMs?: number;
}

export interface LaunchOptions {
  /** A built-in profile id (`ares`, `ymir`, `flycast`, `redream`, `ppsspp`, `duckstation-regtest`, `pcsx2`) or a profile. */
  profile: string | EmulatorProfile;
  /** The emulator program. Omit it for a profile that runs from its own copy (`portable`). */
  executable?: string;
  /** Arguments placed before the profile's own, such as an interpreter or a wrapper. */
  commandArgs?: readonly string[];
  /** The files to run: a ROM, a disc image, an ELF. They must exist unless `checkContent` is false. */
  content?: readonly string[];
  /** A system name the profile knows, such as `n64` for ares. */
  system?: string;
  /** A folder owned by this run. Required when the profile keeps settings or state in one. Created when missing. */
  profileDir?: string;
  /** Folders that must never be used as a profile, such as the real configuration folder. */
  refuseProfileDirs?: readonly string[];
  /** The emulator's install folder, for a `portable` profile. */
  installDir?: string;
  /** Files copied into the profile folder before the launch. */
  seedFiles?: readonly SeedFile[];
  /** Settings applied last, over the profile's defaults, background and mute settings. */
  settings?: Settings;
  /** Mute the emulator. Defaults to true. */
  mute?: boolean;
  /** Let the emulator run without focus. Defaults to true. */
  background?: boolean;
  /** Extra arguments for the profile's `{extra}` slot. */
  extraArgs?: readonly string[];
  /** Values for `{name}` placeholders in the profile. */
  vars?: Readonly<Record<string, string>>;
  /** Environment added to the process; a value of `undefined` removes a variable. */
  env?: Readonly<Record<string, string | undefined>>;
  cwd?: string;
  /** Lines to drop, added to the profile's own. */
  noise?: readonly NoiseRule[];
  /** Keep only these noise rules and ignore the profile's. */
  replaceNoise?: boolean;
  maxLineChars?: number;
  keepLines?: number;
  /**
   * Called for every output line that is not noise, from the first one. Use this instead of `session.onLine` when
   * no line may be missed: it is attached before the process starts.
   */
  onLine?: (event: LineEvent) => void;
  /** Write every output line, noise included, to this file. */
  rawLogPath?: string;
  /** Write the lines that are not noise to this file. */
  filteredLogPath?: string;
  /** The most bytes either log file may hold. Defaults to 16 MiB. */
  maxLogBytes?: number;
  /** Seconds before the first window can be captured. Defaults to the profile's own. */
  bootLatencySec?: number;
  capture?: CaptureConfig;
  /** How long to wait after SIGTERM before SIGKILL, in milliseconds. Defaults to 500. */
  graceMs?: number;
  quit?: QuitCommand;
  /** Runs once when the session stops, for cleanup the process id cannot do (removing a container). */
  onStop?: () => void | Promise<void>;
  /** Check that every content file exists before launching. Defaults to true, and to false for a headless profile. */
  checkContent?: boolean;
}

export interface ExitInfo {
  code: number | null;
  signal: NodeJS.Signals | null;
  /** Set when the process could not be signalled or never exited. */
  error?: Error;
}

export interface CaptureSpec {
  /** Seconds after the launch. */
  atSec: number;
  /** The file to write. */
  file: string;
}

export interface CaptureResult {
  file: string;
  atSec: number;
  /** Seconds after the launch at which the capture ran. */
  takenAtSec: number;
  /** True when the boot latency pushed the capture later than asked. */
  delayed: boolean;
  ok: boolean;
  windowId?: string;
  title?: string;
  error?: string;
}

function resolveInside(root: string, relative: string): string {
  if (path.isAbsolute(relative)) throw new LaunchError('PATH_OUTSIDE_PROFILE', `${relative} is not relative to the profile folder`);
  const full = path.resolve(root, relative);
  const back = path.relative(root, full);
  if (back === '' || back.startsWith('..') || path.isAbsolute(back)) {
    throw new LaunchError('PATH_OUTSIDE_PROFILE', `${relative} leaves the profile folder`);
  }
  return full;
}

async function exists(file: string): Promise<boolean> {
  return (await stat(file).catch(() => undefined)) !== undefined;
}

class LogSink {
  private readonly stream: WriteStream;
  private written = 0;
  private full = false;
  constructor(file: string, private readonly maxBytes: number) {
    this.stream = createWriteStream(file, { flags: 'w' });
    this.stream.on('error', () => {
      this.full = true;
    });
  }
  write(line: string): void {
    if (this.full) return;
    const text = `${line}\n`;
    const size = Buffer.byteLength(text);
    if (this.written + size > this.maxBytes) {
      this.full = true;
      this.stream.write('[log stopped: size limit reached]\n');
      return;
    }
    this.written += size;
    this.stream.write(text);
  }
  close(): Promise<void> {
    return new Promise((resolve) => {
      this.stream.end(() => resolve());
    });
  }
}

/** A running emulator. Make one with `launchEmulator`. */
export class EmulatorSession {
  readonly pid: number;
  readonly profile: EmulatorProfile;
  readonly executable: string;
  readonly argv: readonly string[];
  /** True when the launch carried a mute setting or variable. False when asked not to, or when none is known. */
  readonly muted: boolean;
  /** True when the launch carried the profile's background-running settings. */
  readonly background: boolean;
  readonly profileDir: string | undefined;
  /** Things the caller asked for that this emulator cannot do at launch. */
  readonly warnings: readonly string[];
  /** Settles when the process has ended and its output has been read. Never rejects. */
  readonly exited: Promise<ExitInfo>;
  private readonly child: ChildProcess;
  private readonly reader: LineReader;
  private readonly startedAt = performance.now();
  private readonly options: LaunchOptions;
  private exitInfo: ExitInfo | undefined;
  private stopping: Promise<ExitInfo> | undefined;

  /** @internal Use `launchEmulator`. */
  constructor(init: {
    child: ChildProcess;
    reader: LineReader;
    profile: EmulatorProfile;
    executable: string;
    argv: readonly string[];
    muted: boolean;
    background: boolean;
    profileDir: string | undefined;
    warnings: readonly string[];
    options: LaunchOptions;
    sinks: ReadonlyArray<{ close(): Promise<void> }>;
  }) {
    this.child = init.child;
    this.reader = init.reader;
    this.profile = init.profile;
    this.executable = init.executable;
    this.argv = init.argv;
    this.muted = init.muted;
    this.background = init.background;
    this.profileDir = init.profileDir;
    this.warnings = init.warnings;
    this.options = init.options;
    this.pid = init.child.pid ?? -1;
    const { child, reader, sinks } = init;
    this.exited = new Promise<ExitInfo>((resolve) => {
      let settled = false;
      const settle = (info: ExitInfo): void => {
        if (settled) return;
        settled = true;
        reader.end('stdout');
        reader.end('stderr');
        this.exitInfo = info;
        void Promise.all(sinks.map((sink) => sink.close())).then(() => resolve(info));
      };
      child.once('error', (error) => settle({ code: null, signal: null, error }));
      child.once('exit', (code, signal) => {
        // The pipes can still hold output when 'exit' fires; give them a moment, but never wait on a grandchild
        // that keeps them open.
        const timer = setTimeout(() => settle({ code, signal }), 250);
        child.once('close', () => {
          clearTimeout(timer);
          settle({ code, signal });
        });
      });
    });
  }

  get running(): boolean {
    return this.child.exitCode === null && this.child.signalCode === null && this.exitInfo === undefined;
  }

  /** Seconds since the launch. */
  elapsedSec(): number {
    return (performance.now() - this.startedAt) / 1000;
  }

  /** The kept output lines, noise removed, oldest first. */
  lines(): string[] {
    return this.reader.lines();
  }

  tail(count: number): string[] {
    return this.reader.tail(count);
  }

  get stats(): LineReader['stats'] {
    return this.reader.stats;
  }

  /** Adds a listener for output lines that are not noise. Returns a function that removes it. */
  onLine(listener: (event: LineEvent) => void): () => void {
    return this.reader.onLine(listener);
  }

  /**
   * Resolves with the first line that matches, from the kept lines or from new output. Rejects when the timeout
   * passes or the emulator exits first, naming the last lines it printed.
   */
  waitForLine(pattern: string | RegExp | ((line: string) => boolean), options: { timeoutMs?: number } = {}): Promise<string> {
    const test =
      typeof pattern === 'function'
        ? pattern
        : typeof pattern === 'string'
          ? (line: string): boolean => line.includes(pattern)
          : (line: string): boolean => new RegExp(pattern.source, pattern.flags.replace(/[gy]/g, '')).test(line);
    const already = this.reader.lines().find(test);
    if (already !== undefined) return Promise.resolve(already);
    return new Promise<string>((resolve, reject) => {
      const done = (): void => {
        remove();
        clearTimeout(timer);
      };
      const remove = this.reader.onLine((event) => {
        if (!test(event.line)) return;
        done();
        resolve(event.line);
      });
      const timer = setTimeout(() => {
        done();
        reject(new Error(`no matching line within ${options.timeoutMs ?? 30_000} ms; last lines: ${this.tail(5).join(' | ')}`));
      }, options.timeoutMs ?? 30_000);
      void this.exited.then(() => {
        done();
        reject(new Error(`the emulator exited before a matching line; last lines: ${this.tail(5).join(' | ')}`));
      });
    });
  }

  /** Waits until `sec` seconds after the launch, or until the emulator exits. Resolves true when the time came. */
  async waitUntil(sec: number): Promise<boolean> {
    for (;;) {
      if (!this.running) return false;
      const remaining = sec - this.elapsedSec();
      if (remaining <= 0) return true;
      await Promise.race([sleep(Math.min(remaining * 1000, 200)), this.exited]);
    }
  }

  /**
   * Takes window captures at the given seconds after the launch, never sooner than the boot latency. Results come
   * back in time order; a capture that fails is reported in its result, not thrown, so the rest still run.
   */
  async captureAt(captures: readonly CaptureSpec[]): Promise<CaptureResult[]> {
    const config = this.options.capture;
    if (config === undefined) throw new LaunchError('NO_CAPTURE_CONFIG', 'pass `capture` to launchEmulator to take captures');
    if (this.profile.headless === true) throw new LaunchError('HEADLESS', `${this.profile.label} opens no window; read its output files instead`);
    const floor = this.options.bootLatencySec ?? this.profile.bootLatencySec ?? 0;
    const results: CaptureResult[] = [];
    for (const spec of [...captures].sort((a, b) => a.atSec - b.atSec)) {
      const target = Math.max(spec.atSec, floor);
      const reached = await this.waitUntil(target);
      const base = { file: spec.file, atSec: spec.atSec, delayed: target > spec.atSec };
      if (!reached) {
        results.push({ ...base, takenAtSec: this.elapsedSec(), ok: false, error: 'the emulator exited before the capture' });
        continue;
      }
      await mkdir(path.dirname(path.resolve(spec.file)), { recursive: true });
      const outcome = await takeCapture(config, {
        pid: this.pid,
        ...(this.profile.windowOwner === undefined ? {} : { owner: this.profile.windowOwner }),
        out: spec.file,
      });
      results.push({ ...base, takenAtSec: this.elapsedSec(), ...outcome });
    }
    return results;
  }

  /**
   * Stops the emulator and only it: the quit command when one was given, then SIGTERM to the launched process, then
   * SIGKILL after `graceMs`. It never looks processes up by name. Safe to call more than once. A process the
   * emulator started itself is not signalled.
   */
  stop(): Promise<ExitInfo> {
    this.stopping ??= this.doStop();
    return this.stopping;
  }

  private hasEnded(): boolean {
    return this.exitInfo !== undefined || this.child.exitCode !== null || this.child.signalCode !== null;
  }

  private async doStop(): Promise<ExitInfo> {
    const wait = async (ms: number): Promise<void> => {
      await Promise.race([this.exited, sleep(ms)]);
    };
    let error: Error | undefined;
    try {
      if (!this.hasEnded() && this.options.quit !== undefined) {
        const [command, ...args] = this.options.quit.argv.map((part) => fill(part, { pid: String(this.pid) }));
        if (command !== undefined) await run(command, args).catch(() => '');
        await wait(this.options.quit.waitMs ?? 5000);
      }
      if (!this.hasEnded()) {
        this.signal('SIGTERM');
        await wait(this.options.graceMs ?? 500);
      }
      if (!this.hasEnded()) {
        this.signal('SIGKILL');
        await wait(2000);
      }
    } catch (caught) {
      error = caught instanceof Error ? caught : new Error(String(caught));
    }
    try {
      await this.options.onStop?.();
    } catch (caught) {
      error ??= caught instanceof Error ? caught : new Error(String(caught));
    }
    // The process may have ended just now, with its output still being read.
    const info = this.exitInfo ?? (await Promise.race([this.exited, sleep(3000).then(() => undefined)]));
    if (info === undefined) {
      return { code: null, signal: null, error: error ?? new Error(`process ${this.pid} did not exit after SIGKILL`) };
    }
    return error === undefined ? info : { ...info, error };
  }

  private signal(signal: NodeJS.Signals): void {
    try {
      this.child.kill(signal);
    } catch {
      // It ended between the check and the signal.
    }
  }
}

function pickProfile(profile: string | EmulatorProfile): EmulatorProfile {
  return typeof profile === 'string' ? getProfile(profile) : profile;
}

/** Starts an emulator and returns once the process exists. Throws `LaunchError` for anything wrong with the request. */
export async function launchEmulator(options: LaunchOptions): Promise<EmulatorSession> {
  const profile = pickProfile(options.profile);
  const warnings: string[] = [];
  const content = [...(options.content ?? [])];
  const profileDir = options.profileDir === undefined ? undefined : path.resolve(options.profileDir);

  if (profileDir !== undefined) {
    for (const refused of options.refuseProfileDirs ?? []) {
      const folder = path.resolve(refused);
      if (folder === profileDir || folder.startsWith(profileDir + path.sep)) {
        throw new LaunchError('PROFILE_DIR_REFUSED', `${profileDir} is, or holds, ${folder}; use a folder of its own`);
      }
    }
    await mkdir(profileDir, { recursive: true });
  }
  const needsProfile = profile.settings.kind === 'file' || profile.settings.kind === 'overlay-file' || profile.portable !== undefined;
  if (needsProfile && profileDir === undefined) {
    throw new LaunchError('PROFILE_DIR_REQUIRED', `${profile.label} keeps its settings in a profile folder; pass profileDir`);
  }

  if (options.checkContent ?? profile.headless !== true) {
    for (const file of content) {
      if (!(await exists(file))) throw new LaunchError('CONTENT_NOT_FOUND', `content file not found: ${file}`);
    }
  }

  // Seed files first, so settings edits land on the seeded copies.
  for (const seed of options.seedFiles ?? []) {
    if (profileDir === undefined) throw new LaunchError('PROFILE_DIR_REQUIRED', 'seedFiles need profileDir');
    const target = resolveInside(profileDir, seed.to);
    if (!(await exists(seed.from))) {
      if (seed.optional === true) continue;
      throw new LaunchError('SEED_NOT_FOUND', `seed file not found: ${seed.from}`);
    }
    if ((await exists(target)) && seed.overwrite !== true) continue;
    await mkdir(path.dirname(target), { recursive: true });
    await copyFile(seed.from, target);
  }

  let executable = options.executable;
  if (profile.portable !== undefined) {
    if (profileDir === undefined || options.installDir === undefined) {
      throw new LaunchError('INSTALL_DIR_REQUIRED', `${profile.label} runs from a copy in the profile folder; pass installDir and profileDir`);
    }
    for (const name of profile.portable.files) {
      const from = path.join(options.installDir, name);
      const to = resolveInside(profileDir, name);
      const source = await stat(from).catch(() => undefined);
      if (source === undefined) throw new LaunchError('INSTALL_FILE_NOT_FOUND', `not found in the install folder: ${from}`);
      const current = await stat(to).catch(() => undefined);
      if (current === undefined || current.size !== source.size) await copyFile(from, to);
    }
    executable = path.join(profileDir, profile.portable.executable);
  }
  if (executable === undefined || executable === '') throw new LaunchError('EXECUTABLE_REQUIRED', 'pass `executable`');

  // Settings.
  const wantMute = options.mute ?? true;
  const wantBackground = options.background ?? true;
  const system = options.system === undefined ? undefined : profile.systems?.[options.system];
  if (options.system !== undefined && profile.systems !== undefined && system === undefined) {
    throw new LaunchError('UNKNOWN_SYSTEM', `${profile.label} has no system "${options.system}"; known: ${Object.keys(profile.systems).join(', ')}`);
  }
  const mutes = wantMute ? profile.mute : profile.unmute;
  const muted = wantMute && (profile.mute !== undefined || profile.muteEnv !== undefined);
  if (wantMute && !muted) warnings.push(`${profile.label} has no known launch-time mute`);
  const backgroundSettings = wantBackground ? profile.background : undefined;
  const background = backgroundSettings !== undefined;
  if (wantBackground && !background) warnings.push(`${profile.label} has no known background-running setting`);
  const settings = mergeSettings(profile.defaults, system?.settings, mutes, backgroundSettings, options.settings);

  const vars: Record<string, string> = { ...(profile.vars ?? {}), ...(options.vars ?? {}) };
  if (profileDir !== undefined) vars.profileDir = profileDir;
  if (system !== undefined) vars.system = system.name;
  else if (options.system !== undefined) vars.system = options.system;

  let settingArgs: string[] = [];
  const entries = Object.keys(settings).length;
  switch (profile.settings.kind) {
    case 'none':
      if (entries > 0) throw new LaunchError('SETTINGS_UNSUPPORTED', `${profile.label} takes no launch settings; remove ${Object.keys(settings).join(', ')}`);
      break;
    case 'flags':
      settingArgs = renderFlags(profile.settings.flag, settings);
      break;
    case 'file': {
      if (entries === 0) break;
      const file = resolveInside(profileDir ?? '', profile.settings.path);
      await mkdir(path.dirname(file), { recursive: true });
      const before = (await exists(file)) ? await readFile(file, 'utf8') : '';
      const after = profile.settings.format === 'ini' ? applyIni(before, settings) : applyKeyValue(before, settings);
      if (after !== before) await writeFile(file, after, 'utf8');
      break;
    }
    case 'overlay-file': {
      const file = resolveInside(profileDir ?? '', profile.settings.path);
      await mkdir(path.dirname(file), { recursive: true });
      await writeFile(file, profile.settings.format === 'ini' ? applyIni('', settings) : applyKeyValue('', settings), 'utf8');
      settingArgs = entries === 0 ? [] : [fill(profile.settings.arg, { file })];
      break;
    }
  }

  const argv = [
    ...(options.commandArgs ?? []),
    ...expandArgs(profile.args, { vars, settings: settingArgs, content, extra: [...(options.extraArgs ?? [])] }),
  ];

  // Environment.
  const env: Record<string, string | undefined> = { ...process.env };
  for (const [name, value] of Object.entries(profile.env ?? {})) env[name] = fill(value, vars);
  if (wantMute) for (const [name, value] of Object.entries(profile.muteEnv ?? {})) env[name] = fill(value, vars);
  for (const [name, value] of Object.entries(options.env ?? {})) {
    if (value === undefined) delete env[name];
    else env[name] = value;
  }

  // Output.
  const maxLogBytes = options.maxLogBytes ?? 16 * 1024 * 1024;
  const sinks: LogSink[] = [];
  const raw = options.rawLogPath === undefined ? undefined : new LogSink(options.rawLogPath, maxLogBytes);
  const filtered = options.filteredLogPath === undefined ? undefined : new LogSink(options.filteredLogPath, maxLogBytes);
  if (raw !== undefined) sinks.push(raw);
  if (filtered !== undefined) sinks.push(filtered);
  const noise: NoiseRule[] = [...(options.replaceNoise === true ? [] : (profile.noise ?? [])), ...(options.noise ?? [])];
  const reader = new LineReader({
    noise,
    ...(options.maxLineChars === undefined ? {} : { maxLineChars: options.maxLineChars }),
    ...(options.keepLines === undefined ? {} : { keepLines: options.keepLines }),
    onRawLine: raw === undefined ? () => {} : (event) => raw.write(event.line),
    onLine: (event) => {
      filtered?.write(event.line);
      options.onLine?.(event);
    },
    onListenerError: (error) => warnings.push(`a line listener threw: ${error instanceof Error ? error.message : String(error)}`),
  });

  const child = await new Promise<ChildProcess>((resolve, reject) => {
    const started = spawn(executable, argv, {
      ...(options.cwd === undefined ? {} : { cwd: options.cwd }),
      env: env as NodeJS.ProcessEnv,
      stdio: ['ignore', 'pipe', 'pipe'],
      windowsHide: true,
    });
    started.once('error', (error) => reject(new LaunchError('SPAWN_FAILED', `could not start ${executable}: ${error.message}`)));
    started.once('spawn', () => resolve(started));
  }).catch(async (error: unknown) => {
    await Promise.all(sinks.map((sink) => sink.close()));
    throw error;
  });
  child.stdout?.on('data', (chunk: Buffer) => reader.push(chunk, 'stdout'));
  child.stderr?.on('data', (chunk: Buffer) => reader.push(chunk, 'stderr'));

  return new EmulatorSession({
    child,
    reader,
    profile,
    executable,
    argv,
    muted,
    background,
    profileDir,
    warnings,
    options,
    sinks,
  });
}

export interface RunPlan {
  /** Captures to take, in seconds after the launch. */
  captures?: readonly CaptureSpec[];
  /** Stop this many seconds after the launch, when the emulator is still running. */
  totalSec: number;
}

export interface RunResult {
  pid: number;
  argv: readonly string[];
  muted: boolean;
  background: boolean;
  warnings: readonly string[];
  captures: CaptureResult[];
  /** The kept output lines, noise removed. */
  lines: string[];
  stats: LineReader['stats'];
  exit: ExitInfo;
  /** True when the emulator was still running at `totalSec` and the session stopped it. */
  stoppedBySession: boolean;
  /** True when every capture worked. */
  ok: boolean;
}

/**
 * The whole loop of a capture script: launch, take the captures, wait for the total time, stop only the launched
 * process, and return the captures and the filtered output. The emulator is stopped even when a capture throws.
 */
export async function runAndCapture(options: LaunchOptions, plan: RunPlan): Promise<RunResult> {
  const session = await launchEmulator(options);
  try {
    const captures = plan.captures === undefined || plan.captures.length === 0 ? [] : await session.captureAt(plan.captures);
    await session.waitUntil(plan.totalSec);
    const stoppedBySession = session.running;
    const exit = await session.stop();
    return {
      pid: session.pid,
      argv: session.argv,
      muted: session.muted,
      background: session.background,
      warnings: session.warnings,
      captures,
      lines: session.lines(),
      stats: session.stats,
      exit,
      stoppedBySession,
      ok: captures.every((capture) => capture.ok),
    };
  } finally {
    await session.stop();
  }
}
