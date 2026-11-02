'use strict';
require('./helpers/write-guard.js');

// Krate Manager first-launch statements (Phase 12 spec D12, FR-080, FR-082, FR-084, SC-016a, SC-016b; plan §4.10,
// PD-17, PD-18, PD-28; tasks T010).
//
// WHY. Every first-launch statement is written once, in tools/feed-contract/first-launch-statements.json, and must
// appear word for word in apps/krate-manager/README.md and in docs/manager-card-template.html (the landing card), so a
// README edit can never drift from the card. The macOS statements ("post-u1") stay null until the user has recorded
// both FR-073 rows of specs/krate-manager-phase12/user-checks.md, so no macOS route is written from an assumption
// (FR-076). This file never skips (feed-tools.yml fails a top-level "# SKIP"): each branch of SC-016b is an assertion.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const CONTRACT_FILE = path.join(REPO_ROOT, 'tools', 'feed-contract', 'first-launch-statements.json');
const README_FILE = path.join(REPO_ROOT, 'apps', 'krate-manager', 'README.md');
const CARD_FILE = path.join(REPO_ROOT, 'docs', 'manager-card-template.html');
const USER_CHECKS_FILE = path.join(REPO_ROOT, 'specs', 'krate-manager-phase12', 'user-checks.md');
const FIXTURE_DIR = path.join(REPO_ROOT, 'tools', 'feed-contract', 'fixtures', 'first-launch');

// FR-084: the ids, in this order; the macos.* ids are post-u1, every other id pre-u1.
const IDS = [
  'windows.smartscreen',
  'windows.smart-app-control',
  'windows.localappdata',
  'macos.sequoia',
  'macos.tahoe',
  'macos.one-liner',
  'linux.formats',
  'linux.folders',
  'linux.reaper',
  'linux.graphics',
  'linux.blank-window',
  'all.rescan',
  'floor',
];
const stageOf = (id) => (id.startsWith('macos.') ? 'post-u1' : 'pre-u1');

// FR-080 as plain text (plan §4.10).
const FLOOR =
  'Windows 10 22H2 and Windows 11 x64; macOS 15 and later (Apple silicon and Intel); Linux x86_64 at the glibc of the ' +
  'ubuntu:22.04 build.';

// The README section each pre-u1 statement sits in (plan §4.10: ## First launch -> ### per OS).
const README_SUBSECTION = { windows: '### Windows', linux: '### Linux', all: '### Every system' };
const MACOS_PLACEHOLDER = 'Written from the recorded checks; see the release notes until then.';

// FR-081: the four per-tag installer URLs (names per gen-manager-latest.js ASSET_TABLE) and the README link.
const RELEASE_BASE = 'https://github.com/{{REPO}}/releases/download/manager/v{{MANAGER_VERSION}}/';
const EXPECTED_HREFS = [
  `${RELEASE_BASE}Krate-Manager-{{MANAGER_VERSION}}-Windows-x64-setup.exe`,
  `${RELEASE_BASE}Krate-Manager-{{MANAGER_VERSION}}-macOS.pkg`,
  `${RELEASE_BASE}Krate-Manager-{{MANAGER_VERSION}}-Linux-x86_64.AppImage`,
  `${RELEASE_BASE}Krate-Manager-{{MANAGER_VERSION}}-Linux-amd64.deb`,
  'https://github.com/{{REPO}}/blob/main/apps/krate-manager/README.md#first-launch',
];
const FORBIDDEN_HREF_PARTS = ['.app.tar.gz', '.sig', 'latest.json', '/releases/latest/'];

// The user-checks.md ids (plan §4.10, tasks T010).
const USER_CHECK_IDS = [
  'FR-073-sequoia',
  'FR-073-tahoe',
  'FR-074-a',
  'FR-074-b',
  'FR-074-c',
  'FR-074-d',
  'FR-074-e',
  'FR-074-f',
  'FR-075',
  'R-POST-3',
];
const USER_CHECKS_NOTE = 'Only the user fills a row; agents never perform or fill these checks (FR-070).';

const read = (f) => fs.readFileSync(f, 'utf8');
const collapse = (s) => s.replace(/\s+/g, ' ').trim();

// SC-016a: README text = code-span backticks removed, whitespace runs collapsed.
function readmeText(markdown) {
  return collapse(markdown.replace(/`/g, ''));
}

// SC-016a: card text = tags removed, then the five entities decoded (one pass, so "&amp;lt;" stays "&lt;"), then
// whitespace collapsed.
const ENTITIES = { '&amp;': '&', '&lt;': '<', '&gt;': '>', '&quot;': '"', '&#39;': "'" };
function cardText(html) {
  return collapse(html.replace(/<[^>]*>/g, '').replace(/&(amp|lt|gt|quot|#39);/g, (m) => ENTITIES[m]));
}

// Splits Markdown into sections headed by lines starting with `prefix` (heading line -> body text), ignoring
// headings inside ``` fences. A heading of the same level ends a section; a higher-level heading ends it too.
function sections(markdown, prefix) {
  const level = prefix.trim().length;
  const out = new Map();
  let current = null;
  let inFence = false;
  for (const line of markdown.split(/\r?\n/)) {
    if (line.startsWith('```')) inFence = !inFence;
    const heading = !inFence && /^#{1,6} /.exec(line);
    if (heading) {
      const lineLevel = line.indexOf(' ');
      if (lineLevel === level) {
        assert.equal(out.has(line), false, `heading appears twice: ${line}`);
        current = [];
        out.set(line, current);
        continue;
      }
      if (lineLevel < level) current = null;
    }
    if (current) current.push(line);
  }
  return new Map([...out].map(([k, v]) => [k, v.join('\n')]));
}

// Cells of a Markdown table row ('| a | b |' -> ['a', 'b']); an escaped pipe ('\|') stays inside its cell.
function tableCells(line) {
  const body = line.trim().replace(/^\|/, '').replace(/(?<!\\)\|$/, '');
  return body.split(/(?<!\\)\|/).map((c) => c.trim().replace(/\\\|/g, '|'));
}

// The SC-016b reader (plan §4.10 row format): every table whose header's first cell is `id` and last cell is
// `result`; each data row's id -> recorded (its `result` cell, trimmed, non-empty). A duplicate id throws.
function readUserChecks(markdown) {
  const rows = new Map();
  let header = null;
  for (const line of markdown.split(/\r?\n/)) {
    if (!line.trim().startsWith('|')) {
      header = null;
      continue;
    }
    const cells = tableCells(line);
    if (!header) {
      header = cells;
      continue;
    }
    if (cells.every((c) => /^:?-{3,}:?$/.test(c))) continue;
    if (header[0] !== 'id' || header[header.length - 1] !== 'result') continue;
    assert.equal(cells.length, header.length, `row has ${cells.length} cells, header ${header.length}: ${line}`);
    const id = cells[0];
    if (rows.has(id)) throw new Error(`user-checks: duplicate id row ${id}`);
    rows.set(id, cells[cells.length - 1] !== '');
  }
  return rows;
}

// How many of the two FR-073 rows are recorded. A missing FR-073 id row throws: the file is malformed, never
// "not recorded".
function fr073Recorded(rows) {
  let n = 0;
  for (const id of ['FR-073-sequoia', 'FR-073-tahoe']) {
    if (!rows.has(id)) throw new Error(`user-checks: no ${id} row`);
    if (rows.get(id)) n += 1;
  }
  return n;
}

// SC-016b's rule: fewer than two FR-073 rows recorded -> every post-u1 text null; both recorded -> every post-u1
// text a non-empty string in the README and in the card. Returns the problems (empty when the rule holds).
function postU1Problems(statements, recorded, readme, card) {
  const problems = [];
  for (const s of statements.filter((x) => x.stage === 'post-u1')) {
    if (recorded < 2) {
      if (s.text !== null) problems.push(`${s.id}: text must be null until both FR-073 rows are recorded`);
      continue;
    }
    if (typeof s.text !== 'string' || s.text.trim() === '') {
      problems.push(`${s.id}: both FR-073 rows are recorded, so the text must be a non-empty string`);
      continue;
    }
    if (!readmeText(readme).includes(s.text)) problems.push(`${s.id}: not in the README`);
    if (!cardText(card).includes(s.text)) problems.push(`${s.id}: not in the card`);
  }
  return problems;
}

function loadContract() {
  return JSON.parse(read(CONTRACT_FILE));
}

test('first-launch statements are in the README and the card', async (t) => {
  const contract = loadContract();
  const statements = contract.statements;
  const byId = new Map(statements.map((s) => [s.id, s]));
  const readme = read(README_FILE);
  const card = read(CARD_FILE);
  const preU1 = statements.filter((s) => s.stage === 'pre-u1');

  await t.test('the contract holds exactly the FR-084 ids, in order, with their stages', () => {
    assert.deepEqual(Object.keys(contract), ['statements']);
    assert.deepEqual(
      statements.map((s) => s.id),
      IDS,
    );
    for (const s of statements) {
      assert.deepEqual(Object.keys(s), ['id', 'stage', 'text'], `${s.id}: keys`);
      assert.equal(s.stage, stageOf(s.id), `${s.id}: stage`);
    }
  });

  await t.test('every pre-u1 text is plain: non-empty, no {{, <, backtick or URL (PD-18)', () => {
    for (const s of preU1) {
      assert.equal(typeof s.text, 'string', `${s.id}: text is not a string`);
      assert.notEqual(s.text.trim(), '', `${s.id}: empty text`);
      assert.equal(s.text, s.text.trim(), `${s.id}: leading or trailing whitespace`);
      assert.equal(/\s{2,}|[\r\n\t]/.test(s.text), false, `${s.id}: a whitespace run would never match collapsed text`);
      assert.equal(s.text.includes('{{'), false, `${s.id}: holds {{`);
      assert.equal(s.text.includes('<'), false, `${s.id}: holds <`);
      assert.equal(s.text.includes('`'), false, `${s.id}: holds a backtick`);
      assert.equal(/[a-z][a-z0-9+.-]*:\/\/|www\./i.test(s.text), false, `${s.id}: holds a URL`);
    }
  });

  await t.test('floor is the FR-080 sentence', () => {
    assert.equal(byId.get('floor').text, FLOOR);
  });

  await t.test('every pre-u1 text is in the README and the card, verbatim (SC-016a)', () => {
    const r = readmeText(readme);
    const c = cardText(card);
    const missing = [];
    for (const s of preU1) {
      if (!r.includes(s.text)) missing.push(`README: ${s.id}`);
      if (!c.includes(s.text)) missing.push(`card: ${s.id}`);
    }
    assert.deepEqual(missing, []);
  });

  await t.test('the README has First launch per OS and Supported systems is the floor alone', () => {
    const top = sections(readme, '## ');
    assert.ok(top.has('## First launch'), 'no "## First launch" section (the card links #first-launch)');
    assert.ok(top.has('## Supported systems'), 'no "## Supported systems" section');
    assert.equal(readmeText(top.get('## Supported systems')), FLOOR);
    const sub = sections(top.get('## First launch'), '### ');
    assert.deepEqual([...sub.keys()], ['### Windows', '### macOS', '### Linux', '### Every system']);
    for (const s of preU1.filter((x) => x.id !== 'floor')) {
      const where = README_SUBSECTION[s.id.split('.')[0]];
      assert.ok(readmeText(sub.get(where)).includes(s.text), `${s.id}: not under ${where}`);
    }
    if (statements.filter((s) => s.stage === 'post-u1').every((s) => s.text === null)) {
      assert.equal(readmeText(sub.get('### macOS')), MACOS_PLACEHOLDER, '### macOS must hold only the placeholder');
    }
  });

  await t.test('the card links exactly the four installers and the README section', () => {
    const hrefs = [...card.matchAll(/\bhref\s*=\s*(["'])(.*?)\1/g)].map((m) => m[2]);
    assert.deepEqual([...hrefs].sort(), [...EXPECTED_HREFS].sort());
    for (const h of hrefs) {
      for (const bad of FORBIDDEN_HREF_PARTS) assert.equal(h.includes(bad), false, `${h} holds ${bad}`);
    }
  });

  await t.test('the card has no script, no inline style, 16-space indentation and the floor paragraph', () => {
    assert.equal(/<script/i.test(card), false, 'card holds <script');
    assert.equal(/style\s*=/i.test(card), false, 'card holds style=');
    const lines = card.replace(/\r\n?/g, '\n').replace(/\n+$/, '').split('\n');
    assert.equal(lines[0], '                <div class="plugin-card manager-card">');
    assert.equal(lines[lines.length - 1], '                </div>');
    for (const l of lines) assert.ok(l.startsWith('                '), `line not indented 16 spaces: ${l}`);
    const floors = [...card.matchAll(/<p class="supported-systems">([\s\S]*?)<\/p>/g)].map((m) => cardText(m[1]));
    assert.deepEqual(floors, [FLOOR]);
  });

  await t.test('the SC-016b reader over the fixtures', () => {
    const fixture = (name) => readUserChecks(read(path.join(FIXTURE_DIR, `user-checks-${name}.md`)));
    const nullMac = statements.map((s) => (s.stage === 'post-u1' ? { ...s, text: null } : s));
    const macText = (id) => `macOS fixture statement for ${id}: open the pkg & follow "Installer".`;
    const filledMac = statements.map((s) => (s.stage === 'post-u1' ? { ...s, text: macText(s.id) } : s));
    const docReadme = `## First launch\n\n${IDS.filter((i) => i.startsWith('macos.')).map((i) => `- ${macText(i)}`).join('\n')}\n`;
    const esc = (s) => s.replace(/&/g, '&amp;').replace(/"/g, '&quot;');
    const docCard = IDS.filter((i) => i.startsWith('macos.'))
      .map((i) => `<p>${esc(macText(i))}</p>`)
      .join('\n');

    // none / one recorded: the rule is "every post-u1 text null".
    for (const name of ['none', 'one']) {
      const n = fr073Recorded(fixture(name));
      assert.equal(n, name === 'none' ? 0 : 1, `${name}: recorded count`);
      assert.deepEqual(postU1Problems(nullMac, n, docReadme, docCard), [], `${name}: null texts pass`);
      assert.equal(postU1Problems(filledMac, n, docReadme, docCard).length, 3, `${name}: a written text fails`);
    }

    // both recorded: every post-u1 text non-empty and in both documents.
    const both = fr073Recorded(fixture('both'));
    assert.equal(both, 2);
    assert.deepEqual(postU1Problems(filledMac, both, docReadme, docCard), []);
    assert.equal(postU1Problems(nullMac, both, docReadme, docCard).length, 3, 'both: a null text fails');
    const cardMissingOne = docCard.split('\n').slice(1).join('\n');
    assert.deepEqual(postU1Problems(filledMac, both, docReadme, cardMissingOne), ['macos.sequoia: not in the card']);
    const readmeMissingOne = docReadme.replace(`- ${macText('macos.tahoe')}\n`, '');
    assert.deepEqual(postU1Problems(filledMac, both, readmeMissingOne, docCard), ['macos.tahoe: not in the README']);

    // a missing FR-073 id row: the case fails, never "not recorded".
    assert.throws(() => fr073Recorded(fixture('missing-id')), /no FR-073-tahoe row/);

    // a duplicate id row: malformed.
    const dup = read(path.join(FIXTURE_DIR, 'user-checks-none.md')).replace(
      '| FR-073-tahoe |',
      '| FR-073-sequoia |',
    );
    assert.throws(() => readUserChecks(dup), /duplicate id row FR-073-sequoia/);
  });

  await t.test('the real user-checks.md: every id row once, and the post-u1 rule (SC-016b)', () => {
    const text = read(USER_CHECKS_FILE);
    assert.ok(text.includes(USER_CHECKS_NOTE), 'the FR-070 header note is missing');
    const rows = readUserChecks(text);
    assert.deepEqual([...rows.keys()], USER_CHECK_IDS);
    const recorded = fr073Recorded(rows);
    assert.deepEqual(postU1Problems(statements, recorded, readme, card), []);
  });
});
