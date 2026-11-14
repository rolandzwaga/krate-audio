#!/usr/bin/env node
//
// lint-released-ports.js
// =============================================================================
// Fails when test or tool code takes the URL / port / address of a socket or server and then releases that socket
// (drop(x) / x.close() / delete) while the URL is still meant to be used: a "closed port" made by binding and
// releasing. The OS hands a released port to the next bind, and parallel tests bind all the time.
//
// WHY. The Krate Manager updater-refusals harness built its `offline` scenario's endpoint exactly that way: bind a
// fixture server, take its URL, drop it. The scenarios run as parallel child processes, so another scenario's server
// was handed the freed port before `offline` connected: on CI (ubuntu-24.04, 2026-11-14) `offline` read a fixture's
// reply instead of a refused connection, and `same-version`'s trap proxy counted the stray request - one race, two
// reds, on the push run of a commit whose pull_request run passed. Hold the port instead: a bound, never-listening
// socket (refuses at once and stays reserved), kept alive for as long as the URL is used.
//
// A deliberate release carries `released-port-ok: <reason>` on the release line or the line above.
// Usage: node tools/lint-released-ports.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const ROOTS = ['apps', 'tools', 'tests', 'dsp/tests', 'plugins'];
const SKIP_DIR = new Set(['node_modules', 'target', 'build', '.git', 'extern', '_deps', 'dist', 'gen']);
const EXT = /\.(rs|js|mjs|cjs|ts|cpp|cc|h|hpp)$/;
const WINDOW = 12;
const OK_MARK = 'released-port-ok:';

function walk(dir, out) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const e of entries) {
    if (e.isDirectory()) {
      if (!SKIP_DIR.has(e.name)) walk(path.join(dir, e.name), out);
    } else if (EXT.test(e.name)) {
      out.push(path.join(dir, e.name));
    }
  }
  return out;
}

const stripComment = (line) => line.replace(/\/\/.*$/, '');

/** [{ line, ident, text }]: `ident` had its URL / port / address read, then was released within WINDOW lines. */
function findings(src) {
  const lines = src.split(/\r?\n/);
  const code = lines.map(stripComment);
  const out = [];
  code.forEach((l, i) => {
    // x.url(...), x.port(), x.local_addr(), x.address() (node), x.addr
    for (const m of l.matchAll(/\b([A-Za-z_]\w*)\s*\.\s*(?:url|port|local_addr|address|addr)\s*\(?/g)) {
      const ident = m[1];
      if (/^(self|this|std|Self|req|request|res|response|url|uri)$/.test(ident)) continue;
      const release = new RegExp(`\\bdrop\\s*\\(\\s*${ident}\\s*\\)|\\b${ident}\\s*\\.\\s*(?:close|shutdown|destroy)\\s*\\(|\\bdelete\\s+${ident}\\b`);
      for (let j = i; j < Math.min(code.length, i + WINDOW); ++j) {
        if (!release.test(code[j])) continue;
        // A release inside a closure written after the read (a returned `close: () => server.close()`, a Rust
        // `move || drop(x)`) runs when the caller is done with the URL, not now.
        const between = code.slice(i + 1, j + 1).join('\n');
        if (/=>|\bfunction\b|\|[^|\n]*\|\s*\{?/.test(between)) break;
        if (lines[j].includes(OK_MARK) || (lines[j - 1] || '').includes(OK_MARK)) break;
        out.push({ line: j + 1, ident, text: lines[j].trim() });
        break;
      }
    }
  });
  const seen = new Set();
  return out.filter((f) => !seen.has(f.line) && seen.add(f.line));
}

function selfTest() {
  const cases = [
    ['let closed = bind_loopback(LOOPBACK, vec![])?;\nlet url = closed.url(LATEST);\ndrop(closed);\nOk(url)', 1],
    ['const server = http.createServer();\nserver.listen(0);\nconst port = server.address().port;\nserver.close();\nfetch(`http://127.0.0.1:${port}`);', 1],
    ['let (_refusing, closed) = bind_refusing_loopback(LOOPBACK)?;\nlet rig = Rig::new(spec, move |_| Ok(format!("http://{closed}{LATEST}")))?;', 0],
    ['let url = server.url("/x");\nlet body = get(&url);\ndrop(body);', 0],
    ['let url = srv.url("/x");\n// released-port-ok: the URL is never connected to, only parsed\ndrop(srv);', 0],
    ['const port = s.address().port;\nawait run(port);\n// ... later, teardown after use is fine when it is the last use\n', 0],
    ['const bound = server.address();\nreturn {\n  port: bound.port,\n  close: () =>\n    new Promise((resolve) => {\n      server.close(() => resolve());\n    }),\n};', 0],
  ];
  let failed = 0;
  for (const [src, want] of cases) {
    const got = findings(src).length;
    if (got !== want) { ++failed; console.log(`self-test FAIL: want ${want}, got ${got}:\n  ${src.replace(/\n/g, '\n  ')}`); }
  }
  console.log(failed ? `lint-released-ports self-test: ${failed} FAILED` : `lint-released-ports self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  const files = [];
  for (const r of ROOTS) walk(path.join(REPO_ROOT, r), files);
  let bad = 0;
  for (const file of files) {
    const rel = path.relative(REPO_ROOT, file).split(path.sep).join('/');
    if (rel === 'tools/lint-released-ports.js') continue;  // its own self-test cases
    let src;
    try {
      src = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }
    if (!/\b(?:url|port|local_addr|address|addr)\s*\(?/.test(src)) continue;
    for (const f of findings(src)) {
      ++bad;
      console.log(`::error file=${rel},line=${f.line}::${rel}:${f.line}: ${f.ident} is released after its URL/port was taken`);
      console.log(`  ${f.text}`);
    }
  }
  if (bad > 0) {
    console.log('lint-released-ports: FAILED. A released port goes to the next bind, and parallel tests bind constantly;\n' +
                'the URL then reaches someone else\'s server. Hold the socket for as long as the URL is used (for a\n' +
                `refusing endpoint: bound, never listening), or mark a deliberate release "${OK_MARK} <reason>".`);
    return 1;
  }
  console.log(`lint-released-ports: OK (${files.length} files)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-released-ports: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
