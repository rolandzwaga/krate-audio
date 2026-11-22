#!/usr/bin/env node
// Asserts that a plugin's AUv2 Info.plist carries the plugin's own version.
//
//   node tools/check-au-plist.js [<plugin> ...]      (default: ruinae)
//
// For plugins/<p>/resources/au-info.plist.in (the committed template) it renders the
// @PROJECT_VERSION@ / @AU_COMPONENT_VERSION@ tokens the way cmake/KratePlugin.cmake does
// (krate_plugin_configure_generated_files) and requires:
//   CFBundleShortVersionString == version.json "version"
//   CFBundleVersion            == version.json "version"
//   AudioComponents <version>  == (major << 16) | (minor << 8) | patch  (= kAUcomponentVersion)
// If the generated plugins/<p>/resources/au-info.plist exists (any configured tree), it is held to the
// same values, so a stale or hand-edited generated file is caught too.
//
// Why: the AUv2 .component shipped a literal CFBundleVersion 1.0 and AudioComponents version 1 on every
// release (Ruinae audit R-191), while the AUv3 reported kAUcomponentVersion. The VST3 bundle's version
// stamping in the SDK does not reach the AUv2 plist. Node only; runs on any OS.
// Exit 0 when every plugin agrees, 1 with a report otherwise.
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

function expectedFor(plugin) {
  const vj = path.join(REPO_ROOT, 'plugins', plugin, 'version.json');
  if (!fs.existsSync(vj)) return { error: `no plugins/${plugin}/version.json` };
  const version = JSON.parse(fs.readFileSync(vj, 'utf8')).version;
  const m = /^(\d+)\.(\d+)\.(\d+)/.exec(version);
  if (!m) return { error: `version.json version "${version}" is not major.minor.patch` };
  const au = (Number(m[1]) << 16) | (Number(m[2]) << 8) | Number(m[3]);
  return { version, au: String(au) };
}

function plistValue(text, key, tag) {
  const re = new RegExp(`<key>${key}</key>\\s*<${tag}>([^<]*)</${tag}>`);
  const m = re.exec(text);
  return m ? m[1].trim() : null;
}

function problemsFor(label, text, exp) {
  const problems = [];
  const want = [
    ['CFBundleShortVersionString', 'string', exp.version],
    ['CFBundleVersion', 'string', exp.version],
    ['version', 'integer', exp.au],
  ];
  for (const [key, tag, value] of want) {
    const got = plistValue(text, key, tag);
    if (got !== value) {
      problems.push(`${label}: ${key} is ${got === null ? 'missing' : `"${got}"`}, expected "${value}"`);
    }
  }
  return problems;
}

function checkPlugin(plugin) {
  const exp = expectedFor(plugin);
  if (exp.error) return [exp.error];
  const dir = path.join(REPO_ROOT, 'plugins', plugin, 'resources');
  const tpl = path.join(dir, 'au-info.plist.in');
  const problems = [];
  if (!fs.existsSync(tpl)) {
    problems.push(`plugins/${plugin}/resources/au-info.plist.in is missing (the AUv2 plist must be generated from the plugin version)`);
  } else {
    const rendered = fs.readFileSync(tpl, 'utf8')
      .replace(/@PROJECT_VERSION@/g, exp.version)
      .replace(/@AU_COMPONENT_VERSION@/g, exp.au);
    problems.push(...problemsFor('au-info.plist.in', rendered, exp));
  }
  const generated = path.join(dir, 'au-info.plist');
  if (fs.existsSync(generated)) {
    problems.push(...problemsFor('generated au-info.plist', fs.readFileSync(generated, 'utf8'), exp));
  }
  return problems;
}

const plugins = process.argv.slice(2);
if (plugins.length === 0) plugins.push('ruinae');

let failures = 0;
for (const plugin of plugins) {
  const problems = checkPlugin(plugin);
  if (problems.length) {
    console.error(`FAIL ${plugin}: ${problems.join('; ')}`);
    failures++;
  } else {
    console.log(`OK   ${plugin}: AUv2 plist version matches version.json and kAUcomponentVersion`);
  }
}
if (failures) {
  console.error(`\ncheck-au-plist: ${failures} plugin(s) failed`);
  process.exit(1);
}
