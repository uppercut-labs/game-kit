import { type ChildProcess, spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { once } from 'node:events';
import { describe, expect, it } from 'vitest';
import { ownedPids, privateProfileDir, stopOwned, waitFor } from '../src/index.js';
import { fakeEmulator, isAlive, scratch } from './support.js';

const children: ChildProcess[] = [];

function start(marker: string): ChildProcess {
  const child = spawn(process.execPath, [fakeEmulator, `--profile=${marker}`], { stdio: 'ignore' });
  children.push(child);
  return child;
}

describe('ownedPids', () => {
  const rows = [
    { pid: 1, command: '/opt/my emu/emu --profile=/p/one' },
    { pid: 2, command: '/opt/my emu/emu' },
    { pid: 3, command: '/opt/my emu/emu-other --profile=/p/one' },
    { pid: 4, command: '/opt/my emu/emu --profile=/p/two' },
    { pid: 5, command: 'emu --profile=/p/one' },
  ];
  const list = async () => rows;

  it('matches the executable followed by a space or ending the line, and holds the marker', async () => {
    expect(await ownedPids({ executable: '/opt/my emu/emu', marker: '/p/one' }, list)).toEqual([1]);
    expect(await ownedPids({ executable: '/opt/my emu/emu', marker: 'emu' }, list)).toEqual([1, 2, 4]);
  });

  it('matches a launch with a bare executable and no arguments', async () => {
    expect(await ownedPids({ executable: 'emu', marker: 'emu' }, list)).toEqual([5]);
    expect(await ownedPids({ executable: '/opt/my emu/emu', marker: '/opt/my emu/emu' }, list)).toContain(2);
  });

  it('refuses an empty marker, which would match every instance', async () => {
    await expect(ownedPids({ executable: 'emu', marker: '' }, list)).rejects.toThrow(RangeError);
  });

  it.runIf(process.platform === 'linux')('finds and stops a real process by its marker and leaves a sibling alone', async () => {
    const run = `/p/${randomUUID()}`;
    const mine = start(`${run}/mine`);
    const theirs = start(`${run}/theirs`);
    await waitFor('both processes', async () => (await ownedPids({ executable: process.execPath, marker: run })).length === 2, { timeoutMs: 5000 });
    expect(await ownedPids({ executable: process.execPath, marker: `${run}/mine` })).toEqual([mine.pid]);
    await stopOwned({ executable: process.execPath, marker: `${run}/mine` }, { graceMs: 2000 });
    if (mine.exitCode === null && mine.signalCode === null) await once(mine, 'exit');
    expect(isAlive(mine.pid ?? -1)).toBe(false);
    expect(isAlive(theirs.pid ?? -1)).toBe(true);
    theirs.kill('SIGKILL');
  }, 15_000);
});

describe('waitFor', () => {
  it('resolves with the first value that is not empty and times out otherwise', async () => {
    let count = 0;
    await expect(waitFor('a count of three', () => (++count >= 3 ? count : false), { intervalMs: 5 })).resolves.toBe(3);
    await expect(waitFor('never', () => false, { timeoutMs: 50, intervalMs: 10 })).rejects.toThrow('Timed out waiting for never');
  });
});

describe('privateProfileDir', () => {
  it('makes a new folder each time', async () => {
    const base = await scratch();
    const one = await privateProfileDir(`${base}/work`, 'psp');
    const two = await privateProfileDir(`${base}/work`, 'psp');
    expect(one).not.toBe(two);
    expect(one.startsWith(`${base}/work/psp-`)).toBe(true);
  });
});
