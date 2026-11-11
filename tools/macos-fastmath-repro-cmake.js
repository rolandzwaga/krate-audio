#!/usr/bin/env node
// macos-fastmath-repro-cmake.js - the build recipe tools/macos-fastmath-repro.sh needs, read from the CMake files
// =============================================================================
// The repro script compiles DSP test TUs by hand under the macOS leg's flags. What CMake adds on top of the global
// -ffast-math -ffp-contract=fast differs per TU: the per-source COMPILE_FLAGS of dsp/tests/CMakeLists.txt (the IEEE
// list, the -O2 caps), the target options of the TU's executable (dsp_ieee_tests is IEEE as a whole), its target
// definitions (KRATE_DSP_TESTS_DIR, KRATE_DSP_AETHER_TEST_HOOKS, ...) and whether it links hwy. Reading those here
// keeps the script in step with CMake instead of a hand list that goes stale.
//
//   node tools/macos-fastmath-repro-cmake.js lib [--var NAME=VALUE ...]
//       KrateDSP's sources (dsp/CMakeLists.txt) with their per-source compile flags.
//   node tools/macos-fastmath-repro-cmake.js tu <tu under dsp/tests/unit> ... [--var NAME=VALUE ...]
//       Per TU: its executable(s), compile options (target, then source: CMake's command-line order), definitions,
//       link libraries, and the executable's other sources (sibling candidates for undefined symbols).
//   --var overrides a variable in DEFINITION values only (the script points CMAKE_CURRENT_SOURCE_DIR and
//   CMAKE_BINARY_DIR at its scratch mirror); source paths always resolve against the real tree.
//
// Output: one record per line, tab-separated: <kind> <key> [<value>]. Kinds: src, opt (lib); exe, opt, def, lib, sib,
// warn (tu). Exit 2 on anything it cannot model.
//
// WHAT IT MODELS: add_executable / add_library / target_sources source lists; target_compile_options,
// target_compile_definitions, target_link_libraries (PRIVATE / PUBLIC items; INTERFACE items do not apply to the target
// itself); set_source_files_properties COMPILE_FLAGS / COMPILE_OPTIONS / COMPILE_DEFINITIONS; set() / list(APPEND) and
// ${VAR} expansion; if / elseif / else / endif over CMAKE_CXX_COMPILER_ID MATCHES / STREQUAL, MSVC, APPLE, UNIX, WIN32,
// NOT / AND / OR, evaluated as the macOS leg sees them (AppleClang, APPLE, UNIX).
// WHAT IT CANNOT: generator expressions, function / macro bodies (krate_test_pch, catch_discover_tests are calls it
// skips), foreach bodies, add_compile_options / add_definitions, set_property / set_target_properties on a compile
// property (other properties are ignored), any if()
// condition outside the list above, and directory-level options from parent CMakeLists (the global fast-math flags
// are the script's own). When one of those could change a flag (a flag-affecting command inside an if() it cannot
// evaluate or a foreach body, or a generator expression in a flag), it exits 2 naming the file and line rather than
// guess; a skipped construct that touches no flag is ignored.
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');

function die(msg) {
  process.stderr.write(`macos-fastmath-repro-cmake: ${msg}\n`);
  process.exit(2);
}

// ---- tokenizer -------------------------------------------------------------------------------------------------------
// Returns [{ name, args: [{ text, quoted }], line }]. Comments (# and #[[ ]]) dropped; unquoted arguments keep embedded
// "..." literally (CMake's legacy unquoted form, e.g. KRATE_DSP_TESTS_DIR="${X}"); parentheses inside the arguments
// become their own unquoted tokens, as CMake treats them.
function tokenize(text, file) {
  const cmds = [];
  let i = 0;
  let line = 1;
  const n = text.length;
  const skipComment = () => {
    if (text.startsWith('#[[', i) || /^#\[=*\[/.test(text.slice(i, i + 10))) {
      const m = /^#\[(=*)\[/.exec(text.slice(i));
      const close = `]${m[1]}]`;
      const end = text.indexOf(close, i);
      if (end < 0) die(`${file}:${line}: unterminated bracket comment`);
      line += (text.slice(i, end).match(/\n/g) || []).length;
      i = end + close.length;
    } else {
      while (i < n && text[i] !== '\n') i++;
    }
  };
  while (i < n) {
    const c = text[i];
    if (c === '\n') { line++; i++; continue; }
    if (/\s/.test(c)) { i++; continue; }
    if (c === '#') { skipComment(); continue; }
    const m = /^[A-Za-z_][A-Za-z0-9_]*/.exec(text.slice(i, i + 200));
    if (!m) die(`${file}:${line}: unexpected '${c}'`);
    const name = m[0].toLowerCase();
    const cmdLine = line;
    i += m[0].length;
    while (i < n && /[ \t]/.test(text[i])) i++;
    if (text[i] !== '(') die(`${file}:${line}: expected '(' after ${m[0]}`);
    i++;
    const args = [];
    let depth = 1;
    while (i < n && depth > 0) {
      const ch = text[i];
      if (ch === '\n') { line++; i++; continue; }
      if (/\s/.test(ch)) { i++; continue; }
      if (ch === '#') { skipComment(); continue; }
      if (ch === '(') { depth++; args.push({ text: '(', quoted: false }); i++; continue; }
      if (ch === ')') { depth--; if (depth > 0) args.push({ text: ')', quoted: false }); i++; continue; }
      if (ch === '"') {
        let s = '';
        i++;
        while (i < n && text[i] !== '"') {
          if (text[i] === '\\' && i + 1 < n) {
            const e = text[i + 1];
            if (e === '\n') { line++; i += 2; continue; }
            s += e === 'n' ? '\n' : e === 't' ? '\t' : e;
            i += 2;
            continue;
          }
          if (text[i] === '\n') line++;
          s += text[i++];
        }
        if (i >= n) die(`${file}:${cmdLine}: unterminated quoted argument`);
        i++;
        args.push({ text: s, quoted: true });
        continue;
      }
      if (ch === '[' && /^\[=*\[/.test(text.slice(i, i + 10))) die(`${file}:${line}: bracket arguments are not modelled`);
      let s = '';
      while (i < n && !/[\s()#]/.test(text[i])) {
        if (text[i] === '"') {
          const end = text.indexOf('"', i + 1);
          if (end < 0) die(`${file}:${line}: unterminated quote in unquoted argument`);
          s += text.slice(i, end + 1);
          i = end + 1;
          continue;
        }
        if (text[i] === '\\' && i + 1 < n) { s += text[i + 1]; i += 2; continue; }
        s += text[i++];
      }
      args.push({ text: s, quoted: false });
    }
    if (depth > 0) die(`${file}:${cmdLine}: unterminated ${m[0]}(`);
    cmds.push({ name, args, line: cmdLine });
  }
  return cmds;
}

// ---- evaluation ------------------------------------------------------------------------------------------------------
const MAC = { CMAKE_CXX_COMPILER_ID: 'AppleClang', MSVC: false, APPLE: true, UNIX: true, WIN32: false };
const FLAG_COMMANDS = new Set(['add_executable', 'add_library', 'target_sources', 'target_link_libraries',
  'target_compile_options', 'target_compile_definitions', 'set_source_files_properties', 'add_compile_options',
  'add_compile_definitions', 'add_definitions', 'set_property', 'set_target_properties', 'set', 'list']);
const UNMODELLED_FLAG_COMMANDS = new Set(['add_compile_options', 'add_compile_definitions', 'add_definitions',
  'set_property', 'set_target_properties']);

// Evaluates an if() condition; returns true / false, or null when it is outside the modelled subset.
function evalCond(toks) {
  const t = toks.map((a) => a.text);
  const parseOr = (k) => {
    let [v, j] = parseAnd(k);
    while (j < t.length && t[j] === 'OR') {
      const [w, j2] = parseAnd(j + 1);
      v = v === null || w === null ? null : v || w;
      j = j2;
    }
    return [v, j];
  };
  const parseAnd = (k) => {
    let [v, j] = parseNot(k);
    while (j < t.length && t[j] === 'AND') {
      const [w, j2] = parseNot(j + 1);
      v = v === null || w === null ? null : v && w;
      j = j2;
    }
    return [v, j];
  };
  const parseNot = (k) => {
    if (t[k] === 'NOT') {
      const [v, j] = parseNot(k + 1);
      return [v === null ? null : !v, j];
    }
    return parseAtom(k);
  };
  const parseAtom = (k) => {
    if (t[k] === '(') {
      const [v, j] = parseOr(k + 1);
      return [t[j] === ')' ? v : null, j + 1];
    }
    if (t[k] === 'CMAKE_CXX_COMPILER_ID' && (t[k + 1] === 'MATCHES' || t[k + 1] === 'STREQUAL') && k + 2 < t.length) {
      const id = MAC.CMAKE_CXX_COMPILER_ID;
      return [t[k + 1] === 'MATCHES' ? new RegExp(t[k + 2]).test(id) : t[k + 2] === id, k + 3];
    }
    if (Object.prototype.hasOwnProperty.call(MAC, t[k]) && typeof MAC[t[k]] === 'boolean') return [MAC[t[k]], k + 1];
    return [null, t.length];
  };
  const [v, j] = parseOr(0);
  return j === t.length ? v : null;
}

function parseCMake(file, pathVars, defVars) {
  const dir = path.dirname(file);
  const rel = path.relative(ROOT, file).replace(/\\/g, '/');
  const cmds = tokenize(fs.readFileSync(file, 'utf8'), rel);
  const vars = {};
  const targets = {};
  const srcProps = {};
  const expand = (s, at, map) => s.replace(/\$\{([A-Za-z0-9_]+)\}/g, (all, v) => {
    if (Object.prototype.hasOwnProperty.call(map, v)) return map[v];
    if (Object.prototype.hasOwnProperty.call(vars, v)) return vars[v].join(';');
    die(`${rel}:${at}: unknown variable \${${v}}`);
    return all;
  });
  // A list-valued argument: ${VAR} holding a list expands to several items when unquoted.
  const items = (args, at, map) => {
    const out = [];
    for (const a of args) {
      const e = expand(a.text, at, map);
      if (a.quoted) out.push(e);
      else out.push(...e.split(';').filter((x) => x !== ''));
    }
    return out;
  };
  const resolveSrc = (s) => path.resolve(dir, s);
  const target = (name, at) => targets[name] || die(`${rel}:${at}: unknown target ${name}`);
  const flagArgs = (args, at) => {
    const out = items(args, at, defVars);
    for (const o of out) if (o.includes('$<')) die(`${rel}:${at}: generator expression not modelled: ${o}`);
    return out;
  };
  // Usage-requirement keyword handling: PRIVATE / PUBLIC apply to the target, INTERFACE items do not.
  const ownItems = (list) => {
    const out = [];
    let apply = true;
    for (const x of list) {
      if (x === 'PRIVATE' || x === 'PUBLIC') { apply = true; continue; }
      if (x === 'INTERFACE') { apply = false; continue; }
      if (apply) out.push(x);
    }
    return out;
  };

  // Stack of { active, taken, unknown } for if blocks; foreach / function / macro bodies are skipped.
  const stack = [];
  let skipDepth = 0;
  const isActive = () => stack.every((f) => f.active === true);
  const isUnknown = () => stack.some((f) => f.active === null);
  for (const c of cmds) {
    if (skipDepth > 0) {
      if (['foreach', 'function', 'macro', 'while'].includes(c.name)) skipDepth++;
      else if (['endforeach', 'endfunction', 'endmacro', 'endwhile'].includes(c.name)) skipDepth--;
      else if (FLAG_COMMANDS.has(c.name) && c.name !== 'set' && c.name !== 'list')
        die(`${rel}:${c.line}: ${c.name}() inside a foreach / function / macro body is not modelled`);
      continue;
    }
    if (['foreach', 'function', 'macro', 'while'].includes(c.name)) { skipDepth = 1; continue; }
    if (c.name === 'if') {
      const v = isActive() ? evalCond(c.args) : false;
      stack.push({ active: v, taken: v === true });
      continue;
    }
    if (c.name === 'elseif' || c.name === 'else') {
      const f = stack[stack.length - 1] || die(`${rel}:${c.line}: ${c.name}() without if()`);
      const outerActive = stack.slice(0, -1).every((g) => g.active === true);
      if (f.active === null || !outerActive) { f.active = f.active === null ? null : false; continue; }
      if (f.taken) { f.active = false; continue; }
      const v = c.name === 'else' ? true : evalCond(c.args);
      f.active = v;
      f.taken = v === true;
      continue;
    }
    if (c.name === 'endif') { if (!stack.pop()) die(`${rel}:${c.line}: endif() without if()`); continue; }
    if (isUnknown()) {
      if (FLAG_COMMANDS.has(c.name)) die(`${rel}:${c.line}: ${c.name}() under an if() condition this parser cannot evaluate`);
      continue;
    }
    if (!isActive()) continue;
    // set_property / set_target_properties matter only when they touch a compile property.
    const touchesCompile = c.args.some((x) => /COMPILE|_FLAGS|DEFINITIONS|OPTIONS/.test(x.text));
    if (UNMODELLED_FLAG_COMMANDS.has(c.name) && (touchesCompile || !c.name.startsWith('set_')))
      die(`${rel}:${c.line}: ${c.name}() is not modelled`);
    const a = c.args;
    switch (c.name) {
      case 'set': {
        const name = expand(a[0].text, c.line, pathVars);
        const vals = items(a.slice(1).filter((x) => !(!x.quoted && ['CACHE', 'PARENT_SCOPE', 'FORCE'].includes(x.text))), c.line, pathVars);
        vars[name] = vals;
        break;
      }
      case 'list':
        if (a[0].text === 'APPEND') {
          const name = a[1].text;
          vars[name] = (vars[name] || []).concat(items(a.slice(2), c.line, pathVars));
        }
        break;
      case 'add_executable':
      case 'add_library': {
        const name = a[0].text;
        const rest = items(a.slice(1), c.line, pathVars).filter((x) =>
          !['STATIC', 'SHARED', 'MODULE', 'OBJECT', 'INTERFACE', 'WIN32', 'MACOSX_BUNDLE', 'EXCLUDE_FROM_ALL'].includes(x));
        targets[name] = { sources: rest.filter((x) => /\.(c|cc|cpp|cxx)$/.test(x)).map(resolveSrc), options: [], defs: [], libs: [], line: c.line };
        break;
      }
      case 'target_sources': {
        const t = target(a[0].text, c.line);
        t.sources.push(...ownItems(items(a.slice(1), c.line, pathVars)).filter((x) => /\.(c|cc|cpp|cxx)$/.test(x)).map(resolveSrc));
        break;
      }
      case 'target_compile_options': {
        const t = target(a[0].text, c.line);
        t.options.push(...ownItems(flagArgs(a.slice(1), c.line)).filter((x) => x !== 'BEFORE'));
        break;
      }
      case 'target_compile_definitions':
        target(a[0].text, c.line).defs.push(...ownItems(flagArgs(a.slice(1), c.line)));
        break;
      case 'target_link_libraries':
        target(a[0].text, c.line).libs.push(...ownItems(items(a.slice(1), c.line, pathVars)));
        break;
      case 'set_source_files_properties': {
        const pi = a.findIndex((x) => !x.quoted && x.text === 'PROPERTIES');
        if (pi < 0) die(`${rel}:${c.line}: set_source_files_properties() without PROPERTIES`);
        const files = items(a.slice(0, pi), c.line, pathVars).map(resolveSrc);
        const props = a.slice(pi + 1);
        for (let k = 0; k + 1 < props.length; k += 2) {
          const key = props[k].text;
          const val = flagArgs([props[k + 1]], c.line).join(';');
          for (const f of files) {
            const p = (srcProps[f] = srcProps[f] || { flags: [], options: [], defs: [] });
            // COMPILE_FLAGS is one command-line string; the -O2 / IEEE strings here have no quoted spaces.
            if (key === 'COMPILE_FLAGS') p.flags = val.split(/\s+/).filter(Boolean);
            else if (key === 'COMPILE_OPTIONS') p.options = val.split(';').filter(Boolean);
            else if (key === 'COMPILE_DEFINITIONS') p.defs = val.split(';').filter(Boolean);
          }
        }
        break;
      }
      default:
        break; // project-specific calls (krate_test_pch, catch_discover_tests, ...) and features: no flag effect here
    }
  }
  if (stack.length) die(`${rel}: unterminated if()`);
  return { targets, srcProps };
}

// ---- main ------------------------------------------------------------------------------------------------------------
const argv = process.argv.slice(2);
const mode = argv.shift();
const defVarOverrides = {};
const positional = [];
for (let k = 0; k < argv.length; k++) {
  if (argv[k] === '--var') {
    const m = /^([A-Za-z0-9_]+)=(.*)$/.exec(argv[++k] || '') || die('--var needs NAME=VALUE');
    defVarOverrides[m[1]] = m[2];
  } else positional.push(argv[k]);
}
const out = [];
const rec = (...f) => out.push(f.join('\t'));
const realVars = (dir) => ({
  CMAKE_SOURCE_DIR: ROOT, CMAKE_CURRENT_SOURCE_DIR: dir,
  CMAKE_BINARY_DIR: path.join(ROOT, 'build'), CMAKE_CURRENT_BINARY_DIR: path.join(ROOT, 'build'),
});
const fwd = (p) => p.replace(/\\/g, '/');

if (mode === 'lib') {
  const dir = path.join(ROOT, 'dsp');
  const vars = realVars(dir);
  const { targets, srcProps } = parseCMake(path.join(dir, 'CMakeLists.txt'), vars, { ...vars, ...defVarOverrides });
  const t = targets.KrateDSP || die('dsp/CMakeLists.txt: no KrateDSP target');
  for (const s of t.sources) {
    rec('src', fwd(s));
    const p = srcProps[s] || { flags: [], options: [] };
    for (const o of [...t.options, ...p.flags, ...p.options]) rec('opt', fwd(s), o);
  }
} else if (mode === 'tu') {
  if (!positional.length) die('usage: tu <tu under dsp/tests/unit> ...');
  const dir = path.join(ROOT, 'dsp', 'tests');
  const vars = realVars(dir);
  const { targets, srcProps } = parseCMake(path.join(dir, 'CMakeLists.txt'), vars, { ...vars, ...defVarOverrides });
  for (const tu of positional) {
    const abs = path.resolve(dir, 'unit', tu);
    const exes = Object.keys(targets).filter((n) => targets[n].sources.includes(abs));
    if (!exes.length) { rec('exe', tu, '-'); rec('warn', tu, 'not listed in any dsp/tests/CMakeLists.txt executable'); }
    if (exes.length > 1) rec('warn', tu, `listed in ${exes.length} executables (${exes.join(', ')}); using ${exes[0]}`);
    const p = srcProps[abs] || { flags: [], options: [], defs: [] };
    const t = exes.length ? targets[exes[0]] : { options: [], defs: [], libs: [], sources: [] };
    if (exes.length) rec('exe', tu, exes[0]);
    for (const o of [...t.options, ...p.flags, ...p.options]) rec('opt', tu, o);
    for (const d of [...t.defs, ...p.defs]) rec('def', tu, d);
    for (const l of t.libs) rec('lib', tu, l);
    for (const s of t.sources) if (s !== abs) rec('sib', tu, fwd(s));
  }
  // Per-source flags of the sibling candidates too, so a pulled-in sibling builds as CMake builds it.
  const seen = new Set();
  for (const tu of positional) {
    const abs = path.resolve(dir, 'unit', tu);
    for (const n of Object.keys(targets).filter((x) => targets[x].sources.includes(abs)).slice(0, 1)) {
      for (const s of targets[n].sources) {
        if (seen.has(s)) continue;
        seen.add(s);
        const p = srcProps[s];
        if (p) for (const o of [...p.flags, ...p.options]) rec('sibopt', fwd(s), o);
      }
    }
  }
} else {
  die('usage: node tools/macos-fastmath-repro-cmake.js lib|tu [<tu> ...] [--var NAME=VALUE ...]');
}
process.stdout.write(out.join('\n') + (out.length ? '\n' : ''));
