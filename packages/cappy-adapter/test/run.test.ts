import { afterEach, describe, expect, it } from 'vitest';
import { ENDPOINT_ENV, ProcessSupervisor, TOKEN_ENV, runAdapter } from '../src/index.js';
import { FakeController } from './fake-controller.js';

const controllers: FakeController[] = [];
afterEach(async () => {
  for (const controller of controllers.splice(0)) await controller.close();
});

const identity = { adapter: { name: 'sample-adapter', version: '1.0.0' }, game: { id: 'sample-game' }, handler: {}, handleSignals: false };

describe('runAdapter', () => {
  it('returns 2 and says so when the process was not launched through Cappy', async () => {
    const lines: string[] = [];
    const code = await runAdapter({ ...identity, env: {}, log: (line) => void lines.push(line) });
    expect(code).toBe(2);
    expect(lines.join('\n')).toContain(ENDPOINT_ENV);
  });

  it('returns 1 when Cappy refuses it, and stops the supervisor\'s processes', async () => {
    const controller = await FakeController.listen({ rejectWith: 'CAPABILITY_MISSING' });
    controllers.push(controller);
    const supervisor = new ProcessSupervisor();
    const child = supervisor.start(process.execPath, ['-e', 'setInterval(() => {}, 1000)']);
    const lines: string[] = [];
    const code = await runAdapter({
      ...identity,
      env: { [ENDPOINT_ENV]: controller.endpoint, [TOKEN_ENV]: controller.token },
      supervisor,
      log: (line) => void lines.push(line),
    });
    expect(code).toBe(1);
    expect(lines.join('\n')).toContain('CAPABILITY_MISSING');
    expect((await child.exit).signal).toBe('SIGTERM');
  });

  it('returns 0 when Cappy ends the session, after stopping the supervisor\'s processes', async () => {
    const controller = await FakeController.listen();
    controllers.push(controller);
    const supervisor = new ProcessSupervisor();
    const child = supervisor.start(process.execPath, ['-e', 'setInterval(() => {}, 1000)']);
    const running = runAdapter({ ...identity, env: { [ENDPOINT_ENV]: controller.endpoint, [TOKEN_ENV]: controller.token }, supervisor });
    await controller.waitFor((message) => message.type === 'scenarios');
    controller.hangUp();
    expect(await running).toBe(0);
    expect((await child.exit).signal).toBe('SIGTERM');
  });

  it('logs what Cappy says about a failed operation', async () => {
    const controller = await FakeController.listen();
    controllers.push(controller);
    const lines: string[] = [];
    const running = runAdapter({
      ...identity,
      env: { [ENDPOINT_ENV]: controller.endpoint, [TOKEN_ENV]: controller.token },
      log: (line) => void lines.push(line),
    });
    await controller.waitFor((message) => message.type === 'scenarios');
    controller.send({ type: 'error', code: 'PROTOCOL_OUT_OF_STATE', message: 'late message' });
    await new Promise((resolve) => setTimeout(resolve, 50));
    controller.hangUp();
    await running;
    expect(lines).toEqual(['sample-adapter: Cappy reports PROTOCOL_OUT_OF_STATE: late message']);
  });
});
