#!/usr/bin/env node
// bump-manager-version.js
// =============================================================================
// Sets the Krate Manager's version: writes apps/krate-manager/version.json and every copy of it in one step.
// WHY. version.json is the manager's one version source (spec D18), but Cargo cannot read a version from a JSON file
// and Tauri's config, the three Cargo.lock files and the CHANGELOG all carry the number too; tools/lint-manager-version.js
// fails CI when they drift. A bump by hand meant seven edits in five formats (Phase 12 spec FR-089). This tool makes
// them, or none:
//   - version.json "version";
//   - Cargo.toml's [workspace.package] version line;
//   - src-tauri/tauri.conf.json "version";
//   - in Cargo.lock and every tests/fixtures/*/Cargo.lock, the entries of the workspace's own crates (the [package]
//     names of its members); a third-party crate at the same version is left alone;
//   - CHANGELOG.md: an "## [<old>] - Unreleased" top entry takes the new number (an unreleased version takes the new
//     changes); otherwise a new "## [<new>] - Unreleased" heading goes above the newest entry.
// Before writing it requires lint-manager-version to pass on the tree, the new version to be X.Y.Z (D18) and newer than
// the current one, and every edit's anchor to match exactly once; any refusal writes nothing. After writing, the lint
// runs again and must report the new version everywhere. Each file keeps its line endings. No network, no child process.
//
// Usage: node tools/bump-manager-version.js <x.y.z> [--root <dir>]   (default root: apps/krate-manager)
// Exit codes: 0 = written, 1 = refused (one stderr line "bump-manager-version: <CODE>: <message>", codes E_BUMP_LINT,
// E_BUMP_VERSION, E_BUMP_ANCHOR, E_BUMP_VERIFY), 2 = usage error or internal error.
// =============================================================================
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const lintManagerVersion = require('./lint-manager-version.js');

const NAME = 'bump-manager-version';
const USAGE = 'usage: node tools/bump-manager-version.js <x.y.z> [--root <dir>]';
const DEFAULT_ROOT = path.resolve(__dirname, '..', 'apps', 'krate-manager');
// D18: the bounded X.Y.Z of the manager/vX.Y.Z tag (as tools/lint-manager-version.js).
const VERSION_RE = /^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$/;

class BumpError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

function newer(a, b) {
  const pa = a.split('.').map(Number);
  const pb = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) if (pa[i] !== pb[i]) return pa[i] > pb[i];
  return false;
}

/** The file as LF text and whether it was CRLF, so the edit can write it back in its own line endings. */
function readText(root, rel) {
  const raw = fs.readFileSync(path.join(root, rel), 'utf8');
  return { text: raw.replace(/\r\n/g, '\n'), crlf: raw.includes('\r\n') };
}

/** `text` with `from` replaced by `to`, which must occur exactly once. */
function once(rel, text, from, to) {
  const n = text.split(from).length - 1;
  if (n !== 1) throw new BumpError('E_BUMP_ANCHOR', `${rel}: ${JSON.stringify(from)} occurs ${n} time(s), want 1`);
  return text.replace(from, () => to);
}

/** Cargo.toml: the version line of [workspace.package], the section's only one. */
function bumpWorkspaceToml(rel, text, from, to) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => l.trim() === '[workspace.package]');
  if (start < 0) throw new BumpError('E_BUMP_ANCHOR', `${rel}: no [workspace.package] section`);
  let end = lines.findIndex((l, i) => i > start && /^\s*\[/.test(l));
  if (end < 0) end = lines.length;
  const hits = [];
  for (let i = start + 1; i < end; i++) if (lines[i] === `version = "${from}"`) hits.push(i);
  if (hits.length !== 1) {
    throw new BumpError('E_BUMP_ANCHOR', `${rel}: [workspace.package] holds version = "${from}" ${hits.length} time(s), want 1`);
  }
  lines[hits[0]] = `version = "${to}"`;
  return lines.join('\n');
}

/** CHANGELOG.md: the unreleased top entry renamed, or a new unreleased heading above the newest entry. */
function bumpChangelog(rel, text, from, to) {
  if (text.includes(`## [${to}]`)) throw new BumpError('E_BUMP_ANCHOR', `${rel}: already has a ## [${to}] entry`);
  const top = /^## \[[^\]\n]*\][^\n]*$/m.exec(text);
  if (!top) throw new BumpError('E_BUMP_ANCHOR', `${rel}: no "## [" version heading`);
  if (top[0] === `## [${from}] - Unreleased`) {
    return text.slice(0, top.index) + `## [${to}] - Unreleased` + text.slice(top.index + top[0].length);
  }
  return text.slice(0, top.index) + `## [${to}] - Unreleased\n\n` + text.slice(top.index);
}

/**
 * The workspace's own crate names: the [package] name of each Cargo.toml member. A member with a version of its own
 * (not `version.workspace = true`) is refused: the bump writes only the workspace version.
 */
function memberNames(root) {
  const toml = (rel, sections) =>
    lintManagerVersion.parseTomlSubset(fs.readFileSync(path.join(root, rel), 'utf8'), rel, sections).sections;
  const members = toml('Cargo.toml', ['workspace']).get('workspace')?.get('members');
  if (!Array.isArray(members)) throw new BumpError('E_BUMP_ANCHOR', 'Cargo.toml: no [workspace] members array');
  return members.map((m) => {
    const rel = `${m.replace(/\\/g, '/').replace(/\/+$/, '')}/Cargo.toml`;
    const pkg = toml(rel, ['package']).get('package');
    const name = pkg?.get('name');
    if (typeof name !== 'string') throw new BumpError('E_BUMP_ANCHOR', `${rel}: no [package] name`);
    if (pkg.get('version')?.workspace !== true) {
      throw new BumpError('E_BUMP_ANCHOR', `${rel}: [package] version is not version.workspace = true`);
    }
    return name;
  });
}

/** Cargo.lock and every tests/fixtures/<dir>/Cargo.lock, sorted. */
function lockFiles(root) {
  const fixtures = path.join(root, 'tests', 'fixtures');
  const extra = fs.existsSync(fixtures)
    ? fs
        .readdirSync(fixtures, { withFileTypes: true })
        .filter((d) => d.isDirectory() && fs.existsSync(path.join(fixtures, d.name, 'Cargo.lock')))
        .map((d) => `tests/fixtures/${d.name}/Cargo.lock`)
        .sort()
    : [];
  return ['Cargo.lock', ...extra];
}

/**
 * A lock file's entries for `names`. The root lock must hold every one; a fixture lock holds those its crate depends
 * on (one holding none is left as it is). A name at another version than `from` is drift the lint does not see, so it
 * is refused.
 */
function bumpLock(rel, text, names, from, to) {
  let out = text;
  for (const name of names) {
    const anchor = `[[package]]\nname = "${name}"\nversion = "${from}"\n`;
    const count = out.split(anchor).length - 1;
    const any = out.split(`[[package]]\nname = "${name}"\n`).length - 1;
    if (count > 1 || any !== count) {
      throw new BumpError('E_BUMP_ANCHOR', `${rel}: ${name} is locked ${any} time(s), ${count} of them at ${from}`);
    }
    if (count === 0) {
      if (rel === 'Cargo.lock') throw new BumpError('E_BUMP_ANCHOR', `${rel}: no ${name} ${from} entry`);
      continue;
    }
    out = out.replace(anchor, () => `[[package]]\nname = "${name}"\nversion = "${to}"\n`);
  }
  return out;
}

/** The bump: every new file text first (any refusal writes nothing), then the writes, then the lint again. */
function bump(root, to) {
  if (!VERSION_RE.test(to)) throw new BumpError('E_BUMP_VERSION', `${JSON.stringify(to)} is not X.Y.Z`);
  const before = lintManagerVersion.lint(root);
  if (before.errors.length > 0) {
    const e = before.errors[0];
    throw new BumpError('E_BUMP_LINT', `lint-manager-version fails before the bump: ${e.code}: ${e.file}: ${e.message}`);
  }
  const from = before.version;
  if (!newer(to, from)) throw new BumpError('E_BUMP_VERSION', `${to} is not newer than the current ${from}`);
  const names = memberNames(root);
  const edits = [];
  const edit = (rel, fn) => {
    const f = readText(root, rel);
    const text = fn(rel, f.text);
    if (text !== f.text) edits.push({ rel, text, crlf: f.crlf });
  };
  edit('version.json', (rel, t) => once(rel, t, `"version": "${from}"`, `"version": "${to}"`));
  edit('Cargo.toml', (rel, t) => bumpWorkspaceToml(rel, t, from, to));
  edit('src-tauri/tauri.conf.json', (rel, t) => once(rel, t, `"version": "${from}"`, `"version": "${to}"`));
  for (const lock of lockFiles(root)) edit(lock, (rel, t) => bumpLock(rel, t, names, from, to));
  edit('CHANGELOG.md', (rel, t) => bumpChangelog(rel, t, from, to));
  for (const e of edits) {
    fs.writeFileSync(path.join(root, e.rel), e.crlf ? e.text.replace(/\n/g, '\r\n') : e.text);
  }
  const after = lintManagerVersion.lint(root);
  if (after.errors.length > 0 || after.version !== to) {
    const e = after.errors[0];
    const why = e ? `${e.code}: ${e.file}: ${e.message}` : `version ${after.version}`;
    throw new BumpError('E_BUMP_VERIFY', `the files are written but lint-manager-version fails: ${why}`);
  }
  return { from, to, files: edits.map((e) => e.rel) };
}

function main(argv, io) {
  let root = DEFAULT_ROOT;
  let to = null;
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--root' && i + 1 < argv.length) {
      root = path.resolve(argv[++i]);
      continue;
    }
    if (argv[i] !== '--root' && !argv[i].startsWith('-') && to === null) {
      to = argv[i];
      continue;
    }
    const why = argv[i] === '--root' ? 'missing value for --root' : `unknown argument: ${argv[i]}`;
    io.stderr.write(`${NAME}: ${why}\n${USAGE}\n`);
    return 2;
  }
  if (to === null) {
    io.stderr.write(`${NAME}: missing the new version\n${USAGE}\n`);
    return 2;
  }
  let result;
  try {
    result = bump(root, to);
  } catch (err) {
    if (err instanceof BumpError) {
      io.stderr.write(`${NAME}: ${err.code}: ${err.message}\n`);
      return 1;
    }
    io.stderr.write(`${NAME}: internal error: ${err && err.stack ? err.stack : err}\n`);
    return 2;
  }
  io.stdout.write(`${NAME}: ${result.from} -> ${result.to}: ${result.files.join(', ')}\n`);
  return 0;
}

module.exports = { bump, main };

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);
