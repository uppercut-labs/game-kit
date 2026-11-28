import path from 'node:path';
import { describe, expect, it } from 'vitest';
import { OutputRing, ProcessExitError, ProcessSupervisor } from '../src/index.js';

const node = process.execPath;

describe('OutputRing', () => {
  it('keeps only the last characters', () => {
    const ring = new OutputRing(10);
    ring.push('0123456789');
    ring.push('abc');
    expect(ring.toString()).toBe('3456789abc');
    expect(ring.length).toBe(10);
  });

  it('decodes bytes and keeps a character that is split across chunks whole', () => {
    const bytes = new TextEncoder().encode('é');
    const ring = new OutputRing();
    ring.push(bytes.slice(0, 1));
    ring.push(bytes.slice(1));
    expect(ring.toString()).toBe('é');
  });

  it('returns the last non-empty lines, oldest first, for either line ending', () => {
    const ring = new OutputRing();
    ring.push('one\r\ntwo\n\nthree\nfour\n');
    expect(ring.tail(2)).toEqual(['three', 'four']);
    expect(ring.tail(10)).toEqual(['one', 'two', 'three', 'four']);
    expect(ring.tail(0)).toEqual([]);
  });

  it('clears, and refuses a limit below one', () => {
    const ring = new OutputRing(5);
    ring.push('abc');
    ring.clear();
    expect(ring.toString()).toBe('');
    expect(() => new OutputRing(0)).toThrow(RangeError);
  });
});

describe('ProcessSupervisor', () => {
  it('runs a command and resolves with its output', async () => {
    const supervisor = new ProcessSupervisor();
    const output = await supervisor.run(node, ['-e', "console.log('out'); console.error('err')"]);
    expect(output).toContain('out');
    expect(output).toContain('err');
    expect(supervisor.size).toBe(0);
  });

  it('rejects a failing command with its exit code and the last lines it printed', async () => {
    const supervisor = new ProcessSupervisor();
    const script = "for (const n of ['a','b','c','d']) console.log(n); process.exit(3)";
    const error = await supervisor.run(node, ['-e', script]).catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(ProcessExitError);
    expect((error as ProcessExitError).code).toBe(3);
    expect((error as ProcessExitError).message).toBe(`${path.basename(node)} -e exited 3: b | c | d`);
    expect((error as ProcessExitError).output).toContain('a');
  });

  it('keeps the output of a process that wrote it just before it exited', async () => {
    const supervisor = new ProcessSupervisor();
    const output = await supervisor.run(node, ['-e', "process.stdout.write('x'.repeat(100000) + 'END')"], { ringChars: 20000 });
    expect(output.endsWith('END')).toBe(true);
    expect(output).toHaveLength(20000);
  });

  it('rejects when the command cannot be started', async () => {
    const supervisor = new ProcessSupervisor();
    await expect(supervisor.run('definitely-not-a-command-on-this-machine')).rejects.toMatchObject({ code: 'ENOENT' });
    expect(supervisor.size).toBe(0);
  });

  it('reports a start failure through exit instead of rejecting', async () => {
    const supervisor = new ProcessSupervisor();
    const info = await supervisor.start('definitely-not-a-command-on-this-machine').exit;
    expect(info.code).toBeNull();
    expect(info.error?.message).toMatch(/ENOENT/);
  });

  it('can keep stderr only', async () => {
    const supervisor = new ProcessSupervisor();
    const started = supervisor.start(node, ['-e', "console.log('to stdout'); console.error('to stderr')"], { capture: 'stderr' });
    await started.exit;
    expect(started.output.toString()).toBe('to stderr\n');
  });

  it('passes the working folder and environment to the command', async () => {
    const supervisor = new ProcessSupervisor();
    const output = await supervisor.run(node, ['-e', 'console.log(process.cwd() + "|" + process.env.SAMPLE_VALUE)'], {
      cwd: process.cwd(),
      env: { ...process.env, SAMPLE_VALUE: 'passed' },
    });
    expect(output.trim()).toBe(`${process.cwd()}|passed`);
  });

  it('stops every running process with killAll and says how they ended', async () => {
    const supervisor = new ProcessSupervisor();
    const forever = "setInterval(() => {}, 1000)";
    const first = supervisor.start(node, ['-e', forever]);
    const second = supervisor.start(node, ['-e', forever]);
    expect(supervisor.size).toBe(2);
    const rejected = supervisor.run(node, ['-e', forever]).catch((caught: unknown) => caught);
    await new Promise((resolve) => setTimeout(resolve, 150));
    supervisor.killAll();
    expect((await first.exit).signal).toBe('SIGTERM');
    expect((await second.exit).signal).toBe('SIGTERM');
    expect(((await rejected) as ProcessExitError).message).toMatch(/was stopped by SIGTERM/);
    expect(supervisor.size).toBe(0);
  });

  it('does not wait for a grandchild that keeps the pipes open', async () => {
    const supervisor = new ProcessSupervisor();
    // The parent exits at once; the grandchild inherits stdout and lives for two seconds.
    const script = `require('child_process').spawn(process.execPath, ['-e', 'setTimeout(() => {}, 2000)'], { stdio: 'inherit' }); process.exit(0)`;
    const started = Date.now();
    await supervisor.run(node, ['-e', script], { drainMs: 100 });
    expect(Date.now() - started).toBeLessThan(1500);
  });
});
