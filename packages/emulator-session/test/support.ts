import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterEach } from 'vitest';

export const here = path.dirname(fileURLToPath(import.meta.url));
export const fakeEmulator = path.join(here, 'fake-emulator.mjs');
export const fakeCapture = path.join(here, 'fake-capture.mjs');
export const fakeFinder = path.join(here, 'fake-finder.mjs');

const folders: string[] = [];

/** A scratch folder that is removed after the test. */
export async function scratch(): Promise<string> {
  const folder = await mkdtemp(path.join(os.tmpdir(), 'emulator-session-'));
  folders.push(folder);
  return folder;
}

afterEach(async () => {
  for (const folder of folders.splice(0)) await rm(folder, { recursive: true, force: true });
});

/**
 * The exit `stop()` reports for the fake emulator ended by SIGTERM. On Unix the fake handles SIGTERM and exits with
 * code 143. Windows has no signals: Node ends the process at once with TerminateProcess and reports the signal it was
 * asked to send, so no handler runs.
 */
export const termExit =
  process.platform === 'win32' ? { code: null, signal: 'SIGTERM' as const } : { code: 143, signal: null };

export function isAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}
