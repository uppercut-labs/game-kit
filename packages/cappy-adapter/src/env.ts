import { CappyAdapterError } from './errors.js';
import { ENDPOINT_ENV, TOKEN_ENV } from './protocol.js';

export interface LaunchEnvironment {
  endpoint: string;
  token: string;
}

/** Reads the endpoint and session token Cappy sets for a game it launches. Throws `NOT_LAUNCHED_BY_CAPPY` when either is missing. */
export function readLaunchEnvironment(env: NodeJS.ProcessEnv = process.env): LaunchEnvironment {
  const endpoint = env[ENDPOINT_ENV];
  const token = env[TOKEN_ENV];
  if (endpoint === undefined || endpoint === '' || token === undefined || token === '') {
    throw new CappyAdapterError('NOT_LAUNCHED_BY_CAPPY', `${ENDPOINT_ENV} and ${TOKEN_ENV} are not set; run this adapter through Cappy`);
  }
  return { endpoint, token };
}

/**
 * The value of a command-line option, from `--name value` or `--name=value`, or undefined when it is absent or has no
 * value. Cappy passes a launch argument such as `--movie-dir {movieDir}` this way.
 */
export function optionValue(argv: readonly string[], name: string): string | undefined {
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index] as string;
    if (arg === name) {
      const next = argv[index + 1];
      return next === undefined || next.startsWith('--') ? undefined : next;
    }
    if (arg.startsWith(`${name}=`)) return arg.slice(name.length + 1);
  }
  return undefined;
}
