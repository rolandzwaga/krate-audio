#!/usr/bin/env node
// Asserts that a built VST3 bundle contains the resources the SDK hosting
// validator does NOT check — the ones whose absence ships a blank-UI / crashing
// plugin (e.g. a hand-copied bundle missing Resources/editor.uidesc).
//
//   node tools/check-bundle.js <bundle.vst3> [<bundle2.vst3> ...]
//
// Exit 0 if every bundle has a non-empty editor.uidesc and moduleinfo.json;
// exit 1 (with a report) otherwise. Node only.

const fs = require('fs');
const path = require('path');

const REQUIRED = [
  ['Contents', 'Resources', 'editor.uidesc'],
  ['Contents', 'Resources', 'moduleinfo.json'],
];

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
  if (problems.length) {
    console.error(`FAIL ${name}: ${problems.join('; ')}`);
    failures++;
  } else {
    console.log(`OK   ${name}: editor.uidesc + moduleinfo.json present`);
  }
}

if (failures) {
  console.error(`\ncheck-bundle: ${failures} bundle(s) failed resource checks`);
  process.exit(1);
}
console.log(`\ncheck-bundle: all ${bundles.length} bundle(s) OK`);
