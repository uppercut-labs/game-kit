import { mkdir, mkdtemp, readdir, readFile } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import path from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

export interface ProcessRow {
  pid: number;
  /** The command line as one string, arguments separated by single spaces. */
  command: string;
}

/** Lists running processes with their command lines. Reads /proc on Linux and runs `ps` elsewhere on Unix. */
export async function listProcesses(): Promise<ProcessRow[]> {
  if (process.platform === 'linux') {
    const rows: ProcessRow[] = [];
    for (const entry of await readdir('/proc')) {
      if (!/^\d+$/.test(entry)) continue;
      try {
        const raw = await readFile(`/proc/${entry}/cmdline`, 'utf8');
        const command = raw.split('\0').filter((part, index, all) => part !== '' || index < all.length - 1).join(' ').trim();
        if (command !== '') rows.push({ pid: Number(entry), command });
      } catch {
        // The process ended while we looked.
      }
    }
    return rows;
  }
  if (process.platform === 'win32') throw new Error('listing processes by command line is not supported on Windows');
  const stdout = await new Promise<string>((resolve, reject) => {
    execFile('ps', ['-wwaxo', 'pid=,command='], { maxBuffer: 16 * 1024 * 1024 }, (error, out) => (error === null ? resolve(String(out)) : reject(error)));
  });
  return stdout.split('\n').flatMap((line) => {
    const match = /^\s*(\d+) (.*)$/.exec(line);
    return match === null ? [] : [{ pid: Number(match[1]), command: match[2] ?? '' }];
  });
}

export interface OwnedQuery {
  /** The executable as it appears at the start of the command line, spaces and all. */
  executable: string;
  /** A string only this run's command line holds, such as its private profile path. It cannot be empty. */
  marker: string;
}

/**
 * The pids of processes whose command line is the executable (bare, or followed by a space and arguments) and holds
 * the marker. This finds an emulator that a launcher started for you, such as one opened through the macOS `open`
 * command, whose pid you were never given. A bare executable name with no marker would match somebody else's
 * emulator, so the marker is required.
 */
export async function ownedPids(query: OwnedQuery, list: () => Promise<ProcessRow[]> = listProcesses): Promise<number[]> {
  if (query.executable === '') throw new RangeError('ownedPids needs an executable');
  if (query.marker === '') throw new RangeError('ownedPids needs a marker that belongs to this run; an empty one would match every instance');
  return (await list())
    .filter((row) => (row.command === query.executable || row.command.startsWith(`${query.executable} `)) && row.command.includes(query.marker))
    .map((row) => row.pid)
    .filter((pid) => pid !== process.pid);
}

export interface WaitOptions {
  timeoutMs?: number;
  intervalMs?: number;
  signal?: AbortSignal;
}

/** Polls `check` until it returns something other than `false`, `undefined`, `null` or `0`. Throws on timeout. */
export async function waitFor<T>(description: string, check: () => Promise<T | false | undefined | null | 0> | T | false | undefined | null | 0, options: WaitOptions = {}): Promise<T> {
  const deadline = Date.now() + (options.timeoutMs ?? 20_000);
  do {
    options.signal?.throwIfAborted();
    const found = await check();
    if (found !== false && found !== undefined && found !== null && found !== 0) return found;
    await sleep(options.intervalMs ?? 40, undefined, options.signal === undefined ? undefined : { signal: options.signal });
  } while (Date.now() < deadline);
  throw new Error(`Timed out waiting for ${description}`);
}

function signalPid(pid: number, signal: NodeJS.Signals): void {
  try {
    process.kill(pid, signal);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'ESRCH') throw error;
  }
}

/**
 * Stops the processes `ownedPids` finds: SIGTERM, then SIGKILL after `graceMs` (default 2500) for any that remain.
 * Only processes whose command line holds the marker are touched. Throws when one is still running afterwards.
 */
export async function stopOwned(query: OwnedQuery, options: { graceMs?: number; list?: () => Promise<ProcessRow[]> } = {}): Promise<void> {
  const grace = options.graceMs ?? 2500;
  const list = options.list ?? listProcesses;
  for (const pid of await ownedPids(query, list)) signalPid(pid, 'SIGTERM');
  const gone = async (): Promise<boolean> => (await ownedPids(query, list)).length === 0;
  try {
    await waitFor('owned emulator shutdown', gone, { timeoutMs: grace });
  } catch {
    for (const pid of await ownedPids(query, list)) signalPid(pid, 'SIGKILL');
    await waitFor('owned emulator shutdown', gone, { timeoutMs: grace });
  }
}

/** Makes a new empty folder named `<prefix>-<random>` under `base` (created when missing) and returns its path. */
export async function privateProfileDir(base: string, prefix: string): Promise<string> {
  await mkdir(base, { recursive: true });
  return mkdtemp(path.join(base, `${prefix}-`));
}
