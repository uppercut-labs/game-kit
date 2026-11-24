import { describe, expect, it } from 'vitest';
import { LaunchError, applyIni, applyKeyValue, expandArgs, mergeSettings, renderFlags } from '../src/index.js';

describe('applyIni', () => {
  it('replaces a value in place and keeps the rest of the file', () => {
    const before = '[EmuCore/GS]\r\nOsdShowMessages = true\r\nOther = 1\r\n[Audio]\r\nVolume = 100\r\n';
    const after = applyIni(before, { 'EmuCore/GS/OsdShowMessages': false, 'Audio/Volume': 0 });
    expect(after).toBe('[EmuCore/GS]\r\nOsdShowMessages = false\r\nOther = 1\r\n[Audio]\r\nVolume = 0\r\n');
  });

  it('adds a missing key at the end of its section and a missing section at the end of the file', () => {
    const before = '[A]\nx = 1\n\n[B]\ny = 2\n';
    expect(applyIni(before, { 'A/z': 'three' })).toBe('[A]\nx = 1\nz = three\n\n[B]\ny = 2\n');
    expect(applyIni(before, { 'C/w': 'four' })).toBe('[A]\nx = 1\n\n[B]\ny = 2\n\n[C]\nw = four\n');
  });

  it('puts a key with no section before the first section', () => {
    expect(applyIni('[A]\nx = 1\n', { top: 'yes' })).toBe('top = yes\n[A]\nx = 1\n');
    expect(applyIni('', { 'Sound/Enable': 'False' })).toBe('[Sound]\nEnable = False\n');
  });

  it('keeps a key of the same name in another section', () => {
    expect(applyIni('[A]\nx = 1\n[B]\nx = 2\n', { 'B/x': 9 })).toBe('[A]\nx = 1\n[B]\nx = 9\n');
  });
});

describe('applyKeyValue', () => {
  it('replaces or appends keys', () => {
    expect(applyKeyValue('a=1\nb=2\n', { b: 3, c: 'x' })).toBe('a=1\nb=3\nc=x\n');
    expect(applyKeyValue('', { mode: 'windowed' })).toBe('mode=windowed\n');
  });
});

describe('profile helpers', () => {
  it('merges settings with later layers winning and the first key order kept', () => {
    expect(Object.entries(mergeSettings({ a: 1, b: 2 }, undefined, { b: 3, c: 4 }))).toEqual([['a', 1], ['b', 3], ['c', 4]]);
  });

  it('renders one flag fragment per setting', () => {
    expect(renderFlags(['--setting', '{key}={value}'], { 'Audio/Mute': true, n: 5 })).toEqual(['--setting', 'Audio/Mute=true', '--setting', 'n=5']);
  });

  it('expands list tokens and placeholders, and refuses a placeholder with no value', () => {
    const context = { vars: { dir: '/p' }, settings: ['--s'], content: ['a.rom', 'b.rom'], extra: ['-x'] };
    expect(expandArgs(['-d', '{dir}/x', '{settings}', '{extra}', '{content}'], context)).toEqual(['-d', '/p/x', '--s', '-x', 'a.rom', 'b.rom']);
    expect(() => expandArgs(['{missing}'], context)).toThrow(LaunchError);
  });
});
