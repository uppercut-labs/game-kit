import { CappyAdapter, type CappyAdapterOptions } from './client.js';
import { readLaunchEnvironment } from './env.js';
import { ProcessSupervisor } from './process.js';

export interface RunAdapterOptions extends Omit<CappyAdapterOptions, 'endpoint' | 'token'> {
  /** Where to read `CAPPY_ENDPOINT` and `CAPPY_SESSION_TOKEN`. Defaults to `process.env`. */
  env?: NodeJS.ProcessEnv;
  /** Processes to stop when the connection ends. */
  supervisor?: ProcessSupervisor;
  /** Where diagnostics go. Defaults to a line on stderr. */
  log?(line: string): void;
  /** Stop on SIGINT and SIGTERM. Defaults to true. */
  handleSignals?: boolean;
}

/**
 * Runs an adapter as a game process: reads the launch environment, connects, waits until Cappy closes the connection
 * (or a signal arrives), then stops every process the supervisor started. Resolves with an exit code to hand to
 * `process.exit`: 0 after a normal close, 1 when Cappy refuses or the connection fails, 2 when the process was not
 * launched through Cappy.
 */
export async function runAdapter(options: RunAdapterOptions): Promise<number> {
  const log = options.log ?? ((line: string) => void process.stderr.write(`${line}\n`));
  let launch;
  try {
    launch = readLaunchEnvironment(options.env);
  } catch (error) {
    log(`${options.adapter.name}: ${(error as Error).message}`);
    return 2;
  }
  const adapter = new CappyAdapter({
    ...options,
    ...launch,
    onControllerError:
      options.onControllerError ?? ((error) => log(`${options.adapter.name}: Cappy reports ${error.code}: ${error.message}`)),
  });
  const stop = (): void => adapter.close();
  if (options.handleSignals !== false) {
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  }
  try {
    await adapter.connect();
    await adapter.closed;
    return 0;
  } catch (error) {
    log(`${options.adapter.name}: ${(error as Error).message}`);
    return 1;
  } finally {
    process.removeListener('SIGINT', stop);
    process.removeListener('SIGTERM', stop);
    adapter.close();
    options.supervisor?.killAll();
  }
}
