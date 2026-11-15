#!/usr/bin/env node
//
// check-build-warnings.js
// =============================================================================
// Fails when a captured build log contains any compiler, linker or build-system warning. CLAUDE.md: "All code must
// compile with ZERO warnings"; until 2026-11-14 nothing enforced it (CMake builds with /W4 and -Wall, no /WX or
// -Werror), so third-party C4244 / C4267 warnings sat in the Windows Release build unnoticed.
//
// Recognised forms (a warning counts once per file / line / code / message, however often the log repeats it):
//   MSVC compile   F:\x\a.cpp(12,5): warning C4244: '=': conversion ... [F:\x\a.vcxproj]
//   MSVC link      LINK : warning LNK4098: defaultlib ...   /  x.obj : warning LNK4221: ...
//   MSBuild        C:\...\Microsoft.CppCommon.targets(254,5): warning MSB8065: ...
//   GCC / Clang    /src/a.cpp:12:5: warning: unused variable 'x' [-Wunused-variable]
//   Tools          cc1: warning: ...   ld: warning: ...   /usr/bin/ld: warning: ...   ld.lld: warning: ...
//   Xcode          warning: Run script build phase 'x' will be run during every build ...
//
// CI pipes each build step through `tee build-output.log` and runs this on the log (.github/workflows/ci.yml, all
// three OSes). A warning is fixed at its source, or, for third-party code, silenced for that target only in
// CMakeLists.txt (as for vstgui /wd4267 and pffft's PFDSP).
//
// Usage: node tools/check-build-warnings.js <build-log> [<build-log> ...] | --self-test
// Exit codes: 0 = no warnings, 1 = warnings found, 2 = internal error (unreadable log).
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const PATTERNS = [
  // MSVC compile / MSBuild: path(line[,col]): warning CODE: message [project]
  { re: /^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*warning\s+([A-Z]+\d+)\s*:\s*(.*?)(?:\s+\[[^\]]+\])?\s*$/, file: 1, line: 2, code: 4, msg: 5 },
  // MSVC link: <tool or object> : warning LNKnnnn: message [project]
  { re: /^\s*(.+?)\s+:\s+warning\s+(LNK\d+|MSB\d+)\s*:\s*(.*?)(?:\s+\[[^\]]+\])?\s*$/, file: 1, code: 2, msg: 3 },
  // GCC / Clang: path:line:col: warning: message [-Wflag]
  { re: /^(.+?):(\d+):(?:(\d+):)?\s+warning:\s+(.*?)(?:\s+\[(-W[^\]]+)\])?\s*$/, file: 1, line: 2, msg: 4, code: 5 },
  // Tools without a source location (cc1, cc1plus, ld, libtool, ...) and Xcode: `[tool: ]warning: message`. Xcode
  // prefixes a linker warning with the project and target, so the prefix is a chain:
  // `/p/KrateAudio.xcodeproj: Seraphis: ld: warning: ignoring duplicate libraries: ...` (missed until 2026-11-15).
  { re: /^\s*(?:[\w./+-]+:\s*)*warning:\s+(.*?)\s*$/, msg: 1 },
];

function parse(text) {
  const found = new Map();
  text.split(/\r?\n/).forEach((raw, i) => {
    const line = raw.replace(/\x1b\[[0-9;]*[mK]/g, '');
    for (const p of PATTERNS) {
      const m = p.re.exec(line);
      if (!m) continue;
      const w = {
        file: p.file ? m[p.file].trim() : '',
        line: p.line ? Number(m[p.line]) : 0,
        code: (p.code && m[p.code]) || '',
        msg: m[p.msg].trim(),
        logLine: i + 1,
      };
      const key = `${w.file.replace(/\\/g, '/').toLowerCase()}|${w.line}|${w.code}|${w.msg}`;
      if (!found.has(key)) found.set(key, w);
      break;
    }
  });
  return [...found.values()];
}

/** Workspace-relative path with forward slashes, for a GitHub annotation. */
function relative(file) {
  if (!file) return '';
  const norm = file.replace(/\\/g, '/');
  const root = (process.env.GITHUB_WORKSPACE || process.cwd()).replace(/\\/g, '/').replace(/\/$/, '');
  return norm.toLowerCase().startsWith(`${root.toLowerCase()}/`) ? norm.slice(root.length + 1) : norm;
}

function selfTest() {
  const cases = [
    ['msvc compile', "F:\\p\\_deps\\pffft-src\\src\\pf_cic.cpp(102,17): warning C4244: '=': conversion from 'float' to 'uint64_t', possible loss of data [F:\\p\\build\\PFDSP.vcxproj]", 1],
    ['msvc compile, indented, no column', '  C:\\a\\b.cpp(7): warning C4100: \'x\': unreferenced parameter', 1],
    ['msvc link', 'LINK : warning LNK4098: defaultlib \'LIBCMT\' conflicts with use of other libs [F:\\p\\x.vcxproj]', 1],
    ['msbuild', 'C:\\VS\\Microsoft.CppCommon.targets(254,5): warning MSB8065: Custom build for item "x" succeeded, but specified output "y" has not been created. [F:\\p\\x.vcxproj]', 1],
    ['gcc', "/home/runner/work/r/r/dsp/a.cpp:12:5: warning: unused variable 'x' [-Wunused-variable]", 1],
    ['clang, no flag', '/Users/runner/work/r/r/a.mm:3:1: warning: method possibly missing a [super dealloc] call', 1],
    ['ld', 'ld: warning: object file (x.o) was built for newer macOS version (15.0) than being linked (13.0)', 1],
    ['/usr/bin/ld', '/usr/bin/ld: warning: x.o: missing .note.GNU-stack section implies executable stack', 1],
    ['xcode ld, project + target prefix', "/Users/runner/work/r/r/build/KrateAudio.xcodeproj: Seraphis: ld: warning: ignoring duplicate libraries: '/b/lib/Release/libKrateDSP.a', '/b/lib/Release/libsdk.a'", 1],
    ['cc1 option', "cc1: warning: command-line option '-Wsuggest-override' is valid for C++/ObjC++ but not for C", 1],
    ['xcode run script', "warning: Run script build phase 'Copy' will be run during every build because it does not specify any outputs.", 1],
    ['colourised gcc', "\x1b[01m\x1b[K/src/a.cpp:1:2:\x1b[m\x1b[K \x1b[01;35m\x1b[Kwarning: \x1b[m\x1b[Kunused [-Wunused]", 1],
    ['error is not a warning', '/src/a.cpp:12:5: error: expected \';\'', 0],
    ['compile line', '  pf_cic.cpp', 0],
    ['cmake echo of a flag', 'Building targets: -Wno-warning dsp_core_tests', 0],
    ['test name with warning', '[ctest] warning_counter_test passed', 0],
    ['note', '/src/a.cpp:1:1: note: in instantiation of', 0],
    ['duplicate collapses', "/s/a.cpp:1:1: warning: x [-Wx]\n/s/a.cpp:1:1: warning: x [-Wx]", 1],
  ];
  let failed = 0;
  for (const [name, text, want] of cases) {
    const got = parse(text).length;
    if (got !== want) { ++failed; console.log(`self-test FAIL: ${name}: want ${want}, got ${got}`); }
  }
  console.log(failed ? `check-build-warnings self-test: ${failed} FAILED` : `check-build-warnings self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  const args = process.argv.slice(2);
  if (args.includes('--self-test')) return selfTest();
  if (args.length === 0) { console.error('usage: node tools/check-build-warnings.js <build-log> [...] | --self-test'); return 2; }
  let total = 0;
  for (const logPath of args) {
    let text;
    try {
      text = fs.readFileSync(logPath, 'utf8');
    } catch (err) {
      console.error(`check-build-warnings: cannot read ${logPath}: ${err.message}`);
      return 2;
    }
    const warnings = parse(text);
    total += warnings.length;
    for (const w of warnings) {
      const file = relative(w.file);
      const loc = file ? ` file=${file}${w.line ? `,line=${w.line}` : ''}` : '';
      const what = `${w.code ? `${w.code}: ` : ''}${w.msg}`;
      console.log(`::error${loc}::${file ? `${file}${w.line ? `:${w.line}` : ''}: ` : ''}warning ${what}  (${path.basename(logPath)}:${w.logLine})`);
    }
  }
  if (total > 0) {
    console.log(`check-build-warnings: FAILED, ${total} distinct warning(s). Fix each at its source; a third-party target ` +
                'gets a per-target suppression in CMakeLists.txt, never a global one.');
    return 1;
  }
  console.log(`check-build-warnings: OK (no warnings in ${args.join(', ')})`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`check-build-warnings: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
