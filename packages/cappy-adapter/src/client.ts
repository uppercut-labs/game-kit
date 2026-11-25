import { CappyAdapterError, CappyRejectedError, failureCode, failureMessage } from './errors.js';
import {
  type AdapterMessage,
  type ControllerMessage,
  ERROR_CODES,
  EVENT_NAME,
  type Identity,
  MAX_FAILURE_CODE_LENGTH,
  OPERATION_ID,
  PROTOCOL_VERSION,
  type ParameterValue,
  type Presentation,
  type ReplayHandoff,
  type ScenarioDefinition,
  type Welcome,
  assertValidRegistry,
} from './protocol.js';
import { readReplayHandoff } from './replay.js';

/** The part of the WebSocket API this client uses. The `WebSocket` global of Node 22 and the `ws` package both fit. */
export interface WebSocketLike {
  readonly readyState: number;
  send(data: string): void;
  close(): void;
  addEventListener(type: 'open' | 'close' | 'error' | 'message', listener: (event: any) => void): void;
}

export type WebSocketConstructor = new (url: string) => WebSocketLike;

const OPEN = 1;

/** Why an operation ended, as passed to `dispose`. */
export type DisposeReason = 'completed' | 'failed' | 'cancelled' | 'disconnected';

/**
 * Extra top-level fields for a `started`, `event` or `completed` message, for a controller that defines fields beyond
 * protocol v1 (for example a frame number for a movie recorder). Cappy as published refuses unknown fields, so leave
 * these out unless your controller documents them. The fields of the protocol itself cannot be overridden.
 */
export type ExtensionFields = Readonly<Record<string, unknown>>;

/** What a finished operation reports in `completed`. */
export interface OperationResult {
  result?: Record<string, unknown>;
  /** The replay payload of a freeform recording. See `inlineReplay` and `fileReplay`. */
  replay?: ReplayHandoff;
  /** Extension fields for the `completed` message. */
  fields?: ExtensionFields;
}

/** What a handler uses to report on the operation it is running. Every method does nothing once the operation has ended. */
export interface OperationContext {
  readonly op: string;
  /** Aborted when the operation is cancelled, fails, completes or the connection drops. */
  readonly signal: AbortSignal;
  /** Tells Cappy the operation is running. Sends once; later calls return false. */
  started(fields?: ExtensionFields): boolean;
  /**
   * Reports an event. `t` is operation-relative presented time in milliseconds (never wall-clock time) and is clamped
   * to zero or more. Throws a `TypeError` for a name Cappy would reject, so the mistake shows up in the handler.
   */
  event(name: string, t: number, payload?: unknown, options?: { durationMs?: number; fields?: ExtensionFields }): boolean;
  /** Ends the operation as completed. Sends `started` first when it was not sent, because Cappy never accepts a `completed` before it. */
  completed(result?: Record<string, unknown>, replay?: ReplayHandoff, fields?: ExtensionFields): boolean;
  /** Ends the operation as failed. */
  failed(code: string, message: string): boolean;
}

/** An operation a handler has prepared, or started. All members are optional. */
export interface Operation {
  /**
   * Runs a prepared scenario or replay once Cappy sends `start`. Call `context.started()` when the capture begins.
   * Return an `OperationResult` to complete the operation; return nothing and call `context.completed` later to
   * complete it from an event of your own. Throw (a `CappyAdapterError` picks the code) to fail it.
   */
  start?(context: OperationContext): Promise<OperationResult | void> | OperationResult | void;
  /** Asks a running recording or replay to finish. Return an `OperationResult` to complete it. Ignored when absent. */
  stop?(context: OperationContext): Promise<OperationResult | void> | OperationResult | void;
  /** Releases what the operation holds (processes, timers, files). Runs once, however the operation ends. */
  dispose?(reason: DisposeReason): Promise<void> | void;
}

export interface ScenarioRequest {
  op: string;
  scenario: ScenarioDefinition;
  parameters: Readonly<Record<string, ParameterValue>>;
  presentation: Presentation | undefined;
}

export interface ReplayRequest {
  op: string;
  /** The payload, with its SHA-256 (and size, for a file) already checked. */
  bytes: Uint8Array;
  handoff: ReplayHandoff;
  presentation: Presentation | undefined;
}

/**
 * What a game supplies. Throw from `prepareScenario` or `prepareReplay` to refuse (a `CappyAdapterError` picks the
 * code, for example `INVALID_PARAMETERS`); the client then sends `failed`.
 */
export interface AdapterHandler {
  /** Without it, `prepare_scenario` fails with `UNSUPPORTED_OPERATION`. */
  prepareScenario?(request: ScenarioRequest): Promise<Operation> | Operation;
  /** Starts a freeform recording. Call `context.started()`. Without it, or without the `freeform_recording` capability, `record_start` fails. */
  startRecording?(request: { op: string }, context: OperationContext): Promise<Operation | void> | Operation | void;
  /** Without it, `prepare_replay` fails with `REPLAY_UNSUPPORTED`. */
  prepareReplay?(request: ReplayRequest): Promise<Operation> | Operation;
}

export interface CappyAdapterOptions {
  /** The `CAPPY_ENDPOINT` URL. */
  endpoint: string;
  /** The `CAPPY_SESSION_TOKEN` value. */
  token: string;
  adapter: Identity;
  game: { id: string; name?: string };
  build?: string;
  /** Capability names for the hello. Defaults to `scenarios`. */
  capabilities?: readonly string[];
  /** The scenario registry, sent after the welcome and again on `list_scenarios`. */
  scenarios?: readonly ScenarioDefinition[];
  handler: AdapterHandler;
  /** Defaults to `globalThis.WebSocket`. Pass the `ws` package's class on a Node without the global. */
  WebSocket?: WebSocketConstructor;
  /** How long to wait for the welcome. Defaults to 10 000 ms. */
  handshakeTimeoutMs?: number;
  /** Called for an `error` message from Cappy, which tells the adapter why it failed an operation. */
  onControllerError?(error: { code: string; message: string; op?: string }): void;
}

type Phase = 'preparing' | 'ready' | 'running';

class Active {
  readonly abort = new AbortController();
  readonly context: OperationContext;
  phase: Phase = 'preparing';
  startedSent = false;
  operation: Operation | undefined;

  constructor(
    readonly op: string,
    makeContext: (active: Active) => OperationContext,
  ) {
    this.context = makeContext(this);
  }
}

const textOf = (data: unknown): string | undefined => {
  if (typeof data === 'string') return data;
  if (data instanceof ArrayBuffer) return new TextDecoder().decode(data);
  if (ArrayBuffer.isView(data)) return new TextDecoder().decode(data);
  return undefined;
};

const isOperationResult = (value: unknown): value is OperationResult => typeof value === 'object' && value !== null;

/**
 * A game adapter's connection to Cappy, protocol version 1. It does the handshake, answers the heartbeat, keeps the
 * scenario registry, and turns Cappy's requests into calls on your handler, keeping the operation state Cappy checks:
 * one operation at a time, `started` before `completed`, nothing after the end.
 */
export class CappyAdapter {
  /** Resolves when the connection has closed and the operation that was running has been disposed. */
  readonly closed: Promise<void>;

  private readonly options: CappyAdapterOptions;
  private readonly scenarios: readonly ScenarioDefinition[];
  private readonly capabilities: readonly string[];
  private socket: WebSocketLike | undefined;
  private welcomed = false;
  private active: Active | undefined;
  private resolveClosed: () => void = () => undefined;

  constructor(options: CappyAdapterOptions) {
    this.options = options;
    this.scenarios = options.scenarios ?? [];
    this.capabilities = [...(options.capabilities ?? ['scenarios'])];
    assertValidRegistry(this.capabilities, this.scenarios);
    this.closed = new Promise((resolve) => {
      this.resolveClosed = resolve;
    });
  }

  /**
   * Connects, sends the hello and resolves with Cappy's welcome. Rejects with `CappyRejectedError` when Cappy refuses
   * the hello, and with a `CappyAdapterError` when the socket fails or closes first, or the welcome is late.
   */
  connect(): Promise<Welcome> {
    if (this.socket !== undefined) return Promise.reject(new CappyAdapterError('ALREADY_CONNECTED', 'connect was already called'));
    const Socket = this.options.WebSocket ?? (globalThis as { WebSocket?: WebSocketConstructor }).WebSocket;
    if (typeof Socket !== 'function') {
      return Promise.reject(
        new CappyAdapterError('WEBSOCKET_UNAVAILABLE', 'no WebSocket class: use Node.js 22 or later, or pass the ws package as options.WebSocket'),
      );
    }
    return new Promise<Welcome>((resolve, reject) => {
      const socket = new Socket(this.options.endpoint);
      this.socket = socket;
      let settled = false;
      const settle = (action: () => void): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        action();
      };
      const timer = setTimeout(() => {
        settle(() => reject(new CappyAdapterError('HANDSHAKE_TIMEOUT', 'Cappy did not answer the hello in time')));
        this.dropSocket();
      }, this.options.handshakeTimeoutMs ?? 10_000);

      socket.addEventListener('open', () => this.sendHello());
      socket.addEventListener('message', (event: { data: unknown }) => {
        const text = textOf(event.data);
        if (text !== undefined) this.receive(text, (message) => {
          if (message.type === 'welcome') settle(() => resolve({ protocol: message.protocol, controller: message.controller }));
          else if (message.type === 'reject') {
            settle(() => reject(new CappyRejectedError(message.code, message.message)));
            this.dropSocket();
          }
        });
      });
      socket.addEventListener('error', (event: { error?: unknown; message?: unknown }) => {
        const detail = event?.error instanceof Error ? event.error.message : typeof event?.message === 'string' ? event.message : 'socket error';
        settle(() => reject(new CappyAdapterError('CONNECTION_FAILED', `could not connect to Cappy: ${detail}`)));
      });
      socket.addEventListener('close', () => {
        settle(() => reject(new CappyAdapterError('CONNECTION_CLOSED', 'the connection closed before the welcome')));
        void this.onClose();
      });
    });
  }

  /** Closes the connection. The running operation is disposed as disconnected. */
  close(): void {
    this.dropSocket();
  }

  private dropSocket(): void {
    try {
      this.socket?.close();
    } catch {
      // Already closed.
    }
  }

  private send(message: AdapterMessage): void {
    if (this.socket?.readyState === OPEN) this.socket.send(JSON.stringify(message));
  }

  private sendHello(): void {
    this.send({
      type: 'hello',
      protocol: { min: PROTOCOL_VERSION, max: PROTOCOL_VERSION },
      token: this.options.token,
      adapter: this.options.adapter,
      game: this.options.game,
      ...(this.options.build === undefined ? {} : { build: this.options.build }),
      capabilities: [...this.capabilities],
    });
  }

  private sendScenarios(): void {
    this.send({ type: 'scenarios', scenarios: this.scenarios.map((scenario) => ({ ...scenario })) });
  }

  private fail(op: string, code: string, message: string): void {
    this.send({ type: 'failed', op, code, message });
  }

  private async onClose(): Promise<void> {
    const active = this.active;
    if (active !== undefined) await this.finish(active, 'disconnected');
    this.resolveClosed();
  }

  // Messages from Cappy

  private receive(text: string, onHandshake: (message: ControllerMessage) => void): void {
    let message: ControllerMessage;
    try {
      message = JSON.parse(text) as ControllerMessage;
    } catch {
      return;
    }
    if (typeof message !== 'object' || message === null || typeof message.type !== 'string') return;
    switch (message.type) {
      case 'welcome':
        if (!this.welcomed) {
          this.welcomed = true;
          this.sendScenarios();
          onHandshake(message);
        }
        return;
      case 'reject':
        onHandshake(message);
        return;
      case 'ping':
        // Answer at once, whatever an operation is doing: Cappy gives up on an adapter that is silent for six seconds.
        this.send({ type: 'pong', nonce: String(message.nonce ?? '') });
        return;
      case 'error':
        this.options.onControllerError?.({ code: message.code, message: message.message, ...(message.op === undefined ? {} : { op: message.op }) });
        return;
      default:
        if (this.welcomed) this.dispatch(message);
    }
  }

  private dispatch(message: ControllerMessage): void {
    switch (message.type) {
      case 'list_scenarios':
        this.sendScenarios();
        return;
      case 'prepare_scenario':
        this.prepareScenario(message);
        return;
      case 'record_start':
        this.recordStart(message);
        return;
      case 'prepare_replay':
        this.prepareReplay(message);
        return;
      case 'start':
        this.startOperation(message.op);
        return;
      case 'stop':
        this.stopOperation(message.op);
        return;
      case 'cancel':
        if (this.active?.op === message.op) void this.finish(this.active, 'cancelled');
        return;
      default:
        return;
    }
  }

  private begin(op: string): Active | undefined {
    if (typeof op !== 'string' || !OPERATION_ID.test(op)) return undefined;
    if (this.active !== undefined) {
      this.fail(op, ERROR_CODES.OPERATION_BUSY, 'another operation is active');
      return undefined;
    }
    this.active = new Active(op, (active) => this.contextFor(active));
    return this.active;
  }

  private prepareScenario(message: Extract<ControllerMessage, { type: 'prepare_scenario' }>): void {
    if (this.active !== undefined) {
      this.fail(message.op, ERROR_CODES.OPERATION_BUSY, 'another operation is active');
      return;
    }
    const scenario = this.scenarios.find((candidate) => candidate.id === message.scenario);
    if (scenario === undefined) {
      this.fail(message.op, ERROR_CODES.SCENARIO_NOT_FOUND, `unknown scenario "${message.scenario}"`);
      return;
    }
    const handler = this.options.handler;
    if (handler.prepareScenario === undefined) {
      this.fail(message.op, ERROR_CODES.UNSUPPORTED_OPERATION, 'this adapter does not run scenarios');
      return;
    }
    const active = this.begin(message.op);
    if (active === undefined) return;
    const prepare = handler.prepareScenario.bind(handler);
    void this.prepare(active, 'PREPARE_FAILED', () =>
      prepare({ op: message.op, scenario, parameters: message.parameters ?? {}, presentation: message.presentation }),
    );
  }

  private prepareReplay(message: Extract<ControllerMessage, { type: 'prepare_replay' }>): void {
    if (this.active !== undefined) {
      this.fail(message.op, ERROR_CODES.OPERATION_BUSY, 'another operation is active');
      return;
    }
    const handler = this.options.handler;
    if (handler.prepareReplay === undefined) {
      this.fail(message.op, ERROR_CODES.REPLAY_UNSUPPORTED, 'this adapter does not play replays');
      return;
    }
    const active = this.begin(message.op);
    if (active === undefined) return;
    const prepare = handler.prepareReplay.bind(handler);
    void this.prepare(active, ERROR_CODES.REPLAY_UNSUPPORTED, async () =>
      prepare({
        op: message.op,
        bytes: await readReplayHandoff(message.replay),
        handoff: message.replay,
        presentation: message.presentation,
      }),
    );
  }

  private async prepare(active: Active, fallbackCode: string, build: () => Promise<Operation> | Operation): Promise<void> {
    try {
      const operation = (await build()) ?? {};
      if (this.active !== active) {
        // Cancelled, or the connection dropped, while the handler was preparing.
        await this.dispose(operation, 'cancelled');
        return;
      }
      active.operation = operation;
      active.phase = 'ready';
      this.send({ type: 'ready', op: active.op });
    } catch (error) {
      if (this.active === active) {
        this.fail(active.op, failureCode(error, fallbackCode), failureMessage(error));
        await this.finish(active, 'failed');
      }
    }
  }

  private recordStart(message: Extract<ControllerMessage, { type: 'record_start' }>): void {
    if (this.active !== undefined) {
      this.fail(message.op, ERROR_CODES.OPERATION_BUSY, 'another operation is active');
      return;
    }
    const handler = this.options.handler;
    if (handler.startRecording === undefined || !this.capabilities.includes('freeform_recording')) {
      this.fail(message.op, ERROR_CODES.UNSUPPORTED_OPERATION, 'this adapter does not support freeform_recording');
      return;
    }
    const active = this.begin(message.op);
    if (active === undefined) return;
    active.phase = 'running';
    const startRecording = handler.startRecording.bind(handler);
    void (async () => {
      try {
        const operation = (await startRecording({ op: message.op }, active.context)) ?? {};
        if (this.active !== active) await this.dispose(operation, 'cancelled');
        else active.operation = operation;
      } catch (error) {
        await this.failActive(active, failureCode(error, 'RECORD_FAILED'), failureMessage(error));
      }
    })();
  }

  private startOperation(op: string): void {
    const active = this.active;
    const operation = active?.operation;
    if (active === undefined || active.op !== op || active.phase !== 'ready' || operation?.start === undefined) {
      this.fail(op, ERROR_CODES.PROTOCOL_OUT_OF_STATE, 'no prepared operation matches start');
      return;
    }
    active.phase = 'running';
    void this.run(active, () => operation.start?.(active.context), 'OPERATION_FAILED');
  }

  private stopOperation(op: string): void {
    const active = this.active;
    const operation = active?.operation;
    if (active === undefined || active.op !== op || active.phase !== 'running' || operation?.stop === undefined) return;
    void this.run(active, () => operation.stop?.(active.context), 'STOP_FAILED');
  }

  private async run(active: Active, call: () => Promise<OperationResult | void> | OperationResult | void, fallbackCode: string): Promise<void> {
    try {
      const outcome = await call();
      if (isOperationResult(outcome)) active.context.completed(outcome.result, outcome.replay, outcome.fields);
    } catch (error) {
      await this.failActive(active, failureCode(error, fallbackCode), failureMessage(error));
    }
  }

  private async failActive(active: Active, code: string, message: string): Promise<void> {
    if (this.active !== active) return;
    this.fail(active.op, code.length > MAX_FAILURE_CODE_LENGTH ? 'OPERATION_FAILED' : code, message);
    await this.finish(active, 'failed');
  }

  // Operation state

  private contextFor(active: Active): OperationContext {
    const live = (): boolean => this.active === active;
    return {
      op: active.op,
      signal: active.abort.signal,
      started: (fields) => {
        if (!live() || active.startedSent) return false;
        active.startedSent = true;
        this.send({ ...fields, type: 'started', op: active.op } as AdapterMessage);
        return true;
      },
      event: (name, t, payload, options) => {
        if (!EVENT_NAME.test(name)) throw new TypeError(`event name "${name}" is not one Cappy accepts`);
        if (typeof t !== 'number' || !Number.isFinite(t)) throw new TypeError(`event time must be a finite number, got ${String(t)}`);
        if (!live()) return false;
        this.send({
          ...options?.fields,
          type: 'event',
          op: active.op,
          t: Math.max(0, t),
          event: name,
          ...(options?.durationMs === undefined ? {} : { durationMs: Math.max(0, options.durationMs) }),
          ...(payload === undefined ? {} : { payload }),
        } as AdapterMessage);
        return true;
      },
      completed: (result, replay, fields) => {
        if (!live()) return false;
        if (!active.startedSent) {
          active.startedSent = true;
          this.send({ type: 'started', op: active.op });
        }
        this.send({
          ...fields,
          type: 'completed',
          op: active.op,
          ...(result === undefined ? {} : { result }),
          ...(replay === undefined ? {} : { replay }),
        } as AdapterMessage);
        void this.finish(active, 'completed');
        return true;
      },
      failed: (code, message) => {
        if (!live()) return false;
        this.fail(active.op, code.length === 0 || code.length > MAX_FAILURE_CODE_LENGTH ? 'OPERATION_FAILED' : code, message.slice(0, 2000));
        void this.finish(active, 'failed');
        return true;
      },
    };
  }

  private async finish(active: Active, reason: DisposeReason): Promise<void> {
    if (this.active === active) this.active = undefined;
    if (!active.abort.signal.aborted) active.abort.abort();
    const operation = active.operation;
    active.operation = undefined;
    if (operation !== undefined) await this.dispose(operation, reason);
  }

  private async dispose(operation: Operation, reason: DisposeReason): Promise<void> {
    try {
      await operation.dispose?.(reason);
    } catch {
      // A failing cleanup cannot change how the operation ended.
    }
  }
}
