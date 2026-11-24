# @uppercut-labs/cappy-adapter

A typed client for [Cappy](https://github.com/uppercut-labs/cappy)'s game adapter protocol, version 1. Cappy launches your game or capture tool, and an adapter inside it connects back over a loopback WebSocket to say which scenarios it can run and to report what happens. This package does the protocol part: the hello and welcome, the heartbeat, the scenario registry, and the operation state Cappy checks (one operation at a time, `started` before `completed`, nothing after the end). You write the handler that runs the game.

It also holds the two helpers every adapter ends up repeating: a child-process runner that keeps the tail of its output, and a shutdown that stops what the adapter started when Cappy hangs up.

```sh
npm install @uppercut-labs/cappy-adapter
```

## What runs where

TypeScript compiled to ESM JavaScript, with no runtime dependencies. It needs Node 22 or later, which has a `WebSocket` global; on another runtime pass a class with the same shape (the `ws` package's `WebSocket` works) as `options.WebSocket`. The command examples work in PowerShell, Bash and zsh. The child-process helpers signal the direct child only, so a launcher that starts further processes must pass a signal on itself.

## Example

```ts
import { CappyAdapterError, ProcessSupervisor, optionValue, runAdapter } from '@uppercut-labs/cappy-adapter';

const supervisor = new ProcessSupervisor();
const framesFolder = optionValue(process.argv.slice(2), '--frames-dir');

process.exit(
  await runAdapter({
    adapter: { name: 'sample-renderer', version: '1.0.0' },
    game: { id: 'sample-game', name: 'Sample Game' },
    capabilities: ['scenarios'],
    scenarios: [
      {
        id: 'render_clip',
        name: 'Render a clip',
        parameters: { length: { type: 'integer', default: 60, minimum: 1, maximum: 600 } },
        requiredCapabilities: ['scenarios'],
      },
    ],
    supervisor,
    handler: {
      prepareScenario({ op, parameters }) {
        if (framesFolder === undefined) throw new CappyAdapterError('FRAMES_FOLDER_REQUIRED', 'launch with --frames-dir');
        return {
          async start(context) {
            context.started();
            await supervisor.run('node', ['render.js', '--out', framesFolder, '--frames', String(parameters.length)]);
            context.event('RENDER_DONE', 0, { frames: parameters.length });
            return { result: { op, frames: parameters.length } };
          },
          dispose: () => supervisor.killAll(),
        };
      },
    },
  }),
);
```

`runAdapter` reads `CAPPY_ENDPOINT` and `CAPPY_SESSION_TOKEN`, connects, waits until Cappy closes the connection, stops every process the supervisor started and returns an exit code: 0 after a normal end, 1 when Cappy refuses the adapter or the connection fails, 2 when the process was not launched through Cappy.

## The handler

| Handler method | Cappy request | What it returns |
| --- | --- | --- |
| `prepareScenario({ op, scenario, parameters, presentation })` | `prepare_scenario` | an `Operation`; the client then sends `ready`. Without it the client fails with `UNSUPPORTED_OPERATION` |
| `startRecording({ op }, context)` | `record_start` | an `Operation` (or nothing); call `context.started()` yourself. Needs the `freeform_recording` capability |
| `prepareReplay({ op, bytes, handoff, presentation })` | `prepare_replay` | an `Operation`; the payload is already read and its SHA-256 checked. Without it the client fails with `REPLAY_UNSUPPORTED` |

An `Operation` has three optional methods:

| Method | When it runs |
| --- | --- |
| `start(context)` | Cappy sent `start`. Return `{ result?, replay? }` to complete the operation, or return nothing and call `context.completed()` later |
| `stop(context)` | Cappy sent `stop` (a freeform recording or a replay). Return `{ result?, replay? }` to complete. Ignored when absent |
| `dispose(reason)` | Once, however the operation ends: `completed`, `failed`, `cancelled` or `disconnected` |

The `context` has `op`, a `signal` (an `AbortSignal` that aborts when the operation ends), and `started()`, `event(name, t, payload?, { durationMs? })`, `completed(result?, replay?)` and `failed(code, message)`. Each returns `false` and does nothing once the operation has ended, so a late event from a process you are killing never reaches Cappy. `t` is operation-relative presented time in milliseconds, never wall-clock time. `completed` sends `started` first when you did not, because Cappy never accepts a `completed` before it.

A handler that throws fails the operation. A `CappyAdapterError` (or any error with a string `code`) chooses the code Cappy sees; otherwise the code is `PREPARE_FAILED`, `OPERATION_FAILED`, `STOP_FAILED`, `RECORD_FAILED` or, for a replay, `REPLAY_UNSUPPORTED`. Messages are cut to the 2000 characters Cappy accepts.

The client answers Cappy's pings at once, even while a handler is busy, because Cappy drops an adapter that is silent for six seconds. It fails a second operation with `OPERATION_BUSY`, an unknown scenario with `SCENARIO_NOT_FOUND`, and a `start` that matches nothing with `PROTOCOL_OUT_OF_STATE`. `cancel` is silent: the operation is disposed as `cancelled`.

## Exports

| Export | What it is |
| --- | --- |
| `CappyAdapter` | The connection. `connect()` resolves with Cappy's welcome and rejects with `CappyRejectedError` (the reject code is `error.code`), or a `CappyAdapterError` for a socket failure or a late welcome. `close()`, and `closed`, a promise that settles once the connection is down and the running operation is disposed |
| `runAdapter(options)` | Reads the launch environment, connects, waits, stops the supervisor's processes, returns an exit code |
| `readLaunchEnvironment(env?)`, `optionValue(argv, name)` | The endpoint and token Cappy sets, and a `--name value` or `--name=value` option such as `--movie-dir` |
| `inlineReplay(bytes, format?)`, `fileReplay(path, format?)`, `readReplayHandoff(handoff)` | Build and read the replay payload handoffs, with the SHA-256 checks. An inline payload is at most 1 MiB |
| `ProcessSupervisor` | `run(command, args, options)` resolves with the output tail on exit code 0 and otherwise rejects with `ProcessExitError`; `start(...)` returns `{ child, output, exit }`; `killAll(signal?)` stops everything still running |
| `OutputRing` | The last N characters a process wrote, with `tail(lines)` |
| `ERROR_CODES`, `PROTOCOL_VERSION`, `ENDPOINT_ENV`, `TOKEN_ENV`, limits | The codes Cappy and adapters use, and the protocol's constants |
| `assertValidRegistry` | The check the constructor runs on capabilities and scenarios |

The message types (`AdapterMessage`, `ControllerMessage`, `ScenarioDefinition`, `ReplayHandoff` and the rest) are exported too.

## Notes

- The constructor checks the capability names and scenario registry against the patterns Cappy enforces, so a bad scenario id fails at start-up and not in the middle of a capture.
- The client sends only the fields that protocol v1 defines, unless a handler asks for more. Cappy as published (1.0.2) refuses a message with an unknown field and fails the operation, so put extra values in an event's `payload`. For a controller that does define extra fields (a frame number for a movie recorder, say), `started(fields)`, `event(name, t, payload, { fields })`, `completed(result, replay, fields)` and the `fields` of a returned `OperationResult` add them. The protocol's own fields cannot be overridden.
- Per-game scenario lists, launch commands and render steps stay in the game's own adapter. This package knows nothing about any game.
- The tests run a fake controller written from the protocol page and its message schemas. The same client was also run once against Cappy's own controller code to check that they agree.

## Working on it

From the repository root:

```sh
npm install
npm run check
```

The tests need no display, game or hardware: they start a loopback WebSocket server and short Node child processes.

## License

MIT
