#!/usr/bin/env node
//
// lint-plugin-editor-baseline.js
// =============================================================================
// Every Krate plugin editor carries the same three shared views: the update
// banner, the Check for Updates button and the version label. All three are
// built by Krate::Plugins::createSharedEditorView
// (plugins/shared/src/ui/update_banner_view.h) from the controller's
// UpdateChecker and VERSION_STR.
//
// Why a lint: it is easy to miss on a new plugin and nothing else notices.
// Gradus and Membrum shipped without the update check. Profundum compiled
// the config but never created the checker. Disrumpo and Membrum showed no
// version at all, and Innexus's "VersionLabel" was a hand-made text label.
// Every one was found by a person looking, months later.
//
// The lint walks every directory under plugins/ (except shared), so a new
// plugin is checked from its first push. For each plugin it requires:
//   1. src/update/<plugin>_update_config.h pointing at versions.json;
//   2. a controller that creates the UpdateChecker, calls
//      createSharedEditorView, starts the check and the banner's polling on
//      editor open and stops the polling on close;
//   3. an editor.uidesc with exactly one UpdateBanner, CheckForUpdatesButton
//      and VersionLabel; the banner is its template's last view (it paints
//      over everything); no legacy tag-9999 version label;
//   4. a test calling Krate::TestSupport::openEditorAndCountSharedViews
//      (tests/test_helpers/shared_editor_views_probe.h), which proves the
//      built editor really holds the three views.
//
// Usage:
//   node tools/lint-plugin-editor-baseline.js               # report, exit 1 on violations
//   node tools/lint-plugin-editor-baseline.js --root <dir>  # lint another tree (self-test)
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const rootArg = process.argv.indexOf('--root');
const REPO_ROOT = rootArg > 0 ? path.resolve(process.argv[rootArg + 1]) : path.resolve(__dirname, '..');
const PLUGINS_DIR = path.join(REPO_ROOT, 'plugins');
const SHARED_VIEWS = ['UpdateBanner', 'CheckForUpdatesButton', 'VersionLabel'];

function read(file) {
  return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
}

function filesUnder(dir, exts) {
  if (!fs.existsSync(dir)) return [];
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...filesUnder(full, exts));
    else if (exts.includes(path.extname(entry.name))) out.push(full);
  }
  return out;
}

function count(text, needle) {
  let n = 0;
  for (let i = text.indexOf(needle); i !== -1; i = text.indexOf(needle, i + 1)) n++;
  return n;
}

function lintPlugin(plugin) {
  const dir = path.join(PLUGINS_DIR, plugin);
  const problems = [];

  // 1. The update config.
  const config = path.join(dir, 'src', 'update', `${plugin}_update_config.h`);
  if (!fs.existsSync(config)) {
    problems.push(`missing src/update/${plugin}_update_config.h (copy plugins/vorago/src/update/vorago_update_config.h)`);
  } else if (!read(config).includes('versions.json')) {
    problems.push(`src/update/${plugin}_update_config.h does not point at the versions.json endpoint`);
  }

  // 2. The controller.
  const controller = filesUnder(path.join(dir, 'src', 'controller'), ['.cpp', '.h']).map(read).join('\n');
  const controllerNeeds = [
    ['std::make_unique<Krate::Plugins::UpdateChecker>(', 'create the UpdateChecker in initialize()'],
    ['Krate::Plugins::createSharedEditorView(', 'build the shared views with Krate::Plugins::createSharedEditorView in createCustomView'],
    ['checkForUpdate(', 'start the update check in didOpen (updateChecker_->checkForUpdate(false))'],
    ['startPolling(', 'start the banner polling in didOpen'],
    ['stopPolling(', 'stop the banner polling in willClose'],
  ];
  for (const [needle, what] of controllerNeeds) {
    if (!controller.includes(needle)) problems.push(`controller does not ${what}`);
  }

  // 3. The editor layout.
  const uidescPath = path.join(dir, 'resources', 'editor.uidesc');
  if (!fs.existsSync(uidescPath)) {
    problems.push('missing resources/editor.uidesc');
  } else {
    const uidesc = read(uidescPath);
    for (const view of SHARED_VIEWS) {
      const n = count(uidesc, `custom-view-name="${view}"`);
      if (n !== 1) problems.push(`editor.uidesc has ${n} custom-view-name="${view}" (needs exactly 1)`);
    }
    const banner = uidesc.indexOf('custom-view-name="UpdateBanner"');
    if (banner !== -1) {
      const templateEnd = uidesc.indexOf('</template>', banner);
      const nextView = uidesc.indexOf('<view', banner);
      if (templateEnd === -1 || (nextView !== -1 && nextView < templateEnd)) {
        problems.push('the UpdateBanner is not the last view of its template (it must paint over everything)');
      }
    }
    if (/control-tag="9999"|<control-tag name="VersionLabel"/.test(uidesc)) {
      problems.push('editor.uidesc still has a legacy tag-9999 version label; use custom-view-name="VersionLabel"');
    }
  }

  // 4. The runtime test.
  const tests = filesUnder(path.join(dir, 'tests'), ['.cpp']);
  if (!tests.some((f) => read(f).includes('openEditorAndCountSharedViews('))) {
    problems.push('no test calls Krate::TestSupport::openEditorAndCountSharedViews (add the case to the editor lifecycle test)');
  }

  return problems;
}

function main() {
  if (!fs.existsSync(PLUGINS_DIR)) {
    console.error(`lint-plugin-editor-baseline: no plugins/ under ${REPO_ROOT}`);
    process.exit(2);
  }
  const plugins = fs
    .readdirSync(PLUGINS_DIR, { withFileTypes: true })
    .filter((e) => e.isDirectory() && e.name !== 'shared')
    .filter((e) => fs.existsSync(path.join(PLUGINS_DIR, e.name, 'CMakeLists.txt')))
    .map((e) => e.name)
    .sort();

  let failures = 0;
  for (const plugin of plugins) {
    const problems = lintPlugin(plugin);
    if (problems.length === 0) continue;
    failures += problems.length;
    console.error(`${plugin}:`);
    for (const p of problems) console.error(`  - ${p}`);
  }

  if (failures > 0) {
    console.error(`\nlint-plugin-editor-baseline: ${failures} problem(s). Every plugin editor carries the update banner, ` +
      'the Check for Updates button and the version label (createSharedEditorView).');
    process.exit(1);
  }
  console.log(`lint-plugin-editor-baseline: OK - ${plugins.length} plugins carry the update check and the version label`);
}

try {
  main();
} catch (err) {
  console.error(`lint-plugin-editor-baseline: internal error: ${err.stack || err}`);
  process.exit(2);
}
