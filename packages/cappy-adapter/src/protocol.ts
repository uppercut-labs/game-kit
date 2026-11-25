/*
 * The wire types of Cappy's adapter protocol, version 1. Every message is one UTF-8 JSON text frame with a `type`
 * field. Cappy's own page for adapter authors (docs/protocol.md in the Cappy repository) is the reference.
 */

/** The only protocol version this package speaks. */
export const PROTOCOL_VERSION = 1;

/** Environment variable that carries the WebSocket URL when Cappy launches a game. */
export const ENDPOINT_ENV = 'CAPPY_ENDPOINT';

/** Environment variable that carries the per-launch session token the hello must echo. */
export const TOKEN_ENV = 'CAPPY_SESSION_TOKEN';

/** Largest message Cappy accepts, in bytes. */
export const MAX_MESSAGE_BYTES = 4 * 1024 * 1024;

/** Largest decoded replay payload that may travel inline; anything bigger goes by file. */
export const MAX_INLINE_REPLAY_BYTES = 1024 * 1024;

/** Longest failure message Cappy accepts. */
export const MAX_FAILURE_MESSAGE_LENGTH = 2000;

/** Longest failure code Cappy accepts. */
export const MAX_FAILURE_CODE_LENGTH = 128;

/**
 * Codes that Cappy sends in `reject` and `error` messages, and codes an adapter reports in `failed`. A failure code is
 * any string of 1 to 128 characters, so an adapter may add its own (for example a launcher error).
 */
export const ERROR_CODES = {
  /** reject: the token in the hello was wrong. */
  PROTOCOL_UNAUTHENTICATED: 'PROTOCOL_UNAUTHENTICATED',
  /** reject: no protocol version both sides support. */
  PROTOCOL_VERSION_INCOMPATIBLE: 'PROTOCOL_VERSION_INCOMPATIBLE',
  /** reject or error: the message was not valid JSON or broke its schema. */
  PROTOCOL_INVALID_MESSAGE: 'PROTOCOL_INVALID_MESSAGE',
  /** error: a message that does not fit the operation's state. Also what this client reports for a stray `start`. */
  PROTOCOL_OUT_OF_STATE: 'PROTOCOL_OUT_OF_STATE',
  /** reject: the project requires a capability the hello did not list. */
  CAPABILITY_MISSING: 'CAPABILITY_MISSING',
  /** reject, or failed when a second operation arrives while one is active. */
  OPERATION_BUSY: 'OPERATION_BUSY',
  /** failed: no scenario with that id in the registry. */
  SCENARIO_NOT_FOUND: 'SCENARIO_NOT_FOUND',
  /** failed: the parameters do not suit the scenario. */
  INVALID_PARAMETERS: 'INVALID_PARAMETERS',
  /** failed: the replay is not one this adapter can play. */
  REPLAY_UNSUPPORTED: 'REPLAY_UNSUPPORTED',
  /** failed: the replay payload does not match its SHA-256 or byte count. */
  REPLAY_CORRUPT: 'REPLAY_CORRUPT',
  /** failed: the presentation names a camera the game does not have. */
  UNKNOWN_CAMERA: 'UNKNOWN_CAMERA',
  /** failed: the presentation asks for something the build cannot show, such as another time scale. */
  UNSUPPORTED_PRESENTATION: 'UNSUPPORTED_PRESENTATION',
  /** failed: the adapter does not do this kind of operation (for example freeform recording). */
  UNSUPPORTED_OPERATION: 'UNSUPPORTED_OPERATION',
  /** failed: a handler threw and gave no code of its own. */
  OPERATION_FAILED: 'OPERATION_FAILED',
} as const;

export type ErrorCode = (typeof ERROR_CODES)[keyof typeof ERROR_CODES];

export type ParameterType = 'string' | 'number' | 'integer' | 'boolean';
export type ParameterValue = string | number | boolean;

export interface ScenarioParameterSpec {
  type: ParameterType;
  description?: string;
  required?: boolean;
  default?: ParameterValue;
  enum?: readonly (string | number)[];
  minimum?: number;
  maximum?: number;
}

/** One entry of the scenario registry an adapter sends after the welcome. */
export interface ScenarioDefinition {
  /** Stable lowercase id: letters a to z, digits, `_`, `.` and `-`, starting with a letter or digit. */
  id: string;
  name: string;
  /** Keyed by parameter name (letters, digits and `_`, not starting with a digit). */
  parameters?: Readonly<Record<string, ScenarioParameterSpec>>;
  /** Lowercase snake_case capability names the scenario needs. */
  requiredCapabilities?: readonly string[];
  metadata?: Readonly<Record<string, unknown>>;
}

export interface InlineReplay {
  kind: 'inline';
  encoding: 'base64';
  data: string;
  sha256: string;
  format?: string;
}

export interface FileReplay {
  kind: 'file';
  /** Untrusted on Cappy's side: an adapter-supplied path never grants deletion authority. */
  path: string;
  sha256: string;
  bytes: number;
  format?: string;
}

/** How a replay payload travels between Cappy and an adapter. Its contents belong to the adapter. */
export type ReplayHandoff = InlineReplay | FileReplay;

/** The preset's presentation. Cappy defines `timeScale` (0.1 to 4) and `camera`; every other key is the adapter's own. */
export type Presentation = Readonly<Record<string, unknown>>;

export interface Identity {
  name: string;
  version: string;
}

// Adapter to Cappy

export interface HelloMessage {
  type: 'hello';
  protocol: { min: number; max: number };
  token: string;
  adapter: Identity;
  game: { id: string; name?: string };
  build?: string;
  capabilities: string[];
}

export type AdapterMessage =
  | HelloMessage
  | { type: 'scenarios'; scenarios: ScenarioDefinition[] }
  | { type: 'ready'; op: string }
  | { type: 'started'; op: string }
  | { type: 'event'; op: string; t: number; event: string; durationMs?: number; payload?: unknown }
  | { type: 'completed'; op: string; result?: Record<string, unknown>; replay?: ReplayHandoff }
  | { type: 'failed'; op: string; code: string; message: string }
  | { type: 'pong'; nonce: string };

// Cappy to adapter

export type ControllerMessage =
  | { type: 'welcome'; protocol: number; controller: Identity }
  | { type: 'reject'; code: string; message: string }
  | { type: 'list_scenarios' }
  | {
      type: 'prepare_scenario';
      op: string;
      scenario: string;
      parameters: Readonly<Record<string, ParameterValue>>;
      presentation?: Presentation;
    }
  | { type: 'record_start'; op: string }
  | { type: 'prepare_replay'; op: string; replay: ReplayHandoff; presentation?: Presentation }
  | { type: 'start'; op: string }
  | { type: 'stop'; op: string }
  | { type: 'cancel'; op: string }
  | { type: 'ping'; nonce: string }
  | { type: 'error'; code: string; message: string; op?: string };

/** What Cappy tells the adapter once the handshake succeeds. */
export interface Welcome {
  protocol: number;
  controller: Identity;
}

const CAPABILITY_NAME = /^[a-z][a-z0-9_]{0,63}$/;
const SCENARIO_ID = /^[a-z0-9][a-z0-9_.-]{0,127}$/;
const PARAMETER_NAME = /^[A-Za-z_][A-Za-z0-9_]*$/;

/** The pattern Cappy requires of operation ids. */
export const OPERATION_ID = /^[A-Za-z0-9_-]{1,64}$/;

/** The pattern Cappy requires of event names. */
export const EVENT_NAME = /^[A-Za-z][A-Za-z0-9_.:-]{0,127}$/;

/**
 * Checks a hello's capabilities and a scenario registry against the patterns Cappy enforces, so a bad registry fails
 * when the adapter is built instead of as a protocol violation in the middle of a capture. Throws a `TypeError`
 * naming the first problem.
 */
export function assertValidRegistry(capabilities: readonly string[], scenarios: readonly ScenarioDefinition[]): void {
  for (const name of capabilities) {
    if (!CAPABILITY_NAME.test(name)) {
      throw new TypeError(`capability "${name}" must be lowercase snake_case, starting with a letter`);
    }
  }
  const seen = new Set<string>();
  for (const scenario of scenarios) {
    if (!SCENARIO_ID.test(scenario.id)) {
      throw new TypeError(`scenario id "${scenario.id}" must be lowercase letters, digits, "_", "." or "-"`);
    }
    if (seen.has(scenario.id)) throw new TypeError(`scenario id "${scenario.id}" is listed twice`);
    seen.add(scenario.id);
    if (scenario.name === '') throw new TypeError(`scenario "${scenario.id}" needs a name`);
    for (const name of Object.keys(scenario.parameters ?? {})) {
      if (!PARAMETER_NAME.test(name)) {
        throw new TypeError(`scenario "${scenario.id}" parameter "${name}" must be a plain identifier`);
      }
    }
    for (const name of scenario.requiredCapabilities ?? []) {
      if (!CAPABILITY_NAME.test(name)) {
        throw new TypeError(`scenario "${scenario.id}" capability "${name}" must be lowercase snake_case`);
      }
    }
  }
}
