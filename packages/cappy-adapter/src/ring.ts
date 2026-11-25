/** Keeps the last `limit` characters written to it, so a long-running process cannot grow memory without bound. */
export class OutputRing {
  readonly limit: number;
  private text = '';
  private readonly decoder = new TextDecoder();

  constructor(limit = 4000) {
    if (!Number.isInteger(limit) || limit < 1) throw new RangeError('an output ring needs a limit of at least 1');
    this.limit = limit;
  }

  /** Adds a chunk. Bytes are decoded as UTF-8, with characters that straddle two chunks kept whole. */
  push(chunk: string | Uint8Array): void {
    const piece = typeof chunk === 'string' ? chunk : this.decoder.decode(chunk, { stream: true });
    this.text = (this.text + piece).slice(-this.limit);
  }

  get length(): number {
    return this.text.length;
  }

  toString(): string {
    return this.text;
  }

  /** The last `count` non-empty lines, oldest first. */
  tail(count: number): string[] {
    if (count <= 0) return [];
    return this.text
      .split(/\r?\n/)
      .filter((line) => line.trim() !== '')
      .slice(-count);
  }

  clear(): void {
    this.text = '';
  }
}
