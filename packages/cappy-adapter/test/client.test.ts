import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { afterEach, describe, expect, it } from 'vitest';
import { WebSocket as WsSocket } from 'ws';
import {
  CappyAdapter,
  CappyAdapterError,
  CappyRejectedError,
  ERROR_CODES,
  fileReplay,
  inlineReplay,
  type AdapterHandler,
  type CappyAdapterOptions,
  type DisposeReason,
  type ScenarioDefinition,
  type WebSocketConstructor,
} from '../src/index.js';
import { FakeController, type FakeControllerOptions } from './fake-controller.js';

const scenarios: ScenarioDefinition[] = [
  {
    id: 'sample_run',
    name: 'Sample run',
    parameters: { level: { type: 'integer', minimum: 1, maximum: 9, default: 1, description: 'which level' } },
    requiredCapabilities: ['scenarios'],
  },
  { id: 'idle', name: 'Idle' },
];

const controllers: FakeController[] = [];
const adapters: CappyAdapter[] = [];
const folders: string[] = [];

afterEach(async () => {
  for (const adapter of adapters.splice(0)) adapter.close();
  for (const controller of controllers.splice(0)) await controller.close();
  for (const folder of folders.splice(0)) await rm(folder, { recursive: true, force: true });
});

async function listen(options?: FakeControllerOptions): Promise<FakeController> {
  const controller = await FakeController.listen(options);
  controllers.push(controller);
  return controller;
}

function build(controller: FakeController, handler: AdapterHandler, extra: Partial<CappyAdapterOptions> = {}): CappyAdapter {
  const adapter = new CappyAdapter({
    endpoint: controller.endpoint,
    token: controller.token,
    adapter: { name: 'sample-adapter', version: '1.2.3' },
    game: { id: 'sample-game', name: 'Sample Game' },
    build: 'build-7',
    scenarios,
    handler,
    ...extra,
  });
  adapters.push(adapter);
  return adapter;
}

async function connected(controller: FakeController, handler: AdapterHandler, extra: Partial<CappyAdapterOptions> = {}): Promise<CappyAdapter> {
  const adapter = build(controller, handler, extra);
  await adapter.connect();
  return adapter;
}

const pause = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms));

describe('handshake', () => {
  it('sends a hello that names the adapter, game, build and capabilities, then the scenario registry', async () => {
    const controller = await listen();
    const adapter = build(controller, {}, { capabilities: ['scenarios', 'frame_exact'] });
    const welcome = await adapter.connect();
    expect(welcome).toEqual({ protocol: 1, controller: { name: 'fake-controller', version: '0.0.0' } });
    expect(controller.hello).toMatchObject({
      type: 'hello',
      protocol: { min: 1, max: 1 },
      token: controller.token,
      adapter: { name: 'sample-adapter', version: '1.2.3' },
      game: { id: 'sample-game', name: 'Sample Game' },
      build: 'build-7',
      capabilities: ['scenarios', 'frame_exact'],
    });
    const registry = await controller.waitFor((message) => message.type === 'scenarios');
    expect(registry).toMatchObject({ scenarios: [{ id: 'sample_run', name: 'Sample run' }, { id: 'idle' }] });
    expect(controller.violations).toEqual([]);
  });

  it('advertises only the scenarios capability by default and leaves out an unset build', async () => {
    const controller = await listen();
    const adapter = build(controller, {}, { build: undefined });
    await adapter.connect();
    expect(controller.hello?.capabilities).toEqual(['scenarios']);
    expect(controller.hello).not.toHaveProperty('build');
  });

  it('answers list_scenarios with the registry again', async () => {
    const controller = await listen();
    await connected(controller, {});
    controller.send({ type: 'list_scenarios' });
    await pause(50);
    expect(controller.of('scenarios')).toHaveLength(2);
  });

  it('rejects connect with the code Cappy gave when it refuses the hello', async () => {
    const controller = await listen({ rejectWith: 'PROTOCOL_VERSION_INCOMPATIBLE' });
    const error = await build(controller, {}).connect().catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(CappyRejectedError);
    expect((error as CappyRejectedError).code).toBe('PROTOCOL_VERSION_INCOMPATIBLE');
  });

  it('is refused with PROTOCOL_UNAUTHENTICATED when the token is wrong', async () => {
    const controller = await listen();
    const error = await build(controller, {}, { token: 'a-different-token-0123456' }).connect().catch((caught: unknown) => caught);
    expect((error as CappyRejectedError).code).toBe(ERROR_CODES.PROTOCOL_UNAUTHENTICATED);
  });

  it('rejects connect when nothing is listening', async () => {
    const controller = await listen();
    const endpoint = controller.endpoint;
    await controller.close();
    controllers.pop();
    const adapter = new CappyAdapter({ endpoint, token: 'token-0123456789abcdefghij', adapter: { name: 'a', version: '1' }, game: { id: 'g' }, handler: {} });
    adapters.push(adapter);
    const error = await adapter.connect().catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(CappyAdapterError);
    expect(['CONNECTION_FAILED', 'CONNECTION_CLOSED']).toContain((error as CappyAdapterError).code);
  });

  it('gives up when Cappy never answers the hello', async () => {
    const controller = await listen({ silent: true });
    const error = await build(controller, {}, { handshakeTimeoutMs: 80 }).connect().catch((caught: unknown) => caught);
    expect((error as CappyAdapterError).code).toBe('HANDSHAKE_TIMEOUT');
  });

  it('refuses to connect twice', async () => {
    const controller = await listen();
    const adapter = await connected(controller, {});
    await expect(adapter.connect()).rejects.toMatchObject({ code: 'ALREADY_CONNECTED' });
  });

  it('keeps a request that arrives in the same tick as the welcome', async () => {
    const controller = await listen({ rightBehindWelcome: { type: 'ping', nonce: 'first' } });
    await connected(controller, {});
    await controller.waitFor((message) => message.type === 'pong');
    expect(controller.pongs).toEqual(['first']);
  });

  it('works with the ws package as the WebSocket class', async () => {
    const controller = await listen();
    const adapter = build(controller, {}, { WebSocket: WsSocket as unknown as WebSocketConstructor });
    await adapter.connect();
    expect(controller.hello?.type).toBe('hello');
  });

  it('rejects at construction a registry Cappy would refuse', () => {
    const base = { endpoint: 'ws://127.0.0.1:1', token: 'token-0123456789abcdefghij', adapter: { name: 'a', version: '1' }, game: { id: 'g' }, handler: {} };
    expect(() => new CappyAdapter({ ...base, scenarios: [{ id: 'Bad Id', name: 'x' }] })).toThrow(/scenario id/);
    expect(() => new CappyAdapter({ ...base, scenarios: [{ id: 'a', name: 'x' }, { id: 'a', name: 'y' }] })).toThrow(/twice/);
    expect(() => new CappyAdapter({ ...base, scenarios: [{ id: 'a', name: 'x', parameters: { '1bad': { type: 'string' } } }] })).toThrow(/parameter/);
    expect(() => new CappyAdapter({ ...base, capabilities: ['Frame-Exact'] })).toThrow(/capability/);
  });
});

describe('heartbeat', () => {
  it('answers pings while a handler is still preparing', async () => {
    const controller = await listen({ pingEveryMs: 20 });
    await connected(controller, { prepareScenario: async () => { await pause(200); return { start: () => ({}) }; } });
    controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready', 3000);
    expect(controller.pongs.length).toBeGreaterThanOrEqual(3);
    expect(controller.pongs.slice(0, 2)).toEqual(['n1', 'n2']);
  });
});

describe('scenarios', () => {
  it('runs a scenario: ready, started, events, completed', async () => {
    const controller = await listen();
    const seen: unknown[] = [];
    await connected(controller, {
      prepareScenario: (request) => {
        seen.push({ scenario: request.scenario.id, parameters: request.parameters, presentation: request.presentation });
        return {
          async start(context) {
            context.started();
            context.event('BOSS_APPEAR', 100);
            context.event('IMPACT', 500, { force: 3 }, { durationMs: 120 });
            return { result: { events: 2 } };
          },
        };
      },
    });
    const op = controller.prepareScenario('sample_run', { level: 4 }, { timeScale: 0.5 });
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'completed');
    expect(seen).toEqual([{ scenario: 'sample_run', parameters: { level: 4 }, presentation: { timeScale: 0.5 } }]);
    expect(controller.messages.map((message) => message.type)).toEqual(['scenarios', 'ready', 'started', 'event', 'event', 'completed']);
    expect(controller.of('event')[1]).toEqual({ type: 'event', op, t: 500, event: 'IMPACT', durationMs: 120, payload: { force: 3 } });
    expect(controller.of('completed')[0]).toEqual({ type: 'completed', op, result: { events: 2 } });
    expect(controller.violations).toEqual([]);
  });

  it('sends started first when a handler completes without it, since Cappy never accepts completed before started', async () => {
    const controller = await listen();
    await connected(controller, { prepareScenario: () => ({ start: () => ({ result: {} }) }) });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'completed');
    expect(controller.of('started', op)).toHaveLength(1);
    expect(controller.violations).toEqual([]);
  });

  it('lets a handler complete later from its own events', async () => {
    const controller = await listen();
    let hold: (() => void) | undefined;
    await connected(controller, {
      prepareScenario: () => ({
        start(context) {
          context.started();
          hold = () => context.completed({ done: true });
        },
      }),
    });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'started');
    expect(controller.of('completed')).toHaveLength(0);
    hold?.();
    await controller.waitFor((message) => message.type === 'completed');
    expect(controller.violations).toEqual([]);
  });

  it('clamps negative event times to zero and refuses names Cappy would reject', async () => {
    const controller = await listen();
    const errors: unknown[] = [];
    await connected(controller, {
      prepareScenario: () => ({
        start(context) {
          context.started();
          context.event('EARLY', -5);
          for (const attempt of [() => context.event('bad name', 1), () => context.event('OK', Number.NaN)]) {
            try {
              attempt();
            } catch (error) {
              errors.push((error as Error).constructor.name);
            }
          }
          return {};
        },
      }),
    });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'completed');
    expect(controller.of('event')[0]?.t).toBe(0);
    expect(errors).toEqual(['TypeError', 'TypeError']);
    expect(controller.violations).toEqual([]);
  });

  it('passes extension fields through started, event and completed, and never lets them replace protocol fields', async () => {
    const controller = await listen({ extensionFields: ['frame'] });
    await connected(controller, {
      prepareScenario: () => ({
        start(context) {
          context.started({ frame: 0, type: 'bogus', op: 'bogus' });
          context.event('HIT', 100, { n: 1 }, { fields: { frame: 6, event: 'bogus' } });
          return { result: { ok: true }, fields: { frame: 9 } };
        },
      }),
    });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'completed');
    expect(controller.of('started')[0]).toEqual({ type: 'started', op, frame: 0 });
    expect(controller.of('event')[0]).toEqual({ type: 'event', op, t: 100, event: 'HIT', payload: { n: 1 }, frame: 6 });
    expect(controller.of('completed')[0]).toEqual({ type: 'completed', op, result: { ok: true }, frame: 9 });
    expect(controller.violations).toEqual([]);
  });

  it('leaves extension fields out unless a handler asks, so a strict controller sees plain protocol v1', async () => {
    const controller = await listen();
    await connected(controller, { prepareScenario: () => ({ start: (context) => { context.started(); context.event('E', 1); return {}; } }) });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'completed');
    expect(controller.violations).toEqual([]);
  });

  it('fails an unknown scenario with SCENARIO_NOT_FOUND', async () => {
    const controller = await listen();
    await connected(controller, { prepareScenario: () => ({}) });
    controller.prepareScenario('missing');
    const failed = await controller.waitFor((message) => message.type === 'failed');
    expect(failed).toMatchObject({ code: 'SCENARIO_NOT_FOUND' });
    expect(controller.of('ready')).toHaveLength(0);
  });

  it('fails when a handler refuses, using its code, or PREPARE_FAILED without one', async () => {
    const controller = await listen();
    let attempt = 0;
    await connected(controller, {
      prepareScenario: () => {
        attempt += 1;
        if (attempt === 1) throw new CappyAdapterError(ERROR_CODES.INVALID_PARAMETERS, 'level out of range');
        throw new Error('disk full');
      },
    });
    controller.prepareScenario('sample_run');
    await controller.waitFor((message) => message.type === 'failed');
    controller.prepareScenario('sample_run');
    await controller.waitFor(() => controller.of('failed').length === 2);
    expect(controller.of('failed').map((message) => [message.code, message.message])).toEqual([
      ['INVALID_PARAMETERS', 'level out of range'],
      ['PREPARE_FAILED', 'disk full'],
    ]);
    expect(controller.violations).toEqual([]);
  });

  it('fails when start throws, truncating a long message and replacing an unusable code', async () => {
    const controller = await listen();
    await connected(controller, {
      prepareScenario: () => ({
        start() {
          throw Object.assign(new Error('x'.repeat(5000)), { code: 'C'.repeat(200) });
        },
      }),
    });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    const failed = await controller.waitFor((message) => message.type === 'failed');
    expect(failed).toMatchObject({ code: 'OPERATION_FAILED' });
    expect((failed as { message: string }).message).toHaveLength(2000);
    expect(controller.violations).toEqual([]);
  });

  it('fails a second operation with OPERATION_BUSY and leaves the first running', async () => {
    const controller = await listen();
    await connected(controller, { prepareScenario: () => ({ start: (context) => void context.started() }) });
    const first = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    const second = controller.prepareScenario('idle');
    const failed = await controller.waitFor((message) => message.type === 'failed');
    expect(failed).toMatchObject({ op: second, code: 'OPERATION_BUSY' });
    controller.start(first);
    await controller.waitFor((message) => message.type === 'started');
    expect(controller.of('started', first)).toHaveLength(1);
  });

  it('fails a start that matches no prepared operation with PROTOCOL_OUT_OF_STATE', async () => {
    const controller = await listen();
    await connected(controller, { prepareScenario: () => ({ start: () => ({}) }) });
    controller.send({ type: 'start', op: 'op_stray' });
    const failed = await controller.waitFor((message) => message.type === 'failed');
    expect(failed).toMatchObject({ op: 'op_stray', code: 'PROTOCOL_OUT_OF_STATE' });
  });

  it('fails when the adapter has no handler for scenarios', async () => {
    const controller = await listen();
    await connected(controller, {});
    controller.prepareScenario('idle');
    expect(await controller.waitFor((message) => message.type === 'failed')).toMatchObject({ code: 'UNSUPPORTED_OPERATION' });
  });
});

describe('ending an operation', () => {
  it('cancel disposes a running operation silently and aborts its signal', async () => {
    const controller = await listen();
    const log: string[] = [];
    let context: { event(name: string, t: number): boolean; signal: AbortSignal } | undefined;
    await connected(controller, {
      prepareScenario: () => ({
        start(ctx) {
          ctx.started();
          context = ctx;
        },
        dispose: (reason) => void log.push(reason),
      }),
    });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'started');
    controller.cancel(op);
    await pause(60);
    expect(log).toEqual(['cancelled']);
    expect(context?.signal.aborted).toBe(true);
    const before = controller.messages.length;
    expect(context?.event('LATE', 1)).toBe(false);
    await pause(30);
    expect(controller.messages).toHaveLength(before);
  });

  it('cancel while preparing disposes the operation when it arrives and never sends ready', async () => {
    const controller = await listen();
    const log: string[] = [];
    await connected(controller, {
      prepareScenario: async () => {
        await pause(80);
        return { dispose: (reason) => void log.push(reason) };
      },
    });
    const op = controller.prepareScenario('idle');
    await pause(20);
    controller.cancel(op);
    await pause(200);
    expect(log).toEqual(['cancelled']);
    expect(controller.of('ready')).toHaveLength(0);
  });

  it('frees the connection for the next operation after one ends', async () => {
    const controller = await listen();
    const reasons: DisposeReason[] = [];
    await connected(controller, { prepareScenario: () => ({ start: () => ({}), dispose: (reason) => void reasons.push(reason) }) });
    for (let round = 0; round < 2; round += 1) {
      const op = controller.prepareScenario('idle');
      await controller.waitFor((message) => message.type === 'ready' && message.op === op);
      controller.start(op);
      await controller.waitFor((message) => message.type === 'completed' && message.op === op);
    }
    expect(reasons).toEqual(['completed', 'completed']);
    expect(controller.violations).toEqual([]);
  });

  it('disposes as failed when a handler reports failure through the context', async () => {
    const controller = await listen();
    const reasons: DisposeReason[] = [];
    await connected(controller, {
      prepareScenario: () => ({
        start: (context) => {
          context.started();
          context.failed('EMULATOR_EXIT', 'the emulator exited');
          expect(context.completed()).toBe(false);
        },
        dispose: (reason) => void reasons.push(reason),
      }),
    });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    expect(await controller.waitFor((message) => message.type === 'failed')).toMatchObject({ code: 'EMULATOR_EXIT' });
    await pause(30);
    expect(reasons).toEqual(['failed']);
    expect(controller.of('completed')).toHaveLength(0);
  });

  it('disposes the running operation as disconnected when Cappy drops the connection, then resolves closed', async () => {
    const controller = await listen();
    const reasons: DisposeReason[] = [];
    const adapter = await connected(controller, {
      prepareScenario: () => ({ start: (context) => void context.started(), dispose: (reason) => void reasons.push(reason) }),
    });
    const op = controller.prepareScenario('idle');
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'started');
    controller.crash();
    await adapter.closed;
    expect(reasons).toEqual(['disconnected']);
  });

  it('resolves closed after a normal hang-up with nothing running', async () => {
    const controller = await listen();
    const adapter = await connected(controller, {});
    controller.hangUp();
    await adapter.closed;
  });

  it('reports an error message from Cappy to onControllerError', async () => {
    const controller = await listen();
    const reports: unknown[] = [];
    await connected(controller, {}, { onControllerError: (error) => void reports.push(error) });
    controller.send({ type: 'error', code: 'PROTOCOL_OUT_OF_STATE', message: 'nope', op: 'op_x' });
    await pause(50);
    expect(reports).toEqual([{ code: 'PROTOCOL_OUT_OF_STATE', message: 'nope', op: 'op_x' }]);
  });
});

describe('freeform recording', () => {
  const capabilities = ['scenarios', 'freeform_recording'];

  it('records until stop and hands the replay back inline', async () => {
    const controller = await listen();
    const payload = new TextEncoder().encode('opaque replay bytes');
    await connected(controller, {
      startRecording: (_request, context) => {
        context.started();
        context.event('PLAYER_SPAWN', 0);
        return { stop: () => ({ result: { events: 1 }, replay: inlineReplay(payload, 'sample-replay') }) };
      },
    }, { capabilities });
    const op = controller.recordStart();
    await controller.waitFor((message) => message.type === 'started');
    controller.stop(op);
    const completed = await controller.waitFor((message) => message.type === 'completed');
    expect(completed).toMatchObject({ result: { events: 1 }, replay: { kind: 'inline', format: 'sample-replay' } });
    expect(controller.violations).toEqual([]);
  });

  it('can hand the replay over as a file', async () => {
    const controller = await listen();
    const folder = await mkdtemp(path.join(tmpdir(), 'cappy-adapter-'));
    folders.push(folder);
    const file = path.join(folder, 'take.bin');
    await writeFile(file, 'file bytes');
    await connected(controller, {
      startRecording: (_request, context) => {
        context.started();
        return { stop: async () => ({ replay: await fileReplay(file) }) };
      },
    }, { capabilities });
    const op = controller.recordStart();
    await controller.waitFor((message) => message.type === 'started');
    controller.stop(op);
    const completed = await controller.waitFor((message) => message.type === 'completed');
    expect(completed).toMatchObject({ replay: { kind: 'file', path: file, bytes: 10 } });
    expect(controller.violations).toEqual([]);
  });

  it('fails record_start without the capability or the handler', async () => {
    const controller = await listen();
    await connected(controller, { startRecording: () => ({}) });
    controller.recordStart();
    expect(await controller.waitFor((message) => message.type === 'failed')).toMatchObject({ code: 'UNSUPPORTED_OPERATION' });
  });

  it('fails the recording when the handler throws', async () => {
    const controller = await listen();
    await connected(controller, { startRecording: () => { throw new Error('no capture device'); } }, { capabilities });
    controller.recordStart();
    expect(await controller.waitFor((message) => message.type === 'failed')).toMatchObject({ code: 'RECORD_FAILED', message: 'no capture device' });
    controller.recordStart();
    await controller.waitFor(() => controller.of('failed').length === 2);
  });
});

describe('replays', () => {
  it('checks the payload and hands the verified bytes and the presentation to the handler', async () => {
    const controller = await listen();
    const requests: { text: string; presentation: unknown }[] = [];
    await connected(controller, {
      prepareReplay: (request) => {
        requests.push({ text: new TextDecoder().decode(request.bytes), presentation: request.presentation });
        return { start: () => ({ result: {} }) };
      },
    });
    const op = controller.prepareReplay(inlineReplay(new TextEncoder().encode('replay body')), { timeScale: 0.5, camera: 'close' });
    await controller.waitFor((message) => message.type === 'ready');
    controller.start(op);
    await controller.waitFor((message) => message.type === 'completed');
    expect(requests).toEqual([{ text: 'replay body', presentation: { timeScale: 0.5, camera: 'close' } }]);
  });

  it('fails REPLAY_CORRUPT when the SHA-256 does not match', async () => {
    const controller = await listen();
    await connected(controller, { prepareReplay: () => ({}) });
    const replay = { ...inlineReplay(new TextEncoder().encode('abc')), sha256: '0'.repeat(64) };
    controller.prepareReplay(replay);
    expect(await controller.waitFor((message) => message.type === 'failed')).toMatchObject({ code: 'REPLAY_CORRUPT' });
    expect(controller.of('ready')).toHaveLength(0);
  });

  it('reads a replay handed over as a file', async () => {
    const controller = await listen();
    const folder = await mkdtemp(path.join(tmpdir(), 'cappy-adapter-'));
    folders.push(folder);
    const file = path.join(folder, 'in.bin');
    await writeFile(file, 'from a file');
    let text = '';
    await connected(controller, {
      prepareReplay: (request) => {
        text = new TextDecoder().decode(request.bytes);
        return {};
      },
    });
    controller.prepareReplay(await fileReplay(file));
    await controller.waitFor((message) => message.type === 'ready');
    expect(text).toBe('from a file');
  });

  it('lets the handler refuse a camera or time scale with its own code', async () => {
    const controller = await listen();
    await connected(controller, {
      prepareReplay: (request) => {
        if (request.presentation?.camera !== undefined) throw new CappyAdapterError(ERROR_CODES.UNKNOWN_CAMERA, 'unknown camera "far"');
        return {};
      },
    });
    controller.prepareReplay(inlineReplay(new Uint8Array([1])), { camera: 'far' });
    expect(await controller.waitFor((message) => message.type === 'failed')).toMatchObject({ code: 'UNKNOWN_CAMERA' });
  });

  it('fails REPLAY_UNSUPPORTED when the adapter plays no replays', async () => {
    const controller = await listen();
    await connected(controller, {});
    controller.prepareReplay(inlineReplay(new Uint8Array([1])));
    expect(await controller.waitFor((message) => message.type === 'failed')).toMatchObject({ code: 'REPLAY_UNSUPPORTED' });
  });
});
