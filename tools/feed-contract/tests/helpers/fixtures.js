// Fixture paths, loaders and an in-process gen-feed runner for the feed-contract tests (plan §2, §4.7; spec FR-048).
//
// WHY: every gen-feed test reads the same case layout (fixtures/gen-feed/<case>/) and runs the CLI through the
// exported main() in-process, so its writes pass through the write guard (FR-061). One helper keeps the layout and the
// argv shape in one place.
//
// Read-only: this file never writes. It is scanned by hygiene.test.js like every other module (no exemption), so it
// calls fs through the module object and never requires a child-process module. gen-feed.js is required lazily, so a
// test file that only reads fixtures does not load the generator.
'use strict';
const fs = require('node:fs');
const path = require('node:path');

const FIXTURES = path.resolve(__dirname, '..', '..', 'fixtures');

let genFeed = null;
function loadGenFeed() {
  if (genFeed === null) genFeed = require('../../../gen-feed.js');
  return genFeed;
}

/** Parses a JSON file under fixtures/ (rel uses '/' separators). */
function readJson(rel) {
  return JSON.parse(fs.readFileSync(path.join(FIXTURES, ...rel.split('/')), 'utf8'));
}

/**
 * One gen-feed case directory (plan §4.7 layout).
 * @returns {{dir: string, caseJson: object, pluginsDir: string, releasesPath: string, tagsPath: string,
 *            manifestsDir: string | null}}  manifestsDir is null when the case has no manifests/ directory
 */
function loadCase(name) {
  const dir = path.join(FIXTURES, 'gen-feed', name);
  const caseJson = JSON.parse(fs.readFileSync(path.join(dir, 'case.json'), 'utf8'));
  const manifests = path.join(dir, 'manifests');
  return {
    dir,
    caseJson,
    pluginsDir: path.join(dir, 'plugins'),
    releasesPath: path.join(dir, 'releases.json'),
    tagsPath: path.join(dir, 'tags.txt'),
    manifestsDir: fs.existsSync(manifests) ? manifests : null,
  };
}

/** Runs gen-feed's main(argv, io, hooks) in-process and captures what it prints. */
function runMain(argv, hooks = {}) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = loadGenFeed().main(argv, io, hooks);
  return { code, stdout, stderr };
}

/**
 * The v2-mode argv for a case. git stores no empty directory, so a case without manifests/ needs an empty
 * fs.mkdtemp directory that the calling test makes (and removes) and passes in as emptyManifestsDir.
 */
function v2Argv(caseInfo, outPath, emptyManifestsDir) {
  const manifests = caseInfo.manifestsDir ?? emptyManifestsDir;
  if (typeof manifests !== 'string') {
    throw new Error(`v2Argv: ${caseInfo.dir} has no manifests/ directory; pass an empty temp directory`);
  }
  const argv = ['--plugins-dir', caseInfo.pluginsDir, '--releases', caseInfo.releasesPath, '--manifests', manifests];
  argv.push('--repo', caseInfo.caseJson.repo);
  if (caseInfo.caseJson.managerLatestUrl !== undefined) {
    argv.push('--manager-latest-url', caseInfo.caseJson.managerLatestUrl);
  }
  argv.push('--out', outPath);
  return argv;
}

module.exports = { FIXTURES, readJson, loadCase, runMain, v2Argv };
