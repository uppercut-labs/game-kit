// A stand-in for an emulator, for tests. It records how it was started and prints what the test asks for.
//   FAKE_ARGS_OUT   file to write { argv, env } to, as JSON, once it is set up and handling signals
//   FAKE_ENV_KEYS   comma-separated environment names to include in that file
//   FAKE_LINES      JSON array of lines to print; "err:" at the start sends a line to stderr
//   FAKE_RAW        text printed to stdout exactly as given (no newline added)
//   FAKE_IGNORE_TERM=1   ignore SIGTERM (only SIGKILL stops it); otherwise SIGTERM ends it with code 143.
//                        Unix only: on Windows any signal ends the process at once and no handler runs.
//   FAKE_QUIT_FILE       a file that, once it holds this process's pid, makes it exit with code 7 (a clean quit).
//                        A file rather than a signal, so the quit works the same on Windows.
//   FAKE_EXIT_AFTER_MS   exit with code 3 after this long
import { readFileSync, writeFileSync } from 'node:fs';

const env = process.env;
for (const entry of JSON.parse(env.FAKE_LINES ?? '[]')) {
  if (entry.startsWith('err:')) process.stderr.write(`${entry.slice(4)}\n`);
  else process.stdout.write(`${entry}\n`);
}
if (env.FAKE_RAW) process.stdout.write(env.FAKE_RAW);
if (env.FAKE_IGNORE_TERM === '1') process.on('SIGTERM', () => {});
else process.on('SIGTERM', () => process.exit(143));
if (env.FAKE_QUIT_FILE) {
  setInterval(() => {
    let text = '';
    try {
      text = readFileSync(env.FAKE_QUIT_FILE, 'utf8').trim();
    } catch {
      return;
    }
    if (text === String(process.pid)) process.exit(7);
  }, 20);
}
if (env.FAKE_EXIT_AFTER_MS) setTimeout(() => process.exit(3), Number(env.FAKE_EXIT_AFTER_MS));
setInterval(() => {}, 1000);
// Written last: its existence means the stand-in is ready for signals.
if (env.FAKE_ARGS_OUT) {
  const keys = (env.FAKE_ENV_KEYS ?? '').split(',').filter(Boolean);
  const picked = Object.fromEntries(keys.map((key) => [key, env[key] ?? null]));
  writeFileSync(env.FAKE_ARGS_OUT, JSON.stringify({ argv: process.argv.slice(2), env: picked, pid: process.pid }));
}

