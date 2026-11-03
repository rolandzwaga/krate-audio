#!/usr/bin/env node
//
// manager-readiness-counts.js
// =============================================================================
// Counts the PENDING-CI rows of the Krate Manager phases and checks the release-readiness ledger against them.
//
// WHY. Manager 1.0.0 is ready to dispatch only when every PENDING-CI row of Phases 1-11 is closed by a named green run,
// except the seven post-release rows of D17 (specs/krate-manager-phase12/spec.md FR-100, D14, D17; SC-020, SC-028;
// plan PD-26; tasks TA-16). The counts move whenever a compliance file is rewritten, and the roadmap status lines
// already disagree with the files (spec F-4), so no count is hard-coded: this tool computes both at the readiness
// commit, and its --check-ledger mode refuses a ledger that lists the wrong number of rows, repeats one, leaves one
// open, or exempts any row outside the closed D17 list.
//
// Usage: node tools/manager-readiness-counts.js --specs <dir> --roadmap <file> [--check-ledger <readiness.md>]
//   --specs         the dir holding krate-manager-phase<N>/compliance.md; a compliance row counts when one of its cells
//                   is exactly pending-ci, any case (the `grep -ci "| pending-ci"` reading of spec F-4)
//   --roadmap       the roadmap; per "### Phase <N>:" section, the "PENDING-CI: <n> item(s)" of its first "**Status:"
//                   line (null when the section has none, or its status line has no count)
// Prints one "phase<N> file=<a> status=<b>" line per phase with a compliance file, then "total file=<A> status=<B>"
// over Phases 1-11 only (B is null when any of those status counts is null), exit 0.
// --check-ledger parses the "## PENDING-CI rows" and "## Post-release rows" tables (columns | id | job and assertion |
// closing workflow | run URL | commit | verdict |; "\|" is a pipe inside a cell). A PENDING-CI row is accepted when it
// is closed by a run (https://github.com/<owner>/<repo>/actions/runs/<n>, a 7-40 hex commit, verdict green) or
// deferred (verdict post-release, no run URL, a D17 id that is also in Post-release).
// Each refusal exits 1 with "manager-readiness-counts: <CODE>: <message>" on stderr (every one found is printed):
//   E_READINESS_SPECS      a Phase 1-11 compliance.md is missing
//   E_LEDGER_TABLE         a table is missing, repeated, has another header, or a row has not 6 cells or a bad id
//   E_LEDGER_COUNT         the PENDING-CI row count differs from the Phases 1-11 file total
//   E_LEDGER_REPEAT        a PENDING-CI id repeats
//   E_LEDGER_ROW           a PENDING-CI row is neither closed by a run nor deferred
//   E_LEDGER_POST_RELEASE  the Post-release ids are not exactly the seven D17 ids, or one verdict is not post-release
//   E_LEDGER_BOTH          a PENDING-CI row closed by a run has an id that is also a Post-release id
// Exit 2 = usage, an unreadable argument, or an internal error. Reads only its arguments (and the compliance files
// under --specs); writes nothing; no network, no child process.
// =============================================================================
'use strict';

const fs = require('node:fs');
const path = require('node:path');

const NAME = 'manager-readiness-counts';
const USAGE =
  'usage: node tools/manager-readiness-counts.js --specs <dir> --roadmap <file> [--check-ledger <readiness.md>]';
const OPTIONS = ['--specs', '--roadmap', '--check-ledger'];

// FR-100: the ledger covers Phases 1-11; a later phase's file prints its own line only.
const LAST_LEDGER_PHASE = 11;
const PHASE_DIR_RE = /^krate-manager-phase([1-9]\d*)$/;
const PENDING_CELL_RE = /\|\s*pending-ci\s*\|/i;
const PHASE_HEADING_RE = /^### Phase ([1-9]\d*):/;
const HEADING_RE = /^#{1,3} /;
const STATUS_COUNT_RE = /PENDING-CI: (\d+) item\(s\)/;

// D17: the closed list of post-release rows. A row added here needs a spec revision.
const D17_IDS = [
  'phase11:SC-014:publish',
  'phase11:SC-020',
  'phase12:R-POST-1',
  'phase12:R-POST-2',
  'phase12:R-POST-3',
  // Spec revision 2026-11-03: no Mac before release, so the macOS user checks follow the first real dispatch.
  'phase12:R-POST-4',
  'phase12:R-POST-5',
];
// TA-16: the two row tables' headings and columns.
const PENDING_HEADING = 'PENDING-CI rows';
const POST_HEADING = 'Post-release rows';
const LEDGER_COLUMNS = ['id', 'job and assertion', 'closing workflow', 'run URL', 'commit', 'verdict'];
const ID_RE = /^phase[1-9]\d*:[^\s:|]+(?::[^\s:|]+)?$/;
const SEPARATOR_CELL_RE = /^:?-+:?$/;
const RUN_URL_RE = /^https:\/\/github\.com\/[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+\/actions\/runs\/\d+$/;
const COMMIT_RE = /^[0-9a-f]{7,40}$/i;

class UsageError extends Error {}

class ReadinessError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

const splitLines = (text) => text.split(/\r?\n/);

/** The number of lines of a compliance file with a cell that is exactly `pending-ci` (any case). */
function countCompliance(text) {
  return splitLines(text).filter((l) => PENDING_CELL_RE.test(l)).length;
}

/**
 * Per `### Phase <N>:` section of the roadmap (up to the next heading of level 1-3), the count of the section's first
 * `**Status:` line, or null when it has none or the line holds no `PENDING-CI: <n> item(s)`. Map<phase, number|null>;
 * a repeated phase heading keeps the first section's value.
 */
function countStatusLines(text) {
  const counts = new Map();
  const statusSeen = new Set();
  let current = null;
  for (const line of splitLines(text)) {
    const heading = PHASE_HEADING_RE.exec(line);
    if (heading) {
      const phase = Number(heading[1]);
      current = counts.has(phase) ? null : phase;
      if (current !== null) counts.set(phase, null);
      continue;
    }
    if (HEADING_RE.test(line)) {
      current = null;
      continue;
    }
    if (current !== null && !statusSeen.has(current) && line.startsWith('**Status:')) {
      statusSeen.add(current);
      const m = STATUS_COUNT_RE.exec(line);
      counts.set(current, m ? Number(m[1]) : null);
    }
  }
  return counts;
}

// The cells of a table line, split on unescaped pipes (TA-16: "\|" is a literal pipe), each trimmed; null when the
// line does not start and end with a pipe.
function splitRow(line) {
  const s = line.trim();
  if (!s.startsWith('|')) return null;
  const cells = [];
  let cell = '';
  for (let i = 1; i < s.length; i++) {
    if (s[i] === '\\' && s[i + 1] === '|') {
      cell += '|';
      i++;
    } else if (s[i] === '|') {
      cells.push(cell.trim());
      cell = '';
    } else {
      cell += s[i];
    }
  }
  return cell.trim() === '' ? cells : null;
}

// The rows of the first table under `## <heading>`; pushes E_LEDGER_TABLE errors and returns null when the table
// cannot be read at all.
function parseTable(lines, heading, errors) {
  const refuse = (message) => errors.push({ code: 'E_LEDGER_TABLE', message: `"## ${heading}": ${message}` });
  const at = lines.flatMap((l, i) => (l.trimEnd() === `## ${heading}` ? [i] : []));
  if (at.length !== 1) {
    refuse(`${at.length} headings, not 1`);
    return null;
  }
  let i = at[0] + 1;
  while (i < lines.length && !lines[i].startsWith('|') && !lines[i].startsWith('## ')) i++;
  const table = [];
  for (; i < lines.length && lines[i].startsWith('|'); i++) table.push({ text: lines[i], line: i + 1 });
  if (table.length < 2) {
    refuse('no table');
    return null;
  }
  const header = splitRow(table[0].text);
  if (!header || header.join('|') !== LEDGER_COLUMNS.join('|')) {
    refuse(`line ${table[0].line}: the header is not | ${LEDGER_COLUMNS.join(' | ')} |`);
    return null;
  }
  const separator = splitRow(table[1].text);
  if (!separator || separator.length !== LEDGER_COLUMNS.length || !separator.every((c) => SEPARATOR_CELL_RE.test(c))) {
    refuse(`line ${table[1].line}: no separator row under the header`);
    return null;
  }
  const rows = [];
  for (const { text, line } of table.slice(2)) {
    const cells = splitRow(text);
    if (!cells || cells.length !== LEDGER_COLUMNS.length) {
      refuse(`line ${line}: ${cells ? cells.length : 'unterminated'} cells, not ${LEDGER_COLUMNS.length}`);
      continue;
    }
    const [id, job, workflow, url, commit, verdict] = cells;
    if (!ID_RE.test(id)) {
      refuse(`line ${line}: id "${id}" is not phase<N>:<row>[:<half>]`);
      continue;
    }
    rows.push({ id, job, workflow, url, commit, verdict, line });
  }
  return rows;
}

// The reason a PENDING-CI row is not closed by a run, for the E_LEDGER_ROW message.
function notClosedReason(r) {
  if (r.verdict !== 'green') return `verdict "${r.verdict}" is not green`;
  if (!RUN_URL_RE.test(r.url)) return `run URL "${r.url}" is not https://github.com/<owner>/<repo>/actions/runs/<n>`;
  return `commit "${r.commit}" is not 7-40 hex`;
}

/**
 * PD-26's ledger rules over the text of readiness.md, given the Phases 1-11 compliance file total.
 * Returns { errors: [{ code, message }], pending: rows, postRelease: rows }; a row is
 * { id, job, workflow, url, commit, verdict, line }.
 */
function checkLedger(text, fileTotal) {
  const errors = [];
  const refuse = (code, message) => errors.push({ code, message });
  const lines = splitLines(text);
  const pending = parseTable(lines, PENDING_HEADING, errors);
  const post = parseTable(lines, POST_HEADING, errors);
  if (errors.length > 0) return { errors, pending: pending || [], postRelease: post || [] };

  const postIds = new Set(post.map((r) => r.id));

  // (a) every Phase 1-11 row once.
  if (pending.length !== fileTotal) {
    const counted = `${pending.length} PENDING-CI rows, but the Phases 1-11 compliance files hold ${fileTotal}`;
    refuse('E_LEDGER_COUNT', counted);
  }
  const seen = new Map();
  for (const r of pending) {
    if (seen.has(r.id)) refuse('E_LEDGER_REPEAT', `line ${r.line}: ${r.id} repeats line ${seen.get(r.id)}`);
    else seen.set(r.id, r.line);
  }

  // (b) closed by a run or deferred; (d) never both.
  for (const r of pending) {
    const closed = r.verdict === 'green' && RUN_URL_RE.test(r.url) && COMMIT_RE.test(r.commit);
    if (closed) {
      if (postIds.has(r.id)) {
        refuse('E_LEDGER_BOTH', `line ${r.line}: ${r.id} is closed by a run and is also a Post-release row`);
      }
      continue;
    }
    if (r.verdict === 'post-release') {
      const why = [];
      if (r.url !== '') why.push(`it has a run URL "${r.url}"`);
      if (!D17_IDS.includes(r.id)) why.push('it is not a D17 id');
      if (!postIds.has(r.id)) why.push('it is not in the Post-release table');
      if (why.length === 0) continue;
      refuse('E_LEDGER_ROW', `line ${r.line}: ${r.id} is post-release, but ${why.join(', ')}`);
      continue;
    }
    refuse('E_LEDGER_ROW', `line ${r.line}: ${r.id} is not closed by a run: ${notClosedReason(r)}`);
  }

  // (c) the Post-release table is exactly the seven D17 rows.
  const postSeen = new Set();
  for (const r of post) {
    if (postSeen.has(r.id)) refuse('E_LEDGER_POST_RELEASE', `line ${r.line}: ${r.id} repeats`);
    postSeen.add(r.id);
    if (!D17_IDS.includes(r.id)) refuse('E_LEDGER_POST_RELEASE', `line ${r.line}: ${r.id} is not a D17 id`);
    if (r.verdict !== 'post-release') {
      refuse('E_LEDGER_POST_RELEASE', `line ${r.line}: ${r.id} has verdict "${r.verdict}", not post-release`);
    }
  }
  for (const id of D17_IDS) {
    if (!postIds.has(id)) refuse('E_LEDGER_POST_RELEASE', `the D17 id ${id} is not in the Post-release table`);
  }
  return { errors, pending, postRelease: post };
}

function parseArgs(argv) {
  const opts = {};
  for (let i = 0; i < argv.length; i++) {
    const name = argv[i];
    if (!OPTIONS.includes(name)) throw new UsageError(`unknown argument: ${name}`);
    if (i + 1 >= argv.length) throw new UsageError(`missing value for ${name}`);
    if (Object.hasOwn(opts, name)) throw new UsageError(`${name} given twice`);
    opts[name] = argv[++i];
  }
  for (const required of ['--specs', '--roadmap']) {
    if (!Object.hasOwn(opts, required)) throw new UsageError(`${required} is required`);
  }
  return opts;
}

function readInput(file, flag) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch (err) {
    throw new UsageError(`${flag} ${file}: unreadable (${err.code || err.message})`);
  }
}

// Map<phase, compliance text> of every krate-manager-phase<N>/compliance.md under `specs`; a phase dir without the
// file (a phase not yet closed) is skipped.
function readCompliance(specs) {
  let names;
  try {
    names = fs.readdirSync(specs);
  } catch (err) {
    throw new UsageError(`--specs ${specs}: unreadable (${err.code || err.message})`);
  }
  const files = new Map();
  for (const name of names) {
    const m = PHASE_DIR_RE.exec(name);
    if (!m) continue;
    const file = path.join(specs, name, 'compliance.md');
    try {
      files.set(Number(m[1]), fs.readFileSync(file, 'utf8'));
    } catch (err) {
      if (err.code === 'ENOENT' || err.code === 'ENOTDIR') continue;
      throw new UsageError(`--specs ${file}: unreadable (${err.code || err.message})`);
    }
  }
  const missing = [];
  for (let n = 1; n <= LAST_LEDGER_PHASE; n++) {
    if (!files.has(n)) missing.push(path.join(specs, `krate-manager-phase${n}`, 'compliance.md'));
  }
  if (missing.length > 0) throw new ReadinessError('E_READINESS_SPECS', `not found: ${missing.join(', ')}`);
  return files;
}

// The count lines and the Phases 1-11 file total.
function countLines(compliance, statusCounts) {
  const lines = [];
  let fileTotal = 0;
  let statusTotal = 0;
  for (const phase of [...compliance.keys()].sort((a, b) => a - b)) {
    const file = countCompliance(compliance.get(phase));
    const status = statusCounts.has(phase) ? statusCounts.get(phase) : null;
    lines.push(`phase${phase} file=${file} status=${status}`);
    if (phase <= LAST_LEDGER_PHASE) {
      fileTotal += file;
      statusTotal = status === null || statusTotal === null ? null : statusTotal + status;
    }
  }
  lines.push(`total file=${fileTotal} status=${statusTotal}`);
  return { lines, fileTotal };
}

function main(argv, io) {
  try {
    let opts;
    let roadmapText;
    let ledgerText;
    let compliance;
    try {
      opts = parseArgs(argv);
      roadmapText = readInput(opts['--roadmap'], '--roadmap');
      if (opts['--check-ledger'] !== undefined) ledgerText = readInput(opts['--check-ledger'], '--check-ledger');
      compliance = readCompliance(opts['--specs']);
    } catch (err) {
      if (!(err instanceof UsageError)) throw err;
      io.stderr.write(`${NAME}: ${err.message}\n${USAGE}\n`);
      return 2;
    }
    const { lines, fileTotal } = countLines(compliance, countStatusLines(roadmapText));
    io.stdout.write(`${lines.join('\n')}\n`);
    if (ledgerText === undefined) return 0;
    const result = checkLedger(ledgerText, fileTotal);
    if (result.errors.length > 0) {
      for (const e of result.errors) io.stderr.write(`${NAME}: ${e.code}: ${e.message}\n`);
      return 1;
    }
    const closed = result.pending.filter((r) => r.verdict === 'green').length;
    io.stdout.write(
      `ledger ok: ${result.pending.length} PENDING-CI rows (${closed} closed by a run, ` +
        `${result.pending.length - closed} deferred), ${result.postRelease.length} post-release rows\n`,
    );
    return 0;
  } catch (err) {
    if (err instanceof ReadinessError) {
      io.stderr.write(`${NAME}: ${err.code}: ${err.message}\n`);
      return 1;
    }
    io.stderr.write(`${NAME}: internal error: ${err && err.stack ? err.stack : err}\n`);
    return 2;
  }
}

module.exports = { main, countCompliance, countStatusLines, checkLedger, D17_IDS };

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);
