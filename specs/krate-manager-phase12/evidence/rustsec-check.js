#!/usr/bin/env node
// L-RUSTSEC for Krate Manager Phase 12 (tasks.md T015 fourth bullet, lane L-RUSTSEC, TA-14; spec FR-102, FR-104, SC-021;
// plan PD-19). Run by the MAIN LOOP only, on the day it asks the user for the readiness `release-manager.yml` dry run
// (TA-14), and again by G5 if the ledger closes more than 7 days later. Never by an agent, a test or a gate: it is the
// phase's only network read (tasks.md Rule 1).
//
// WHAT: Node's global `fetch` (Node >= 18; https://nodejs.org/api/globals.html#fetch) of the three RustSec package pages
// (spec.md:137: "RustSec package pages live at https://rustsec.org/packages/<crate>.html"; a 404 means the crate has no
// advisory page), the three crate versions locked in apps/krate-manager/Cargo.lock (the `version = "..."` line after
// `name = "rquickjs"`, `"rquickjs-core"`, `"rquickjs-sys"`, plan PD-19), and one ready-to-paste row for the `## RustSec`
// table of apps/krate-manager/RELEASING.md, whose columns are PD-19's
// `| version | date | rquickjs | rquickjs-core | rquickjs-sys | result |` (date = today's UTC date, YYYY-MM-DD, which
// the preflight accepts for 7 days, FR-104). With an advisory the row's result is the ids, for the user to answer with
// a bump (the preflight refuses a bare id list: it wants "<ids> answered by bump to rquickjs <v>, ...", PD-19).
//
// Per page: 404 -> no advisory; 200 -> its `RUSTSEC-\d{4}-\d{4}` ids (sorted, distinct); a 200 page holding no id, any
// other status, or a network error is a FAILED reading (never read as "no advisory").
//
// Usage (repo root; Node only, no dependencies):
//   node specs/krate-manager-phase12/evidence/rustsec-check.js              # the reading (network)
//   node specs/krate-manager-phase12/evidence/rustsec-check.js --self-test  # fixture HTML and a fixture lock, no network
// Last line: `RUSTSEC NO ADVISORY` exit 0 | `RUSTSEC ADVISORY: <ids>` exit 1 | `RUSTSEC CHECK FAILED: <reason>` exit 2.
// It writes nothing; stdout only.
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const LOCK = path.join(ROOT, 'apps', 'krate-manager', 'Cargo.lock');
const MANAGER_VERSION = '0.1.0'; // the release this row is for (tasks.md T015, TA-14)
const CRATES = ['rquickjs', 'rquickjs-core', 'rquickjs-sys'];
const pageUrl = (crate) => `https://rustsec.org/packages/${crate}.html`;
const FETCH_TIMEOUT_MS = 30 * 1000;
const ID_RE = /\bRUSTSEC-\d{4}-\d{4}\b/g;

// ---- pure helpers (covered by --self-test) ----------------------------------------------------------------------

/**
 * The locked version of each of CRATES: the first `version = "..."` line of the `[[package]]` block whose
 * `name = "<crate>"`. Throws when a crate is missing or locked twice (two versions would make the row ambiguous).
 */
function lockVersions(lockText) {
  const found = Object.fromEntries(CRATES.map((c) => [c, []]));
  for (const block of lockText.replace(/\r\n/g, '\n').split(/^\[\[package\]\]$/m)) {
    const name = (/^name = "([^"]+)"$/m.exec(block) || [])[1];
    if (!name || !(name in found)) continue;
    const version = (/^version = "([^"]+)"$/m.exec(block) || [])[1];
    if (!version) throw new Error(`Cargo.lock: package ${name} has no version line`);
    found[name].push(version);
  }
  for (const c of CRATES) {
    if (found[c].length === 0) throw new Error(`Cargo.lock: no package ${c}`);
    if (found[c].length > 1) throw new Error(`Cargo.lock: ${c} locked ${found[c].length} times (${found[c].join(', ')})`);
  }
  return Object.fromEntries(CRATES.map((c) => [c, found[c][0]]));
}

/** The distinct advisory ids an HTML page names, sorted. */
function advisoryIds(html) {
  return [...new Set(html.match(ID_RE) || [])].sort();
}

/** One page's reading: { kind: 'none' } (404), { kind: 'advisory', ids } (200 with ids), or { kind: 'failed', reason }. */
function pageReading(status, html) {
  if (status === 404) return { kind: 'none' };
  if (status === 200) {
    const ids = advisoryIds(html);
    return ids.length ? { kind: 'advisory', ids } : { kind: 'failed', reason: 'status 200 but no RUSTSEC id on the page' };
  }
  return { kind: 'failed', reason: `status ${status}` };
}

/** The RELEASING.md row (PD-19 columns). */
function releasingRow(versions, date, ids) {
  const result = ids.length ? ids.join(', ') : 'no advisory';
  return `| ${MANAGER_VERSION} | ${date} | ${CRATES.map((c) => versions[c]).join(' | ')} | ${result} |`;
}

/**
 * The whole reading with the network and the clock injected: `fetchPage(url)` resolves { status, text } or rejects.
 * Resolves { lines, exitCode }; the last line is the verdict.
 */
async function runCheck({ fetchPage, lockText, date }) {
  const out = [];
  let versions;
  try {
    versions = lockVersions(lockText);
  } catch (e) {
    return { lines: [`RUSTSEC CHECK FAILED: ${e.message}`], exitCode: 2 };
  }
  const readings = await Promise.all(CRATES.map(async (crate) => {
    const url = pageUrl(crate);
    try {
      const { status, text } = await fetchPage(url);
      return { crate, url, status: String(status), ...pageReading(status, text) };
    } catch (e) {
      return { crate, url, status: 'no response', kind: 'failed', reason: `fetch error: ${e.message}` };
    }
  }));
  for (const r of readings) {
    const what = r.kind === 'none' ? 'no advisory page' : r.kind === 'advisory' ? `advisories ${r.ids.join(', ')}`
      : `FAILED (${r.reason})`;
    out.push(`page ${r.url}: ${r.status} -> ${what}`);
  }
  out.push(`Cargo.lock: ${CRATES.map((c) => `${c} ${versions[c]}`).join(', ')}`);
  const failed = readings.filter((r) => r.kind === 'failed');
  if (failed.length) {
    out.push(`RUSTSEC CHECK FAILED: ${failed.map((r) => `${r.crate} ${r.reason}`).join('; ')}`);
    return { lines: out, exitCode: 2 };
  }
  const ids = [...new Set(readings.flatMap((r) => r.ids || []))].sort();
  out.push('RELEASING.md ## RustSec row (paste into the table):');
  out.push(releasingRow(versions, date, ids));
  if (ids.length) {
    out.push(`RUSTSEC ADVISORY: ${ids.join(', ')}`);
    return { lines: out, exitCode: 1 };
  }
  out.push('RUSTSEC NO ADVISORY');
  return { lines: out, exitCode: 0 };
}

async function selfTest() {
  const failures = [];
  let cases = 0;
  const check = async (name, fn) => { cases++; try { await fn(); } catch (e) { failures.push(`${name}: ${e.message}`); } };
  const eq = (got, want) => { if (got !== want) throw new Error(`got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); };
  const throws = (fn, re) => {
    let msg = null;
    try { fn(); } catch (e) { msg = e.message; }
    if (msg === null || !re.test(msg)) throw new Error(`did not throw ${re} (got ${JSON.stringify(msg)})`);
  };

  const pkg = (name, version) => ['[[package]]', `name = "${name}"`, `version = "${version}"`,
    'source = "registry+https://github.com/rust-lang/crates.io-index"', 'checksum = "00"', 'dependencies = [',
    ` "${name}-dep",`, ']', ''].join('\n');
  const lock = ['# This file is automatically @generated by Cargo.', 'version = 4', '', pkg('rquickjs', '0.14.1'),
    pkg('rquickjs-core', '0.14.0'), pkg('rquickjs-macro', '9.9.9'), pkg('rquickjs-sys', '0.14.2'), pkg('serde', '1.0.0')]
    .join('\n');
  const versions = { rquickjs: '0.14.1', 'rquickjs-core': '0.14.0', 'rquickjs-sys': '0.14.2' };

  await check('lock: each crate its own version (rquickjs-macro is not rquickjs)', () =>
    eq(JSON.stringify(lockVersions(lock)), JSON.stringify(versions)));
  await check('lock: CRLF text', () => eq(JSON.stringify(lockVersions(lock.replace(/\n/g, '\r\n'))), JSON.stringify(versions)));
  await check('lock: a missing crate throws', () => throws(() => lockVersions(lock.replace(pkg('rquickjs-sys', '0.14.2'), '')),
    /no package rquickjs-sys/));
  await check('lock: a crate locked twice throws', () => throws(() => lockVersions(`${lock}\n${pkg('rquickjs', '0.13.0')}`),
    /rquickjs locked 2 times/));
  await check('lock: the real apps/krate-manager/Cargo.lock reads three versions', () => {
    const v = lockVersions(fs.readFileSync(LOCK, 'utf8'));
    eq(CRATES.every((c) => /^\d+\.\d+\.\d+/.test(v[c])), true);
  });

  const advisoryHtml = '<html><body><h1>rquickjs</h1><ul>' +
    '<li><a href="/advisories/RUSTSEC-2025-0042.html">RUSTSEC-2025-0042</a> use after free</li>' +
    '<li><a href="/advisories/RUSTSEC-2024-0007.html">RUSTSEC-2024-0007</a></li></ul>' +
    '<p>See RUSTSEC-2025-0042 above. Not an id: XRUSTSEC-2025-00421, RUSTSEC-25-1.</p></body></html>';
  await check('ids: distinct and sorted, embedded non-ids ignored', () =>
    eq(advisoryIds(advisoryHtml).join(','), 'RUSTSEC-2024-0007,RUSTSEC-2025-0042'));
  await check('page: 404 -> no advisory', () => eq(pageReading(404, '<html>Not Found</html>').kind, 'none'));
  await check('page: 200 with ids -> advisory', () => eq(pageReading(200, advisoryHtml).ids.join(','),
    'RUSTSEC-2024-0007,RUSTSEC-2025-0042'));
  await check('page: 200 without an id -> failed, never no advisory', () => eq(pageReading(200, '<html>rquickjs</html>').kind,
    'failed'));
  await check('page: 500 -> failed', () => eq(pageReading(500, '').reason, 'status 500'));
  await check('page: 301 -> failed', () => eq(pageReading(301, '').kind, 'failed'));

  await check('row: no advisory, each crate its own version', () =>
    eq(releasingRow(versions, '2026-11-02', []), '| 0.1.0 | 2026-11-02 | 0.14.1 | 0.14.0 | 0.14.2 | no advisory |'));
  await check('row: the ids for the user to answer', () => eq(releasingRow(versions, '2026-11-02', ['RUSTSEC-2025-0042']),
    '| 0.1.0 | 2026-11-02 | 0.14.1 | 0.14.0 | 0.14.2 | RUSTSEC-2025-0042 |'));
  await check('urls are the three package pages (spec.md:137)', () => eq(CRATES.map(pageUrl).join(' '),
    'https://rustsec.org/packages/rquickjs.html https://rustsec.org/packages/rquickjs-core.html' +
    ' https://rustsec.org/packages/rquickjs-sys.html'));

  // The whole flow with an injected fetch: no network.
  const asked = [];
  const fake = (pages) => async (url) => {
    asked.push(url);
    const p = pages[url];
    if (p instanceof Error) throw p;
    return p || { status: 404, text: 'Not Found' };
  };
  const date = '2026-11-02';
  await check('flow: three 404 -> NO ADVISORY, the row, exit 0', async () => {
    asked.length = 0;
    const r = await runCheck({ fetchPage: fake({}), lockText: lock, date });
    eq(r.exitCode, 0);
    eq(r.lines[r.lines.length - 1], 'RUSTSEC NO ADVISORY');
    eq(r.lines[r.lines.length - 2], '| 0.1.0 | 2026-11-02 | 0.14.1 | 0.14.0 | 0.14.2 | no advisory |');
    eq(r.lines[0], 'page https://rustsec.org/packages/rquickjs.html: 404 -> no advisory page');
    eq(r.lines.includes('Cargo.lock: rquickjs 0.14.1, rquickjs-core 0.14.0, rquickjs-sys 0.14.2'), true);
    eq(asked.slice().sort().join(' '), CRATES.map(pageUrl).sort().join(' '));
  });
  await check('flow: one advisory page -> ADVISORY: <ids>, the row names them, exit 1', async () => {
    const r = await runCheck({ fetchPage: fake({ [pageUrl('rquickjs-core')]: { status: 200, text: advisoryHtml } }),
      lockText: lock, date });
    eq(r.exitCode, 1);
    eq(r.lines[r.lines.length - 1], 'RUSTSEC ADVISORY: RUSTSEC-2024-0007, RUSTSEC-2025-0042');
    eq(r.lines[r.lines.length - 2], '| 0.1.0 | 2026-11-02 | 0.14.1 | 0.14.0 | 0.14.2 | RUSTSEC-2024-0007, RUSTSEC-2025-0042 |');
  });
  await check('flow: ids on two pages are merged and deduplicated', async () => {
    const r = await runCheck({ fetchPage: fake({ [pageUrl('rquickjs')]: { status: 200, text: advisoryHtml },
      [pageUrl('rquickjs-sys')]: { status: 200, text: 'RUSTSEC-2025-0042 RUSTSEC-2026-0001' } }), lockText: lock, date });
    eq(r.lines[r.lines.length - 1], 'RUSTSEC ADVISORY: RUSTSEC-2024-0007, RUSTSEC-2025-0042, RUSTSEC-2026-0001');
  });
  await check('flow: a network error -> CHECK FAILED, no row, exit 2', async () => {
    const r = await runCheck({ fetchPage: fake({ [pageUrl('rquickjs-sys')]: new Error('getaddrinfo ENOTFOUND') }),
      lockText: lock, date });
    eq(r.exitCode, 2);
    eq(r.lines[r.lines.length - 1], 'RUSTSEC CHECK FAILED: rquickjs-sys fetch error: getaddrinfo ENOTFOUND');
    eq(r.lines.some((l) => l.startsWith('| 0.1.0 ')), false);
  });
  await check('flow: a 503 -> CHECK FAILED', async () => {
    const r = await runCheck({ fetchPage: fake({ [pageUrl('rquickjs')]: { status: 503, text: '' } }), lockText: lock, date });
    eq(r.lines[r.lines.length - 1], 'RUSTSEC CHECK FAILED: rquickjs status 503');
  });
  await check('flow: an unreadable lock -> CHECK FAILED before any fetch', async () => {
    asked.length = 0;
    const r = await runCheck({ fetchPage: fake({}), lockText: 'version = 4\n', date });
    eq(r.exitCode, 2);
    eq(r.lines[r.lines.length - 1], 'RUSTSEC CHECK FAILED: Cargo.lock: no package rquickjs');
    eq(asked.length, 0);
  });

  for (const f of failures) console.log(`self-test FAIL ${f}`);
  console.log(failures.length ? `RUSTSEC SELF-TEST RED (${failures.length} of ${cases} failed)`
    : `RUSTSEC SELF-TEST GREEN (${cases} cases)`);
  return failures.length ? 1 : 0;
}

async function fetchPage(url) {
  const res = await fetch(url, { signal: AbortSignal.timeout(FETCH_TIMEOUT_MS) });
  return { status: res.status, text: await res.text() };
}

async function main(argv) {
  if (argv.includes('--self-test')) return selfTest();
  let lockText;
  try {
    lockText = fs.readFileSync(LOCK, 'utf8');
  } catch (e) {
    console.log(`RUSTSEC CHECK FAILED: ${e.message}`);
    return 2;
  }
  const date = new Date().toISOString().slice(0, 10);
  const r = await runCheck({ fetchPage, lockText, date });
  for (const l of r.lines) console.log(l);
  return r.exitCode;
}

module.exports = { CRATES, pageUrl, lockVersions, advisoryIds, pageReading, releasingRow, runCheck };
if (require.main === module) main(process.argv.slice(2)).then((code) => { process.exitCode = code; });
