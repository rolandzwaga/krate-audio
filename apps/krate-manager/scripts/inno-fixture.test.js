// node:test: the CI fixture Inno script keeps the plugin installers' behaviour-deciding lines
// (spec FR-101; plan §4.6). Both scripts are normalised by their own `#define AppName "<name>"` line,
// so this source names neither the plugin nor the fixture (the roster scan, `fixture_names_confined`).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const APP_ROOT = path.resolve(import.meta.dirname, '..');
const REPO_ROOT = path.resolve(APP_ROOT, '..', '..');
const FIXTURE_ISS = path.join(APP_ROOT, 'tests', 'fixtures', 'inno', 'fixture.iss');
const PLUGIN_ISS = path.join(REPO_ROOT, 'plugins', 'iterum', 'installers', 'windows', 'setup.iss');

/** The `[Setup]` directives whose whole line must be equal (`setup.iss:50`, `:53`, `:63-64`). */
const DIRECTIVES = [
  'PrivilegesRequired',
  'UsePreviousAppDir',
  'ArchitecturesAllowed',
  'ArchitecturesInstallIn64BitMode',
];
/** Inno roots no fixture line may name: it must write only under `{#Root}` (FR-111). */
const BANNED = ['{commonpf}', '{commonappdata}', '{pf}', '{sys}', '{win}'];

/**
 * `text` split into lines with its AppName replaced by `<APP>`, its AppId by `<APPID>`, and the
 * plugin roots by the fixture's (`{commonpf}\Common Files` → `{#Root}`, `{commonappdata}` →
 * `{#Root}\ProgramData`).
 * @param {string} text
 * @param {string} label
 * @returns {string[]}
 */
function normalise(text, label) {
  const lines = text.split(/\r?\n/);
  const names = lines.flatMap((l) => /^#define AppName "([^"]+)"$/.exec(l)?.slice(1) ?? []);
  const name = names.at(0);
  assert.ok(names.length === 1 && name !== undefined, `${label}: want one #define AppName line, got ${String(names.length)}`);
  return lines.map((l) =>
    l
      .split(name)
      .join('<APP>')
      .replace(/^AppId=.*$/, 'AppId=<APPID>')
      .split('{commonpf}\\Common Files')
      .join('{#Root}')
      .split('{commonappdata}')
      .join('{#Root}\\ProgramData'),
  );
}

/**
 * Every entry line (not blank, not a `;` comment) per `[Section]`, with its 0-based line index.
 * @param {string[]} lines
 * @returns {Map<string, { index: number, text: string }[]>}
 */
function sections(lines) {
  /** @type {Map<string, { index: number, text: string }[]>} */
  const out = new Map();
  /** @type {{ index: number, text: string }[]} */
  let current = [];
  lines.forEach((raw, index) => {
    const text = raw.trim();
    const header = /^\[([A-Za-z]+)\]$/.exec(text);
    if (header !== null) {
      const key = header.at(1) ?? '';
      current = out.get(key) ?? [];
      out.set(key, current);
    } else if (text !== '' && !text.startsWith(';')) {
      current.push({ index, text });
    }
  });
  return out;
}

/**
 * The listed lines of a normalised script: each directive's lines, every `[Files]` entry's
 * `Flags:` value, every `[UninstallDelete]` entry.
 * @param {string[]} lines
 */
function listed(lines) {
  const s = sections(lines);
  const setup = s.get('Setup') ?? [];
  return {
    directives: DIRECTIVES.map((key) => setup.filter((e) => e.text.startsWith(`${key}=`)).map((e) => e.text)),
    flags: (s.get('Files') ?? []).map((e) => /(?:^|;)\s*Flags:\s*([^;]*)/.exec(e.text)?.[1]?.trim() ?? '(no Flags)'),
    uninstallDelete: (s.get('UninstallDelete') ?? []).map((e) => e.text),
  };
}

/**
 * Every drift of the fixture script from the plugin script, one message per listed line.
 * @param {string} fixtureText
 * @param {string} pluginText
 * @returns {string[]}
 */
function drifts(fixtureText, pluginText) {
  const f = listed(normalise(fixtureText, 'fixture.iss'));
  const p = listed(normalise(pluginText, 'setup.iss'));
  const out = [];
  DIRECTIVES.forEach((key, i) => {
    const fl = f.directives[i] ?? [];
    const pl = p.directives[i] ?? [];
    if (pl.length !== 1) out.push(`${key}: the plugin script has ${String(pl.length)} lines`);
    if (fl.length !== 1 || fl[0] !== pl[0]) out.push(`${key}: fixture ${JSON.stringify(fl)} vs plugin ${JSON.stringify(pl)}`);
  });
  if (p.flags.length === 0) out.push('[Files] Flags: the plugin script has no [Files] entry');
  for (let i = 0; i < Math.max(f.flags.length, p.flags.length); i++) {
    const [a, b] = [f.flags.at(i), p.flags.at(i)];
    if (a !== b) out.push(`[Files] entry ${String(i + 1)} Flags: fixture ${String(a)} vs plugin ${String(b)}`);
  }
  if (p.uninstallDelete.length === 0) out.push('[UninstallDelete]: the plugin script has no entry');
  for (let i = 0; i < Math.max(f.uninstallDelete.length, p.uninstallDelete.length); i++) {
    const [a, b] = [f.uninstallDelete.at(i), p.uninstallDelete.at(i)];
    if (a !== b) out.push(`[UninstallDelete] entry ${String(i + 1)}: fixture ${String(a)} vs plugin ${String(b)}`);
  }
  fixtureText.split(/\r?\n/).forEach((line, i) => {
    for (const token of BANNED) {
      if (line.toLowerCase().includes(token)) out.push(`banned root ${token}: fixture.iss:${String(i + 1)}`);
    }
  });
  return out;
}

/**
 * `text` with line `index` (0-based) replaced by `edit(line)`.
 * @param {string} text
 * @param {number} index
 * @param {(line: string) => string} edit
 */
function plant(text, index, edit) {
  const lines = text.split('\n');
  lines[index] = edit(lines[index] ?? '');
  return lines.join('\n');
}

await test('fixture inno script keeps the plugin directives', () => {
  const fixtureText = readFileSync(FIXTURE_ISS, 'utf8');
  const pluginText = readFileSync(PLUGIN_ISS, 'utf8');
  assert.deepEqual(drifts(fixtureText, pluginText), []);

  const fixtureLines = fixtureText.split('\n');
  const s = sections(fixtureLines.map((l) => l.replace(/\r$/, '')));
  const setup = s.get('Setup') ?? [];
  const files = s.get('Files') ?? [];
  const uninstallDelete = s.get('UninstallDelete') ?? [];
  assert.ok(files.length > 0 && uninstallDelete.length > 0, 'the fixture has [Files] and [UninstallDelete] entries');

  /** @type {[string, string][]} each planted fixture text and the label its drift must carry */
  const plants = [];
  for (const key of DIRECTIVES) {
    const entry = setup.find((e) => e.text.startsWith(`${key}=`));
    assert.ok(entry !== undefined, `fixture.iss has ${key}`);
    plants.push([plant(fixtureText, entry.index, (l) => l.replace('=', '=drift')), `${key}:`]);
    plants.push([plant(fixtureText, entry.index, () => ''), `${key}:`]);
  }
  files.forEach((entry, i) => {
    plants.push([
      plant(fixtureText, entry.index, (l) => l.replace(/Flags:\s*/, 'Flags: uninsneveruninstall ')),
      `[Files] entry ${String(i + 1)} Flags:`,
    ]);
  });
  uninstallDelete.forEach((entry, i) => {
    plants.push([plant(fixtureText, entry.index, (l) => l.replace('.vst3', '.vst3\\drift')), `[UninstallDelete] entry ${String(i + 1)}:`]);
  });
  for (const token of BANNED) {
    plants.push([`${fixtureText}\n; ${token}\n`, `banned root ${token}:`]);
    plants.push([`${fixtureText}\n; ${token.toUpperCase()}\n`, `banned root ${token}:`]);
  }
  for (const [text, label] of plants) {
    const found = drifts(text, pluginText);
    assert.ok(
      found.some((m) => m.startsWith(label)),
      `a planted drift labelled "${label}" is reported; got ${JSON.stringify(found)}`,
    );
  }
});
