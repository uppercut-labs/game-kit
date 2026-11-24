import { describe, expect, it } from 'vitest';
import { schemaViolation } from './fake-controller.js';

// The fake controller is only worth trusting if it refuses what Cappy refuses, so check its strictness directly.
describe('the fake controller schema', () => {
  it('accepts well formed messages', () => {
    const good = [
      { type: 'started', op: 'op_1' },
      { type: 'event', op: 'op_1', t: 0, event: 'HIT', payload: { a: 1 } },
      { type: 'completed', op: 'op_1', result: {} },
      { type: 'failed', op: 'op_1', code: 'X', message: '' },
      { type: 'pong', nonce: '1' },
    ];
    for (const message of good) expect(schemaViolation(message)).toBeUndefined();
  });

  it('refuses an unknown field, which is what Cappy does (a frame number, for example)', () => {
    for (const message of [
      { type: 'started', op: 'op_1', frame: 0 },
      { type: 'event', op: 'op_1', t: 0, event: 'HIT', frame: 3 },
      { type: 'completed', op: 'op_1', frame: 9, result: {} },
      { type: 'ready', op: 'op_1', extra: true },
    ]) {
      expect(schemaViolation(message)).toMatch(/unrecognized key/);
    }
  });

  it('refuses bad names, times and codes', () => {
    expect(schemaViolation({ type: 'event', op: 'op_1', t: -1, event: 'HIT' })).toBeDefined();
    expect(schemaViolation({ type: 'event', op: 'op_1', t: 1, event: '1HIT' })).toBeDefined();
    expect(schemaViolation({ type: 'failed', op: 'op_1', code: 'C'.repeat(129), message: '' })).toBeDefined();
    expect(schemaViolation({ type: 'started', op: 'bad op' })).toBeDefined();
  });
});
