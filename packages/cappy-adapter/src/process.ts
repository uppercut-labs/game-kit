import { type ChildProcess, spawn } from 'node:child_process';
import path from 'node:path';
import { OutputRing } from './ring.js';

export interface ProcessOptions {
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  /** How many trailing characters of output to keep. Defaults to 4000. */
  ringChars?: number;
  /** `both` (the default) keeps stdout and stderr in one ring; `stderr` keeps stderr only and ignores stdout. */
  capture?: 'both' | 'stderr';
  /** Hide the console window on Windows. Defaults to true. */
  windowsHide?: boolean;
  /** How long to wait, after the process exits, for its output to finish arriving. Defaults to 250 ms. */
  drainMs?: number;
}

export interface ExitInfo {
  /** The exit code, or null when a signal ended the process or it never started. */
  code: number | null;
  signal: NodeJS.Signals | null;
  /** Set when the process could not be started or could not be signalled, for example `ENOENT`. */
  error?: Error;
}

/** A started process: the child, the ring that holds its tail output, and a promise for how it ended. */
export interface ManagedProcess {
  readonly child: ChildProcess;
  readonly output: OutputRing;
  /** Settles once, when the process exits or fails to start. Never rejects. */
  readonly exit: Promise<ExitInfo>;
}

/** A command that ended with a non-zero code or a signal. `output` is the tail the ring kept. */
export class ProcessExitError extends Error {
  readonly command: string;
  readonly args: readonly string[];
  readonly code: number | null;
  readonly signal: NodeJS.Signals | null;
  readonly output: string;

  constructor(command: string, args: readonly string[], info: ExitInfo, output: OutputRing) {
    const name = `${path.basename(command)} ${args[0] ?? ''}`.trim();
    const tail = output.tail(3).join(' | ');
    const how = info.signal === null ? `exited ${String(info.code)}` : `was stopped by ${info.signal}`;
    super(tail === '' ? `${name} ${how}` : `${name} ${how}: ${tail}`);
    this.name = 'ProcessExitError';
    this.command = command;
    this.args = args;
    this.code = info.code;
    this.signal = info.signal;
    this.output = output.toString();
  }
}

/**
 * Starts child processes and remembers the ones still running, so a shutdown can stop them all. Output is kept in a
 * ring, so a failure can name the last lines a command printed.
 *
 * `kill` signals the direct child only. A launcher that starts further processes must pass the signal on itself.
 */
export class ProcessSupervisor {
  private readonly running = new Set<ManagedProcess>();

  /** How many started processes have not exited yet. */
  get size(): number {
    return this.running.size;
  }

  /** Starts a process and returns at once. */
  start(command: string, args: readonly string[] = [], options: ProcessOptions = {}): ManagedProcess {
    const output = new OutputRing(options.ringChars);
    const capture = options.capture ?? 'both';
    const child = spawn(command, [...args], {
      cwd: options.cwd,
      env: options.env,
      stdio: ['ignore', capture === 'both' ? 'pipe' : 'ignore', 'pipe'],
      windowsHide: options.windowsHide ?? true,
    });
    child.stdout?.on('data', (chunk: Buffer) => output.push(chunk));
    child.stderr?.on('data', (chunk: Buffer) => output.push(chunk));
    const drainMs = options.drainMs ?? 250;
    const exit = new Promise<ExitInfo>((resolve) => {
      let settled = false;
      const settle = (info: ExitInfo): void => {
        if (settled) return;
        settled = true;
        this.running.delete(managed);
        resolve(info);
      };
      child.once('error', (error) => settle({ code: null, signal: null, error }));
      child.once('exit', (code, signal) => {
        // The streams can still hold output when 'exit' fires; give them a moment, but never wait on a grandchild
        // that keeps the pipes open.
        const timer = setTimeout(() => settle({ code, signal }), drainMs);
        child.once('close', () => {
          clearTimeout(timer);
          settle({ code, signal });
        });
      });
    });
    const managed: ManagedProcess = { child, output, exit };
    this.running.add(managed);
    return managed;
  }

  /** Runs a command to the end. Resolves with its output tail on exit code 0, else rejects with a `ProcessExitError`. */
  async run(command: string, args: readonly string[] = [], options: ProcessOptions = {}): Promise<string> {
    const managed = this.start(command, args, options);
    const info = await managed.exit;
    if (info.error !== undefined) throw info.error;
    if (info.code !== 0) throw new ProcessExitError(command, args, info, managed.output);
    return managed.output.toString();
  }

  /** Sends a signal to every process that is still running. */
  killAll(signal: NodeJS.Signals = 'SIGTERM'): void {
    for (const managed of this.running) {
      try {
        managed.child.kill(signal);
      } catch {
        // The process ended between the check and the signal.
      }
    }
  }
}
