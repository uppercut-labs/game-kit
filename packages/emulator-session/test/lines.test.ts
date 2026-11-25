import { describe, expect, it } from 'vitest';
import { LineReader, compileNoise } from '../src/index.js';

describe('LineReader', () => {
  it('splits lines across chunks, handles CRLF and skips blank lines', () => {
    const reader = new LineReader();
    reader.push('one\r\ntw');
    reader.push('o\n\n   \nthree');
    expect(reader.lines()).toEqual(['one', 'two']);
    reader.end();
    expect(reader.lines()).toEqual(['one', 'two', 'three']);
  });

  it('keeps a multi-byte character whole when it straddles two chunks', () => {
    const reader = new LineReader();
    const bytes = Buffer.from('café ゲーム\n', 'utf8');
    reader.push(bytes.subarray(0, 4));
    reader.push(bytes.subarray(4, 9));
    reader.push(bytes.subarray(9));
    expect(reader.lines()).toEqual(['café ゲーム']);
  });

  it('cuts a long line, drops the rest of it and goes on with the next line', () => {
    const seen: Array<{ line: string; truncated: boolean }> = [];
    const reader = new LineReader({ maxLineChars: 10, onLine: (event) => seen.push({ line: event.line, truncated: event.truncated }) });
    reader.push('x'.repeat(25));
    reader.push('y'.repeat(25));
    reader.push('\nshort\n');
    expect(seen).toEqual([
      { line: 'xxxxxxxxxx', truncated: true },
      { line: 'short', truncated: false },
    ]);
    expect(reader.stats.truncated).toBe(1);
  });

  it('holds a bounded amount however much arrives without a newline', () => {
    const reader = new LineReader({ maxLineChars: 1000, keepLines: 5 });
    const block = 'z'.repeat(1_000_000);
    for (let index = 0; index < 20; index += 1) reader.push(block);
    reader.push('\n');
    expect(reader.lines()).toHaveLength(1);
    expect(reader.lines()[0]).toHaveLength(1000);
  });

  it('remembers only the latest lines and counts what it pushed out', () => {
    const reader = new LineReader({ keepLines: 3 });
    for (let index = 1; index <= 7; index += 1) reader.push(`line ${index}\n`);
    expect(reader.lines()).toEqual(['line 5', 'line 6', 'line 7']);
    expect(reader.tail(2)).toEqual(['line 6', 'line 7']);
    expect(reader.stats).toEqual({ total: 7, noise: 0, truncated: 0, evicted: 4 });
  });

  it('drops noise from kept lines and listeners but still reports it as raw', () => {
    const kept: string[] = [];
    const raw: string[] = [];
    const reader = new LineReader({
      noise: ['Stalled compile', /^Vulkan: /, { regex: 'resizing output', flags: 'i' }],
      onLine: (event) => kept.push(event.line),
      onRawLine: (event) => raw.push(event.line),
    });
    reader.push('Stalled compile 12ms\nVulkan: device\nResizing Output\nreal line\n');
    expect(kept).toEqual(['real line']);
    expect(reader.lines()).toEqual(['real line']);
    expect(raw).toHaveLength(4);
    expect(reader.stats.noise).toBe(3);
  });

  it('keeps stdout and stderr lines apart when their chunks interleave', () => {
    const reader = new LineReader();
    reader.push('out-', 'stdout');
    reader.push('err-', 'stderr');
    reader.push('one\n', 'stdout');
    reader.push('two\n', 'stderr');
    expect(reader.lines()).toEqual(['out-one', 'err-two']);
  });

  it('keeps going when a listener throws', () => {
    const errors: unknown[] = [];
    const reader = new LineReader({
      onLine: () => {
        throw new Error('boom');
      },
      onListenerError: (error) => errors.push(error),
    });
    reader.push('a\nb\n');
    expect(reader.lines()).toEqual(['a', 'b']);
    expect(errors).toHaveLength(2);
  });

  it('rejects limits that make no sense', () => {
    expect(() => new LineReader({ maxLineChars: 0 })).toThrow(RangeError);
    expect(() => new LineReader({ keepLines: -1 })).toThrow(RangeError);
  });
});

describe('compileNoise', () => {
  it('does not carry state between lines for a global expression', () => {
    const isNoise = compileNoise([/abc/g]);
    expect([isNoise('abc'), isNoise('abc'), isNoise('abc')]).toEqual([true, true, true]);
  });
});
