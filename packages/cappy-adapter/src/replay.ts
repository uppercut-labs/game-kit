import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { CappyAdapterError } from './errors.js';
import { ERROR_CODES, type FileReplay, type InlineReplay, MAX_INLINE_REPLAY_BYTES, type ReplayHandoff } from './protocol.js';

const sha256Hex = (bytes: Uint8Array): string => createHash('sha256').update(bytes).digest('hex');

/**
 * The bytes of a replay payload Cappy handed over. Checks the SHA-256, and the byte count for a file, and throws a
 * `CappyAdapterError` coded `REPLAY_CORRUPT` when they do not match or `REPLAY_UNSUPPORTED` when there is no usable
 * handoff.
 */
export async function readReplayHandoff(replay: ReplayHandoff | undefined): Promise<Uint8Array> {
  if (replay === undefined || replay === null || typeof replay !== 'object') {
    throw new CappyAdapterError(ERROR_CODES.REPLAY_UNSUPPORTED, 'missing replay handoff');
  }
  let bytes: Uint8Array;
  if (replay.kind === 'inline') {
    bytes = Buffer.from(replay.data, 'base64');
  } else if (replay.kind === 'file') {
    bytes = await readFile(replay.path);
    if (bytes.byteLength !== replay.bytes) {
      throw new CappyAdapterError(ERROR_CODES.REPLAY_CORRUPT, `replay file is ${bytes.byteLength} bytes, expected ${replay.bytes}`);
    }
  } else {
    throw new CappyAdapterError(ERROR_CODES.REPLAY_UNSUPPORTED, `unsupported replay kind "${String((replay as { kind?: unknown }).kind)}"`);
  }
  if (sha256Hex(bytes) !== replay.sha256) {
    throw new CappyAdapterError(ERROR_CODES.REPLAY_CORRUPT, 'replay payload sha256 mismatch');
  }
  return bytes;
}

/** A replay handoff that carries the payload inline. Throws a `RangeError` over 1 MiB; use `fileReplay` for those. */
export function inlineReplay(bytes: Uint8Array, format?: string): InlineReplay {
  if (bytes.byteLength > MAX_INLINE_REPLAY_BYTES) {
    throw new RangeError(`an inline replay is at most ${MAX_INLINE_REPLAY_BYTES} bytes; hand large payloads over as a file`);
  }
  return {
    kind: 'inline',
    encoding: 'base64',
    data: Buffer.from(bytes).toString('base64'),
    sha256: sha256Hex(bytes),
    ...(format === undefined ? {} : { format }),
  };
}

/** A replay handoff for a payload the adapter already wrote to `path`. Reads the file once for its size and SHA-256. */
export async function fileReplay(path: string, format?: string): Promise<FileReplay> {
  const bytes = await readFile(path);
  return { kind: 'file', path, sha256: sha256Hex(bytes), bytes: bytes.byteLength, ...(format === undefined ? {} : { format }) };
}
