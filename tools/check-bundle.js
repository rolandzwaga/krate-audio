#!/usr/bin/env node
// Asserts that a built VST3 bundle contains the resources the SDK hosting
// validator does NOT check — the ones whose absence ships a blank-UI / crashing
// plugin (e.g. a hand-copied bundle missing Resources/editor.uidesc) — and that
// the bundle's moduleinfo.json carries the plugin's own version. The SDK writes
// the top-level "Version" from CMake's PROJECT_VERSION; in this monorepo that is
// the root project's version unless the plugin overrides it, and every bundle
// shipped "0.8.0" while its classes said 0.12.2.1 (found 2026-10-29). Tools that
// read moduleinfo.json for version detection (the plugin manager) need it right.
//
//   node tools/check-bundle.js <bundle.vst3> [<bundle2.vst3> ...]
//
// Exit 0 if every bundle has a non-empty editor.uidesc and moduleinfo.json and
// moduleinfo.json's "Version" (and every class Version) matches
// plugins/<name>/version.json; exit 1 (with a report) otherwise. Node only.

const fs = require('fs');
const path = require('path');

const REQUIRED = [
  ['Contents', 'Resources', 'editor.uidesc'],
  ['Contents', 'Resources', 'moduleinfo.json'],
];

const REPO_ROOT = path.resolve(__dirname, '..');

// moduleinfotool emits JSON5-style trailing commas; strip them before parsing.
function parseModuleInfo(text) {
  return JSON.parse(text.replace(/,(\s*[}\]])/g, '$1'));
}

// Returns a list of problems (empty when the versions agree).
function checkVersions(bundle, name) {
  const pluginDir = name.replace(/\.vst3$/i, '').toLowerCase();
  const versionJsonPath = path.join(REPO_ROOT, 'plugins', pluginDir, 'version.json');
  if (!fs.existsSync(versionJsonPath)) {
    return [`no plugins/${pluginDir}/version.json to compare against`];
  }
  const expected = JSON.parse(fs.readFileSync(versionJsonPath, 'utf8')).version;
  let info;
  try {
    info = parseModuleInfo(fs.readFileSync(path.join(bundle, ...REQUIRED[1]), 'utf8'));
  } catch (e) {
    return [`moduleinfo.json unparseable: ${e.message}`];
  }
  const problems = [];
  if (info.Version !== expected) {
    problems.push(`moduleinfo.json Version "${info.Version}" != version.json "${expected}"`);
  }
  for (const cls of info.Classes || []) {
    const v = cls.Version || '';
    if (v !== expected && !v.startsWith(`${expected}.`)) {
      problems.push(`class "${cls.Name}" Version "${v}" != version.json "${expected}"`);
    }
  }
  return problems;
}

const bundles = process.argv.slice(2);
if (bundles.length === 0) {
  console.error('usage: node tools/check-bundle.js <bundle.vst3> [<bundle2.vst3> ...]');
  process.exit(2);
}

let failures = 0;
for (const bundle of bundles) {
  const name = path.basename(bundle);
  if (!fs.existsSync(bundle)) {
    console.error(`FAIL ${name}: bundle not found at ${bundle}`);
    failures++;
    continue;
  }
  const problems = [];
  for (const rel of REQUIRED) {
    const p = path.join(bundle, ...rel);
    const relStr = rel.join('/');
    if (!fs.existsSync(p)) {
      problems.push(`missing ${relStr}`);
    } else if (fs.statSync(p).size === 0) {
      problems.push(`empty ${relStr}`);
    }
  }
  if (problems.length === 0) {
    problems.push(...checkVersions(bundle, name));
  }
  if (problems.length) {
    console.error(`FAIL ${name}: ${problems.join('; ')}`);
    failures++;
  } else {
    console.log(`OK   ${name}: editor.uidesc + moduleinfo.json present, version matches version.json`);
  }
}

if (failures) {
  console.error(`\ncheck-bundle: ${failures} bundle(s) failed resource checks`);
  process.exit(1);
}
console.log(`\ncheck-bundle: all ${bundles.length} bundle(s) OK`);
