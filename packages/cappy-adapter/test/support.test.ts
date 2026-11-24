import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { afterEach, describe, expect, it } from 'vitest';
import {
  assertValidRegistry,
  CappyAdapterError,
  ENDPOINT_ENV,
  failureCode,
  failureMessage,
  fileReplay,
  inlineReplay,
  optionValue,
  readLaunchEnvironment,
  readReplayHandoff,
  TOKEN_ENV,
} from '../src/index.js';

const folders: string[] = [];
afterEach(async () => {
  for (const folder of folders.splice(0)) await rm(folder, { recursive: true, force: true });
});

async function tempFolder(): Promise<string> {
  const folder = await mkdtemp(path.join(tmpdir(), 'cappy-adapter-'));
  folders.push(folder);
  return folder;
}

describe('launch environment', () => {
  it('reads the endpoint and token Cappy sets', () => {
    expect(readLaunchEnvironment({ [ENDPOINT_ENV]: 'ws://127.0.0.1:1', [TOKEN_ENV]: 'abc' })).toEqual({ endpoint: 'ws://127.0.0.1:1', token: 'abc' });
  });

  it('names both variables when one is missing or empty', () => {
    for (const env of [{}, { [ENDPOINT_ENV]: 'ws://x' }, { [ENDPOINT_ENV]: '', [TOKEN_ENV]: 't' }]) {
      const error = (() => {
        try {
          return readLaunchEnvironment(env);
        } catch (caught) {
          return caught as CappyAdapterError;
        }
      })() as CappyAdapterError;
      expect(error.code).toBe('NOT_LAUNCHED_BY_CAPPY');
      expect(error.message).toContain(ENDPOINT_ENV);
      expect(error.message).toContain(TOKEN_ENV);
    }
  });
});

describe('optionValue', () => {
  it('reads both spellings', () => {
    expect(optionValue(['--movie-dir', '/tmp/frames'], '--movie-dir')).toBe('/tmp/frames');
    expect(optionValue(['x', '--movie-dir=/tmp/frames'], '--movie-dir')).toBe('/tmp/frames');
  });

  it('returns undefined when the option is absent or has no value, instead of the first argument', () => {
    expect(optionValue(['first'], '--movie-dir')).toBeUndefined();
    expect(optionValue([], '--movie-dir')).toBeUndefined();
    expect(optionValue(['--movie-dir'], '--movie-dir')).toBeUndefined();
    expect(optionValue(['--movie-dir', '--other', 'v'], '--movie-dir')).toBeUndefined();
  });
});

describe('failure details', () => {
  it('uses a string code of a sensible length and otherwise the fallback', () => {
    expect(failureCode(Object.assign(new Error('x'), { code: 'ENOENT' }), 'F')).toBe('ENOENT');
    expect(failureCode(Object.assign(new Error('x'), { code: 2 }), 'F')).toBe('F');
    expect(failureCode(Object.assign(new Error('x'), { code: '' }), 'F')).toBe('F');
    expect(failureCode('text', 'F')).toBe('F');
    expect(failureCode(undefined, 'F')).toBe('F');
  });

  it('turns any thrown value into a message of at most 2000 characters', () => {
    expect(failureMessage(new Error('boom'))).toBe('boom');
    expect(failureMessage('plain')).toBe('plain');
    expect(failureMessage(new Error('y'.repeat(2500)))).toHaveLength(2000);
  });
});

describe('replay handoffs', () => {
  it('round-trips an inline payload', async () => {
    const bytes = new Uint8Array([0, 1, 2, 250, 251]);
    const handoff = inlineReplay(bytes, 'fmt');
    expect(handoff).toMatchObject({ kind: 'inline', encoding: 'base64', format: 'fmt' });
    expect(handoff.sha256).toMatch(/^[a-f0-9]{64}$/);
    expect(Array.from(await readReplayHandoff(handoff))).toEqual(Array.from(bytes));
  });

  it('leaves the format out when none is given', () => {
    expect(inlineReplay(new Uint8Array([1]))).not.toHaveProperty('format');
  });

  it('refuses an inline payload over 1 MiB', () => {
    expect(() => inlineReplay(new Uint8Array(1024 * 1024 + 1))).toThrow(RangeError);
    expect(inlineReplay(new Uint8Array(1024 * 1024)).kind).toBe('inline');
  });

  it('round-trips a file payload and checks its size and SHA-256', async () => {
    const folder = await tempFolder();
    const file = path.join(folder, 'payload.bin');
    await writeFile(file, 'payload');
    const handoff = await fileReplay(file, 'fmt');
    expect(handoff).toMatchObject({ kind: 'file', path: file, bytes: 7, format: 'fmt' });
    expect(new TextDecoder().decode(await readReplayHandoff(handoff))).toBe('payload');
    await expect(readReplayHandoff({ ...handoff, bytes: 8 })).rejects.toMatchObject({ code: 'REPLAY_CORRUPT' });
    await expect(readReplayHandoff({ ...handoff, sha256: 'f'.repeat(64) })).rejects.toMatchObject({ code: 'REPLAY_CORRUPT' });
  });

  it('reports a missing or unknown handoff as REPLAY_UNSUPPORTED', async () => {
    await expect(readReplayHandoff(undefined)).rejects.toMatchObject({ code: 'REPLAY_UNSUPPORTED' });
    await expect(readReplayHandoff({ kind: 'tape' } as never)).rejects.toMatchObject({ code: 'REPLAY_UNSUPPORTED' });
  });
});

describe('assertValidRegistry', () => {
  it('accepts a registry that follows the protocol patterns', () => {
    expect(() => assertValidRegistry(['scenarios', 'frame_exact'], [{ id: 'a.b-c_1', name: 'A', parameters: { level_2: { type: 'integer' } } }])).not.toThrow();
  });

  it('names the first problem', () => {
    expect(() => assertValidRegistry(['Scenarios'], [])).toThrow(/capability "Scenarios"/);
    expect(() => assertValidRegistry([], [{ id: 'x', name: '' }])).toThrow(/needs a name/);
    expect(() => assertValidRegistry([], [{ id: 'x', name: 'X', requiredCapabilities: ['Bad'] }])).toThrow(/capability "Bad"/);
  });
});
