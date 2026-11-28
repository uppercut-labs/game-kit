import { open } from 'node:fs/promises';
import path from 'node:path';
import { type EmulatorProfile, LaunchError, type Settings, type SettingValue, applyIni, mergeSettings } from './profile.js';
import type { LaunchOptions, ProfileFile } from './session.js';

/** The fields of a Dolphin DTM movie header that decide how it is played. */
export interface DtmHeader {
  /** The six-character game id the movie was made for, without trailing NULs. */
  gameId: string;
  /** True for a Wii movie. */
  wii: boolean;
  /** One bit per pad: bits 0 to 3 are GameCube ports 1 to 4, bits 4 to 7 are Wii Remotes 1 to 4. */
  controllers: number;
  /** True when the movie starts from a savestate rather than from power on. */
  fromSaveState: boolean;
  /** The frame count and the input count the header records. */
  frames: bigint;
  inputs: bigint;
}

/** The size of a DTM header in bytes. */
export const DTM_HEADER_SIZE = 256;

/** The SI device numbers Dolphin uses for a GameCube port: nothing plugged in, and a standard controller. */
export const SI_DEVICE_NONE = 0;
export const SI_DEVICE_GC_CONTROLLER = 6;

/** Reads a DTM header from its bytes. Throws `LaunchError` (`BAD_MOVIE`) for anything that is not one. */
export function parseDtmHeader(bytes: Uint8Array): DtmHeader {
  if (bytes.length < DTM_HEADER_SIZE) throw new LaunchError('BAD_MOVIE', `a DTM header is ${DTM_HEADER_SIZE} bytes; got ${bytes.length}`);
  if (bytes[0] !== 0x44 || bytes[1] !== 0x54 || bytes[2] !== 0x4d || bytes[3] !== 0x1a) {
    throw new LaunchError('BAD_MOVIE', 'not a DTM movie: the file does not start with "DTM" and 0x1A');
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let gameId = '';
  for (let index = 4; index < 10 && bytes[index] !== 0; index += 1) gameId += String.fromCharCode(bytes[index] ?? 0);
  return {
    gameId,
    wii: bytes[10] !== 0,
    controllers: bytes[11] ?? 0,
    fromSaveState: bytes[12] !== 0,
    frames: view.getBigUint64(13, true),
    inputs: view.getBigUint64(21, true),
  };
}

/** Reads the header of a DTM file. */
export async function readDtmHeader(file: string): Promise<DtmHeader> {
  const handle = await open(file, 'r').catch((error: NodeJS.ErrnoException) => {
    throw new LaunchError('MOVIE_NOT_FOUND', `cannot read the movie ${file}: ${error.message}`);
  });
  try {
    const buffer = new Uint8Array(DTM_HEADER_SIZE);
    const { bytesRead } = await handle.read(buffer, 0, DTM_HEADER_SIZE, 0);
    return parseDtmHeader(buffer.subarray(0, bytesRead));
  } finally {
    await handle.close();
  }
}

/** The `--config` settings that plug a standard controller into each GameCube port the mask names, and none elsewhere. */
export function siDeviceSettings(controllers: number): Record<string, number> {
  const settings: Record<string, number> = {};
  for (let port = 0; port < 4; port += 1) {
    settings[`Dolphin.Core.SIDevice${port}`] = (controllers >> port) & 1 ? SI_DEVICE_GC_CONTROLLER : SI_DEVICE_NONE;
  }
  return settings;
}

/** Where Dolphin writes its log and dumps inside a profile (user) folder. */
export function dolphinPaths(profileDir: string): { log: string; frames: string; audio: string; config: string } {
  const root = path.resolve(profileDir);
  return {
    log: path.join(root, 'Logs', 'dolphin.log'),
    frames: path.join(root, 'Dump', 'Frames'),
    audio: path.join(root, 'Dump', 'Audio'),
    config: path.join(root, 'Config'),
  };
}

export interface DolphinOptions extends Omit<LaunchOptions, 'profile' | 'profileDir'> {
  /** Dolphin's user folder for this run (`--user`). */
  profileDir: string;
  /** The profile to start from. Defaults to the built-in `dolphin`. */
  profile?: EmulatorProfile;
  /** Settings written to `Config/GFX.ini` as `Section/Name` keys, for example `{ 'Settings/AspectRatio': 1 }`. */
  graphics?: Settings;
  /**
   * Turn on the OSReport log: `Config/Logger.ini` sends the OSREPORT and OSREPORT_HLE logs to `Logs/dolphin.log` and to
   * the console, so the session's line reader sees them.
   */
  osReport?: boolean;
  /** Dump the audio to `Dump/Audio` without asking. */
  dumpAudio?: boolean;
  /** Dump the frames to `Dump/Frames` without asking. */
  dumpFrames?: boolean;
  /**
   * A DTM movie to play (`--movie`). The GameCube ports are set from the controller bits in its header: a standard
   * controller where the movie has a pad, nothing elsewhere, as Dolphin needs for the movie to replay.
   */
  movie?: string;
  /**
   * Check that the movie was made for the file being booted. Dolphin names a DOL or ELF `ID-` and the start of its file
   * name (`ID-gam` for `game.dol`), and plays nothing when the movie's id differs. Defaults to true; the check only
   * applies to a DOL or ELF.
   */
  checkMovieGameId?: boolean;
}

/** The game id Dolphin gives a DOL or ELF it boots: `ID-` and the file name, cut to the six characters a DTM holds. */
export function executableGameId(file: string): string {
  return `ID-${path.basename(file, path.extname(file))}`.slice(0, 6);
}

const LOGGER_INI: Settings = {
  'Options/Verbosity': 1,
  'Options/WriteToConsole': 'True',
  'Options/WriteToFile': 'True',
  'Options/WriteToWindow': 'False',
  'Logs/OSREPORT': 'True',
  'Logs/OSREPORT_HLE': 'True',
};

/**
 * Builds the launch options for a Dolphin run: the `dolphin` profile, the graphics and log files, the dump settings and
 * the movie with its controller ports. Everything else in `options` is passed through, and `settings` given here win
 * over the ones this adds. Throws `LaunchError` for a content list that is not one file or a movie that is not a DTM.
 */
export async function dolphinOptions(options: DolphinOptions): Promise<LaunchOptions> {
  const { graphics, osReport, dumpAudio, dumpFrames, movie, checkMovieGameId, profile, ...rest } = options;
  if (rest.content === undefined || rest.content.length !== 1) {
    throw new LaunchError('CONTENT_REQUIRED', 'Dolphin boots one file with --exec; pass it as the only entry in content');
  }
  const files: ProfileFile[] = [];
  if (graphics !== undefined) files.push({ to: 'Config/GFX.ini', text: applyIni('', graphics) });
  if (osReport === true) files.push({ to: 'Config/Logger.ini', text: applyIni('', LOGGER_INI) });

  const added: Record<string, SettingValue> = {};
  if (dumpAudio === true) Object.assign(added, { 'Dolphin.DSP.DumpAudio': 'True', 'Dolphin.DSP.DumpAudioSilent': 'True' });
  if (dumpFrames === true) Object.assign(added, { 'Dolphin.Movie.DumpFrames': 'True', 'Dolphin.Movie.DumpFramesSilent': 'True' });
  const extraArgs = [...(rest.extraArgs ?? [])];
  if (movie !== undefined) {
    const header = await readDtmHeader(movie);
    if (header.wii) throw new LaunchError('BAD_MOVIE', `${movie} is a Wii movie; only GameCube movies set the controller ports`);
    const booted = rest.content[0] ?? '';
    if ((checkMovieGameId ?? true) && /\.(dol|elf)$/i.test(booted) && header.gameId !== executableGameId(booted)) {
      throw new LaunchError(
        'BAD_MOVIE',
        `${movie} was made for game id "${header.gameId}", but Dolphin calls ${path.basename(booted)} "${executableGameId(booted)}" and would play nothing`,
      );
    }
    Object.assign(added, siDeviceSettings(header.controllers));
    extraArgs.push('--movie', path.resolve(movie));
  }

  return {
    ...rest,
    profile: profile ?? 'dolphin',
    profileDir: options.profileDir,
    settings: mergeSettings(added, rest.settings),
    writeFiles: [...(rest.writeFiles ?? []), ...files],
    extraArgs,
  };
}
