#!/usr/bin/env node
//
// lint-manager-version.js
// =============================================================================
// Fails when the Krate Manager's version is written in more than one place, or when the copies disagree.
//
// WHY. The manager has ONE version source, apps/krate-manager/version.json (roadmap Q1; spec D18, FR-060 to FR-063).
// The same number must reach the bundle (src-tauri/tauri.conf.json "version", which the updater compares against the
// feed) and the binary (Cargo's [workspace.package] version and every member's [package] version, which becomes
// CARGO_PKG_VERSION). A drift ships an app that reports one version and updates as another. So this lint checks that
// tauri.conf.json and every Cargo manifest equal version.json, and that no second copy exists: no "version" key in a
// src-tauri/tauri.*.conf.json platform file or in package.json (npm needs none for a package that is never published).
// It reads only a small TOML subset (section and [[table]] headers, key = "string", key = true|false,
// key.workspace = true, single- or multi-line string arrays; PD-13) in the sections it checks, and refuses any other
// line there with E_TOML_UNSUPPORTED instead of guessing. No TOML dependency.
//
// Usage: node tools/lint-manager-version.js [--root <dir>]   (default root: apps/krate-manager)
// Exit codes: 0 = clean, 1 = at least one error (one stderr line each,
// "lint-manager-version: <CODE>: <file>: <message>", in a fixed order), 2 = usage error or internal error.
// =============================================================================
'use strict';

const fs = require('node:fs');
const path = require('node:path');

const NAME = 'lint-manager-version';
const USAGE = 'usage: node tools/lint-manager-version.js [--root <dir>]';
const DEFAULT_ROOT = path.resolve(__dirname, '..', 'apps', 'krate-manager');
// D18: the bounded X.Y.Z of the manager/vX.Y.Z tag.
const VERSION_RE = /^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$/;

const KEY = '[A-Za-z0-9_-]+';
const HEADER_RE = new RegExp(`^\\[\\s*(${KEY}(?:\\s*\\.\\s*${KEY})*)\\s*\\]$`);
const ARRAY_HEADER_RE = new RegExp(`^\\[\\[\\s*(${KEY}(?:\\s*\\.\\s*${KEY})*)\\s*\\]\\]$`);
const WORKSPACE_KEY_RE = new RegExp(`^(${KEY})\\s*\\.\\s*workspace\\s*=\\s*true$`);
const KEY_VALUE_RE = new RegExp(`^(${KEY})\\s*=\\s*(.+)$`);
const GLOB_RE = /[*?[\]]/;
const SIMPLE_ESCAPES = { '"': '"', '\\': '\\', n: '\n', t: '\t', r: '\r', b: '\b', f: '\f' };

// Cuts a trailing # comment that sits outside a "basic" or 'literal' string.
function stripComment(line) {
  let quote = null;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (quote === '"' && c === '\\') {
      i++;
    } else if (quote) {
      if (c === quote) quote = null;
    } else if (c === '"' || c === "'") {
      quote = c;
    } else if (c === '#') {
      return line.slice(0, i);
    }
  }
  return line;
}

// Parses a basic string starting at s[pos] === '"'. Returns { value, end } (end = index after the closing quote) or
// null for anything outside the subset (no closing quote, an escape other than the simple ones).
function parseBasicString(s, pos) {
  if (s[pos] !== '"') return null;
  let value = '';
  for (let i = pos + 1; i < s.length; i++) {
    const c = s[i];
    if (c === '"') return { value, end: i + 1 };
    if (c === '\\') {
      const e = SIMPLE_ESCAPES[s[i + 1]];
      if (e === undefined) return null;
      value += e;
      i++;
    } else {
      value += c;
    }
  }
  return null;
}

// "[ "a", "b", ]" -> ['a', 'b']; null when it is not an array of basic strings.
function parseStringArray(text) {
  if (!text.startsWith('[') || !text.endsWith(']')) return null;
  const inner = text.slice(1, -1);
  const out = [];
  let i = 0;
  const skipWs = () => {
    while (i < inner.length && /\s/.test(inner[i])) i++;
  };
  for (;;) {
    skipWs();
    if (i >= inner.length) return out;
    const str = parseBasicString(inner, i);
    if (!str) return null;
    out.push(str.value);
    i = str.end;
    skipWs();
    if (i >= inner.length) return out;
    if (inner[i] !== ',') return null;
    i++;
  }
}

// A value in the subset: "string", true, false, or a one-line string array. undefined when outside the subset.
function parseValue(text) {
  if (text === 'true') return true;
  if (text === 'false') return false;
  if (text.startsWith('"')) {
    const str = parseBasicString(text, 0);
    return str && str.end === text.length ? str.value : undefined;
  }
  if (text.startsWith('[')) {
    const arr = parseStringArray(text);
    return arr === null ? undefined : arr;
  }
  return undefined;
}

// Reads the sections named in readSections ("workspace", "workspace.package", "package", ...) of a TOML text.
// Lines outside those sections are skipped unparsed. Values: string, boolean, string[], or { workspace: true } for
// key.workspace = true. CRLF-safe.
function parseTomlSubset(text, file, readSections) {
  const wanted = new Set(readSections);
  const sections = new Map();
  const errors = [];
  const lines = text.split(/\r?\n/);
  let current = null; // the Map of the read section we are in, or null
  let currentName = '';
  const unsupported = (lineNo, why) =>
    errors.push({
      code: 'E_TOML_UNSUPPORTED',
      file,
      message: `line ${lineNo}: outside the lint's TOML subset in [${currentName}]: ${why}`,
    });

  for (let i = 0; i < lines.length; i++) {
    const lineNo = i + 1;
    const line = stripComment(lines[i]).trim();
    if (line === '') continue;

    const arrayHeader = ARRAY_HEADER_RE.exec(line);
    const header = arrayHeader ? null : HEADER_RE.exec(line);
    if (arrayHeader) {
      current = null; // a [[table]] is never a read section (PD-13)
      continue;
    }
    if (header) {
      currentName = header[1].replace(/\s+/g, '');
      current = null;
      if (wanted.has(currentName)) {
        if (sections.has(currentName)) {
          unsupported(lineNo, 'the table is defined twice');
        } else {
          current = new Map();
          sections.set(currentName, current);
        }
      }
      continue;
    }
    if (!current) continue;

    let key;
    let value;
    const ws = WORKSPACE_KEY_RE.exec(line);
    const kv = ws ? null : KEY_VALUE_RE.exec(line);
    if (ws) {
      key = ws[1];
      value = { workspace: true };
    } else if (kv) {
      key = kv[1];
      let raw = kv[2].trim();
      if (raw.startsWith('[') && !raw.includes(']')) {
        // A multi-line string array: gather lines up to the one holding the closing bracket.
        let j = i + 1;
        while (j < lines.length && !raw.includes(']')) {
          raw += ' ' + stripComment(lines[j]).trim();
          j++;
        }
        if (!raw.includes(']')) {
          unsupported(lineNo, `${lines[i].trim()} (array never closed)`);
          i = lines.length;
          continue;
        }
        i = j - 1;
      }
      value = parseValue(raw);
    }
    if (value === undefined) {
      unsupported(lineNo, lines[i].trim());
      continue;
    }
    if (current.has(key)) {
      unsupported(lineNo, `${key} is set twice`);
      continue;
    }
    current.set(key, value);
  }
  return { sections, errors };
}

// Reads root/rel. Returns { text } or { missing: true } or { failure: '<reason>' }.
function readText(root, rel) {
  try {
    return { text: fs.readFileSync(path.join(root, ...rel.split('/')), 'utf8') };
  } catch (err) {
    if (err.code === 'ENOENT' || err.code === 'ENOTDIR') return { missing: true };
    return { failure: `unreadable (${err.code || err.message})` };
  }
}

// Reads root/rel as JSON. Returns { json } or { problem: '<message>' }; missingOk turns a missing file into { absent }.
function readJson(root, rel, missingOk) {
  const r = readText(root, rel);
  if (r.missing) return missingOk ? { absent: true } : { problem: 'missing' };
  if (r.failure) return { problem: r.failure };
  try {
    return { json: JSON.parse(r.text) };
  } catch {
    return { problem: 'not JSON' };
  }
}

const isObject = (v) => typeof v === 'object' && v !== null && !Array.isArray(v);
const hasVersionKey = (v) => isObject(v) && Object.hasOwn(v, 'version');

function lint(root) {
  const errors = [];
  const report = (code, file, message) => errors.push({ code, file, message });
  let expected = null; // version.json's version, when it is a valid X.Y.Z

  // 1. version.json
  {
    const r = readJson(root, 'version.json', false);
    if (r.problem) {
      report('E_VERSION_FILE', 'version.json', r.problem);
    } else if (!isObject(r.json) || typeof r.json.version !== 'string') {
      report('E_VERSION_FILE', 'version.json', 'version missing or not a string');
    } else if (!VERSION_RE.test(r.json.version)) {
      report('E_VERSION_FORMAT', 'version.json', `version ${JSON.stringify(r.json.version)} is not X.Y.Z`);
    } else {
      expected = r.json.version;
    }
  }
  const mismatch = (file, label, actual) => {
    if (expected !== null && actual !== expected) {
      report('E_VERSION_MISMATCH', file, `${label}${actual} != version.json ${expected}`);
    }
  };

  // 2. src-tauri/tauri.conf.json
  {
    const file = 'src-tauri/tauri.conf.json';
    const r = readJson(root, file, false);
    if (r.problem) {
      report('E_TAURI_CONF', file, r.problem);
    } else if (!isObject(r.json) || typeof r.json.version !== 'string') {
      report('E_TAURI_CONF', file, 'version missing or not a string');
    } else {
      mismatch(file, 'version ', r.json.version);
    }
  }

  // 3. every src-tauri/tauri.*.conf.json, sorted: no version key
  {
    let names = [];
    try {
      names = fs.readdirSync(path.join(root, 'src-tauri'));
    } catch {
      names = []; // src-tauri missing: already reported through tauri.conf.json
    }
    for (const name of names.filter((n) => /^tauri\..+\.conf\.json$/.test(n)).sort()) {
      const file = `src-tauri/${name}`;
      const r = readJson(root, file, false);
      if (r.problem) {
        report('E_TAURI_CONF', file, r.problem);
      } else if (hasVersionKey(r.json)) {
        report('E_VERSION_SOURCE', file, 'carries a version key; version.json is the only version source');
      }
    }
  }

  // 4. package.json: no version key
  {
    const r = readJson(root, 'package.json', true);
    if (r.problem) {
      report('E_VERSION_SOURCE', 'package.json', `${r.problem}, so it cannot be checked for a version key`);
    } else if (hasVersionKey(r.json)) {
      report('E_VERSION_SOURCE', 'package.json', 'carries a version key; version.json is the only version source');
    }
  }

  // 5. root Cargo.toml: [workspace] members, [workspace.package] version
  const members = [];
  {
    const file = 'Cargo.toml';
    const r = readText(root, file);
    if (r.missing || r.failure) {
      report('E_CARGO_TOML', file, r.failure || 'missing');
    } else {
      const parsed = parseTomlSubset(r.text, file, ['workspace', 'workspace.package']);
      errors.push(...parsed.errors);
      const clean = parsed.errors.length === 0;
      const list = parsed.sections.get('workspace')?.get('members');
      if (Array.isArray(list)) {
        for (const m of list) {
          if (GLOB_RE.test(m)) {
            report('E_CARGO_TOML', file, `[workspace] members entry ${JSON.stringify(m)} is a glob; name every member`);
          } else {
            members.push(path.posix.normalize(m.replace(/\\/g, '/')).replace(/\/+$/, ''));
          }
        }
      } else if (clean || list !== undefined) {
        report('E_CARGO_TOML', file, '[workspace] has no members array of strings');
      }
      const version = parsed.sections.get('workspace.package')?.get('version');
      if (typeof version === 'string') {
        mismatch(file, '[workspace.package] version ', version);
      } else if (version !== undefined) {
        report('E_CARGO_TOML', file, '[workspace.package] version is not a string');
      } else if (clean) {
        report('E_CARGO_TOML', file, '[workspace.package] has no version');
      }
    }
  }

  // 6. each member's Cargo.toml, in members order
  for (const m of members) {
    const file = `${m}/Cargo.toml`;
    const r = readText(root, file);
    if (r.missing || r.failure) {
      report('E_CARGO_TOML', file, r.failure || 'missing (named in Cargo.toml [workspace] members)');
      continue;
    }
    const parsed = parseTomlSubset(r.text, file, ['package']);
    errors.push(...parsed.errors);
    const version = parsed.sections.get('package')?.get('version');
    if (typeof version === 'string') {
      mismatch(file, '[package] version ', version);
    } else if (isObject(version) && version.workspace === true) {
      // version.workspace = true: inherits [workspace.package] version, checked above.
    } else if (version !== undefined) {
      report('E_CARGO_TOML', file, '[package] version is neither version.workspace = true nor a string');
    } else if (parsed.errors.length === 0) {
      report('E_CARGO_TOML', file, '[package] has no version (need version.workspace = true or version = "X.Y.Z")');
    }
  }

  return { errors, version: expected };
}

function main(argv, io) {
  let root = DEFAULT_ROOT;
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--root' && i + 1 < argv.length) {
      root = path.resolve(argv[++i]);
      continue;
    }
    const why = argv[i] === '--root' ? 'missing value for --root' : `unknown argument: ${argv[i]}`;
    io.stderr.write(`${NAME}: ${why}\n${USAGE}\n`);
    return 2;
  }
  let result;
  try {
    result = lint(root);
  } catch (err) {
    io.stderr.write(`${NAME}: internal error: ${err && err.stack ? err.stack : err}\n`);
    return 2;
  }
  for (const e of result.errors) io.stderr.write(`${NAME}: ${e.code}: ${e.file}: ${e.message}\n`);
  if (result.errors.length > 0) return 1;
  io.stdout.write(`${NAME}: ok, version ${result.version} everywhere\n`);
  return 0;
}

module.exports = { lint, main, parseTomlSubset };

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);
