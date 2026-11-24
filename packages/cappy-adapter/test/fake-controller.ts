import { createHash } from 'node:crypto';
import { WebSocketServer, type WebSocket } from 'ws';
import type { AdapterMessage, ControllerMessage } from '../src/index.js';

/*
 * A stand-in for Cappy's side of protocol v1, written from the protocol page and its message schemas. It checks every
 * adapter message the way Cappy does: unknown fields on a message are refused, patterns and limits are enforced, and
 * the operation state is tracked (`ready` only while preparing, `started` only after `start`, `completed` only while
 * running). Anything it dislikes is recorded in `violations`, so a test can assert that the adapter never broke the
 * protocol.
 */

const OP = /^[A-Za-z0-9_-]{1,64}$/;
const EVENT = /^[A-Za-z][A-Za-z0-9_.:-]{0,127}$/;
const CAPABILITY = /^[a-z][a-z0-9_]{0,63}$/;
const SCENARIO_ID = /^[a-z0-9][a-z0-9_.-]{0,127}$/;
const PARAMETER = /^[A-Za-z_][A-Za-z0-9_]*$/;
const SHA = /^[a-f0-9]{64}$/;

type Json = Record<string, unknown>;
const isObject = (value: unknown): value is Json => typeof value === 'object' && value !== null && !Array.isArray(value);

function onlyKeys(value: Json, allowed: readonly string[], where: string): string | undefined {
  const extra = Object.keys(value).filter((key) => !allowed.includes(key));
  return extra.length > 0 ? `${where}: unrecognized key "${extra[0]}"` : undefined;
}

function checkReplay(replay: unknown): string | undefined {
  if (!isObject(replay)) return 'replay: not an object';
  if (replay.kind === 'inline') {
    return (
      onlyKeys(replay, ['kind', 'encoding', 'data', 'sha256', 'format'], 'replay') ??
      (replay.encoding !== 'base64' ? 'replay: encoding must be base64' : undefined) ??
      (typeof replay.data !== 'string' || !/^[A-Za-z0-9+/]*={0,2}$/.test(replay.data) ? 'replay: data is not base64' : undefined) ??
      (typeof replay.sha256 !== 'string' || !SHA.test(replay.sha256) ? 'replay: sha256' : undefined)
    );
  }
  if (replay.kind === 'file') {
    return (
      onlyKeys(replay, ['kind', 'path', 'sha256', 'bytes', 'format'], 'replay') ??
      (typeof replay.path !== 'string' || replay.path === '' ? 'replay: path' : undefined) ??
      (typeof replay.sha256 !== 'string' || !SHA.test(replay.sha256) ? 'replay: sha256' : undefined) ??
      (!Number.isInteger(replay.bytes) || (replay.bytes as number) < 0 ? 'replay: bytes' : undefined)
    );
  }
  return 'replay: unknown kind';
}

function checkScenario(scenario: unknown): string | undefined {
  if (!isObject(scenario)) return 'scenario: not an object';
  const extra = onlyKeys(scenario, ['id', 'name', 'parameters', 'requiredCapabilities', 'metadata'], 'scenario');
  if (extra !== undefined) return extra;
  if (typeof scenario.id !== 'string' || !SCENARIO_ID.test(scenario.id)) return 'scenario: id';
  if (typeof scenario.name !== 'string' || scenario.name === '') return 'scenario: name';
  if (scenario.parameters !== undefined) {
    if (!isObject(scenario.parameters)) return 'scenario: parameters';
    for (const [name, spec] of Object.entries(scenario.parameters)) {
      if (!PARAMETER.test(name) || !isObject(spec)) return `scenario: parameter ${name}`;
      const bad = onlyKeys(spec, ['type', 'description', 'required', 'default', 'enum', 'minimum', 'maximum'], `parameter ${name}`);
      if (bad !== undefined) return bad;
      if (!['string', 'number', 'integer', 'boolean'].includes(spec.type as string)) return `parameter ${name}: type`;
    }
  }
  for (const capability of (scenario.requiredCapabilities as unknown[] | undefined) ?? []) {
    if (typeof capability !== 'string' || !CAPABILITY.test(capability)) return 'scenario: requiredCapabilities';
  }
  return undefined;
}

/** The reason Cappy would refuse an adapter message, or undefined when the message is well formed. */
export function schemaViolation(message: unknown): string | undefined {
  if (!isObject(message) || typeof message.type !== 'string') return 'not a message';
  const op = (): string | undefined => (typeof message.op === 'string' && OP.test(message.op) ? undefined : `${message.type}: op`);
  switch (message.type) {
    case 'hello': {
      const protocol = message.protocol;
      if (!isObject(protocol) || protocol.min !== 1 || protocol.max !== 1) return 'hello: protocol';
      if (typeof message.token !== 'string' || message.token.length < 16 || message.token.length > 256) return 'hello: token';
      const adapter = message.adapter;
      if (!isObject(adapter) || onlyKeys(adapter, ['name', 'version'], 'adapter') !== undefined) return 'hello: adapter';
      if (typeof adapter.name !== 'string' || adapter.name === '' || typeof adapter.version !== 'string' || adapter.version === '') return 'hello: adapter';
      if (!isObject(message.game) || typeof message.game.id !== 'string' || message.game.id === '') return 'hello: game';
      if (message.build !== undefined && (typeof message.build !== 'string' || message.build === '' || message.build.length > 256)) return 'hello: build';
      if (!Array.isArray(message.capabilities) || !message.capabilities.every((name) => typeof name === 'string' && CAPABILITY.test(name))) {
        return 'hello: capabilities';
      }
      return undefined;
    }
    case 'scenarios':
      if (onlyKeys(message, ['type', 'scenarios'], 'scenarios') !== undefined) return 'scenarios: unrecognized key';
      if (!Array.isArray(message.scenarios)) return 'scenarios: list';
      return message.scenarios.map(checkScenario).find((problem) => problem !== undefined);
    case 'ready':
    case 'started':
      return onlyKeys(message, ['type', 'op'], message.type) ?? op();
    case 'event':
      return (
        onlyKeys(message, ['type', 'op', 't', 'event', 'durationMs', 'payload'], 'event') ??
        op() ??
        (typeof message.t !== 'number' || !(message.t >= 0) ? 'event: t' : undefined) ??
        (typeof message.event !== 'string' || !EVENT.test(message.event) ? 'event: name' : undefined) ??
        (message.durationMs !== undefined && (typeof message.durationMs !== 'number' || !(message.durationMs >= 0)) ? 'event: durationMs' : undefined)
      );
    case 'completed':
      return (
        onlyKeys(message, ['type', 'op', 'result', 'replay'], 'completed') ??
        op() ??
        (message.result !== undefined && !isObject(message.result) ? 'completed: result' : undefined) ??
        (message.replay !== undefined ? checkReplay(message.replay) : undefined)
      );
    case 'failed':
      return (
        onlyKeys(message, ['type', 'op', 'code', 'message'], 'failed') ??
        op() ??
        (typeof message.code !== 'string' || message.code === '' || message.code.length > 128 ? 'failed: code' : undefined) ??
        (typeof message.message !== 'string' || message.message.length > 2000 ? 'failed: message' : undefined)
      );
    case 'pong':
      return onlyKeys(message, ['type', 'nonce'], 'pong') ?? (typeof message.nonce === 'string' && message.nonce.length <= 64 ? undefined : 'pong: nonce');
    default:
      return `unknown adapter message "${message.type}"`;
  }
}

type State = 'preparing' | 'ready' | 'starting' | 'running' | 'stopping' | 'done';

export interface FakeControllerOptions {
  /** Defaults to a fixed 24-character token. */
  token?: string;
  /** Answer the hello with a reject carrying this code, instead of a welcome. */
  rejectWith?: string;
  /** Never answer the hello. */
  silent?: boolean;
  /** Send this message in the same tick as the welcome. */
  rightBehindWelcome?: ControllerMessage;
  /** Ping every this many milliseconds once welcomed. */
  pingEveryMs?: number;
}

/** A listening fake controller and the messages it has seen. */
export class FakeController {
  readonly token: string;
  readonly endpoint: string;
  /** Adapter messages after the hello, in order. */
  readonly messages: AdapterMessage[] = [];
  /** Every protocol problem found in what the adapter sent. */
  readonly violations: string[] = [];
  readonly pongs: string[] = [];
  hello: Record<string, unknown> | undefined;
  private socket: WebSocket | undefined;
  private readonly states = new Map<string, State>();
  private counter = 0;
  private waiters: (() => void)[] = [];
  private pinger: NodeJS.Timeout | undefined;

  private constructor(
    private readonly server: WebSocketServer,
    private readonly options: FakeControllerOptions,
    port: number,
  ) {
    this.token = options.token ?? 'token-0123456789abcdefghij';
    this.endpoint = `ws://127.0.0.1:${port}`;
    server.on('connection', (socket) => this.onConnection(socket));
  }

  static async listen(options: FakeControllerOptions = {}): Promise<FakeController> {
    const server = new WebSocketServer({ host: '127.0.0.1', port: 0 });
    await new Promise<void>((resolve) => server.once('listening', () => resolve()));
    const port = (server.address() as { port: number }).port;
    return new FakeController(server, options, port);
  }

  async close(): Promise<void> {
    clearInterval(this.pinger);
    for (const client of this.server.clients) client.terminate();
    await new Promise<void>((resolve) => this.server.close(() => resolve()));
  }

  /** Close the adapter's connection, like Cappy ending a session. */
  hangUp(): void {
    this.socket?.close();
  }

  /** Drop the connection without a close handshake. */
  crash(): void {
    this.socket?.terminate();
  }

  send(message: ControllerMessage): void {
    this.socket?.send(JSON.stringify(message));
  }

  /** Cappy's id for a new operation. */
  newOp(): string {
    this.counter += 1;
    return `op_test-${this.counter}`;
  }

  /** Begins an authored scenario and returns its operation id. */
  prepareScenario(scenario: string, parameters: Record<string, string | number | boolean> = {}, presentation?: Record<string, unknown>): string {
    const op = this.newOp();
    this.states.set(op, 'preparing');
    this.send({ type: 'prepare_scenario', op, scenario, parameters, ...(presentation === undefined ? {} : { presentation }) });
    return op;
  }

  recordStart(): string {
    const op = this.newOp();
    this.states.set(op, 'starting');
    this.send({ type: 'record_start', op });
    return op;
  }

  prepareReplay(replay: Extract<ControllerMessage, { type: 'prepare_replay' }>['replay'], presentation?: Record<string, unknown>): string {
    const op = this.newOp();
    this.states.set(op, 'preparing');
    this.send({ type: 'prepare_replay', op, replay, ...(presentation === undefined ? {} : { presentation }) });
    return op;
  }

  start(op: string): void {
    this.states.set(op, 'starting');
    this.send({ type: 'start', op });
  }

  stop(op: string): void {
    this.states.set(op, 'stopping');
    this.send({ type: 'stop', op });
  }

  cancel(op: string): void {
    this.states.set(op, 'done');
    this.send({ type: 'cancel', op });
  }

  /** Messages of one type, for one operation when `op` is given. */
  of<T extends AdapterMessage['type']>(type: T, op?: string): Extract<AdapterMessage, { type: T }>[] {
    return this.messages.filter(
      (message): message is Extract<AdapterMessage, { type: T }> => message.type === type && (op === undefined || (message as { op?: string }).op === op),
    );
  }

  /** Resolves when a message matching `predicate` has arrived (including ones that already did). */
  async waitFor(predicate: (message: AdapterMessage) => boolean, timeoutMs = 2000): Promise<AdapterMessage> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const found = this.messages.find(predicate);
      if (found !== undefined) return found;
      if (Date.now() > deadline) throw new Error(`timed out waiting for a message; saw ${JSON.stringify(this.messages)}`);
      await new Promise<void>((resolve) => {
        this.waiters.push(resolve);
        setTimeout(resolve, 25);
      });
    }
  }

  private onConnection(socket: WebSocket): void {
    this.socket = socket;
    socket.on('message', (data, isBinary) => {
      if (isBinary) {
        this.violations.push('binary frame');
        return;
      }
      this.onText(data.toString(), socket);
    });
  }

  private onText(text: string, socket: WebSocket): void {
    let message: unknown;
    try {
      message = JSON.parse(text);
    } catch {
      this.violations.push('not JSON');
      return;
    }
    const problem = schemaViolation(message);
    if (problem !== undefined) {
      this.violations.push(problem);
      return;
    }
    const typed = message as AdapterMessage;
    if (typed.type === 'hello') {
      this.hello = typed as unknown as Record<string, unknown>;
      if (this.options.silent === true) return;
      if (this.options.rejectWith !== undefined || typed.token !== this.token) {
        const code = this.options.rejectWith ?? 'PROTOCOL_UNAUTHENTICATED';
        this.send({ type: 'reject', code, message: `refused: ${code}` });
        socket.close();
        return;
      }
      this.send({ type: 'welcome', protocol: 1, controller: { name: 'fake-controller', version: '0.0.0' } });
      if (this.options.rightBehindWelcome !== undefined) this.send(this.options.rightBehindWelcome);
      if (this.options.pingEveryMs !== undefined) {
        let nonce = 0;
        this.pinger = setInterval(() => this.send({ type: 'ping', nonce: `n${(nonce += 1)}` }), this.options.pingEveryMs);
      }
      return;
    }
    this.track(typed);
    this.messages.push(typed);
    if (typed.type === 'pong') this.pongs.push(typed.nonce);
    for (const wake of this.waiters.splice(0)) wake();
  }

  /** Apply the operation state rules Cappy enforces. */
  private track(message: AdapterMessage): void {
    if (!('op' in message)) return;
    const state = this.states.get(message.op);
    if (state === undefined) {
      // A failed for an operation Cappy started is fine; anything else for an unknown operation is not.
      if (message.type !== 'failed') this.violations.push(`${message.type} for unknown operation ${message.op}`);
      return;
    }
    switch (message.type) {
      case 'ready':
        if (state !== 'preparing') this.violations.push(`ready in state ${state}`);
        this.states.set(message.op, 'ready');
        return;
      case 'started':
        if (state !== 'starting') this.violations.push(`started in state ${state}`);
        this.states.set(message.op, 'running');
        return;
      case 'event':
        return;
      case 'completed':
        if (state !== 'running' && state !== 'stopping') this.violations.push(`completed in state ${state}`);
        if (message.replay?.kind === 'inline' && createHash('sha256').update(Buffer.from(message.replay.data, 'base64')).digest('hex') !== message.replay.sha256) {
          this.violations.push('inline replay does not match its sha256');
        }
        this.states.set(message.op, 'done');
        return;
      case 'failed':
        this.states.set(message.op, 'done');
        return;
    }
  }
}
