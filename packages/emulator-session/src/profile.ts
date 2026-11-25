import type { NoiseRule } from './lines.js';

/** A setting value. Booleans and numbers are written as their text. */
export type SettingValue = string | number | boolean;
export type Settings = Readonly<Record<string, SettingValue>>;

/**
 * How settings reach the emulator.
 *
 * - `flags`: one command-line fragment per setting. `flag` is the fragment, with `{key}` and `{value}` filled in.
 * - `file`: edited in place under the profile folder before the launch (created when it does not exist).
 * - `overlay-file`: written fresh under the profile folder and named on the command line. `arg` holds `{file}`.
 * - `none`: the emulator takes no settings from here; overrides are refused.
 */
export type SettingsTransport =
  | { kind: 'flags'; flag: readonly string[] }
  | { kind: 'file'; path: string; format: 'ini' | 'keyvalue' }
  | { kind: 'overlay-file'; path: string; format: 'ini' | 'keyvalue'; arg: string }
  | { kind: 'none' };

/** A launch recipe for one emulator, as data. It holds no install path, home folder or drive layout. */
export interface EmulatorProfile {
  id: string;
  label: string;
  /** The owner or process name a window finder looks for. Absent for emulators with no window to capture. */
  windowOwner?: string;
  /**
   * The arguments after the executable. An element that is exactly `{settings}`, `{content}` or `{extra}` expands
   * to a list: the rendered settings, the content files and the caller's `extraArgs`. Any element may hold
   * `{name}` placeholders, filled from `profileDir`, `system`, `file` and the `vars` the caller passes; a placeholder
   * with no value is an error.
   */
  args: readonly string[];
  settings: SettingsTransport;
  /** Settings that are always applied. */
  defaults?: Settings;
  /** Settings that let the emulator keep running when it has no focus. Applied unless `background` is false. */
  background?: Settings;
  /** Settings that mute it. Absent when no launch-time way is known; the session then reports it was not muted. */
  mute?: Settings;
  /** Settings that unmute it, for a caller that asks for sound. */
  unmute?: Settings;
  /** Environment for the process. Values may hold the same placeholders as `args`. */
  env?: Readonly<Record<string, string>>;
  /** Environment that mutes it. */
  muteEnv?: Readonly<Record<string, string>>;
  /** Names for the systems one emulator can run, with settings each needs. Picked by the `system` launch option. */
  systems?: Readonly<Record<string, { name: string; settings?: Settings }>>;
  /** Default values for `{name}` placeholders. */
  vars?: Readonly<Record<string, string>>;
  /** Lines the emulator prints that are not worth keeping. */
  noise?: readonly NoiseRule[];
  /** How long a first window usually takes to appear, in seconds. Captures are not taken sooner. */
  bootLatencySec?: number;
  /**
   * An emulator that runs from the folder it is in: these files are copied from the install folder into the profile
   * folder when missing or a different size, and `executable` is run from there.
   */
  portable?: { executable: string; files: readonly string[] };
  /** True for a runner that writes frames or files and opens no window. `captureAt` is refused. */
  headless?: boolean;
  /** Anything a caller should know that the fields above cannot say. */
  notes?: string;
}

export class LaunchError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.name = 'LaunchError';
    this.code = code;
  }
}

export function settingText(value: SettingValue): string {
  return typeof value === 'string' ? value : String(value);
}

const LIST_TOKENS = new Set(['{settings}', '{content}', '{extra}']);

export interface ExpandContext {
  vars: Readonly<Record<string, string>>;
  settings: readonly string[];
  content: readonly string[];
  extra: readonly string[];
}

/** Fills `{name}` placeholders in one string. */
export function fill(template: string, vars: Readonly<Record<string, string>>): string {
  return template.replace(/\{([A-Za-z_][A-Za-z0-9_]*)\}/g, (_match, name: string) => {
    const value = vars[name];
    if (value === undefined) throw new LaunchError('UNKNOWN_PLACEHOLDER', `no value for {${name}}`);
    return value;
  });
}

/** Expands an argument template into the argument list. */
export function expandArgs(template: readonly string[], context: ExpandContext): string[] {
  const out: string[] = [];
  for (const element of template) {
    if (LIST_TOKENS.has(element)) {
      out.push(...(element === '{settings}' ? context.settings : element === '{content}' ? context.content : context.extra));
    } else {
      out.push(fill(element, context.vars));
    }
  }
  return out;
}

/** Merges settings left to right; later ones win. */
export function mergeSettings(...layers: Array<Settings | undefined>): Record<string, SettingValue> {
  const merged: Record<string, SettingValue> = {};
  for (const layer of layers) {
    if (layer !== undefined) Object.assign(merged, layer);
  }
  return merged;
}

/** One command-line fragment per setting, in the order given. */
export function renderFlags(flag: readonly string[], settings: Settings): string[] {
  const out: string[] = [];
  for (const [key, value] of Object.entries(settings)) {
    for (const part of flag) out.push(part.replace('{key}', key).replace('{value}', settingText(value)));
  }
  return out;
}

function splitKey(key: string): { section: string; name: string } {
  const slash = key.lastIndexOf('/');
  return slash === -1 ? { section: '', name: key } : { section: key.slice(0, slash), name: key.slice(slash + 1) };
}

/** The line range of one INI section: `from` is the first line after the header, `to` is where the next section starts. */
function sectionRange(lines: readonly string[], section: string): { from: number; to: number } | undefined {
  const header = (line: string): string | undefined => /^\s*\[(.*)\]\s*$/.exec(line)?.[1];
  let from = section === '' ? 0 : -1;
  for (let index = 0; index < lines.length; index += 1) {
    const name = header(lines[index] ?? '');
    if (name === undefined) continue;
    if (from !== -1) return { from, to: index };
    if (name === section) from = index + 1;
  }
  return from === -1 ? undefined : { from, to: lines.length };
}

/**
 * Applies settings to the text of an INI file. A key is `Section/Name`; the section is everything before the last
 * slash, so `EmuCore/GS/Name` is the name `Name` in the section `[EmuCore/GS]`. A key with no slash belongs before
 * the first section. A value that exists is replaced in place; a missing key is added after the last line of its
 * section; a missing section is added at the end of the file. Spacing around `=` and the line ending are kept.
 */
export function applyIni(text: string, settings: Settings): string {
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  const lines = text === '' ? [] : text.split(/\r?\n/);
  if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();
  for (const [key, value] of Object.entries(settings)) {
    const { section, name } = splitKey(key);
    const wanted = settingText(value);
    const range = sectionRange(lines, section);
    if (range === undefined) {
      if (lines.length > 0 && (lines[lines.length - 1] ?? '').trim() !== '') lines.push('');
      lines.push(`[${section}]`, `${name} = ${wanted}`);
      continue;
    }
    let last = range.from;
    let done = false;
    for (let index = range.from; index < range.to && !done; index += 1) {
      const line = lines[index] ?? '';
      if (line.trim() !== '') last = index + 1;
      const pair = /^(\s*)([^=;#\s][^=]*?)(\s*=\s*)(.*)$/.exec(line);
      if (pair !== null && pair[2] === name) {
        lines[index] = `${pair[1] ?? ''}${name}${pair[3] ?? ' = '}${wanted}`;
        done = true;
      }
    }
    if (!done) lines.splice(last, 0, `${name} = ${wanted}`);
  }
  return lines.length === 0 ? '' : lines.join(eol) + eol;
}

/** Applies settings to the text of a `key=value` file with no sections. A value that exists is replaced in place. */
export function applyKeyValue(text: string, settings: Settings): string {
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  const lines = text === '' ? [] : text.split(/\r?\n/);
  if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();
  for (const [key, value] of Object.entries(settings)) {
    const wanted = `${key}=${settingText(value)}`;
    const index = lines.findIndex((line) => line.slice(0, line.indexOf('=')).trim() === key && line.includes('='));
    if (index === -1) lines.push(wanted);
    else lines[index] = wanted;
  }
  return lines.length === 0 ? '' : lines.join(eol) + eol;
}
