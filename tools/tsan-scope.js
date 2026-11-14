#!/usr/bin/env node
// =============================================================================
// tsan-scope.js - what the ThreadSanitizer nightly builds and runs
// =============================================================================
// ThreadSanitizer finds data races only in code that actually runs on more than one thread during the run, and costs
// 5-15x. Most suites are single-threaded, so the TSan lane (tsan-nightly.yml) runs only the test files that spawn
// threads (std::thread / std::jthread / std::async), found here by scanning the sources, never by a hand-kept list or
// tag: a new threaded test is in scope the night it lands.
//
//   node tools/tsan-scope.js --targets   the executables that compile those files (space-separated)
//   node tools/tsan-scope.js --filter    the Catch2 filter for them: [#<file>] per file (run with -# /
//                                        --filenames-as-tags), each alternative minus [long], perf and [wallclock]
//   node tools/tsan-scope.js --check     exit 1 when a threaded test file is in no add_executable() list
//   node tools/tsan-scope.js --self-test
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const THREADS = /\bstd::(?:thread|jthread|async)\b/;
// Excluded inside every OR alternative (Catch2 binds ~ to its own comma-separated alternative). [long] renders and
// the timing cases cannot run at TSan's slowdown; [wallclock] bars cannot hold under it.
const EXCLUDE = '~[long]~[performance]~[perf]~[benchmark]~[!benchmark]~[wallclock]~[.]';

function testDirs() {
  const dirs = [path.join(ROOT, 'dsp', 'tests'), path.join(ROOT, 'tests')];
  for (const p of fs.readdirSync(path.join(ROOT, 'plugins'))) dirs.push(path.join(ROOT, 'plugins', p, 'tests'));
  return dirs.filter((d) => fs.existsSync(d));
}

function walk(dir, pred, out = []) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, pred, out);
    else if (pred(p)) out.push(p);
  }
  return out;
}

const rel = (p) => path.relative(ROOT, p).split(path.sep).join('/');

/** Repo-relative paths of the test sources that spawn threads, sorted. */
function threadedFiles() {
  return testDirs().flatMap((d) => walk(d, (p) => p.endsWith('.cpp')))
    .filter((p) => THREADS.test(fs.readFileSync(p, 'utf8').replace(/\/\/.*$/gm, '')))
    .map(rel).sort();
}

/** [{ target, sources: [token] }] for every add_executable(...) and target_sources(...) block of one CMakeLists text
 *  (target_sources: a conditional source such as shared_tests' Linux-only test_libcurl_fetch.cpp). */
function executables(text) {
  const out = [];
  const clean = text.replace(/#.*$/gm, '');
  const re = /\b(?:add_executable|target_sources)\s*\(/g;
  let m;
  while ((m = re.exec(clean)) !== null) {
    let depth = 1;
    let i = re.lastIndex;
    while (i < clean.length && depth > 0) {
      if (clean[i] === '(') depth += 1;
      else if (clean[i] === ')') depth -= 1;
      i += 1;
    }
    const tokens = clean.slice(re.lastIndex, i - 1).split(/\s+/).filter(Boolean)
      .filter((t) => !/^(?:PRIVATE|PUBLIC|INTERFACE)$/.test(t));
    if (tokens.length > 0) out.push({ target: tokens[0], sources: tokens.slice(1) });
  }
  return out;
}

/** file (repo-relative) -> [targets], from every CMakeLists.txt under the test directories. */
function owners(files) {
  const lists = testDirs().flatMap((d) => walk(d, (p) => path.basename(p) === 'CMakeLists.txt'));
  const map = new Map(files.map((f) => [f, new Set()]));
  for (const cm of lists) {
    for (const exe of executables(fs.readFileSync(cm, 'utf8'))) {
      for (const tok of exe.sources) {
        // ${CMAKE_CURRENT_SOURCE_DIR}/x.cpp, unit/x.cpp, x.cpp: compare the path tail.
        const tail = tok.replace(/^\$\{[^}]+\}\//, '').replace(/^\.\//, '');
        if (!tail.endsWith('.cpp')) continue;
        for (const f of files) {
          if (f === rel(path.join(path.dirname(cm), tail)) || (path.basename(f) === tail && f.startsWith(rel(path.dirname(cm))))) {
            map.get(f).add(exe.target);
          }
        }
      }
    }
  }
  return map;
}

/** The Catch2 filter: one alternative per file, each with the exclusions. */
function filterFor(files) {
  return files.map((f) => `[#${path.basename(f, '.cpp')}]${EXCLUDE}`).join(',');
}

function selfTest() {
  const cm = 'add_executable(a_tests\n  unit/x_test.cpp  # comment (with parens)\n  ${CMAKE_CURRENT_SOURCE_DIR}/y_test.cpp\n)\n'
    + 'set_source_files_properties(unit/x_test.cpp PROPERTIES COMPILE_FLAGS "-O2")\nadd_executable(b_tests z.cpp)\n'
    + 'if(LINUX)\n  target_sources(a_tests PRIVATE w_test.cpp)\nendif()\n';
  const exes = executables(cm);
  const cases = [
    ['three source blocks parsed', exes.length, 3],
    ['target_sources keyword dropped', `${exes[2].target}:${exes[2].sources.join(' ')}`, 'a_tests:w_test.cpp'],
    ['first target name', exes[0].target, 'a_tests'],
    ['comment with parens ignored', exes[0].sources.join(' '), 'unit/x_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/y_test.cpp'],
    ['property lists are not executables', exes[1].target, 'b_tests'],
    ['filter alternative per file, exclusions in each', filterFor(['a/x_test.cpp', 'b/y.cpp']),
      `[#x_test]${EXCLUDE},[#y]${EXCLUDE}`],
  ];
  let bad = 0;
  for (const [what, got, want] of cases) {
    const ok = got === want;
    if (!ok) bad += 1;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}: ${JSON.stringify(got)}`);
  }
  console.log(bad === 0 ? `tsan-scope self-test: OK (${cases.length})` : `tsan-scope self-test: ${bad} FAILED`);
  return bad === 0 ? 0 : 1;
}

function main() {
  const a = process.argv.slice(2);
  if (a.includes('--self-test')) return selfTest();
  const files = threadedFiles();
  const map = owners(files);
  if (a.includes('--check')) {
    const orphans = files.filter((f) => map.get(f).size === 0);
    for (const f of orphans) console.log(`::error file=${f}::${f} spawns threads but is in no add_executable() list: tsan-scope cannot run it`);
    console.log(orphans.length ? `tsan-scope: FAILED (${orphans.length} unowned)` : `tsan-scope: OK (${files.length} threaded test files)`);
    return orphans.length ? 1 : 0;
  }
  if (a.includes('--targets')) {
    console.log([...new Set(files.flatMap((f) => [...map.get(f)]))].sort().join(' '));
    return 0;
  }
  if (a.includes('--filter')) {
    console.log(filterFor(files));
    return 0;
  }
  for (const f of files) console.log(`${f}\t${[...map.get(f)].join(' ') || '(no executable)'}`);
  return 0;
}

process.exit(main());
