#!/usr/bin/env node
//
// manager-release-preflight.js
// =============================================================================
// The first job of release-manager.yml: refuses a Krate Manager release before anything is built, and writes the
// release notes and the release body.
//
// WHY. A manager release must carry one version everywhere, the pinned updater config, a dated CHANGELOG entry, a tag
// that does not exist yet, the repository the app trusts for its own packages, and a release key that is not a test
// key (specs/krate-manager-phase11/spec.md FR-051, D15; plan §4.9; PD-27, SI-40; tasks TA-12). Each refusal has its
// own code so a failed dispatch says why without a log dive. The notes are the newest CHANGELOG section only (FR-044);
// the release body is that section plus a fixed Install block (FR-063).
//
// Usage: node tools/manager-release-preflight.js --app-dir <dir> --dry-run <true|false> --tags <file>
//          --repo <owner/repo> [--notes <out>] [--body <out>] [--github-output <file>] [--pubkey <file>]
//   --tags     two sections: "# tags" (one refs/tags/<name> per line, git ls-remote --tags origin without the sha and
//              the ^{} lines) and "# releases" (one release tag name per line, gh release list --limit 1000)
//   --pubkey   the release public key (default tools/feed-contract/keys/krate-signing.pub)
// Checks, in order, each exit 1 with "manager-release-preflight: <CODE>: <message>" on stderr: E_PREFLIGHT_VERSION,
// E_PREFLIGHT_UPDATER_CONFIG, E_PREFLIGHT_CHANGELOG, E_PREFLIGHT_CHANGELOG_UNDATED (real run; a dry run warns),
// E_PREFLIGHT_TAG_LIST, E_PREFLIGHT_REPO, E_PREFLIGHT_TAG_EXISTS, E_SIGNING_CONFIG (real run only). Nothing is written
// unless every check passes. Exit 2 = usage or internal error. Offline: no network, no child process.
// =============================================================================
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const util = require('node:util');

const { lint } = require('./lint-manager-version.js');
const { refuseTestKey } = require('./gen-release-manifest.js');

const NAME = 'manager-release-preflight';
const USAGE = [
  'usage: node tools/manager-release-preflight.js --app-dir <dir> --dry-run <true|false> --tags <file>',
  '         --repo <owner/repo> [--notes <out>] [--body <out>] [--github-output <file>] [--pubkey <file>]',
].join('\n');
const DEFAULT_PUBKEY = path.join(__dirname, 'feed-contract', 'keys', 'krate-signing.pub');
// D2: the compiled updater endpoint (tools/feed-publish.js MANAGER_LATEST_URL; the T013 guard pins the two equal).
const UPDATER_ENDPOINT = 'https://krateaudio.com/manager/latest.json';
// release.yml's gh release list --limit: a list this long may be truncated (FR-051).
const RELEASE_LIST_LIMIT = 1000;
const REPO_RE = /^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/;
const MANAGER_REPO_RE = /^pub const MANAGER_REPO: &str = "([^"]+)";\r?$/gm;
const DATE_RE = /^(\d{4})-(\d{2})-(\d{2})$/;
const OPTIONS = ['--app-dir', '--dry-run', '--tags', '--repo', '--notes', '--body', '--github-output', '--pubkey'];

class PreflightError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}
const refuse = (code, message) => new PreflightError(code, message);

class UsageError extends Error {}

/** The repository of the one `pub const MANAGER_REPO: &str = "…";` line; throws when there are zero or several. */
function managerRepo(text) {
  const found = [...text.matchAll(MANAGER_REPO_RE)].map((m) => m[1]);
  if (found.length !== 1) throw new Error(`${found.length} MANAGER_REPO lines, not 1`);
  return found[0];
}

// The indexes of the `## [<version>]` heading lines.
function headingIndexes(lines, version) {
  const head = `## [${version}]`;
  return lines.flatMap((l, i) => (l === head || l.startsWith(`${head} `) ? [i] : []));
}

/**
 * FR-044: the lines after the one `## [<version>]` heading up to the next `## [` heading (exclusive), without leading
 * and trailing blank lines, joined with LF. Throws E_PREFLIGHT_CHANGELOG when the heading is missing or not unique.
 */
function changelogSection(text, version) {
  const lines = text.split(/\r?\n/);
  const at = headingIndexes(lines, version);
  if (at.length !== 1) throw refuse('E_PREFLIGHT_CHANGELOG', `${at.length} "## [${version}]" headings, not 1`);
  let end = lines.findIndex((l, i) => i > at[0] && l.startsWith('## ['));
  if (end < 0) end = lines.length;
  const section = lines.slice(at[0] + 1, end);
  while (section.length > 0 && section[0].trim() === '') section.shift();
  while (section.length > 0 && section[section.length - 1].trim() === '') section.pop();
  return section.join('\n');
}

/** FR-063: the CHANGELOG section, then the fixed Install block naming the D6 assets and the macOS one-liner. */
function releaseBody({ version, repo, notes }) {
  const a = (suffix) => `Krate-Manager-${version}-${suffix}`;
  const pkg = a('macOS.pkg');
  const url = `https://github.com/${repo}/releases/download/manager/v${version}/${pkg}`;
  const install = [
    `- **Windows (x64):** download \`${a('Windows-x64-setup.exe')}\` and run it. It installs for the current user.`,
    `- **macOS 15 or later (Apple silicon and Intel):** install \`${pkg}\` from Terminal`,
    '  (provisional: see the Krate Manager first-launch notes when they are published):',
    '',
    '  ```sh',
    `  curl -fLO ${url} && sudo installer -pkg ${pkg} -target /`,
    '  ```',
    '',
    '  It installs `Krate Manager.app` into `/Applications`.',
    `- **Linux (x86_64):** download \`${a('Linux-x86_64.AppImage')}\`, make it executable (\`chmod +x\`) and run it,`,
    `  or install \`${a('Linux-amd64.deb')}\` with \`sudo apt install ./${a('Linux-amd64.deb')}\`.`,
    '',
    `\`${a('macOS-universal.app.tar.gz')}\`, the \`.sig\` files and \`latest.json\` are what an installed Krate ` +
      'Manager',
    'downloads to update itself; installing does not need them.',
    '',
  ];
  return `${notes}\n\n## Install\n\n${install.join('\n')}`;
}

// The tags file -> { tags: Set<name>, releases: string[] }; throws E_PREFLIGHT_TAG_LIST on any other shape.
function parseTags(text) {
  const tags = new Set();
  const releases = [];
  let section = null;
  const seen = new Set();
  text.split(/\r?\n/).forEach((line, i) => {
    const bad = (why) => refuse('E_PREFLIGHT_TAG_LIST', `tags file line ${i + 1}: ${why}`);
    if (line.trim() === '') return;
    if (line === '# tags' || line === '# releases') {
      if (seen.has(line)) throw bad(`"${line}" appears twice`);
      if (line === '# releases' && section !== 'tags') throw bad('"# releases" before "# tags"');
      seen.add(line);
      section = line.slice(2);
      return;
    }
    if (section === null) throw bad('a line before "# tags"');
    if (section === 'tags') {
      const m = /^refs\/tags\/(\S+)$/.exec(line);
      if (!m) throw bad('not refs/tags/<name>');
      tags.add(m[1]);
    } else {
      if (!/^\S+$/.test(line)) throw bad('not one release tag name');
      releases.push(line);
    }
  });
  if (!seen.has('# releases')) throw refuse('E_PREFLIGHT_TAG_LIST', 'tags file has no "# releases" section');
  return { tags, releases };
}

function readAppFile(appDir, rel, code) {
  try {
    return fs.readFileSync(path.join(appDir, ...rel.split('/')), 'utf8');
  } catch (err) {
    throw refuse(code, `${rel}: unreadable (${err.code || err.message})`);
  }
}

function isRealDate(s) {
  const m = DATE_RE.exec(s);
  if (!m) return false;
  const d = new Date(Date.UTC(Number(m[1]), Number(m[2]) - 1, Number(m[3])));
  return d.toISOString().slice(0, 10) === s;
}

/**
 * Runs every check in order (throws a PreflightError naming the first refusal) and returns
 * { version, tag, notes, body, warnings }. opts: { appDir, dryRun, tagsText, repo, pubkeyText }.
 */
function preflight({ appDir, dryRun, tagsText, repo, pubkeyText }) {
  const warnings = [];

  // E_PREFLIGHT_VERSION: one version everywhere (lint-manager-version.js).
  const linted = lint(appDir);
  if (linted.errors.length > 0) {
    const all = linted.errors.map((e) => `${e.code}: ${e.file}: ${e.message}`).join('; ');
    throw refuse('E_PREFLIGHT_VERSION', all);
  }
  const version = linted.version;
  const tag = `manager/v${version}`;

  // E_PREFLIGHT_UPDATER_CONFIG: plugins.updater is D2's object, pubkey the base64 of the release public key's bytes.
  {
    const conf = JSON.parse(readAppFile(appDir, 'src-tauri/tauri.conf.json', 'E_PREFLIGHT_UPDATER_CONFIG'));
    const expected = {
      pubkey: Buffer.from(pubkeyText, 'utf8').toString('base64'),
      endpoints: [UPDATER_ENDPOINT],
      requireSignedVersion: true,
      allowDowngrades: false,
      windows: { installMode: 'passive' },
    };
    const actual = conf.plugins?.updater;
    if (actual === null || typeof actual !== 'object' || Array.isArray(actual)) {
      throw refuse('E_PREFLIGHT_UPDATER_CONFIG', 'src-tauri/tauri.conf.json has no plugins.updater object');
    }
    const keys = [...new Set([...Object.keys(expected), ...Object.keys(actual)])];
    const differ = keys.filter((k) => !util.isDeepStrictEqual(actual[k], expected[k]));
    if (differ.length > 0) {
      const where = differ.map((k) => `plugins.updater.${k}`).join(', ');
      throw refuse('E_PREFLIGHT_UPDATER_CONFIG', `src-tauri/tauri.conf.json differs from D2's config at ${where}`);
    }
  }

  // E_PREFLIGHT_CHANGELOG, E_PREFLIGHT_CHANGELOG_UNDATED (D15).
  const changelog = readAppFile(appDir, 'CHANGELOG.md', 'E_PREFLIGHT_CHANGELOG');
  const notes = changelogSection(changelog, version);
  {
    const lines = changelog.split(/\r?\n/);
    const heading = lines[headingIndexes(lines, version)[0]];
    const rest = heading.slice(`## [${version}]`.length);
    if (rest === ' - Unreleased') {
      const why = `CHANGELOG.md: "## [${version}] - Unreleased"; a release needs "## [${version}] - YYYY-MM-DD"`;
      if (!dryRun) throw refuse('E_PREFLIGHT_CHANGELOG_UNDATED', why);
      warnings.push(`${why} (dry run continues)`);
    } else if (!(rest.startsWith(' - ') && isRealDate(rest.slice(3)))) {
      throw refuse('E_PREFLIGHT_CHANGELOG', `CHANGELOG.md: "${heading}" is neither dated (YYYY-MM-DD) nor Unreleased`);
    }
  }

  // E_PREFLIGHT_TAG_LIST: a full release list may be truncated.
  const { tags, releases } = parseTags(tagsText);
  if (releases.length >= RELEASE_LIST_LIMIT) {
    throw refuse(
      'E_PREFLIGHT_TAG_LIST',
      `the release list has ${releases.length} lines (limit ${RELEASE_LIST_LIMIT}), so it may be truncated`,
    );
  }

  // E_PREFLIGHT_REPO (PD-27, SI-40): the dispatching repository is the one the app trusts for its packages.
  {
    const rel = 'crates/core/src/self_update.rs';
    const text = readAppFile(appDir, rel, 'E_PREFLIGHT_REPO');
    let pinned;
    try {
      pinned = managerRepo(text);
    } catch (err) {
      throw refuse('E_PREFLIGHT_REPO', `${rel}: ${err.message}`);
    }
    if (pinned !== repo) throw refuse('E_PREFLIGHT_REPO', `--repo ${repo} != MANAGER_REPO ${pinned} (${rel})`);
  }

  // E_PREFLIGHT_TAG_EXISTS, on both kinds of run (FR-051).
  if (tags.has(tag)) throw refuse('E_PREFLIGHT_TAG_EXISTS', `tag ${tag} exists on the remote`);
  if (releases.includes(tag)) throw refuse('E_PREFLIGHT_TAG_EXISTS', `release ${tag} exists`);

  // E_SIGNING_CONFIG, real run only: never a test key as the release key (gen-release-manifest.js, PD-23).
  if (!dryRun) {
    try {
      refuseTestKey(pubkeyText);
    } catch (err) {
      if (typeof err.code !== 'string') throw err;
      throw refuse('E_SIGNING_CONFIG', err.message);
    }
  }

  return { version, tag, notes, body: releaseBody({ version, repo, notes }), warnings };
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
  for (const required of ['--app-dir', '--dry-run', '--tags', '--repo']) {
    if (!Object.hasOwn(opts, required)) throw new UsageError(`${required} is required`);
  }
  if (opts['--dry-run'] !== 'true' && opts['--dry-run'] !== 'false') {
    throw new UsageError(`--dry-run must be true or false, not ${opts['--dry-run']}`);
  }
  if (!REPO_RE.test(opts['--repo'])) throw new UsageError(`--repo must be <owner>/<repo>, not ${opts['--repo']}`);
  return opts;
}

function readInput(file, flag) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch (err) {
    throw new UsageError(`${flag} ${file}: unreadable (${err.code || err.message})`);
  }
}

function writeAtomic(file, text) {
  const tmp = `${file}.tmp-${process.pid}`;
  fs.writeFileSync(tmp, text);
  fs.renameSync(tmp, file);
}

function main(argv, io) {
  let opts;
  let pubkeyText;
  let tagsText;
  try {
    opts = parseArgs(argv);
    tagsText = readInput(opts['--tags'], '--tags');
    pubkeyText = readInput(opts['--pubkey'] ?? DEFAULT_PUBKEY, '--pubkey');
  } catch (err) {
    if (!(err instanceof UsageError)) throw err;
    io.stderr.write(`${NAME}: ${err.message}\n${USAGE}\n`);
    return 2;
  }
  const dryRun = opts['--dry-run'] === 'true';
  try {
    const result = preflight({
      appDir: path.resolve(opts['--app-dir']),
      dryRun,
      tagsText,
      repo: opts['--repo'],
      pubkeyText,
    });
    for (const w of result.warnings) io.stdout.write(`::warning title=preflight::${w}\n`);
    if (opts['--notes'] !== undefined) writeAtomic(opts['--notes'], result.notes);
    if (opts['--body'] !== undefined) writeAtomic(opts['--body'], result.body);
    if (opts['--github-output'] !== undefined) {
      fs.appendFileSync(opts['--github-output'], `version=${result.version}\ntag=${result.tag}\n`);
    }
    io.stdout.write(
      `::notice title=preflight::${result.tag}: --repo ${opts['--repo']} equals MANAGER_REPO; ` +
        `${dryRun ? 'dry run' : 'real run'} passes every check\n`,
    );
    return 0;
  } catch (err) {
    if (err instanceof PreflightError) {
      io.stderr.write(`${NAME}: ${err.code}: ${err.message}\n`);
      return 1;
    }
    io.stderr.write(`${NAME}: internal error: ${err && err.stack ? err.stack : err}\n`);
    return 2;
  }
}

module.exports = { main, preflight, releaseBody, changelogSection, managerRepo };

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);
