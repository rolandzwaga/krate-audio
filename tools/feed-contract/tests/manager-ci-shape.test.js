'use strict';
require('./helpers/write-guard.js');

// SC-019: .github/workflows/manager-ci.yml keeps the Krate Manager Phase 4 shape (spec
// specs/krate-manager-phase4/spec.md FR-070 to FR-077, FR-080, FR-106, FR-107, SC-019, SC-021; plan §4.16, §4.17,
// §4.18, PD-11, PD-12, PD-20, PD-27).
// The workflow is read as text with the indentation-based helpers of workflow-shape.test.js (PD-20: copied in form,
// no shared helper file): 2-space job keys under `jobs:`, 6-space `- ` step items, 8-space step keys. No YAML
// library. checkManagerCi(files) is pure and returns every finding; the mutation subtests apply string edits to
// in-memory copies. Files are only read.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const util = require('node:util');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const WORKFLOW = path.join(REPO_ROOT, '.github', 'workflows', 'manager-ci.yml');
const RELEASE_MANAGER = path.join(REPO_ROOT, '.github', 'workflows', 'release-manager.yml');
const APP_DIR = path.join(REPO_ROOT, 'apps', 'krate-manager');
const REQUIRED = path.join(APP_DIR, 'tests', 'required-tests.json');
const PACKAGE_JSON = path.join(APP_DIR, 'package.json');

function readText(file) {
  return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
}

function readFiles() {
  return {
    workflow: readText(WORKFLOW),
    required: readText(REQUIRED),
    packageJson: readText(PACKAGE_JSON),
    releaseManager: fs.existsSync(RELEASE_MANAGER) ? readText(RELEASE_MANAGER) : null,
  };
}

// --- YAML-text helpers (form of workflow-shape.test.js:18-113).

function indentOf(line) {
  return line.length - line.trimStart().length;
}

function stripComment(value) {
  return value.replace(/\s+#.*$/, '').trim();
}

function unquote(value) {
  const m = /^'(.*)'$/.exec(value) || /^"(.*)"$/.exec(value);
  return m ? m[1] : value;
}

/** Names of the 2-space keys under the top-level `jobs:` mapping, in file order (empty when there is no jobs:). */
function jobNames(text) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => /^jobs:\s*$/.test(l));
  if (start < 0) return [];
  const names = [];
  for (const line of lines.slice(start + 1)) {
    if (/^\S/.test(line)) break;
    const m = /^ {2}([A-Za-z0-9_-]+):\s*$/.exec(line);
    if (m) names.push(m[1]);
  }
  return names;
}

/** The text of job `job` under `jobs:`: its key line up to the next line indented 0-2 spaces, or null. */
function jobBlock(text, job) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => /^jobs:\s*$/.test(l));
  if (start < 0) return null;
  const head = lines.findIndex((l, i) => i > start && l === `  ${job}:`);
  if (head < 0) return null;
  let end = head + 1;
  while (end < lines.length && !(lines[end].trim() !== '' && indentOf(lines[end]) <= 2)) end++;
  return lines.slice(head, end).join('\n');
}

/** The job's steps: each 6-space `- ` item, as {name, label, text, index}; label is the name or `uses: <action>`. */
function stepBlocks(jobText) {
  const lines = jobText.split('\n');
  const steps = [];
  let current = null;
  for (const line of lines) {
    if (/^ {6}- /.test(line)) {
      const named = /^ {6}- name:\s*(.*)$/.exec(line);
      const uses = /^ {6}- uses:\s*(.*)$/.exec(line);
      const name = named ? unquote(stripComment(named[1])) : '';
      current = { name, label: name || (uses ? `uses: ${stripComment(uses[1])}` : ''), lines: [line] };
      steps.push(current);
    } else if (current && line.trim() !== '' && indentOf(line) < 6) {
      current = null;
    } else if (current) {
      current.lines.push(line);
    }
  }
  return steps.map((s, index) => ({ name: s.name, label: s.label, text: s.lines.join('\n'), index }));
}

/** The value of the first `key:` line at exactly `indent` spaces in `block` (comments stripped), or null. */
function field(block, indent, key) {
  const re = new RegExp(`^ {${indent}}${key.replace(/[-]/g, '\\-')}:(.*)$`);
  for (const line of block.split('\n')) {
    const m = re.exec(line);
    if (m) return stripComment(m[1]);
  }
  return null;
}

/** The lines nested under the `key:` line at exactly `indent` spaces (its block value), or null. */
function nested(block, indent, key) {
  const lines = block.split('\n');
  const head = lines.findIndex((l) => new RegExp(`^ {${indent}}${key}:\\s*(\\|[-+]?)?\\s*$`).test(l));
  if (head < 0) return null;
  const out = [];
  for (const line of lines.slice(head + 1)) {
    if (line.trim() !== '' && indentOf(line) <= indent) break;
    out.push(line);
  }
  return out;
}

/** Trimmed, non-blank lines of a nested() result, or null. */
function trimmed(lines) {
  return lines === null ? null : lines.map((l) => l.trim()).filter((l) => l !== '');
}

/** A step's `run:` command text (block or single line), or ''. */
function runText(step) {
  const block = nested(step.text, 8, 'run');
  if (block) return block.join('\n');
  const single = /^ {8}run:\s*(.*)$/m.exec(step.text);
  return single ? single[1] : '';
}

/** A run: script's trimmed, non-blank lines. */
function scriptLines(step) {
  return runText(step).split('\n').map((l) => l.trim()).filter((l) => l !== '');
}

function count(text, needle) {
  return text.split(needle).length - 1;
}

// --- The Phase 4 shape (plan §4.16, binding).

const PATHS = ["- 'apps/krate-manager/**'", "- '.github/workflows/manager-ci.yml'", "- 'tools/lint-manager-version.js'"];
const TRIGGERS = ['push', 'pull_request', 'workflow_dispatch'];
const CONCURRENCY = ['group: manager-ci-${{ github.ref }}', 'cancel-in-progress: true'];
const STRATEGY = ['fail-fast: false', 'matrix:', 'os: [windows-latest, macos-latest, ubuntu-24.04]'];
const DEFAULTS = ['run:', 'shell: bash', 'working-directory: apps/krate-manager'];
const APP = 'target/release/bundle/macos/Krate Manager.app';
const PLIST = `${APP}/Contents/Info.plist`;

// Plan §4.16: the steps in order (unnamed `uses:` steps by their action).
const STEPS = [
  'uses: actions/checkout@v6',
  'uses: actions/setup-node@v4',
  'Linux system packages',
  'Rust toolchain',
  'uses: Swatinem/rust-cache@v2',
  'npm ci',
  'version lint',
  'cargo fmt',
  'clippy',
  'clippy dev-feed',
  'clippy-guard',
  'cargo test',
  'typecheck',
  'lint',
  'vitest',
  'required tests ran',
  'tauri build',
  'build warnings',
  'dev-feed absent',
  'artifact version',
  'plist: LSMinimumSystemVersion',
  'plist: no LSFileQuarantineEnabled',
  'codesign verify',
  'codesign ad-hoc',
  'Checkout unchanged',
];

const MACOS_STEPS = [
  'plist: LSMinimumSystemVersion',
  'plist: no LSFileQuarantineEnabled',
  'codesign verify',
  'codesign ad-hoc',
];

// The steps that each print a ::notice on pass and a ::error on fail (D21).
const ASSERTION_STEPS = [
  'version lint',
  'clippy dev-feed',
  'clippy-guard',
  'required tests ran',
  'build warnings',
  'dev-feed absent',
  'artifact version',
  ...MACOS_STEPS,
  'Checkout unchanged',
];

// Plan §4.16: the command lines each run: step must hold (trimmed, exact).
const COMMANDS = {
  'Linux system packages': [
    'sudo apt-get update',
    'sudo apt-get install -y libwebkit2gtk-4.1-dev build-essential curl wget file libxdo-dev libssl-dev' +
      ' libayatana-appindicator3-dev librsvg2-dev',
  ],
  'Rust toolchain': ['rustup toolchain install', 'rustc --version', 'cargo clippy --version'],
  'npm ci': ['npm ci'],
  'version lint': ['if node ../../tools/lint-manager-version.js; then'],
  'cargo fmt': ['cargo fmt --all --check'],
  clippy: ['cargo clippy --workspace --all-targets --locked -- -D warnings'],
  'clippy dev-feed': ['if cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings; then'],
  'clippy-guard': [
    'out="$RUNNER_TEMP/guard.txt"',
    'rc=0',
    'CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path tests/fixtures/clippy-guard/Cargo.toml' +
      ' --all-targets --locked -- -D warnings > "$out" 2>&1 || rc=$?',
    "total=$(grep -c '^error: use of a disallowed method `' \"$out\" || true)",
    "from_os=$(grep -c '^error: use of a disallowed method `krate_manager_lib::os_roots::FromOs::from_os`'" +
      ' "$out" || true)',
    "new=$(grep -c '^error: use of a disallowed method `krate_manager_core::roots::Roots::new`' \"$out\" || true)",
    "envvar=$(grep -c '^error: use of a disallowed method `std::env::var`' \"$out\" || true)",
    "lint=$(grep -cE 'disallowed[-_]methods' \"$out\" || true)",
    "other=$(grep -E '^error' \"$out\" | grep -vcE '^error: (use of a disallowed method `|could not compile `)'" +
      ' || true)',
    'if [ "$rc" != "0" ] && [ "$total" = "3" ] && [ "$from_os" = "1" ] && [ "$new" = "1" ] &&' +
      ' [ "$envvar" = "1" ] && [ "$lint" -ge 1 ] && [ "$other" = "0" ]; then',
  ],
  'cargo test': ['cargo test --workspace --locked 2>&1 | tee "$RUNNER_TEMP/cargo-test.txt"'],
  typecheck: ['npm run typecheck'],
  lint: ['npm run lint'],
  vitest: ['npm test -- --reporter=default --reporter=json --outputFile.json="$RUNNER_TEMP/vitest.json"'],
  'required tests ran': [
    'node - "$RUNNER_TEMP/cargo-test.txt" "$RUNNER_TEMP/vitest.json" "$RUNNER_OS" <<\'REQUIRED\'',
    'const key = { Windows: "windows", macOS: "macos", Linux: "linux" }[runnerOs];',
    'const req = JSON.parse(fs.readFileSync("tests/required-tests.json", "utf8"));',
    'const names = [...req.cargo.all, ...(key ? req.cargo[key] : [])];',
    'const m = /^test (\\S+)(?: - should panic)? \\.\\.\\. ok$/.exec(raw);',
    'for (const n of names) { const c = passed.get(n) || 0; if (c !== 1) bad.push(`cargo ${n}: ${c} passing' +
      ' line(s), want 1`); }',
    'const c = results.filter((r) => r.title === t && r.status === "passed").length;',
    'if (bad.length) process.exit(1);',
    'REQUIRED',
  ],
  'tauri build': ['npm run tauri build 2>&1 | tee "$RUNNER_TEMP/tauri-build.txt"'],
  'build warnings': [
    'clean="$RUNNER_TEMP/tauri-build.clean.txt"',
    "esc=$(printf '\\033')",
    'sed "s/${esc}\\[[0-9;]*m//g" "$RUNNER_TEMP/tauri-build.txt" > "$clean"',
    "finished=$(grep -cE '^ *Finished `release` profile' \"$clean\" || true)",
    "n=$(grep -c '^warning:' \"$clean\" || true)",
    'if [ "$finished" -ge 1 ] && [ "$n" = "0" ]; then',
  ],
  'dev-feed absent': [
    'case "$RUNNER_OS" in Windows) bin=target/release/krate-manager.exe ;; *) bin=target/release/krate-manager ;; esac',
    "marker=$(grep -cF 'dev-feed-url.txt' src-tauri/src/dev_feed.rs || true)",
    "hits=$(grep -acF 'dev-feed-url.txt' \"$bin\" || true)",
    'if [ "$marker" -ge 1 ] && [ "$hits" = "0" ]; then',
  ],
  'artifact version': [
    'want=$(node -p "require(\'./version.json\').version")',
    'got=$(powershell -NoProfile -Command "(Get-Item \'target/release/krate-manager.exe\').VersionInfo.ProductVersion"' +
      " | tr -d '\\r')",
    'nsis="target/release/bundle/nsis/Krate Manager_${want}_x64-setup.exe"',
    `got=$(plutil -extract CFBundleShortVersionString raw "${PLIST}")`,
    'debs=(target/release/bundle/deb/*.deb)',
    'if [ "${#debs[@]}" = "1" ] && [ -f "${debs[0]}" ]; then got=$(dpkg-deb -f "${debs[0]}" Version);' +
      ' else got="debs=${#debs[@]}"; fi',
    'if [ "$got" = "$want" ]; then',
  ],
  'plist: LSMinimumSystemVersion': [
    `v=$(plutil -extract LSMinimumSystemVersion raw "${PLIST}")`,
    'if [ "$v" = "15.0" ]; then echo "::notice title=plist: LSMinimumSystemVersion::15.0"; else echo' +
      ' "::error title=plist: LSMinimumSystemVersion::$v"; exit 1; fi',
  ],
  'plist: no LSFileQuarantineEnabled': [
    `if ! out=$(plutil -p "${PLIST}"); then`,
    'if grep -q LSFileQuarantineEnabled <<<"$out"; then',
  ],
  'codesign verify': [`if codesign --verify --deep --strict -v "${APP}"; then`],
  'codesign ad-hoc': [
    `info=$(codesign -dv "${APP}" 2>&1) || true`,
    "if grep -q 'Signature=adhoc' <<<\"$info\"; then",
  ],
  'Checkout unchanged': ['if [ -z "$(git status --porcelain)" ]; then', 'git status --porcelain'],
};

// The steps that run on one OS only, with their `if:`; every other step runs on every leg.
const GATED = new Map([
  ['Linux system packages', "runner.os == 'Linux'"],
  ...MACOS_STEPS.map((n) => [n, "runner.os == 'macOS'"]),
]);

// FR-076 and SC-021: no flag that serialises a test run.
const SERIAL_FLAGS = [
  '--test-threads',
  '--no-file-parallelism',
  '--fileParallelism=false',
  'fileParallelism: false',
  '--maxWorkers',
  'maxWorkers:',
  '--pool',
  '-j 1',
  '--test-concurrency=1',
  '--test-isolation=none',
];
// FR-076: nothing elevated (sudo only for the Linux apt packages) and nothing launched.
const ELEVATION = ['Start-Process', 'RunAs', 'runas ', 'msiexec'];
// PD-27: a match test that pipes into grep -q (any flag cluster holding q).
const PIPE_GREP_Q = /\|\s*grep(?:\s+-[A-Za-z]+)*\s+-[A-Za-z]*q/;
const DEV_FEED_STEPS = ['clippy dev-feed', 'dev-feed absent'];

// Plan §4.18, verbatim.
const EXPECTED_REQUIRED = {
  cargo: {
    all: [
      'roots::tests::new_validates_and_touches_nothing',
      'roots::tests::with_configured_plugin_root_cases',
      'roots::tests::logs_dir_is_state_logs',
      'roots::tests::sandboxed_roots_refuse_an_outside_configured_root',
      'platform::tests::default_roots_touches_nothing',
      'test_support::tests::check_under_temp_cases',
      'test_support::tests::check_under_temp_symlink_escape',
      'test_support::tests::temp_roots_in_outside_panics',
      'test_support::tests::temp_roots_in_refusal_creates_nothing',
      'test_support::tests::temp_roots_on_temp_dir_itself_panics',
      'test_support::tests::temp_roots_inside_temp_ok',
      'core_sources_hold_no_production_literal',
      'injection_scanner_reports_planted_violations',
      'core_dependencies_are_the_fr007_set',
      'src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots',
      'disallowed_methods_expectations_are_the_seven_d6_sites',
      'settings::tests::round_trip_values',
      'settings::tests::written_bytes_are_d10',
      'settings::tests::missing_file_gives_defaults_and_writes_nothing',
      'settings::tests::plugin_root_or_default_uses_vst3_user',
      'settings::tests::load_rejections_keep_defaults_and_file',
      'settings::tests::save_rejects_invalid_values_and_writes_nothing',
      'settings::tests::save_refuses_future_version_file',
      'settings::tests::save_overwrites_corrupt_and_invalid_files',
      'settings::tests::fault_at_each_step_keeps_previous_file',
      'settings::tests::abort_leaves_previous_file_and_stray_temp_is_ignored',
      'real_src_tauri_config_passes_the_guard',
      'planted_config_changes_fail_the_guard',
      'frontend_source_scan_reports_planted_strings',
      'real_app_metadata_passes_the_guard',
      'planted_app_metadata_changes_fail_the_guard',
      'navigation::tests::is_app_origin_cases',
      'navigation::tests::app_origin_cases',
      'helper_stub_exits_2_with_one_line',
      'context::tests::assemble_cases',
      'state_lock::tests::second_acquire_is_held_while_first_lives',
      'state_lock::tests::acquire_creates_state_dir_and_lock_file_only',
      'state_lock::tests::lock_file_survives_drop',
      'state_lock::tests::acquire_io_error_is_named',
      'context::tests::assemble_locked_instance',
      'context::tests::startup_notice_cases',
      'context::tests::configured_root_cases',
      'context::tests::reassemble_cases',
      'context::tests::sandbox_holds_through_assemble_and_reassemble',
      'log_sink::tests::format_line_cases',
      'log_sink::tests::rotating_log_cases',
      'context::tests::startup_errors_are_logged',
      'context::tests::parse_dev_feed_url_cases',
    ],
    windows: ['platform::windows::tests::default_roots_from_env'],
    macos: ['platform::macos::tests::default_roots_from_env'],
    linux: ['platform::linux::tests::default_roots_from_env'],
  },
  vitest: [
    'shows the title and the version',
    'shows "version unavailable" when app_version fails',
    'shows only the notice when another instance is running',
  ],
};

function parseJson(text, where, bad) {
  try {
    return JSON.parse(text);
  } catch (e) {
    bad.push(`${where}: not JSON (${e.message})`);
    return null;
  }
}

function checkWorkflowTop(wf, bad) {
  const on = nested(wf, 0, 'on');
  if (on === null) {
    bad.push('manager-ci.yml: no top-level on: block');
  } else {
    const onText = on.join('\n');
    const triggers = on.map((l) => /^ {2}([A-Za-z0-9_-]+):/.exec(l)).filter(Boolean).map((m) => m[1]);
    if (!util.isDeepStrictEqual(triggers, TRIGGERS)) bad.push(`on: triggers ${JSON.stringify(triggers)}`);
    for (const trig of ['push', 'pull_request']) {
      const block = nested(onText, 2, trig);
      const paths = block === null ? null : trimmed(nested(block.join('\n'), 4, 'paths'));
      if (!util.isDeepStrictEqual(paths, PATHS)) bad.push(`on.${trig}.paths ${JSON.stringify(paths)}`);
    }
  }
  const perms = trimmed(nested(wf, 0, 'permissions'));
  if (!util.isDeepStrictEqual(perms, ['contents: read'])) bad.push(`permissions ${JSON.stringify(perms)}`);
  if (/\b[a-z-]+:\s*write\b/.test(wf)) bad.push('manager-ci.yml: a write permission');
  const conc = trimmed(nested(wf, 0, 'concurrency'));
  if (!util.isDeepStrictEqual(conc, CONCURRENCY)) bad.push(`concurrency ${JSON.stringify(conc)}`);
  if (wf.includes('ubuntu-latest')) bad.push('manager-ci.yml: an ubuntu-latest leg (D16 pins ubuntu-24.04)');
  if (wf.includes('secrets.')) bad.push('manager-ci.yml: a secrets. reference');
  if (/^\s*environment\s*:/m.test(wf)) bad.push('manager-ci.yml: an environment: key');
  for (const f of SERIAL_FLAGS) if (wf.includes(f)) bad.push(`manager-ci.yml: serial test flag ${f} (FR-076)`);
  if (wf.includes('\\x1b')) bad.push('manager-ci.yml: a \\x1b escape (PD-11: build the ESC with printf)');
  if (count(wf, 'tests/required-tests.json') !== 1) bad.push('manager-ci.yml: tests/required-tests.json not read once');
}

function checkJob(wf, bad) {
  const jobs = jobNames(wf);
  if (!util.isDeepStrictEqual(jobs, ['check'])) bad.push(`jobs ${JSON.stringify(jobs)}`);
  const block = jobBlock(wf, 'check');
  if (block === null) {
    bad.push('manager-ci.yml: no check job');
    return null;
  }
  if (field(block, 4, 'name') !== 'check (${{ matrix.os }})') bad.push(`check.name ${field(block, 4, 'name')}`);
  if (field(block, 4, 'runs-on') !== '${{ matrix.os }}') bad.push(`check.runs-on ${field(block, 4, 'runs-on')}`);
  const strategy = trimmed(nested(block, 4, 'strategy'));
  if (!util.isDeepStrictEqual(strategy, STRATEGY)) bad.push(`check.strategy ${JSON.stringify(strategy)}`);
  const defaults = trimmed(nested(block, 4, 'defaults'));
  if (!util.isDeepStrictEqual(defaults, DEFAULTS)) bad.push(`check.defaults ${JSON.stringify(defaults)}`);
  return stepBlocks(block);
}

function checkSteps(wf, steps, bad) {
  const labels = steps.map((s) => s.label);
  if (!util.isDeepStrictEqual(labels, STEPS)) {
    const missing = STEPS.filter((n) => !labels.includes(n));
    const extra = labels.filter((n) => !STEPS.includes(n));
    bad.push(`steps out of the plan §4.16 order; missing ${JSON.stringify(missing)}, extra ${JSON.stringify(extra)}`);
  }
  const byName = new Map();
  for (const s of steps) {
    if (byName.has(s.label)) bad.push(`step "${s.label}" appears more than once`);
    byName.set(s.label, s);
  }
  const step = (n) => byName.get(n) || null;

  const node = step('uses: actions/setup-node@v4');
  if (node && field(node.text, 10, 'node-version') !== "'24'") bad.push('setup-node: node-version is not \'24\'');
  const cache = step('uses: Swatinem/rust-cache@v2');
  if (cache && field(cache.text, 10, 'workspaces') !== 'apps/krate-manager -> target') {
    bad.push('rust-cache: workspaces is not apps/krate-manager -> target');
  }

  for (const s of steps) {
    const run = runText(s);
    const gate = field(s.text, 8, 'if');
    const want = GATED.get(s.label) ?? null;
    if (gate !== want) bad.push(`${s.label}: if: ${gate}, want ${want}`);
    if (run === '') continue;
    const shell = field(s.text, 8, 'shell');
    if (shell !== null && shell !== 'bash') bad.push(`${s.label}: shell ${shell}`);
    if (scriptLines(s)[0] !== 'set -euo pipefail') bad.push(`${s.label}: the script does not start set -euo pipefail`);
    if (run.includes('${{')) bad.push(`${s.label}: a \${{ }} expression inside run: text`);
    if (PIPE_GREP_Q.test(run)) bad.push(`${s.label}: a pipe into grep -q (PD-27)`);
    if (run.includes('sudo') && s.label !== 'Linux system packages') bad.push(`${s.label}: sudo (FR-076)`);
    for (const e of ELEVATION) if (run.includes(e)) bad.push(`${s.label}: ${e.trim()} (FR-076)`);
  }

  for (const [name, lines] of Object.entries(COMMANDS)) {
    const s = step(name);
    if (!s) {
      bad.push(`no step "${name}"`);
      continue;
    }
    const have = new Set(scriptLines(s));
    for (const l of lines) if (!have.has(l)) bad.push(`${name}: no line ${JSON.stringify(l)}`);
  }

  for (const name of ASSERTION_STEPS) {
    const s = step(name);
    if (!s) continue;
    const run = runText(s);
    if (!run.includes('::notice')) bad.push(`${name}: no ::notice`);
    if (!run.includes('::error')) bad.push(`${name}: no ::error`);
  }

  const vitest = step('vitest');
  const required = step('required tests ran');
  if (!required) bad.push('no step "required tests ran" (FR-077)');
  else if (!vitest || required.index !== vitest.index + 1) bad.push('"required tests ran" does not follow vitest');

  const checkoutStep = step('Checkout unchanged');
  if (checkoutStep && field(checkoutStep.text, 8, 'working-directory') !== '.') {
    bad.push('Checkout unchanged: not run from the repository root');
  }
  if (steps.length > 0 && steps[steps.length - 1].label !== 'Checkout unchanged') {
    bad.push('Checkout unchanged is not the last step (FR-075)');
  }

  // FR-106, FR-107: dev-feed only inside the two named steps, never on a tauri build line.
  const inSteps = DEV_FEED_STEPS.reduce((n, name) => n + (step(name) ? count(step(name).text, 'dev-feed') : 0), 0);
  for (const name of DEV_FEED_STEPS) if (!step(name)) bad.push(`no step "${name}" (FR-071, FR-106, FR-107)`);
  if (count(wf, 'dev-feed') !== inSteps) bad.push('manager-ci.yml: dev-feed outside the two named steps');
  for (const line of wf.split('\n')) {
    if (line.includes('tauri build') && line.includes('dev-feed')) bad.push(`dev-feed on a tauri build line: ${line.trim()}`);
  }
}

function checkPackageJson(text, bad) {
  const pkg = parseJson(text, 'package.json', bad);
  if (pkg === null) return;
  for (const [name, cmd] of Object.entries(pkg.scripts || {})) {
    for (const f of SERIAL_FLAGS) if (String(cmd).includes(f)) bad.push(`package.json scripts.${name}: ${f} (FR-076)`);
    if (String(cmd).includes('dev-feed')) bad.push(`package.json scripts.${name}: dev-feed (FR-106)`);
  }
}

function checkRequired(text, bad) {
  const req = parseJson(text, 'required-tests.json', bad);
  if (req === null) return;
  for (const os of ['windows', 'macos', 'linux']) {
    const name = `platform::${os}::tests::default_roots_from_env`;
    const list = req.cargo && req.cargo[os];
    if (!Array.isArray(list) || !list.includes(name)) bad.push(`required-tests.json: cargo.${os} lacks ${name}`);
  }
  if (!util.isDeepStrictEqual(req, EXPECTED_REQUIRED)) bad.push('required-tests.json differs from plan §4.18');
}

/** Every finding on the given file texts; [] when the shape holds. */
function checkManagerCi(files) {
  const bad = [];
  const wf = files.workflow.replace(/\r\n/g, '\n');
  checkWorkflowTop(wf, bad);
  const steps = checkJob(wf, bad);
  if (steps) checkSteps(wf, steps, bad);
  checkPackageJson(files.packageJson, bad);
  if (files.releaseManager !== null && files.releaseManager.includes('dev-feed')) {
    bad.push('release-manager.yml: dev-feed (FR-107)');
  }
  checkRequired(files.required, bad);
  return bad;
}

// --- Mutations (SC-019 and the two plan-review cases), as string edits on in-memory copies.

/** `text` with the one occurrence of `from` replaced; throws when `from` does not occur exactly once. */
function replaceOnce(text, from, to) {
  const n = count(text, from);
  if (n !== 1) throw new Error(`mutation anchor occurs ${n} time(s): ${JSON.stringify(from)}`);
  return text.replace(from, () => to);
}

/** `text` without the step named `name` (its `- name:` line up to the next step item or the end). */
function removeStep(text, name) {
  const lines = text.split('\n');
  const head = lines.findIndex((l) => {
    const m = /^ {6}- name:\s*(.*)$/.exec(l);
    return m !== null && unquote(stripComment(m[1])) === name;
  });
  if (head < 0) throw new Error(`mutation: no step ${name}`);
  let end = head + 1;
  while (end < lines.length && !/^ {6}- /.test(lines[end]) && !(lines[end].trim() !== '' && indentOf(lines[end]) < 6)) {
    end++;
  }
  return [...lines.slice(0, head), ...lines.slice(end)].join('\n');
}

const onWorkflow = (edit) => (files) => ({ ...files, workflow: edit(files.workflow) });

const MUTATIONS = [
  [
    'an ubuntu-latest leg',
    onWorkflow((w) => replaceOnce(w, 'ubuntu-24.04]', 'ubuntu-latest]')),
    /ubuntu-latest/,
  ],
  ['no clippy-guard step', onWorkflow((w) => removeStep(w, 'clippy-guard')), /no step "clippy-guard"/],
  [
    'cargo test -- --test-threads=1',
    onWorkflow((w) => replaceOnce(w, 'cargo test --workspace --locked 2>&1', 'cargo test --workspace --locked -- --test-threads=1 2>&1')),
    /--test-threads/,
  ],
  [
    'a secrets. reference',
    onWorkflow((w) =>
      replaceOnce(w, '      - name: npm ci\n', '      - name: npm ci\n        env:\n          TOKEN: ${{ secrets.GITHUB_TOKEN }}\n'),
    ),
    /secrets\./,
  ],
  [
    'permissions: contents: write',
    onWorkflow((w) => replaceOnce(w, 'permissions:\n  contents: read', 'permissions:\n  contents: write')),
    /permissions/,
  ],
  ['the macOS codesign verify step removed', onWorkflow((w) => removeStep(w, 'codesign verify')), /codesign verify/],
  ['the macOS codesign ad-hoc step removed', onWorkflow((w) => removeStep(w, 'codesign ad-hoc')), /codesign ad-hoc/],
  [
    'the required tests ran step removed',
    onWorkflow((w) => removeStep(w, 'required tests ran')),
    /no step "required tests ran"/,
  ],
  ['the clippy dev-feed step removed', onWorkflow((w) => removeStep(w, 'clippy dev-feed')), /no step "clippy dev-feed"/],
  ['the dev-feed absent step removed', onWorkflow((w) => removeStep(w, 'dev-feed absent')), /no step "dev-feed absent"/],
  [
    '--features dev-feed on the tauri build line',
    onWorkflow((w) => replaceOnce(w, 'npm run tauri build 2>&1', 'npm run tauri build -- --features dev-feed 2>&1')),
    /dev-feed on a tauri build line/,
  ],
  [
    'required-tests.json without platform::macos::tests::default_roots_from_env under macos',
    (files) => {
      const req = JSON.parse(files.required);
      req.cargo.macos = req.cargo.macos.filter((n) => n !== 'platform::macos::tests::default_roots_from_env');
      return { ...files, required: JSON.stringify(req, null, 2) };
    },
    /cargo\.macos lacks platform::macos::tests::default_roots_from_env/,
  ],
  [
    'plan review: codesign ad-hoc as a pipe into grep -q',
    onWorkflow((w) =>
      replaceOnce(
        w,
        `info=$(codesign -dv "${APP}" 2>&1) || true\n          if grep -q 'Signature=adhoc' <<<"$info"; then`,
        `app="${APP}"\n          if codesign -dv "$app" 2>&1 | grep -q 'Signature=adhoc'; then`,
      ),
    ),
    /codesign ad-hoc: a pipe into grep -q/,
  ],
  [
    'plan review: the build warnings strip as sed with \\x1b',
    onWorkflow((w) => replaceOnce(w, 'sed "s/${esc}\\[[0-9;]*m//g"', "sed 's/\\x1b\\[[0-9;]*m//g'")),
    /\\x1b/,
  ],
];

test('manager-ci.yml keeps the Phase 4 shape', async (t) => {
  const files = readFiles();

  await t.test('the real files give no finding', () => {
    assert.deepEqual(checkManagerCi(files), []);
  });

  await t.test('a CRLF copy of the real files gives no finding', () => {
    const crlf = { ...files, workflow: files.workflow.replace(/\n/g, '\r\n') };
    assert.deepEqual(checkManagerCi(crlf), []);
  });

  await t.test('a release-manager.yml naming dev-feed is a finding', () => {
    const errs = checkManagerCi({ ...files, releaseManager: 'run: cargo build --features dev-feed\n' });
    assert.ok(errs.some((e) => /release-manager\.yml: dev-feed/.test(e)), JSON.stringify(errs));
  });

  await t.test('a package.json script with a serial flag or dev-feed is a finding', () => {
    for (const cmd of ['vitest run --no-file-parallelism', 'tauri build --features dev-feed']) {
      const pkg = JSON.parse(files.packageJson);
      pkg.scripts.test = cmd;
      const errs = checkManagerCi({ ...files, packageJson: JSON.stringify(pkg) });
      assert.ok(errs.some((e) => e.startsWith('package.json scripts.test:')), `${cmd}: ${JSON.stringify(errs)}`);
    }
  });

  for (const [name, mutate, expected] of MUTATIONS) {
    await t.test(`mutation: ${name}`, () => {
      const mutated = mutate(files);
      assert.notDeepEqual(mutated, files, 'the mutation changed nothing');
      const errs = checkManagerCi(mutated);
      assert.ok(errs.length > 0, 'no finding');
      assert.ok(errs.some((e) => expected.test(e)), `no finding matches ${expected}: ${JSON.stringify(errs)}`);
    });
  }
});
