import { StringDecoder } from 'node:string_decoder';

/** A line to drop: a substring, a regular expression, or the JSON-safe form `{ regex, flags }`. */
export type NoiseRule = string | RegExp | { regex: string; flags?: string };

/** Turns noise rules into one predicate that is true for a line to drop. */
export function compileNoise(rules: readonly NoiseRule[]): (line: string) => boolean {
  const tests = rules.map((rule): ((line: string) => boolean) => {
    if (typeof rule === 'string') return (line) => line.includes(rule);
    if (rule instanceof RegExp) {
      // A global or sticky expression keeps state between calls; test a copy that does not.
      const plain = new RegExp(rule.source, rule.flags.replace(/[gy]/g, ''));
      return (line) => plain.test(line);
    }
    const plain = new RegExp(rule.regex, (rule.flags ?? '').replace(/[gy]/g, ''));
    return (line) => plain.test(line);
  });
  return (line) => tests.some((test) => test(line));
}

export interface LineEvent {
  /** The line without its line ending. Never empty. */
  line: string;
  /** True when the line was longer than `maxLineChars` and the rest was dropped. */
  truncated: boolean;
  /** Which stream the line came from, as named by the caller of `push`. */
  source: string;
  /** Position among all non-blank lines seen, counted from 1, noise included. */
  seq: number;
}

export interface LineReaderOptions {
  /** The longest line kept, in characters. Longer lines are cut and flagged. Defaults to 4096. */
  maxLineChars?: number;
  /** How many of the latest kept lines to remember. Defaults to 400. */
  keepLines?: number;
  /** Lines to drop from the kept lines and from `onLine`. */
  noise?: readonly NoiseRule[];
  /** Called for every line that is not noise. */
  onLine?: (event: LineEvent) => void;
  /** Called for every line, noise included. */
  onRawLine?: (event: LineEvent) => void;
  /** Called when a listener throws; the reader goes on. */
  onListenerError?: (error: unknown) => void;
}

export interface LineStats {
  /** Non-blank lines seen. */
  total: number;
  /** Lines dropped as noise. */
  noise: number;
  /** Lines cut at `maxLineChars`. */
  truncated: number;
  /** Kept lines pushed out of memory by newer ones. */
  evicted: number;
}

interface StreamState {
  decoder: StringDecoder;
  pending: string;
  truncated: boolean;
  discarding: boolean;
}

/**
 * Splits output into lines with bounded memory: a line is cut at `maxLineChars`, only the latest `keepLines` lines
 * are remembered, and noise is dropped. Several streams (stdout and stderr) can feed one reader; each keeps its own
 * partial line, so their chunks never splice into each other's lines.
 */
export class LineReader {
  readonly maxLineChars: number;
  readonly keepLines: number;
  readonly stats: LineStats = { total: 0, noise: 0, truncated: 0, evicted: 0 };
  private readonly kept: string[] = [];
  private readonly streams = new Map<string, StreamState>();
  private readonly isNoise: (line: string) => boolean;
  private readonly listeners = new Set<(event: LineEvent) => void>();
  private readonly options: LineReaderOptions;

  constructor(options: LineReaderOptions = {}) {
    this.maxLineChars = options.maxLineChars ?? 4096;
    this.keepLines = options.keepLines ?? 400;
    if (!Number.isInteger(this.maxLineChars) || this.maxLineChars < 1) {
      throw new RangeError('maxLineChars must be a whole number of at least 1');
    }
    if (!Number.isInteger(this.keepLines) || this.keepLines < 0) {
      throw new RangeError('keepLines must be a whole number of at least 0');
    }
    this.isNoise = compileNoise(options.noise ?? []);
    this.options = options;
    if (options.onLine !== undefined) this.listeners.add(options.onLine);
  }

  /** Adds a listener for lines that are not noise. Returns a function that removes it. */
  onLine(listener: (event: LineEvent) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  /** Feeds a chunk. Bytes are decoded as UTF-8, with characters that straddle two chunks kept whole. */
  push(chunk: string | Uint8Array, source = 'stdout'): void {
    const state = this.state(source);
    const text = typeof chunk === 'string' ? chunk : state.decoder.write(Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength));
    let start = 0;
    for (;;) {
      const newline = text.indexOf('\n', start);
      if (newline === -1) {
        this.absorb(state, text.slice(start));
        return;
      }
      this.absorb(state, text.slice(start, newline));
      this.finish(state, source);
      start = newline + 1;
    }
  }

  /** Ends a stream: a partial last line is emitted. Call once per stream when it closes. */
  end(source = 'stdout'): void {
    const state = this.state(source);
    const rest = state.decoder.end();
    if (rest !== '') this.absorb(state, rest);
    this.finish(state, source);
  }

  /** The kept lines, oldest first. */
  lines(): string[] {
    return [...this.kept];
  }

  /** The last `count` kept lines, oldest first. */
  tail(count: number): string[] {
    return count <= 0 ? [] : this.kept.slice(-count);
  }

  private state(source: string): StreamState {
    let state = this.streams.get(source);
    if (state === undefined) {
      state = { decoder: new StringDecoder('utf8'), pending: '', truncated: false, discarding: false };
      this.streams.set(source, state);
    }
    return state;
  }

  private absorb(state: StreamState, piece: string): void {
    if (state.discarding || piece === '') return;
    state.pending += piece;
    if (state.pending.length > this.maxLineChars) {
      state.pending = state.pending.slice(0, this.maxLineChars);
      state.truncated = true;
      state.discarding = true;
    }
  }

  private finish(state: StreamState, source: string): void {
    let line = state.pending;
    const truncated = state.truncated;
    state.pending = '';
    state.truncated = false;
    state.discarding = false;
    if (line.endsWith('\r')) line = line.slice(0, -1);
    if (line.trim() === '') return;
    this.stats.total += 1;
    if (truncated) this.stats.truncated += 1;
    const event: LineEvent = { line, truncated, source, seq: this.stats.total };
    this.call(this.options.onRawLine, event);
    if (this.isNoise(line)) {
      this.stats.noise += 1;
      return;
    }
    if (this.keepLines > 0) {
      this.kept.push(line);
      if (this.kept.length > this.keepLines) {
        this.kept.shift();
        this.stats.evicted += 1;
      }
    } else {
      this.stats.evicted += 1;
    }
    for (const listener of [...this.listeners]) this.call(listener, event);
  }

  private call(listener: ((event: LineEvent) => void) | undefined, event: LineEvent): void {
    if (listener === undefined) return;
    try {
      listener(event);
    } catch (error) {
      this.options.onListenerError?.(error);
    }
  }
}
