// A stand-in for a window capture command. It writes a small file to the last argument and records its arguments.
//   --fail   write nothing and exit 0 (a capture tool that quietly failed)
import { appendFileSync, writeFileSync } from 'node:fs';

const args = process.argv.slice(2);
const out = args[args.length - 1];
if (!args.includes('--fail')) writeFileSync(out, 'PNGDATA');
appendFileSync(`${out}.args`, `${JSON.stringify(args)}\n`);
