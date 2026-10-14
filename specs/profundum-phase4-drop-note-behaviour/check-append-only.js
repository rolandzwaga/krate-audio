#!/usr/bin/env node
// =============================================================================
// check-append-only.js - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// T003 / SC-020: append-only + public-surface gate for MonoHandler (plan S12.3).
//
// Checks on dsp/include/krate/dsp/processors/mono_handler.h against <baseRef>:
//   1. No removed line: `git diff -U0 <base> -- <file>` has no `-` line other
//      than the `--- ` header. Each offender is printed with its hunk header.
//   2. Public surface: the set of public member names of `class MonoHandler`
//      added in the working tree (working set - base set) is EXACTLY
//      {setGlideMode, glideMode}.
//   3. Namespace-scope additions (e.g. `enum class GlideMode`) are listed as INFO.
// One PASS/FAIL line per check; exit non-zero on any FAIL.
//
// Usage: node specs/profundum-phase4-drop-note-behaviour/check-append-only.js [baseRef=3d8ca19f]
//        node specs/profundum-phase4-drop-note-behaviour/check-append-only.js --self-test
// =============================================================================
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const kDefaultBase = '3d8ca19f';
const kFile = 'dsp/include/krate/dsp/processors/mono_handler.h';
const kClassName = 'MonoHandler';
const kAllowedAdded = ['setGlideMode', 'glideMode'];

const repoRoot = path.resolve(__dirname, '..', '..');

// -----------------------------------------------------------------------------
// Source scrubbing: comments and string/char literals become spaces (newlines
// kept), so brace counting and access tracking see only code.
// -----------------------------------------------------------------------------
function scrub(src) {
  let out = '';
  let i = 0;
  const n = src.length;
  while (i < n) {
    const c = src[i];
    const d = i + 1 < n ? src[i + 1] : '';
    if (c === '/' && d === '/') {
      while (i < n && src[i] !== '\n') { out += ' '; ++i; }
      continue;
    }
    if (c === '/' && d === '*') {
      out += '  ';
      i += 2;
      while (i < n && !(src[i] === '*' && src[i + 1] === '/')) {
        out += src[i] === '\n' ? '\n' : ' ';
        ++i;
      }
      if (i < n) { out += '  '; i += 2; }
      continue;
    }
    if (c === '"' || (c === '\'' && !/[0-9A-Za-z_]/.test(i > 0 ? src[i - 1] : ''))) {
      const q = c;
      out += q;
      ++i;
      while (i < n && src[i] !== q && src[i] !== '\n') {
        if (src[i] === '\\') { out += ' '; ++i; }
        out += ' ';
        ++i;
      }
      if (i < n) { out += src[i]; ++i; }
      continue;
    }
    out += c;
    ++i;
  }
  // Preprocessor lines carry no declarations we gate on.
  return out.replace(/^[ \t]*#.*$/gm, (m) => ' '.repeat(m.length));
}

// Find the index just past the `{` opening `class <name>` / `struct <name>`
// (a definition, not a forward declaration), and the default access.
function findClassBody(code, name) {
  const re = new RegExp(`\\b(class|struct)\\s+${name}\\b[^;{]*\\{`, 'g');
  const m = re.exec(code);
  if (!m) return null;
  return { start: m.index, bodyStart: m.index + m[0].length, defaultAccess: m[1] === 'class' ? 'private' : 'public' };
}

// Index of the `}` matching the `{` just before `from`.
function matchBrace(code, from) {
  let depth = 1;
  for (let i = from; i < code.length; ++i) {
    if (code[i] === '{') ++depth;
    else if (code[i] === '}') {
      --depth;
      if (depth === 0) return i;
    }
  }
  return -1;
}

// Strip attributes and leading template headers from a declaration.
function cleanDecl(text) {
  let t = text.replace(/\[\[[\s\S]*?\]\]/g, ' ').replace(/\s+/g, ' ').trim();
  // template <...> (angle brackets counted)
  while (/^template\s*</.test(t)) {
    let depth = 0;
    let k = t.indexOf('<');
    for (; k < t.length; ++k) {
      if (t[k] === '<') ++depth;
      else if (t[k] === '>') { --depth; if (depth === 0) break; }
    }
    t = t.slice(k + 1).trim();
  }
  return t;
}

// Declared name of one member declaration, or null if it declares none we gate
// on (nested types, using/typedef, friend, static_assert, empty statements).
function declName(rawDecl) {
  const t = cleanDecl(rawDecl);
  if (t === '') return null;
  if (/^(using|typedef|friend|static_assert)\b/.test(t)) return null;
  if (/^(enum|struct|class|union)\b/.test(t)) return null;
  const op = /\boperator\s*(\(\)|[^\s(]+)\s*\(/.exec(t);
  if (op) return `operator${op[1]}`;
  // The declarator name ends at the first `(` (function), `=` or `{`
  // (initialised data member), else at the end (plain data member).
  const cuts = ['(', '=', '{'].map((ch) => t.indexOf(ch)).filter((k) => k >= 0);
  const cut = cuts.length ? Math.min(...cuts) : t.length;
  const head = t.slice(0, cut).replace(/\[[^\]]*\]\s*$/, '').trimEnd();
  const id = /(~?[A-Za-z_][A-Za-z0-9_]*)$/.exec(head);
  return id ? id[1] : null;
}

// Walk the body of `class <name>`; return the Set of names declared in a public
// section. Nested braces (function bodies, nested types, brace initialisers)
// are skipped as a unit.
function publicMembers(src, name) {
  const code = scrub(src);
  const cls = findClassBody(code, name);
  if (!cls) throw new Error(`class ${name} not found`);
  const end = matchBrace(code, cls.bodyStart);
  if (end < 0) throw new Error(`class ${name}: unbalanced braces`);
  let access = cls.defaultAccess;
  const names = new Set();
  let stmt = '';
  let i = cls.bodyStart;
  const record = () => {
    if (access === 'public') {
      const nm = declName(stmt);
      if (nm) names.add(nm);
    }
    stmt = '';
  };
  while (i < end) {
    const c = code[i];
    if (c === ':' && code[i + 1] !== ':' && (i === 0 || code[i - 1] !== ':')) {
      const kw = stmt.trim();
      if (kw === 'public' || kw === 'protected' || kw === 'private') {
        access = kw;
        stmt = '';
        ++i;
        continue;
      }
    }
    if (c === ';') { record(); ++i; continue; }
    if (c === '{') {
      const close = matchBrace(code, i + 1);
      if (close < 0) throw new Error(`class ${name}: unbalanced braces in body`);
      const t = cleanDecl(stmt);
      const isType = /^(enum|struct|class|union)\b/.test(t);
      const isFunction = t.includes('(') && !/=\s*$/.test(t) && !isType;
      if (isFunction) {
        record(); // function definition: no trailing `;`
      } else {
        stmt += ' {} '; // brace initialiser or nested type: finish at the `;`
      }
      i = close + 1;
      continue;
    }
    stmt += c;
    ++i;
  }
  return names;
}

// Namespace-scope type/alias/constant names outside `class <name>`.
function namespaceScopeNames(src, name) {
  let code = scrub(src);
  const cls = findClassBody(code, name);
  if (cls) {
    const end = matchBrace(code, cls.bodyStart);
    if (end >= 0) code = code.slice(0, cls.start) + code.slice(end + 1);
  }
  const names = new Set();
  const patterns = [
    /\benum\s+(?:class|struct)\s+([A-Za-z_]\w*)/g,
    /\benum\s+(?!class\b|struct\b)([A-Za-z_]\w*)/g,
    /\b(?:struct|class|union)\s+([A-Za-z_]\w*)\s*(?:final\s*)?[:{]/g,
    /\busing\s+([A-Za-z_]\w*)\s*=/g,
    /\bconstexpr\b[^;=(]*?\b([A-Za-z_]\w*)\s*=/g,
  ];
  for (const re of patterns) {
    let m;
    while ((m = re.exec(code)) !== null) names.add(m[1]);
  }
  return names;
}

// -----------------------------------------------------------------------------
// Checks (pure functions; exercised by --self-test).
// -----------------------------------------------------------------------------
function check1(diffText) {
  const offenders = [];
  let hunk = '(no hunk header)';
  for (const line of diffText.split(/\r?\n/)) {
    if (line.startsWith('@@')) { hunk = line; continue; }
    if (line.startsWith('--- ')) continue;
    if (line.startsWith('-')) offenders.push({ hunk, line });
  }
  return { pass: offenders.length === 0, offenders };
}

function check2(baseText, newText) {
  const baseSet = publicMembers(baseText, kClassName);
  const newSet = publicMembers(newText, kClassName);
  const added = [...newSet].filter((x) => !baseSet.has(x)).sort();
  const missing = kAllowedAdded.filter((x) => !added.includes(x));
  const unexpected = added.filter((x) => !kAllowedAdded.includes(x));
  return { pass: missing.length === 0 && unexpected.length === 0, added, missing, unexpected };
}

function check3(baseText, newText) {
  const b = namespaceScopeNames(baseText, kClassName);
  return [...namespaceScopeNames(newText, kClassName)].filter((x) => !b.has(x)).sort();
}

function report(r1, r2, info) {
  if (r1.pass) {
    console.log('check 1 (no removed line): PASS');
  } else {
    console.log(`check 1 (no removed line): FAIL (${r1.offenders.length} removed line(s))`);
    for (const o of r1.offenders) console.log(`  ${o.hunk}\n    ${o.line}`);
  }
  const addedStr = r2.added.length ? r2.added.join(', ') : '(none)';
  if (r2.pass) {
    console.log(`check 2 (public surface): PASS (added public: ${addedStr})`);
  } else {
    const parts = [];
    if (r2.missing.length) parts.push(`missing ${r2.missing.join(', ')}`);
    if (r2.unexpected.length) parts.push(`unexpected ${r2.unexpected.join(', ')}`);
    console.log(`check 2 (public surface): FAIL (${parts.join('; ')}; added public: ${addedStr})`);
  }
  if (info.length === 0) console.log('INFO: no namespace-scope additions');
  for (const nm of info) console.log(`INFO: namespace-scope addition: ${nm}`);
  return r1.pass && r2.pass;
}

// -----------------------------------------------------------------------------
// --self-test: synthetic (base, new, diff) triples.
// -----------------------------------------------------------------------------
function selfTest() {
  const base = [
    'namespace Krate { namespace DSP {',
    'enum class MonoMode : uint8_t { LastNote = 0 };',
    'class MonoHandler {',
    'public:',
    '    MonoHandler() noexcept { ramp_.snapTo(0.0f); }',
    '    void prepare(double sampleRate) noexcept { sampleRate_ = static_cast<float>(sampleRate); }',
    '    [[nodiscard]] float getCurrentFrequency() const noexcept { return 440.0f; }',
    '    static constexpr int kMaxStackSize = 16;',
    'private:',
    '    void removeAtIndex(uint8_t index) noexcept { (void)index; }',
    '    float sampleRate_ = 44100.0f;',
    '    std::array<int, 16> stack_{};',
    '};',
    '}  // namespace DSP',
    '}  // namespace Krate',
  ];
  const insertPublic = (lines, extra) => {
    const out = lines.slice();
    out.splice(out.indexOf('private:'), 0, ...extra);
    return out;
  };
  const insertPrivate = (lines, extra) => {
    const out = lines.slice();
    out.splice(out.indexOf('};'), 0, ...extra);
    return out;
  };
  const kSet = '    void setGlideMode(GlideMode mode) noexcept { glideMode_ = mode; }';
  const kGet = '    [[nodiscard]] GlideMode glideMode() const noexcept { return glideMode_; }';
  const baseText = base.join('\n');

  const cases = [];
  // (i) a diff with one removed line -> check 1 FAIL.
  {
    const diff = [
      'diff --git a/mono_handler.h b/mono_handler.h',
      '--- a/mono_handler.h',
      '+++ b/mono_handler.h',
      '@@ -7 +7 @@ public:',
      '-    [[nodiscard]] float getCurrentFrequency() const noexcept { return 440.0f; }',
      '+    [[nodiscard]] float getCurrentFrequency() const noexcept { return 441.0f; }',
    ].join('\n');
    const r = check1(diff);
    cases.push({ name: '(i) removed line -> check 1 FAIL', ok: !r.pass && r.offenders.length === 1 });
    const clean = check1(['--- a/x', '+++ b/x', '@@ -8,0 +9 @@', '+    int added_ = 0;'].join('\n'));
    cases.push({ name: '(i-control) pure addition -> check 1 PASS', ok: clean.pass });
  }
  // (ii) public preciseRetime besides the two allowed -> check 2 FAIL.
  {
    const nt = insertPublic(base, [kSet, kGet, '    void preciseRetime() noexcept {}']).join('\n');
    const r = check2(baseText, nt);
    cases.push({
      name: '(ii) extra public preciseRetime -> check 2 FAIL',
      ok: !r.pass && r.unexpected.join() === 'preciseRetime' && r.missing.length === 0,
    });
  }
  // (iii) only setGlideMode -> check 2 FAIL (missing glideMode).
  {
    const nt = insertPublic(base, [kSet]).join('\n');
    const r = check2(baseText, nt);
    cases.push({
      name: '(iii) only setGlideMode -> check 2 FAIL (missing glideMode)',
      ok: !r.pass && r.missing.join() === 'glideMode' && r.unexpected.length === 0,
    });
  }
  // (iv) exactly the two in public: plus a private processPreciseGlide -> PASS.
  {
    const lines = insertPrivate(insertPublic(base, [kSet, kGet]), [
      '    float processPreciseGlide() noexcept { return 0.0f; }',
      '    GlideMode glideMode_ = GlideMode::ConstantTime;',
    ]);
    lines.splice(2, 0, 'enum class GlideMode : uint8_t { ConstantTime = 0, Drop = 1 };');
    const nt = lines.join('\n');
    const r = check2(baseText, nt);
    const info = check3(baseText, nt);
    cases.push({
      name: '(iv) exactly the two public + private processPreciseGlide -> check 2 PASS',
      ok: r.pass && r.added.join() === 'glideMode,setGlideMode',
    });
    cases.push({ name: '(iv-info) GlideMode listed as namespace-scope addition', ok: info.join() === 'GlideMode' });
  }

  let allOk = true;
  for (const c of cases) {
    console.log(`${c.ok ? 'ok  ' : 'BAD '} ${c.name}`);
    if (!c.ok) allOk = false;
  }
  console.log(allOk ? 'self-test: PASS' : 'self-test: FAIL');
  return allOk;
}

// -----------------------------------------------------------------------------
function git(args) {
  return execFileSync('git', args, { cwd: repoRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
}

function main() {
  const arg = process.argv[2];
  if (arg === '--self-test') {
    process.exit(selfTest() ? 0 : 1);
  }
  const baseRef = arg || kDefaultBase;
  let diffText;
  let baseText;
  try {
    diffText = git(['diff', '-U0', baseRef, '--', kFile]);
    baseText = git(['show', `${baseRef}:${kFile}`]);
  } catch (e) {
    process.stderr.write(`check-append-only: ERROR: git failed: ${e.message}\n`);
    process.exit(2);
  }
  const newText = fs.readFileSync(path.join(repoRoot, kFile), 'utf8');
  console.log(`check-append-only: ${kFile} vs ${baseRef}`);
  let ok;
  try {
    ok = report(check1(diffText), check2(baseText, newText), check3(baseText, newText));
  } catch (e) {
    process.stderr.write(`check-append-only: ERROR: ${e.message}\n`);
    process.exit(2);
  }
  process.exit(ok ? 0 : 1);
}

main();
