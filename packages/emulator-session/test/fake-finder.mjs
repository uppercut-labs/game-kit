// A stand-in for a window finder. It prints "<id> <title>" once it has been asked `after` times, else nothing.
//   node fake-finder.mjs <state file> <after> <pid> <owner>
import { existsSync, readFileSync, writeFileSync } from 'node:fs';

const [state, after, pid, owner] = process.argv.slice(2);
const asked = (existsSync(state) ? Number(readFileSync(state, 'utf8')) : 0) + 1;
writeFileSync(state, String(asked));
if (asked >= Number(after)) process.stdout.write(`4242 ${owner} window for ${pid}\n`);
