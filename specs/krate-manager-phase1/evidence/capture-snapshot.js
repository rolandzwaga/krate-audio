#!/usr/bin/env node
// One-time capture of the gen-feed snapshot fixture (Krate Manager Phase 1, task T008, spec FR-071).
//
// WHY: SC-001 proves that the legacy generator reproduces today's live versions.json byte for byte. That needs the
// real inputs frozen once: the nine plugins' version.json and setup.iss blobs, the origin tag list, the GitHub
// release list and the live feed. This is the phase's only network step. It is an agent action, never a test: tests
// read only the copies this script writes.
//
// Usage (from anywhere; paths resolve against the repo root):
//   node specs/krate-manager-phase1/evidence/capture-snapshot.js
// Behaviour:
//   - refuses to run when the snapshot directory already exists (the data is frozen, never re-captured over);
//   - captures everything into memory, then checks it against the spec's recorded real data (spec.md "Real data read
//     this session": 88 tags, 88 releases with the same names, no prerelease, no draft, nine plugins in the live
//     file with v1 keys only, nine setup.iss each with one AppId line);
//   - writes the directory only when every check passes. On any difference it writes nothing, prints each
//     difference and exits 1 (the spec row must be amended before the snapshot can be made).
// Last line: SNAPSHOT WRITTEN <dir> | SNAPSHOT REFUSED (<reason>)
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const SNAPSHOT_NAME = 'snapshot-2026-10-29';
const OUT = path.join(ROOT, 'tools', 'feed-contract', 'fixtures', 'gen-feed', SNAPSHOT_NAME);
const LIVE_URL = 'https://rolandzwaga.github.io/krate-audio/versions.json';
const REPO = 'rolandzwaga/krate-audio';

// The spec's recorded real data (spec.md, "Real data read this session (2026-10-29)").
const EXPECT = { tags: 88, releases: 88, plugins: 9 };
const V1_KEYS = ['version', 'download_url', 'release_notes'];

// git ls-remote --tags origin -> tags.txt. Copied verbatim into the fixture README.
const tagsFromLsRemote = (text) => [...new Set(text.split(/\r?\n/).filter((l) => /^[0-9a-f]+\trefs\/tags\//.test(l) && !l.endsWith('^{}')).map((l) => l.replace(/^[0-9a-f]+\trefs\/tags\//, '')))].join('\n') + '\n';

function run(cmd, args) {
  return childProcess.execFileSync(cmd, args, { cwd: ROOT, maxBuffer: 64 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe'] });
}

function refuse(reason, details) {
  for (const d of details || []) console.log(`  - ${d}`);
  console.log(`SNAPSHOT REFUSED (${reason})`);
  process.exit(1);
}

if (fs.existsSync(OUT)) refuse(`${path.relative(ROOT, OUT)} already exists; the snapshot is frozen`);

// 1. Plugin blobs from HEAD (LF bytes, identical on every OS; the working tree is CRLF under core.autocrlf=true).
const head = run('git', ['rev-parse', 'HEAD']).toString().trim();
const dirs = run('git', ['ls-tree', '-r', '--name-only', 'HEAD', 'plugins']).toString().split(/\r?\n/)
  .map((l) => /^plugins\/([^/]+)\/version\.json$/.exec(l)).filter(Boolean).map((m) => m[1]);
const blobs = [];
for (const dir of dirs) {
  for (const rel of ['version.json', 'installers/windows/setup.iss']) {
    blobs.push({ rel: `plugins/${dir}/${rel}`, bytes: run('git', ['show', `HEAD:plugins/${dir}/${rel}`]) });
  }
}

// 2. Network captures.
const lsRemote = run('git', ['ls-remote', '--tags', 'origin']).toString();
const tagsText = tagsFromLsRemote(lsRemote);
const releasesBytes = run('gh', ['release', 'list', '--limit', '200', '--json', 'tagName,isPrerelease,isDraft,publishedAt']);
const liveBytes = run('curl', ['-sL', LIVE_URL]);

// 3. Check against the recorded data.
const problems = [];
const tags = tagsText.split('\n').filter(Boolean);
if (tags.length !== EXPECT.tags) problems.push(`tags: ${tags.length}, spec records ${EXPECT.tags}`);
let releases = [];
try { releases = JSON.parse(releasesBytes.toString()); } catch (e) { problems.push(`releases.json does not parse: ${e.message}`); }
const prereleases = releases.filter((r) => r.isPrerelease).map((r) => r.tagName);
const drafts = releases.filter((r) => r.isDraft).map((r) => r.tagName);
if (releases.length !== EXPECT.releases) problems.push(`releases: ${releases.length}, spec records ${EXPECT.releases}`);
if (prereleases.length) problems.push(`prereleases present: ${prereleases.join(', ')}`);
if (drafts.length) problems.push(`drafts present: ${drafts.join(', ')}`);
const tagSet = new Set(tags);
const relSet = new Set(releases.map((r) => r.tagName));
const onlyTags = tags.filter((t) => !relSet.has(t));
const onlyReleases = [...relSet].filter((t) => !tagSet.has(t));
if (onlyTags.length) problems.push(`tags without a release: ${onlyTags.join(', ')}`);
if (onlyReleases.length) problems.push(`releases without a tag: ${onlyReleases.join(', ')}`);
let live = null;
try { live = JSON.parse(liveBytes.toString()); } catch (e) { problems.push(`live versions.json does not parse: ${e.message}`); }
const livePlugins = live && live.plugins && typeof live.plugins === 'object' ? Object.keys(live.plugins) : [];
if (live) {
  if (live.schema_version !== 1) problems.push(`live schema_version: ${JSON.stringify(live.schema_version)}, expected 1`);
  if (livePlugins.length !== EXPECT.plugins) problems.push(`live plugins: ${livePlugins.length}, spec records ${EXPECT.plugins}`);
  for (const name of livePlugins) {
    const extra = Object.keys(live.plugins[name]).filter((k) => !V1_KEYS.includes(k));
    if (extra.length) problems.push(`live plugin ${name} carries non-v1 keys: ${extra.join(', ')}`);
  }
}
if (dirs.length !== EXPECT.plugins) problems.push(`plugin directories with version.json at HEAD: ${dirs.length}, expected ${EXPECT.plugins}`);
const names = dirs.map((dir) => JSON.parse(blobs.find((b) => b.rel === `plugins/${dir}/version.json`).bytes.toString()).name);
const missingLive = names.filter((n) => !livePlugins.includes(n));
if (missingLive.length) problems.push(`version.json names missing from the live file: ${missingLive.join(', ')}`);
for (const b of blobs.filter((x) => x.rel.endsWith('setup.iss'))) {
  const n = (b.bytes.toString().match(/^AppId=\{\{/gm) || []).length;
  if (n !== 1) problems.push(`${b.rel}: ${n} AppId={{ lines, expected 1`);
}
if (problems.length) refuse('capture differs from the spec recorded data; amend the spec row first', problems);

// 4. Write the snapshot.
const write = (rel, bytes) => {
  const p = path.join(OUT, ...rel.split('/'));
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, bytes);
};
for (const b of blobs) write(b.rel, b.bytes);
write('tags.txt', Buffer.from(tagsText, 'utf8'));
write('releases.json', releasesBytes);
write('live-versions.json', liveBytes);
write('case.json', Buffer.from(JSON.stringify({ group: 'clean', modes: ['v2', 'legacy'], repo: REPO, exit: 0 }) + '\n', 'utf8'));

const readme = [
  `# Snapshot fixture \`${SNAPSHOT_NAME}\``,
  '',
  'Frozen real inputs for the gen-feed tests (spec `specs/krate-manager-phase1/spec.md` FR-071, SC-001, SC-002, SC-014,',
  'SC-025). Tests read only these copies and never the network. Never edit a file here by hand and never re-capture over',
  'it: the directory name and SC-001 pin this data.',
  '',
  `Captured ${new Date().toISOString()} by \`specs/krate-manager-phase1/evidence/capture-snapshot.js\` (task T008), with the`,
  `plugin blobs taken from commit \`${head}\`. The data matches the spec's "Real data read this session (2026-10-29)".`,
  '',
  '## Commands',
  '',
  '| File | Command |',
  '|---|---|',
  '| `plugins/<dir>/version.json` | `git show HEAD:plugins/<dir>/version.json` (stored blob bytes, LF) |',
  '| `plugins/<dir>/installers/windows/setup.iss` | `git show HEAD:plugins/<dir>/installers/windows/setup.iss` (stored blob bytes) |',
  '| `tags.txt` | `git ls-remote --tags origin`, transformed by the Node code below |',
  '| `releases.json` | `gh release list --limit 200 --json tagName,isPrerelease,isDraft,publishedAt` (stdout as emitted) |',
  `| \`live-versions.json\` | \`curl -sL ${LIVE_URL}\` (raw bytes) |`,
  '| `case.json` | written by the script: the gen-feed case shape (plan §4.7), group `clean` |',
  '',
  'The Node code that turned the `git ls-remote --tags origin` stdout into `tags.txt` (strip each line\'s',
  '`<sha>\\trefs/tags/` prefix, drop every `^{}` peel line, de-duplicate, keep input order, one bare tag per line, LF, final LF):',
  '',
  '```js',
  `const tagsFromLsRemote = ${tagsFromLsRemote.toString()};`,
  '```',
  '',
  '## Counts',
  '',
  '| Item | Count |',
  '|---|---|',
  `| tags (\`tags.txt\`) | ${tags.length} |`,
  `| releases (\`releases.json\`, same tag names as \`tags.txt\`) | ${releases.length} |`,
  `| prereleases | ${prereleases.length} |`,
  `| drafts | ${drafts.length} |`,
  `| plugins in \`live-versions.json\` (schema_version 1, keys \`${V1_KEYS.join('`, `')}\` only) | ${livePlugins.length} |`,
  `| plugin directories copied (\`plugins/*/version.json\` at HEAD; \`plugins/shared\` has none) | ${dirs.length} |`,
  '',
  `Plugin directories: ${dirs.map((d) => `\`${d}\``).join(', ')}.`,
  '',
].join('\n');
write('README.md', Buffer.from(readme, 'utf8'));

console.log(`tags ${tags.length}, releases ${releases.length}, prereleases ${prereleases.length}, drafts ${drafts.length}, live plugins ${livePlugins.length}, plugin dirs ${dirs.length}`);
console.log(`SNAPSHOT WRITTEN ${path.relative(ROOT, OUT).replace(/\\/g, '/')}`);
