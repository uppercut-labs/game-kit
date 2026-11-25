import { MAX_FAILURE_CODE_LENGTH, MAX_FAILURE_MESSAGE_LENGTH } from './protocol.js';

/** An error that carries a protocol failure code. A handler throws one to choose the code Cappy sees. */
export class CappyAdapterError extends Error {
  readonly code: string;

  constructor(code: string, message: string) {
    super(message);
    this.name = 'CappyAdapterError';
    this.code = code;
  }
}

/** Cappy refused the hello. `code` is Cappy's reject code, for example `PROTOCOL_UNAUTHENTICATED`. */
export class CappyRejectedError extends CappyAdapterError {
  constructor(code: string, message: string) {
    super(code, `Cappy rejected the adapter (${code}): ${message}`);
    this.name = 'CappyRejectedError';
  }
}

/** The failure code to send for a thrown value: its own string `code` when it is a valid one, else `fallback`. */
export function failureCode(error: unknown, fallback: string): string {
  const code = (error as { code?: unknown } | null | undefined)?.code;
  return typeof code === 'string' && code.length > 0 && code.length <= MAX_FAILURE_CODE_LENGTH ? code : fallback;
}

/** The failure message to send for a thrown value, cut to the length Cappy accepts. */
export function failureMessage(error: unknown): string {
  const text = error instanceof Error ? error.message : String(error);
  return text.length > MAX_FAILURE_MESSAGE_LENGTH ? text.slice(0, MAX_FAILURE_MESSAGE_LENGTH) : text;
}
