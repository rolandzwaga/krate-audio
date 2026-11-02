#!/usr/bin/env node
// Generates the Krate update feed (versions.json) from the plugins' version.json files and the release tags.
//
// WHY: the feed was built by inline Node in .github/workflows/release.yml (the "Generate Update Checker Endpoint"
// step), which nothing could test. This tool reproduces that step exactly (--legacy), and builds the schema_version 2
// feed the manager reads (spec specs/krate-manager-phase1/spec.md FR-040 to FR-049; plan §4.5).
// It is pure and offline: it reads the files it is given and writes --out; it runs no git and fetches nothing.
// It names no plugin: every name comes from a version.json.
//
// Usage:
//   node tools/gen-feed.js --plugins-dir <dir> --releases <file> --manifests <dir> --repo <owner/name>
//                          [--manager-latest-url <url>] --out <file>
//   node tools/gen-feed.js --legacy --plugins-dir <dir> --tags <file> --out <file>
//   node tools/gen-feed.js --project-v1 <feed>
//   --legacy      today's schema_version 1 feed, byte for byte: per plugin the first `<name lowercased>/v*` tag in
//                 git's -v:refname order; --tags holds one bare tag name per line (what `git tag -l` prints)
//   --project-v1  prints the v1 view of a feed (schema_version 1, version / download_url / release_notes) on stdout
//   (v2 mode)     per plugin the stable entry is the highest non-prerelease release (the Q9 rule), its prerelease
//                 sibling the highest prerelease above it; platforms / install come from the release.json in
//                 --manifests whose tag is that release's; --releases is `gh release list --json
//                 tagName,isPrerelease,isDraft,publishedAt` output
// Exit codes: 0 clean, 1 abort (nothing written), 2 usage (nothing written), 3 feed written with >= 1 degraded plugin.
// Errors go to stderr as `gen-feed: <CODE>: <message>`, degrades as `gen-feed: WARN <CODE>: <message>`; the codes and
// their classes are in tools/feed-contract/CONTRACT.md and contract.js CODES.
// Exports main(argv, io, hooks), buildFeed, buildLegacyFeed, projectV1, gitVersionCompare, readPluginsDir,
// cutChangelogSection, extractAppId, assembleEntry.
'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const {
  ContractError,
  CODES,
  checkAssembledEntry,
  checkReleaseManifest,
  checkUrl,
  compareSemVer,
  findInstallDuplicates,
  isPluginName,
  isStableCore,
  parseSemVer,
  validateFeed,
} = require('./feed-contract/contract.js');

const USAGE = [
  'usage: node tools/gen-feed.js --plugins-dir <dir> --releases <file> --manifests <dir> --repo <owner/name>',
  '                              [--manager-latest-url <url>] --out <file>',
  '       node tools/gen-feed.js --legacy --plugins-dir <dir> --tags <file> --out <file>',
  '       node tools/gen-feed.js --project-v1 <feed>',
  '',
].join('\n');

const json = (value) => JSON.stringify(value, null, 2) + '\n';
const show = (value) => JSON.stringify(value);
const isObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));

// --- arguments

const VALUE_FLAGS = new Set([
  '--plugins-dir',
  '--releases',
  '--manifests',
  '--repo',
  '--manager-latest-url',
  '--out',
  '--tags',
  '--project-v1',
]);
const MODE_FLAGS = {
  v2: {
    allowed: ['--plugins-dir', '--releases', '--manifests', '--repo', '--manager-latest-url', '--out'],
    required: ['--plugins-dir', '--releases', '--manifests', '--repo', '--out'],
  },
  legacy: { allowed: ['--legacy', '--plugins-dir', '--tags', '--out'], required: ['--plugins-dir', '--tags', '--out'] },
  'project-v1': { allowed: ['--project-v1'], required: ['--project-v1'] },
};

const usageError = (message) => new ContractError('E_USAGE', message);

/** @returns {{mode: 'v2' | 'legacy' | 'project-v1', opts: Map<string, string | true>}} */
function parseArgs(argv) {
  const opts = new Map();
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (a !== '--legacy' && !VALUE_FLAGS.has(a)) throw usageError(`unknown argument ${JSON.stringify(a)}`);
    if (opts.has(a)) throw usageError(`${a} given twice`);
    if (a === '--legacy') {
      opts.set(a, true);
      continue;
    }
    const value = argv[i + 1];
    if (value === undefined || value === '' || value.startsWith('--')) throw usageError(`${a} needs a value`);
    opts.set(a, value);
    ++i;
  }
  if (opts.has('--legacy') && opts.has('--project-v1')) throw usageError('--legacy and --project-v1 are two modes');
  const mode = opts.has('--legacy') ? 'legacy' : opts.has('--project-v1') ? 'project-v1' : 'v2';
  const { allowed, required } = MODE_FLAGS[mode];
  for (const flag of opts.keys()) {
    if (!allowed.includes(flag)) throw usageError(`${flag} is not a flag of the ${mode} mode`);
  }
  for (const flag of required) {
    if (!opts.has(flag)) throw usageError(`the ${mode} mode needs ${flag}`);
  }
  return { mode, opts };
}

// --- file IO (main's side; the builders below are pure)

function readText(file, label) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch (err) {
    throw new ContractError('E_INPUT', `${label} ${file}: cannot read (${err.code || err.message})`, { file });
  }
}

function parseJsonText(text, file, label) {
  try {
    return JSON.parse(text);
  } catch (err) {
    throw new ContractError('E_INPUT', `${label} ${file}: not JSON (${err.message})`, { file });
  }
}

/**
 * The plugin inputs under dir: every entry that holds a version.json, in bytewise order of the entry names (the nine
 * lower-case directory names sort as `ls plugins/*\/version.json` does, release.yml:653-654).
 * @returns {Array<{dir: string, versionJson: object, changelog?: string, setupIss?: string}>}
 */
function readPluginsDir(dir, { withSetupIss }) {
  let names;
  try {
    names = fs.readdirSync(dir);
  } catch (err) {
    throw new ContractError('E_INPUT', `--plugins-dir ${dir}: cannot read (${err.code || err.message})`, { file: dir });
  }
  names.sort((a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8')));
  const plugins = [];
  for (const name of names) {
    const versionFile = path.join(dir, name, 'version.json');
    if (!fs.existsSync(versionFile)) continue;
    const plugin = { dir: name, versionJson: parseJsonText(readText(versionFile, 'version.json'), versionFile, 'version.json') };
    const changelogFile = path.join(dir, name, 'CHANGELOG.md');
    if (fs.existsSync(changelogFile)) plugin.changelog = readText(changelogFile, 'CHANGELOG.md');
    const setupFile = path.join(dir, name, 'installers', 'windows', 'setup.iss');
    if (withSetupIss && fs.existsSync(setupFile)) plugin.setupIss = readText(setupFile, 'setup.iss');
    plugins.push(plugin);
  }
  return plugins;
}

/** The release.json files of --manifests: every `*.json` file, bytewise by name, labelled `manifests/<name>`. */
function readManifestsDir(dir) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch (err) {
    throw new ContractError('E_INPUT', `--manifests ${dir}: cannot read (${err.code || err.message})`, { file: dir });
  }
  return entries
    .filter((d) => d.isFile() && d.name.endsWith('.json'))
    .map((d) => d.name)
    .sort(bytewise)
    .map((name) => ({ file: `manifests/${name}`, text: readText(path.join(dir, name), 'release.json') }));
}

/**
 * Writes text to outPath through a temp file in the same directory and a rename, so a reader never sees a partial
 * feed. Any failure removes the temp file and is E_INPUT naming --out.
 */
function writeAtomic(outPath, text) {
  const tmp = path.join(
    path.dirname(outPath),
    `.${path.basename(outPath)}.${process.pid}.${crypto.randomBytes(4).toString('hex')}.tmp`,
  );
  try {
    fs.writeFileSync(tmp, text, { flag: 'wx' });
    fs.renameSync(tmp, outPath);
  } catch (err) {
    try {
      fs.rmSync(tmp, { force: true });
    } catch {
      // The temp file was never created (or cannot be touched); the E_INPUT below is the report.
    }
    throw new ContractError('E_INPUT', `--out ${outPath}: cannot write (${err.code || err.message})`, { file: outPath });
  }
}

// --- git's version order

const isDigit = (c) => c >= 0x30 && c <= 0x39;

/**
 * git's `--sort=v:refname` order with no versionsort.suffix configured, i.e. strverscmp, implemented from its
 * documented semantics (glibc manual, "String/Array Comparison"; plan §1.2, §4.5.3), not from git's or glibc's code.
 * Strings are compared bytewise up to the first difference; when that difference falls inside a run of digits, the
 * run decides: a run without a leading zero is an integer (the longer run is larger), a run made only of zeros so far
 * is smaller the more zeros it has, and a run with a leading zero then other digits compares lexically.
 * A string that is a prefix of the other sorts first, so `1.1.0` < `1.1.0-beta.1`. Returns <0, 0 or >0.
 */
function gitVersionCompare(a, b) {
  const x = Buffer.from(a, 'utf8');
  const y = Buffer.from(b, 'utf8');
  const at = (buf, i) => (i < buf.length ? buf[i] : 0);
  let i = 0;
  while (at(x, i) === at(y, i)) {
    if (i >= x.length) return 0;
    ++i;
  }
  const ca = at(x, i);
  const cb = at(y, i);
  const byteOrder = ca - cb;
  // The digits both strings share right before the difference.
  let start = i;
  while (start > 0 && isDigit(x[start - 1])) --start;
  const shared = x.subarray(start, i);
  const runLength = (buf) => {
    let n = 0;
    while (isDigit(at(buf, i + n))) ++n;
    return n;
  };
  const byLength = () => {
    const d = runLength(x) - runLength(y);
    return d !== 0 ? d : byteOrder;
  };
  if (shared.length === 0) {
    // Two numbers starting here without a leading zero: the longer is larger. Anything else: bytewise.
    return isDigit(ca) && ca !== 0x30 && isDigit(cb) && cb !== 0x30 ? byLength() : byteOrder;
  }
  if (shared[0] !== 0x30) return byLength(); // inside an integer
  if (shared.some((c) => c !== 0x30)) return byteOrder; // leading zero, then other digits: lexical
  // Only zeros so far: the side whose run goes on has more leading zeros (or a fraction) and sorts first.
  if (isDigit(ca) && isDigit(cb)) return byteOrder;
  if (isDigit(ca)) return -1;
  if (isDigit(cb)) return 1;
  return byteOrder;
}

// --- legacy mode (FR-040): release.yml:645-685, byte for byte

/** The bare tag names of --tags text (FR-040): LF or CRLF, one optional final newline. */
function parseTagsText(tagsText) {
  const lines = tagsText.split(/\r?\n/);
  if (lines[lines.length - 1] === '') lines.pop();
  lines.forEach((line, i) => {
    const where = `--tags line ${i + 1}`;
    if (line === '') throw new ContractError('E_INPUT', `${where}: empty line`, { field: '--tags' });
    if (/\s/.test(line)) throw new ContractError('E_INPUT', `${where}: whitespace in a tag name (raw git ls-remote output?)`, { field: '--tags' });
    if (line.includes('^')) throw new ContractError('E_INPUT', `${where}: '^' in a tag name (a peeled ref line?)`, { field: '--tags' });
    if (line.startsWith('refs/')) throw new ContractError('E_INPUT', `${where}: a full ref name, not a bare tag name`, { field: '--tags' });
  });
  return lines;
}

/**
 * Today's feed: for each plugin in directory order, the first `<name lowercased>/v*` tag in git's -v:refname order,
 * version = the tag after its last `/v` (release.yml:672), skipped without a tag; entry {version, download_url: url,
 * release_notes: ""}; a repeated name overwrites in place, as the object in release.yml does. Nothing else is checked:
 * this mode shows what today's step would publish.
 */
function buildLegacyFeed({ plugins, tagsText }) {
  const tags = parseTagsText(tagsText);
  const out = {};
  for (const plugin of plugins) {
    const name = plugin.versionJson !== null && typeof plugin.versionJson === 'object' ? plugin.versionJson.name : undefined;
    if (typeof name !== 'string') {
      throw new ContractError('E_INPUT', `${plugin.dir}/version.json: "name" is not a string`, { field: 'name' });
    }
    const prefix = name.toLowerCase() + '/v';
    const candidates = tags.filter((tag) => tag.startsWith(prefix)).sort((a, b) => gitVersionCompare(b, a));
    if (candidates.length === 0) continue;
    out[name] = {
      version: candidates[0].replace(/.*\/v/, ''),
      download_url: plugin.versionJson.url,
      release_notes: '',
    };
  }
  return json({ schema_version: 1, plugins: out });
}

// --- v1 projection (FR-047)

/** What a v1 reader sees of a feed: version / download_url / release_notes per plugin, plugin order kept. */
function projectV1(doc) {
  const plugins = doc !== null && typeof doc === 'object' ? doc.plugins : undefined;
  if (plugins === null || typeof plugins !== 'object' || Array.isArray(plugins)) {
    throw new ContractError('E_INPUT', 'the feed has no "plugins" object', { field: 'plugins' });
  }
  const out = {};
  for (const [name, entry] of Object.entries(plugins)) {
    if (entry === null || typeof entry !== 'object' || Array.isArray(entry)) {
      throw new ContractError('E_INPUT', `plugins.${name} is not an object`, { plugin: name });
    }
    const v1 = {};
    for (const key of ['version', 'download_url', 'release_notes']) {
      if (Object.hasOwn(entry, key)) v1[key] = entry[key];
    }
    out[name] = v1;
  }
  return json({ schema_version: 1, plugins: out });
}

// --- v2 mode (FR-041 to FR-049; plan §4.5.2, §4.5.4)

const REPO_RE = /^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$/u;
const PUB_DATE_RE = /^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$/u; // FR-005, the form gh emits
const APP_ID_RE = /^AppId=\{\{([0-9A-F-]{36})\}[ \t]*$/mu; // plan §4.5.2: Inno's "{{" is one literal "{"
const MANAGER_LATEST_SUFFIX = '/manager/latest.json'; // FR-011

/**
 * FR-044 (PD-4): the CHANGELOG section of `version`, i.e. the lines after the first line starting with the literal
 * `## [<version>]`, up to the next line starting `## [`; CRLF -> LF; leading and trailing blank lines trimmed. undefined
 * when the text, the section or its content is missing. One section, uncapped (Clarifications Q6).
 */
function cutChangelogSection(text, version) {
  if (typeof text !== 'string') return undefined;
  const lines = text.replace(/\r\n/gu, '\n').split('\n');
  const header = `## [${version}]`;
  const start = lines.findIndex((line) => line.startsWith(header));
  if (start < 0) return undefined;
  const next = lines.findIndex((line, i) => i > start && line.startsWith('## ['));
  const section = lines.slice(start + 1, next < 0 ? lines.length : next);
  const blank = (line) => /^[ \t]*$/u.test(line);
  while (section.length > 0 && blank(section[0])) section.shift();
  while (section.length > 0 && blank(section[section.length - 1])) section.pop();
  return section.length > 0 ? section.join('\n') : undefined;
}

/**
 * FR-043 check 2: the AppId of an Inno Setup script, `{<GUID>}`, from its first `AppId={{<GUID>}` line (CRLF
 * normalised). A single-brace `AppId={GUID}` is not that line (Inno would read `{` as a constant), so it gives undefined.
 */
function extractAppId(setupIssText) {
  if (typeof setupIssText !== 'string') return undefined;
  const m = APP_ID_RE.exec(setupIssText.replace(/\r\n/gu, '\n'));
  return m ? `{${m[1]}}` : undefined;
}

/** FR-005 / FR-010 tag versions: bounded core, then an optional SemVer 2.0.0 prerelease suffix. */
function isBoundedSemVer(version) {
  const dash = version.indexOf('-');
  return isStableCore(dash < 0 ? version : version.slice(0, dash)) && parseSemVer(version) !== null;
}

/** Step 1 for the plugin inputs (abort class): E_PLUGIN_NAME, E_V1_TYPE, E_URL_*, E_DUPLICATE_PLUGIN. */
function checkPluginInputs(plugins) {
  const byLowerName = new Map();
  for (const plugin of plugins) {
    const file = `${plugin.dir}/version.json`;
    const versionJson = plugin.versionJson;
    const name = isObject(versionJson) ? versionJson.name : undefined;
    if (!isPluginName(name)) {
      throw new ContractError(
        'E_PLUGIN_NAME',
        `${file}: "name" ${show(name)} is not a plugin name (^[A-Z][A-Za-z0-9]{0,63}$, not a Windows reserved device name)`,
        { file, field: 'name' },
      );
    }
    if (typeof versionJson.url !== 'string') {
      throw new ContractError(
        'E_V1_TYPE',
        `${file}: plugin ${name}: "url" is ${show(versionJson.url)}, not a string (it is the v1 download_url)`,
        { plugin: name, file, field: 'url' },
      );
    }
    try {
      checkUrl(versionJson.url, { field: 'url' });
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      throw new ContractError(err.code, `${file}: plugin ${name}: ${err.message} (it is the v1 download_url)`, {
        plugin: name,
        file,
        field: 'url',
      });
    }
    const lower = name.toLowerCase();
    if (byLowerName.has(lower)) {
      const other = byLowerName.get(lower);
      throw new ContractError(
        'E_DUPLICATE_PLUGIN',
        `${file}: plugin ${name}: the name equals ${show(other.name)} of ${other.dir}/version.json after toLowerCase() ` +
          `(one folder on NTFS and default APFS, the same ${lower}/v* tags)`,
        { plugin: name, file, field: 'name' },
      );
    }
    byLowerName.set(lower, { name, dir: plugin.dir });
  }
}

/** Step 1 for --releases (abort class, E_INPUT): row shapes, unique tag names, non-draft publishedAt. */
function checkReleaseRows(releases) {
  if (!Array.isArray(releases)) throw new ContractError('E_INPUT', '--releases: not a JSON array', { field: '--releases' });
  const seen = new Set();
  releases.forEach((row, i) => {
    const where = `--releases row ${i + 1}`;
    const fail = (why) => new ContractError('E_INPUT', `${where}: ${why}`, { field: '--releases' });
    if (!isObject(row)) throw fail('not an object');
    if (typeof row.tagName !== 'string' || row.tagName === '') throw fail(`tagName ${show(row.tagName)} is not a tag name`);
    if (typeof row.isPrerelease !== 'boolean') throw fail(`isPrerelease ${show(row.isPrerelease)} is not a boolean`);
    if (typeof row.isDraft !== 'boolean') throw fail(`isDraft ${show(row.isDraft)} is not a boolean`);
    if (seen.has(row.tagName)) throw fail(`tagName ${show(row.tagName)} appears twice`);
    seen.add(row.tagName);
    if (!row.isDraft && (typeof row.publishedAt !== 'string' || !PUB_DATE_RE.test(row.publishedAt))) {
      throw fail(`publishedAt ${show(row.publishedAt)} of ${row.tagName} is not YYYY-MM-DDTHH:MM:SSZ`);
    }
  });
}

/**
 * Step 2, the Q9 rule (FR-042), here and nowhere else: drafts dropped; of the releases tagged `<name lowercased>/v…`,
 * stable = the highest non-prerelease, sibling = the highest prerelease strictly above it. null when there is no
 * stable release (the plugin is omitted). The channel comes from isPrerelease alone (FR-018).
 * @returns {{stable: {tagName, publishedAt, version}, sibling: {tagName, publishedAt, version} | null} | null}
 */
function selectReleases(name, releases) {
  const prefix = name.toLowerCase() + '/v';
  let stable = null;
  const prereleases = [];
  for (const row of releases) {
    if (row.isDraft || !row.tagName.startsWith(prefix)) continue;
    const version = row.tagName.slice(prefix.length);
    if (row.isPrerelease ? !isBoundedSemVer(version) : !isStableCore(version)) {
      const rule = row.isPrerelease ? 'a bounded core with an optional prerelease suffix' : 'a bounded three-part core';
      throw new ContractError(
        'E_TAG_FORMAT',
        `plugin ${name}: release ${row.tagName}${row.isPrerelease ? ' (prerelease)' : ''}: version ${show(version)} is not ${rule}`,
        { plugin: name, field: 'tagName' },
      );
    }
    const release = { tagName: row.tagName, publishedAt: row.publishedAt, version };
    if (!row.isPrerelease) {
      if (stable === null || compareSemVer(version, stable.version) > 0) stable = release;
    } else {
      prereleases.push(release);
    }
  }
  if (stable === null) return null;
  let sibling = null;
  for (const release of prereleases) {
    if (compareSemVer(release.version, stable.version) <= 0) continue;
    if (sibling === null || compareSemVer(release.version, sibling.version) > 0) sibling = release;
  }
  return { stable, sibling };
}

/**
 * Step 3 (PD-8, Clarifications 2026-10-30 Q-A): manifests by tag, files bytewise. A file that is not JSON, or whose
 * `tag` is not a string, is ignored with one unattributed warning naming the file.
 * @returns {Map<string, Array<{file: string, doc: object}>>}
 */
function indexManifests(manifests, warnings) {
  const byTag = new Map();
  for (const { file, text } of [...manifests].sort((a, b) => bytewise(a.file, b.file))) {
    let doc;
    try {
      doc = JSON.parse(text);
    } catch (err) {
      warnings.push({ code: 'E_MANIFEST_SCHEMA', plugin: null, file, message: `${file}: not JSON (${err.message})` });
      continue;
    }
    if (!isObject(doc) || typeof doc.tag !== 'string') {
      const tag = isObject(doc) ? show(doc.tag) : 'absent (not a JSON object)';
      warnings.push({ code: 'E_MANIFEST_SCHEMA', plugin: null, file, field: 'tag', message: `${file}: "tag" is ${tag}, not a string` });
      continue;
    }
    if (!byTag.has(doc.tag)) byTag.set(doc.tag, []);
    byTag.get(doc.tag).push({ file, doc });
  }
  return byTag;
}

const releaseNotesUrl = (repo, tag) => `https://github.com/${repo}/releases/tag/${tag}`; // FR-043

/** C2 assets -> C1 platforms (FR-043): keys bytewise; each asset minus `name`, its payload minus `list`. */
function platformsFrom(assets) {
  const platforms = {};
  for (const key of Object.keys(assets).sort(bytewise)) {
    const asset = assets[key];
    const platform = { url: asset.url, size: asset.size, sha256: asset.sha256, signature: asset.signature, format: asset.format };
    if (asset.min_os !== undefined) platform.min_os = asset.min_os;
    if (asset.payload !== undefined) platform.payload = { files: asset.payload.files, sha256: asset.payload.sha256 };
    platforms[key] = platform;
  }
  return platforms;
}

/**
 * One manifest of one plugin: C2 in full (checkReleaseManifest), FR-043 check 1 (only when version.json `version`
 * equals the manifest's), check 2 (the setup.iss AppId). Throws the first failure as a ContractError (degrade class).
 */
function checkManifest(plugin, { file, doc }, repo) {
  checkReleaseManifest(doc, { repo, file });
  const versionJson = plugin.versionJson;
  const name = versionJson.name;
  const install = doc.install;
  const mismatch = (field, why, where = file) =>
    new ContractError('E_MANIFEST_MISMATCH', `${where}: install.${field}: ${why}`, { plugin: name, file: where, field: `install.${field}` });
  if (versionJson.version === doc.version) {
    if (install.preset_subdir !== versionJson.preset_subdir) {
      throw mismatch(
        'preset_subdir',
        `is ${show(install.preset_subdir)}, ${plugin.dir}/version.json preset_subdir is ${show(versionJson.preset_subdir)}`,
      );
    }
    const pkgId = versionJson.identifier !== undefined ? versionJson.identifier : `com.krateaudio.${name.toLowerCase()}`;
    if (install.macos_pkg_id !== pkgId) {
      const source = versionJson.identifier !== undefined ? 'version.json identifier' : 'com.krateaudio.<name lowercased>';
      throw mismatch('macos_pkg_id', `is ${show(install.macos_pkg_id)}, expected ${show(pkgId)} (${source})`);
    }
  }
  const iss = `${plugin.dir}/installers/windows/setup.iss`;
  if (plugin.setupIss === undefined) throw mismatch('windows_uninstall_key', 'the file is missing, so no AppId to compare', iss);
  const appId = extractAppId(plugin.setupIss);
  if (appId === undefined) throw mismatch('windows_uninstall_key', 'the file has no AppId={{<GUID>} line', iss);
  if (install.windows_uninstall_key !== `${appId}_is1`) {
    throw mismatch('windows_uninstall_key', `${file} says ${show(install.windows_uninstall_key)}, the AppId gives ${show(`${appId}_is1`)}`, iss);
  }
}

/** A degraded entry (FR-049): `platforms` and `install` dropped from the entry and from its sibling; key order kept. */
function stripInstallFacts(entry) {
  for (const part of [entry, entry.prerelease]) {
    if (!isObject(part)) continue;
    delete part.platforms;
    delete part.install;
  }
}

/** A warning record for a ContractError found while checking one plugin (message names the plugin, file, field). */
function warningOf(err, plugin) {
  const warning = { code: err.code, plugin, message: `plugin ${plugin}: ${err.message}` };
  if (err.file !== undefined) warning.file = err.file;
  if (err.field !== undefined) warning.field = err.field;
  return warning;
}

/**
 * Step 4 for one plugin: the entry in FR-043 key order, its prerelease sibling, platforms / install from the
 * manifests when every check passes, then the PD-10 backstop (checkAssembledEntry). The first failure degrades the
 * plugin: no platforms / install on the entry or the sibling, and one warning.
 * @param {{plugin: {dir: string, versionJson: object, changelog?: string, setupIss?: string},
 *          stable: {tagName: string, publishedAt: string, version: string},
 *          sibling?: {tagName: string, publishedAt: string, version: string},
 *          stableManifest?: {file: string, doc: object}, siblingManifest?: {file: string, doc: object}, repo: string}} args
 * @returns {{entry: object, warning?: {code: string, plugin: string, file?: string, field?: string, message: string}}}
 */
function assembleEntry({ plugin, stable, sibling, stableManifest, siblingManifest, repo }) {
  const name = plugin.versionJson.name;
  const entry = {
    version: stable.version,
    download_url: plugin.versionJson.url,
    release_notes: '',
    tag: stable.tagName,
    pub_date: stable.publishedAt,
    channel: 'stable',
    release_notes_url: releaseNotesUrl(repo, stable.tagName),
  };
  const notes = cutChangelogSection(plugin.changelog, stable.version);
  if (notes !== undefined) entry.notes_markdown = notes;
  let prerelease;
  if (sibling) {
    prerelease = {
      tag: sibling.tagName,
      pub_date: sibling.publishedAt,
      channel: 'beta',
      release_notes_url: releaseNotesUrl(repo, sibling.tagName),
    };
    const siblingNotes = cutChangelogSection(plugin.changelog, sibling.version);
    if (siblingNotes !== undefined) prerelease.notes_markdown = siblingNotes;
  }
  const parts = [
    [entry, stableManifest],
    [prerelease, siblingManifest],
  ].filter(([part, manifest]) => part !== undefined && manifest !== undefined);
  try {
    for (const [, manifest] of parts) checkManifest(plugin, manifest, repo);
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    if (prerelease) entry.prerelease = prerelease;
    return { entry, warning: warningOf(err, name) };
  }
  for (const [part, manifest] of parts) {
    part.platforms = platformsFrom(manifest.doc.assets);
    part.install = structuredClone(manifest.doc.install);
  }
  if (prerelease) entry.prerelease = prerelease;
  if (parts.length > 0) {
    try {
      checkAssembledEntry(name, entry);
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      stripInstallFacts(entry);
      return { entry, warning: warningOf(err, name) };
    }
  }
  return { entry };
}

/**
 * The schema_version 2 feed (plan §4.5.4 steps 1-7). Throws a ContractError for an abort-class failure; degrade-class
 * findings come back as `warnings` (the plugin's entry is then v1-only, FR-049).
 * @param {{plugins: Array<{dir: string, versionJson: object, changelog?: string, setupIss?: string}>,
 *          releases: Array<{tagName: string, isPrerelease: boolean, isDraft: boolean, publishedAt: string}>,
 *          manifests?: Array<{file: string, text: string}>, repo: string, managerLatestUrl?: string}} inputs
 * @param {{beforeSelfCheck?: (doc: object) => void}} [hooks]  test-only (PD-16): sees the document before step 7
 * @returns {{doc: object, text: string, warnings: Array<{code: string, plugin: string | null, file?: string,
 *            field?: string, message: string}>}}
 */
function buildFeed({ plugins, releases, manifests = [], repo, managerLatestUrl }, hooks = {}) {
  // 1. inputs (abort class)
  checkPluginInputs(plugins);
  checkReleaseRows(releases);
  if (typeof repo !== 'string' || !REPO_RE.test(repo)) {
    throw new ContractError('E_INPUT', `--repo ${show(repo)} is not <owner>/<name>`, { field: '--repo' });
  }
  if (managerLatestUrl !== undefined) {
    try {
      checkUrl(managerLatestUrl, { field: '--manager-latest-url' });
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      throw new ContractError('E_INPUT', `${err.message} (${err.code})`, { field: '--manager-latest-url' });
    }
    if (!managerLatestUrl.endsWith(MANAGER_LATEST_SUFFIX)) {
      throw new ContractError('E_INPUT', `--manager-latest-url ${managerLatestUrl} does not end ${MANAGER_LATEST_SUFFIX}`, {
        field: '--manager-latest-url',
      });
    }
  }
  // 2. release selection (abort class: E_TAG_FORMAT)
  const selections = plugins.map((plugin) => ({ plugin, selection: selectReleases(plugin.versionJson.name, releases) }));
  // 3. manifests by tag
  const warnings = [];
  const byTag = indexManifests(manifests, warnings);
  // 4. entries
  const out = {};
  const degraded = new Set();
  for (const { plugin, selection } of selections) {
    if (selection === null) continue;
    const name = plugin.versionJson.name;
    const { stable, sibling } = selection;
    const claims = [stable, sibling].filter(Boolean).map((release) => [release, byTag.get(release.tagName) ?? []]);
    const duplicate = claims.find(([, list]) => list.length > 1);
    let result;
    if (duplicate) {
      const [release, list] = duplicate;
      const files = list.map((m) => m.file);
      result = assembleEntry({ plugin, stable, sibling: sibling ?? undefined, repo });
      result.warning = {
        code: 'E_MANIFEST_DUPLICATE',
        plugin: name,
        file: files[0],
        field: 'tag',
        message: `plugin ${name}: ${files.join(' and ')} all claim tag ${release.tagName}; all are ignored`,
      };
    } else {
      const [stableList, siblingList] = [byTag.get(stable.tagName), sibling ? byTag.get(sibling.tagName) : undefined];
      result = assembleEntry({
        plugin,
        stable,
        sibling: sibling ?? undefined,
        stableManifest: stableList?.[0],
        siblingManifest: siblingList?.[0],
        repo,
      });
    }
    out[name] = result.entry;
    if (result.warning) {
      warnings.push(result.warning);
      degraded.add(name);
    }
  }
  // 5. install-fact uniqueness (FR-014, FR-049): every plugin of a shared value degrades.
  const healthy = Object.fromEntries(Object.entries(out).filter(([name]) => !degraded.has(name)));
  const shared = new Map();
  for (const dup of findInstallDuplicates(healthy)) {
    for (const name of dup.plugins) if (!shared.has(name)) shared.set(name, dup);
  }
  for (const name of Object.keys(out)) {
    const dup = shared.get(name);
    if (!dup) continue;
    stripInstallFacts(out[name]);
    warnings.push({
      code: 'E_INSTALL_DUPLICATE',
      plugin: name,
      field: `install.${dup.field}`,
      message: `plugin ${name}: install.${dup.field} ${show(dup.value)} is shared by plugins ${show(dup.plugins[0])} and ${show(dup.plugins[1])} (FR-014)`,
    });
  }
  // 6. the document
  const doc = { schema_version: 2, plugins: out };
  if (managerLatestUrl !== undefined) doc.manager = { latest_json_url: managerLatestUrl };
  // 7. self-check (FR-045)
  hooks.beforeSelfCheck?.(doc);
  for (const schema of ['v2', 'v1']) {
    const result = validateFeed(doc, { schema });
    if (!result.valid) {
      const [first] = result.errors;
      throw new ContractError('E_OUTPUT_SCHEMA', `the generated feed fails ${schema}: ${first.message} (a gen-feed bug)`, {
        instancePath: first.instancePath,
      });
    }
  }
  return { doc, text: json(doc), warnings };
}

// --- CLI

/**
 * @param {string[]} argv
 * @param {{stdout: {write(s: string): void}, stderr: {write(s: string): void}}} io
 * @param {{beforeSelfCheck?: (doc: object) => void}} [hooks]  test-only seam of the v2 self-check (plan PD-16)
 * @returns {0|1|2|3}
 */
function main(argv, io, hooks = {}) {
  try {
    const { mode, opts } = parseArgs(argv);
    if (mode === 'legacy') {
      const plugins = readPluginsDir(opts.get('--plugins-dir'), { withSetupIss: false });
      const tagsPath = opts.get('--tags');
      const text = buildLegacyFeed({ plugins, tagsText: readText(tagsPath, '--tags') });
      writeAtomic(opts.get('--out'), text);
      return 0;
    }
    if (mode === 'project-v1') {
      const feedPath = opts.get('--project-v1');
      io.stdout.write(projectV1(parseJsonText(readText(feedPath, '--project-v1'), feedPath, '--project-v1')));
      return 0;
    }
    const releasesPath = opts.get('--releases');
    const { text, warnings } = buildFeed(
      {
        plugins: readPluginsDir(opts.get('--plugins-dir'), { withSetupIss: true }),
        releases: parseJsonText(readText(releasesPath, '--releases'), releasesPath, '--releases'),
        manifests: readManifestsDir(opts.get('--manifests')),
        repo: opts.get('--repo'),
        managerLatestUrl: opts.get('--manager-latest-url'),
      },
      hooks,
    );
    for (const w of warnings) {
      if (!String(CODES[w.code]).includes('degrade')) throw new Error(`gen-feed bug: ${w.code} reported as a degrade`);
    }
    writeAtomic(opts.get('--out'), text);
    for (const w of warnings) io.stderr.write(`gen-feed: WARN ${w.code}: ${w.message}\n`);
    return warnings.length > 0 ? 3 : 0;
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    io.stderr.write(`gen-feed: ${err.code}: ${err.message}\n`);
    if (CODES[err.code] === 'usage') {
      io.stderr.write(USAGE);
      return 2;
    }
    return 1;
  }
}

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);

module.exports = {
  main,
  buildFeed,
  buildLegacyFeed,
  projectV1,
  gitVersionCompare,
  readPluginsDir,
  cutChangelogSection,
  extractAppId,
  assembleEntry,
  selectReleases,
};
