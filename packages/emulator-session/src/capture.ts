import { execFile } from 'node:child_process';
import { stat } from 'node:fs/promises';
import { setTimeout as sleep } from 'node:timers/promises';
import { LaunchError, fill } from './profile.js';

export interface CaptureRequest {
  /** The process id of the emulator the session launched. */
  pid: number;
  /** The owner name the profile gives for window lookups, when it has one. */
  owner?: string;
  /** The file to write. */
  out: string;
  /** The window id the finder returned, when a finder ran. */
  windowId?: string;
  /** The window title the finder returned after the id, when it printed one. */
  title?: string;
}

interface FinderOptions {
  /**
   * A command that prints the emulator's window on its first line as `<id>` or `<id> <title>`. `{pid}` and `{owner}`
   * are filled in. It must only look: it must not raise or activate the window.
   */
  windowInfoCommand?: readonly string[];
  /** How many times to ask for the window before giving up. Defaults to 5. */
  windowRetries?: number;
  /** How long to wait between asks, in milliseconds. Defaults to 300. */
  windowRetryMs?: number;
}

/**
 * How to take a window picture. None of these may raise, activate or focus the window: a capture that needs the
 * window in front is a capture of whatever is in front.
 */
export type CaptureConfig =
  | ({ kind: 'macos-screencapture'; screencapture?: string } & FinderOptions & { windowInfoCommand: readonly string[] })
  | ({ kind: 'command'; argv: readonly string[] } & FinderOptions)
  | { kind: 'function'; capture: (request: CaptureRequest) => Promise<void> };

export interface CaptureOutcome {
  ok: boolean;
  windowId?: string;
  title?: string;
  error?: string;
}

export function run(command: string, args: readonly string[], timeoutMs = 15_000): Promise<string> {
  return new Promise((resolve, reject) => {
    execFile(command, [...args], { timeout: timeoutMs, windowsHide: true, maxBuffer: 1024 * 1024 }, (error, stdout, stderr) => {
      if (error !== null) {
        const detail = String(stderr).trim().split(/\r?\n/).slice(-1)[0] ?? '';
        reject(new Error(detail === '' ? error.message : `${error.message}: ${detail}`));
      } else {
        resolve(String(stdout));
      }
    });
  });
}

function parseWindowInfo(output: string): { id: string; title?: string } | undefined {
  const first = output.split(/\r?\n/).find((line) => line.trim() !== '');
  const match = first === undefined ? null : /^\s*(\d+)(?:\s+(.*))?$/.exec(first);
  if (match === null) return undefined;
  const title = match[2]?.trim();
  return title === undefined || title === '' ? { id: match[1] ?? '' } : { id: match[1] ?? '', title };
}

function template(argv: readonly string[], vars: Record<string, string>): string[] {
  return argv.map((part) => fill(part, vars));
}

/** Takes one capture. Never throws for a failed capture; the outcome says what went wrong. */
export async function takeCapture(config: CaptureConfig, request: CaptureRequest): Promise<CaptureOutcome> {
  try {
    if (config.kind === 'function') {
      await config.capture(request);
      return await verified(request.out, {});
    }
    const vars: Record<string, string> = { pid: String(request.pid), owner: request.owner ?? '', out: request.out };
    let windowId: string | undefined;
    let title: string | undefined;
    if (config.windowInfoCommand !== undefined) {
      const [command, ...args] = template(config.windowInfoCommand, vars);
      if (command === undefined) throw new LaunchError('BAD_CAPTURE_CONFIG', 'windowInfoCommand is empty');
      const retries = config.windowRetries ?? 5;
      const wait = config.windowRetryMs ?? 300;
      let last = 'the window finder printed no window id';
      for (let attempt = 1; attempt <= retries && windowId === undefined; attempt += 1) {
        try {
          const found = parseWindowInfo(await run(command, args));
          if (found !== undefined) {
            windowId = found.id;
            title = found.title;
          }
        } catch (error) {
          last = error instanceof Error ? error.message : String(error);
        }
        if (windowId === undefined && attempt < retries) await sleep(wait);
      }
      if (windowId === undefined) return { ok: false, error: `no window for pid ${request.pid} after ${retries} tries: ${last}` };
      vars.windowId = windowId;
      vars.title = title ?? '';
    }
    const argv =
      config.kind === 'macos-screencapture'
        ? [config.screencapture ?? 'screencapture', '-x', '-o', '-l', '{windowId}', '{out}']
        : config.argv;
    const [command, ...args] = template(argv, vars);
    if (command === undefined) throw new LaunchError('BAD_CAPTURE_CONFIG', 'the capture command is empty');
    await run(command, args);
    return await verified(request.out, { ...(windowId === undefined ? {} : { windowId }), ...(title === undefined ? {} : { title }) });
  } catch (error) {
    return { ok: false, error: error instanceof Error ? error.message : String(error) };
  }
}

async function verified(out: string, extra: { windowId?: string; title?: string }): Promise<CaptureOutcome> {
  const info = await stat(out).catch(() => undefined);
  if (info === undefined || info.size === 0) return { ok: false, error: `the capture wrote no file at ${out}`, ...extra };
  return { ok: true, ...extra };
}
