'use strict';
require('./helpers/write-guard.js');

// SC-019: .github/workflows/manager-ci.yml keeps the Krate Manager Phase 4 shape (spec
// specs/krate-manager-phase4/spec.md FR-070 to FR-077, FR-080, FR-106, FR-107, SC-019, SC-021; plan §4.16, §4.17,
// §4.18, PD-11, PD-12, PD-20, PD-27), plus Phase 5's additions (specs/krate-manager-phase5/spec.md FR-083 to FR-085;
// plan §4.12, §4.13, SI-4; tasks TA-8): the tools/feed-contract path, the roster absent step, the live-network ban.
// Phase 10 (specs/krate-manager-phase10/spec.md FR-100 to FR-109, FR-111, SC-017 to SC-020, SC-031; plan §4.8.4,
// §8.2, PD-20 to PD-22, PD-27, SI-2, SI-15, SI-18, SI-27, SI-29): the check job's helper steps, the `changes` job and
// the two path-gated elevated jobs `helper-elevated` and `inno-locked-file`, checked by the `elevated jobs are
// path-gated` subtest.
// Phase 11 (specs/krate-manager-phase11/spec.md FR-073, FR-083, FR-084, FR-091; plan §4.11, PD-32, PD-33, PD-38): the
// four check steps `clippy updater-e2e`, `clippy updater-refusals`, `updater refusals`, `updater-e2e absent`.
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
  return unpinActions(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n'));
}

// Third-party actions are pinned by commit SHA with the release as a comment (`owner/repo@<40 hex> # vX.Y.Z`, kept
// current by Dependabot). Read as `owner/repo@pinned`: a moved pin stays green, an unpinned `@v6` reference fails.
function unpinActions(text) {
  return text.replace(/@[0-9a-f]{40} # v\d+(?:\.\d+)*/g, '@pinned');
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

// Phase 5 plan §4.12 (SI-4): the feed-contract tools join the path filter.
const PATHS = [
  "- 'apps/krate-manager/**'",
  "- '.github/workflows/manager-ci.yml'",
  "- 'tools/lint-manager-version.js'",
  "- 'tools/feed-contract/**'",
];
const TRIGGERS = ['push', 'pull_request', 'workflow_dispatch'];
const CONCURRENCY = ['group: manager-ci-${{ github.ref }}', 'cancel-in-progress: true'];
const STRATEGY = ['fail-fast: false', 'matrix:', 'os: [windows-latest, macos-latest, ubuntu-24.04]'];
const DEFAULTS = ['run:', 'shell: bash', 'working-directory: apps/krate-manager'];
const APP = 'target/release/bundle/macos/Krate Manager.app';
const PLIST = `${APP}/Contents/Info.plist`;

// Plan §4.16: the steps in order (unnamed `uses:` steps by their action).
const STEPS = [
  'uses: actions/checkout@pinned',
  'uses: actions/setup-node@pinned',
  'Linux system packages',
  'Rust toolchain',
  'uses: Swatinem/rust-cache@pinned',
  'npm ci',
  'version lint',
  'cargo fmt',
  'clippy',
  'clippy dev-feed',
  'clippy ci-temp-roots',
  'clippy manual-elevation example',
  'clippy updater-e2e',
  'clippy-guard',
  'clippy updater-refusals',
  'cargo test',
  'test ci-temp-roots',
  'updater refusals',
  'core dependency tree',
  'typecheck',
  'lint',
  'node --test',
  'vitest',
  'required tests ran',
  'tauri build',
  'build warnings',
  'helper bundled',
  'ci-temp-roots absent',
  'dev-feed absent',
  'updater-e2e absent',
  'roster absent',
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
  'clippy ci-temp-roots',
  'clippy manual-elevation example',
  'clippy updater-e2e',
  'clippy-guard',
  'clippy updater-refusals',
  'test ci-temp-roots',
  'updater refusals',
  'core dependency tree',
  'node --test',
  'required tests ran',
  'build warnings',
  'helper bundled',
  'ci-temp-roots absent',
  'dev-feed absent',
  'updater-e2e absent',
  'roster absent',
  'artifact version',
  ...MACOS_STEPS,
  'Checkout unchanged',
];

// Phase 11 PD-33 (tasks TA-22, T017): written from specs/krate-manager-phase11/evidence/d9-windows.txt, which reads
// `D9 WINDOWS LOADS`. true: `updater refusals` has no if: (all three legs); false (`D9 WINDOWS LOAD FAILURE ...`): it
// has exactly `if: runner.os != 'Windows'`.
const HARNESS_ON_WINDOWS = true;
// Phase 11 FR-082 (SI-35; tasks T014): the 27 harness lines, the 26 scenarios plus `confinement`, sorted.
const HARNESS_NAMES = [
  'api-host',
  'busy',
  'busy-after-download',
  'confinement',
  'corrupt-manifest',
  'deb-install',
  'downgrade',
  'endpoint-latest-path',
  'failed-install-clears',
  'good',
  'latest-path',
  'map-errors',
  'no-platform',
  'offline',
  'other-key',
  'production-inputs',
  'recheck-replaces',
  'redirect-loop',
  'redirect-to-api-host',
  'redirect-to-http',
  'replayed-old',
  'rsign-format',
  'same-version',
  'socks-proxy',
  'stall',
  'tampered',
  'unversioned',
];
const HARNESS_MANIFEST = 'tests/fixtures/updater-refusals/Cargo.toml';
const E2E_MARKER = 'krate-e2e-marker-v1';

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
  // Phase 10 FR-103a, FR-105 (plan §4.8.4).
  'clippy ci-temp-roots': [
    'if cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings; then',
  ],
  'clippy manual-elevation example': [
    'if cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings;' +
      ' then',
  ],
  // Phase 11 PD-32 (FR-073, FR-083): compiled and linted with dummy KRATE_E2E_* values, never run.
  'clippy updater-e2e': [
    'if cargo clippy -p krate-manager --all-targets --features updater-e2e --locked -- -D warnings; then',
  ],
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
  // Phase 11 FR-083: the harness is clippy- and fmt-clean on all three legs, whatever D9 measured (PD-33).
  'clippy updater-refusals': [
    `CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path ${HARNESS_MANIFEST} --all-targets --locked --` +
      ' -D warnings || rc=$?',
    `cargo fmt --check --manifest-path ${HARNESS_MANIFEST} || frc=$?`,
    'if [ "$rc" = "0" ] && [ "$frc" = "0" ]; then',
  ],
  'cargo test': ['cargo test --workspace --locked 2>&1 | tee "$RUNNER_TEMP/cargo-test.txt"'],
  // Phase 10 FR-103a: its own log, so the required tests ran counts are unaffected.
  'test ci-temp-roots': [
    'log="$RUNNER_TEMP/cargo-test-ci-roots.txt"',
    'cargo test -p krate-manager-helper --features ci-temp-roots --locked 2>&1 | tee "$log"',
    "n=$(tr -d '\\r' < \"$log\" | grep -cxF 'test cli::tests::ci_roots_file_is_read ... ok' || true)",
    'if [ "$n" = "1" ]; then',
  ],
  // Phase 11 FR-084 (FR-082, FR-081 (f), PD-38): every name ok once, the summary, then both plants exit 1 with their
  // code as their only stdout line and no `ok ` line.
  'updater refusals': [
    'case "$RUNNER_OS" in Windows) bin=target/debug/updater-refusals.exe ;; *) bin=target/debug/updater-refusals ;;' +
      ' esac',
    `CARGO_TARGET_DIR="$PWD/target" cargo build --manifest-path ${HARNESS_MANIFEST} --locked`,
    '"$bin" > "$log" || rc=$?',
    `names=(${HARNESS_NAMES.join(' ')})`,
    'c=$(tr -d \'\\r\' < "$log" | grep -cxF "ok $n" || true)',
    'oks=$(tr -d \'\\r\' < "$log" | grep -c \'^ok \' || true)',
    "summary=$(tr -d '\\r' < \"$log\" | grep -cxF 'harness: 27 ok, 0 not ok' || true)",
    'if [ "$rc" != "0" ] || [ "${#names[@]}" != "27" ] || [ "$oks" != "27" ] || [ "$summary" != "1" ]; then',
    'for plant in committed-key:E_HARNESS_KEY bind-any:E_HARNESS_BIND; do',
    '"$bin" --plant "$p" > "$out" || prc=$?',
    'lines=$(tr -d \'\\r\' < "$out" | grep -c . || true)',
    "first=$(tr -d '\\r' < \"$out\" | sed -n '1p')",
    'pok=$(tr -d \'\\r\' < "$out" | grep -c \'^ok \' || true)',
    'if [ "$prc" != "1" ] || [ "$lines" != "1" ] || [ "${first%%:*}" != "$code" ] || [ "$pok" != "0" ]; then',
    'echo "::notice title=updater refusals ($RUNNER_OS)::27 ok, 0 not ok; plants exit 1"',
  ],
  'core dependency tree': [
    'cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}" > "$out"',
    'if [ -z "$hits" ] && [ "$n" -ge 1 ]; then',
  ],
  typecheck: ['npm run typecheck'],
  lint: ['npm run lint'],
  // Phase 10 FR-109 (PD-22: between lint and vitest).
  'node --test': ['if node --test --test-reporter=tap "scripts/*.test.js" | tee "$RUNNER_TEMP/node-tap.txt"; then'],
  vitest: ['npm test -- --reporter=default --reporter=json --outputFile.json="$RUNNER_TEMP/vitest.json"'],
  'required tests ran': [
    'node - "$RUNNER_TEMP/cargo-test.txt" "$RUNNER_TEMP/vitest.json" "$RUNNER_TEMP/node-tap.txt" "$RUNNER_OS"' +
      " <<'REQUIRED'",
    'const [cargoLog, vitestJson, nodeTap, runnerOs] = process.argv.slice(2);',
    'const key = { Windows: "windows", macOS: "macos", Linux: "linux" }[runnerOs];',
    'const req = JSON.parse(fs.readFileSync("tests/required-tests.json", "utf8"));',
    'const names = [...req.cargo.all, ...(key ? req.cargo[key] : [])];',
    'const m = /^test (\\S+)(?: - should panic)? \\.\\.\\. ok$/.exec(raw);',
    'for (const n of names) { const c = passed.get(n) || 0; if (c !== 1) bad.push(`cargo ${n}: ${c} passing' +
      ' line(s), want 1`); }',
    'const c = results.filter((r) => r.title === t && r.status === "passed").length;',
    // Phase 10 FR-109, PD-27: the TAP read leniently, every node name passing exactly once.
    'const m = /^ *ok \\d+ - (.*?)(?: # .*)?$/.exec(raw);',
    'for (const t of req.node) { const c = tap.get(t) || 0; if (c !== 1) bad.push(`node "${t}": ${c} passing' +
      ' line(s), want 1`); }',
    'if (bad.length) process.exit(1);',
    'console.log(`::notice title=required tests ran (${runnerOs})::${names.length} cargo + ${req.vitest.length}' +
      ' vitest + ${req.node.length} node`);',
    'REQUIRED',
  ],
  // Phase 10 FR-104, PD-20: the step keeps its name and its tee; npm run bundle runs tauri build.
  'tauri build': ['npm run bundle 2>&1 | tee "$RUNNER_TEMP/tauri-build.txt"'],
  'build warnings': [
    'clean="$RUNNER_TEMP/tauri-build.clean.txt"',
    "esc=$(printf '\\033')",
    'sed "s/${esc}\\[[0-9;]*m//g" "$RUNNER_TEMP/tauri-build.txt" > "$clean"',
    "finished=$(grep -cE '^ *Finished `release` profile' \"$clean\" || true)",
    "n=$(grep -c '^warning:' \"$clean\" || true)",
    'if [ "$finished" -ge 1 ] && [ "$n" = "0" ]; then',
  ],
  // Phase 10 FR-104 (SC-020): what the bundler produced, never cargo's output.
  'helper bundled': [
    'tuple=$(rustc --print host-tuple)',
    'setups=(target/release/bundle/nsis/*-setup.exe)',
    'if ! command -v 7z > /dev/null; then echo "::error title=helper bundled (Windows)::7z not found"; exit 1; fi',
    '7z l -slt "${setups[0]}" > "$RUNNER_TEMP/nsis-list.txt"',
    'if [ "$nh" != "1" ] || [ "$na" != "1" ] || [ "${helper%krate-manager-helper.exe}" != "${app%krate-manager.exe}" ];' +
      ' then',
    '7z e "${setups[0]}" -o"$out" "$helper" -y > /dev/null',
    "got=$(sha256sum < \"$out/krate-manager-helper.exe\" | cut -d' ' -f1)",
    "want=$(sha256sum < \"$staged\" | cut -d' ' -f1)",
    `bundled="${APP}/Contents/MacOS/krate-manager-helper"`,
    "n=$(dpkg-deb -c \"${debs[0]}\" | grep -c 'krate-manager-helper' || true)",
  ],
  // Phase 10 FR-103 (SC-019); Linux builds no helper binary (PD-20, D18).
  'ci-temp-roots absent': [
    'tree=$(cargo tree -p krate-manager-helper -e features --locked)',
    "hits=$(grep -cF 'ci-temp-roots' <<<\"$tree\" || true)",
    "marker=$(grep -cF -- '--ci-roots' crates/helper/src/cli.rs || true)",
    'echo "::notice::no Linux helper binary is built (D18)"',
    "inb=$(grep -acF -- '--ci-roots' \"$bundled\" || true)",
    "ins=$(grep -acF -- '--ci-roots' \"$staged\" || true)",
    'if [ "$hits" = "0" ] && [ "$marker" -ge 1 ] && [ "$inb" = "0" ] && [ "$ins" = "0" ]; then',
  ],
  'dev-feed absent': [
    'case "$RUNNER_OS" in Windows) bin=target/release/krate-manager.exe ;; *) bin=target/release/krate-manager ;; esac',
    "marker=$(grep -cF 'dev-feed-url.txt' src-tauri/src/dev_feed.rs || true)",
    "hits=$(grep -acF 'dev-feed-url.txt' \"$bin\" || true)",
    'if [ "$marker" -ge 1 ] && [ "$hits" = "0" ]; then',
  ],
  // Phase 11 FR-073 (SI-25): no updater-e2e feature in the tree, the marker in source, none in the release binaries.
  'updater-e2e absent': [
    'tree=$(cargo tree -p krate-manager -e features --locked)',
    "hits=$(grep -cF 'updater-e2e' <<<\"$tree\" || true)",
    `marker=$(grep -cF '${E2E_MARKER}' src-tauri/src/self_update.rs || true)`,
    'Windows) bins=(target/release/krate-manager.exe) ;;',
    `macOS) bins=(target/release/krate-manager "${APP}/Contents/MacOS/krate-manager") ;;`,
    '*) bins=(target/release/krate-manager) ;;',
    `n=$(grep -acF '${E2E_MARKER}' "$b" || true)`,
    'if [ "$hits" = "0" ] && [ "$marker" -ge 1 ] && [ "$inb" = "0" ]; then',
  ],
  // Phase 5 plan §4.12 (FR-083 as amended by SI-17).
  'roster absent': ['node - "$bin" <<\'ROSTER\''],
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
  // Phase 11 PD-33: only under `D9 WINDOWS LOAD FAILURE`.
  ...(HARNESS_ON_WINDOWS ? [] : [['updater refusals', "runner.os != 'Windows'"]]),
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
// Phase 5 FR-084: the opt-in live-network test never runs in CI (no github.com call from a gate).
const LIVE_NETWORK = 'live-network';

// --- Phase 10: the changes job and the two path-gated elevated jobs (FR-100 to FR-107, FR-111; plan §4.8.4, §8.2).

const JOBS = ['check', 'changes', 'helper-elevated', 'inno-locked-file'];
const ELEVATED_JOBS = ['helper-elevated', 'inno-locked-file'];
const GATE_IF = "needs.changes.outputs.elevated == 'true'";
// FR-102b plus SI-27's two workspace files; a trailing `/` is a directory prefix, any other entry an exact path.
const CHANGED_PATHS = [
  'apps/krate-manager/crates/helper/',
  'apps/krate-manager/tests/fixtures/helper/',
  'apps/krate-manager/tests/fixtures/inno/',
  'apps/krate-manager/tests/fixtures/locked-dll/',
  'apps/krate-manager/tests/fixtures/argv-recorder/',
  'apps/krate-manager/scripts/write-test-job.js',
  'apps/krate-manager/Cargo.toml',
  'apps/krate-manager/Cargo.lock',
  '.github/workflows/manager-ci.yml',
];
// PD-21 (SI-18): the event values through env:, never ${{ }} in run: text.
const CHANGES_ENV = [
  'EVENT: ${{ github.event_name }}',
  'BEFORE: ${{ github.event.before }}',
  'SHA: ${{ github.sha }}',
  'BASE: ${{ github.event.pull_request.base.sha }}',
  'HEAD: ${{ github.event.pull_request.head.sha }}',
];
// PD-21: workflow_dispatch -> true; pull_request base -> head; push before -> sha; the two fail-open branches.
const CHANGES_LINES = [
  'if [ "$EVENT" = "workflow_dispatch" ]; then',
  'elevated=true; why="workflow_dispatch"',
  'if [ "$EVENT" = "pull_request" ]; then from="$BASE"; to="$HEAD"; else from="$BEFORE"; to="$SHA"; fi',
  'if [ -z "$from" ] || [ -z "${from//0/}" ]; then',
  'elevated=true; why="no base commit (fail open)"',
  'elif ! files=$(git diff --name-only "$from" "$to"); then',
  'elevated=true; why="git diff failed (fail open)"',
  'echo "elevated=$elevated" >> "$GITHUB_OUTPUT"',
];
const CI_FEATURE = '--features ci-temp-roots';
// FR-103a: the only check steps that name the feature, each a -p krate-manager-helper command.
const CI_FEATURE_STEPS = {
  'clippy ci-temp-roots': 'cargo clippy -p krate-manager-helper ',
  'test ci-temp-roots': 'cargo test -p krate-manager-helper ',
};
// Phase 11 PD-32 (FR-083, SI-32): within check, the feature and the three KRATE_E2E_* names only in this step, a
// -p krate-manager command, its env: exactly the three dummies, and no dev-feed in it.
const E2E_FEATURE = '--features updater-e2e';
const E2E_FEATURE_STEPS = { 'clippy updater-e2e': 'cargo clippy -p krate-manager ' };
const E2E_NAMES = ['KRATE_E2E_FEED_URL', 'KRATE_E2E_MARKER_DIR', 'KRATE_E2E_OLD_VERSION'];
const E2E_ENV = [
  "KRATE_E2E_FEED_URL: 'http://127.0.0.1:9/versions.json'",
  "KRATE_E2E_MARKER_DIR: 'never-run'",
  "KRATE_E2E_OLD_VERSION: '0.0.0'",
];
const HELPER_BUILD = 'cargo build -p krate-manager-helper --release --locked --features ci-temp-roots';
const MANUAL = 'manual-elevation';
const MANUAL_STEP = 'clippy manual-elevation example';
// FR-106 (SI-29): only in helper-elevated / inno-locked-file.
const ELEVATED_ONLY = ['pkgbuild', '/usr/sbin/installer', 'installer -pkg', 'Minionguyjpro/Inno-Setup-Action', 'ISCC'];
const INNO_ACTION = 'uses: Minionguyjpro/Inno-Setup-Action@pinned';
// FR-106: the helper binary run with an argument (one line: spaces or tabs, then --job, --ci-roots or a $-variable).
const HELPER_RUN = /krate-manager-helper(?:\.exe)?['"]?[ \t]+(?:--job|--ci-roots|\$)/g;
// FR-100: the Windows run form of the GUI-subsystem helper; macOS runs it under sudo.
const WINDOWS_HELPER_RUN = 'Start-Process -FilePath $helper ';
const MACOS_HELPER_RUN = 'sudo "$helper" --job ';
// FR-111: the fixture receipt is forgotten again whatever happened.
const FORGET = 'sudo pkgutil --forget com.krateaudio.fixture || true';
const FORGET_IF = "always() && runner.os == 'macOS'";

/** Every match of the global regex `re` in `text`. */
function countMatches(text, re) {
  return (text.match(re) || []).length;
}

/** The entries of the `PATHS=(` … `)` array in a run: script, trimmed, or null. */
function pathArray(run) {
  const lines = run.split('\n').map((l) => l.trim());
  const head = lines.indexOf('PATHS=(');
  if (head < 0) return null;
  const end = lines.indexOf(')', head);
  return end < 0 ? null : lines.slice(head + 1, end).filter((l) => l !== '');
}

/** FR-100 to FR-107 on the Phase 10 jobs (SC-031); every finding into `bad`. */
function checkElevatedJobs(wf, bad) {
  const blocks = Object.fromEntries(JOBS.map((j) => [j, jobBlock(wf, j)]));
  for (const j of JOBS) if (blocks[j] === null) bad.push(`manager-ci.yml: no ${j} job`);
  if (JOBS.some((j) => blocks[j] === null)) return;
  const steps = Object.fromEntries(JOBS.map((j) => [j, stepBlocks(blocks[j])]));
  const named = (job, name) => steps[job].find((s) => s.label === name) || null;

  // FR-102b: check is never gated; the two elevated jobs are, by the changes job.
  for (const key of ['needs', 'if']) {
    if (field(blocks.check, 4, key) !== null) bad.push(`check: ${key}: ${field(blocks.check, 4, key)} (check is not gated)`);
  }
  for (const j of ELEVATED_JOBS) {
    if (field(blocks[j], 4, 'needs') !== 'changes') bad.push(`${j}: needs ${field(blocks[j], 4, 'needs')}, want changes`);
    if (field(blocks[j], 4, 'if') !== GATE_IF) bad.push(`${j}: if ${field(blocks[j], 4, 'if')}, want ${GATE_IF}`);
  }
  // FR-100, FR-102, FR-102b: contents: read, no environment, no secret.
  for (const j of JOBS.slice(1)) {
    const perms = trimmed(nested(blocks[j], 4, 'permissions'));
    if (!util.isDeepStrictEqual(perms, ['contents: read'])) bad.push(`${j}: permissions ${JSON.stringify(perms)}`);
    if (/^\s*environment\s*:/m.test(blocks[j])) bad.push(`${j}: an environment: key`);
    if (blocks[j].includes('secrets.')) bad.push(`${j}: a secrets. reference`);
  }

  // The changes job (PD-21, SI-2, SI-18, SI-27).
  if (field(blocks.changes, 4, 'runs-on') !== 'ubuntu-24.04') {
    bad.push(`changes.runs-on ${field(blocks.changes, 4, 'runs-on')}, want ubuntu-24.04`);
  }
  const outputs = trimmed(nested(blocks.changes, 4, 'outputs'));
  if (!util.isDeepStrictEqual(outputs, ['elevated: ${{ steps.paths.outputs.elevated }}'])) {
    bad.push(`changes.outputs ${JSON.stringify(outputs)}`);
  }
  const labels = steps.changes.map((s) => s.label);
  if (!util.isDeepStrictEqual(labels, ['uses: actions/checkout@pinned', 'changed paths'])) {
    bad.push(`changes: steps ${JSON.stringify(labels)}`);
  }
  const checkout = steps.changes[0];
  if (!checkout || field(checkout.text, 10, 'fetch-depth') !== '0') bad.push('changes: checkout without fetch-depth: 0');
  const paths = named('changes', 'changed paths');
  if (paths) {
    if (field(paths.text, 8, 'id') !== 'paths') bad.push('changes: changed paths has no id: paths');
    if (field(paths.text, 8, 'shell') !== 'bash') bad.push('changes: changed paths is not shell: bash');
    const env = trimmed(nested(paths.text, 8, 'env'));
    if (!util.isDeepStrictEqual(env, CHANGES_ENV)) bad.push(`changes: env ${JSON.stringify(env)}`);
    const run = runText(paths);
    if (run.includes('${{')) bad.push('changes: a ${{ }} expression inside run: text (SI-18)');
    if (scriptLines(paths)[0] !== 'set -euo pipefail') bad.push('changes: the script does not start set -euo pipefail');
    const have = new Set(scriptLines(paths));
    for (const l of CHANGES_LINES) if (!have.has(l)) bad.push(`changes: no line ${JSON.stringify(l)}`);
    const list = pathArray(run);
    if (!util.isDeepStrictEqual(list, CHANGED_PATHS)) {
      bad.push(`changes: changed-paths list ${JSON.stringify(list)}, want FR-102b + SI-27`);
    }
  }

  // helper-elevated (FR-100) and inno-locked-file (FR-102).
  const he = blocks['helper-elevated'];
  if (field(he, 4, 'name') !== 'helper-elevated (${{ matrix.os }})') bad.push(`helper-elevated.name ${field(he, 4, 'name')}`);
  if (field(he, 4, 'runs-on') !== '${{ matrix.os }}') bad.push(`helper-elevated.runs-on ${field(he, 4, 'runs-on')}`);
  const matrix = trimmed(nested(he, 4, 'strategy'));
  if (!util.isDeepStrictEqual(matrix, ['fail-fast: false', 'matrix:', 'os: [macos-latest, windows-latest]'])) {
    bad.push(`helper-elevated.strategy ${JSON.stringify(matrix)}`);
  }
  if (!he.includes(HELPER_BUILD)) bad.push(`helper-elevated: no ${HELPER_BUILD}`);
  if (count(he, 'pkgbuild --root ') < 1) bad.push('helper-elevated: no pkgbuild of the fixture pkg');
  if (count(he, INNO_ACTION) < 1) bad.push('helper-elevated: no fixture Inno compile');
  const forget = steps['helper-elevated'].find((s) => runText(s).includes(FORGET));
  if (!forget) bad.push(`helper-elevated: no step running ${FORGET} (FR-111)`);
  else if (field(forget.text, 8, 'if') !== FORGET_IF) bad.push(`helper-elevated: the forget step if: ${field(forget.text, 8, 'if')}`);
  // FR-100, FR-107: every Windows step naming the helper runs it with pwsh and Start-Process -Wait -PassThru.
  let windowsRuns = 0;
  let macosRuns = 0;
  for (const s of steps['helper-elevated']) {
    const run = runText(s);
    if (!run.includes('$helper')) continue;
    const gate = field(s.text, 8, 'if');
    if (gate === "runner.os == 'Windows'") {
      if (field(s.text, 8, 'shell') !== 'pwsh') bad.push(`helper-elevated: Windows helper step "${s.label}" is not shell: pwsh`);
      const starts = run.split('\n').filter((l) => l.includes(WINDOWS_HELPER_RUN));
      if (starts.length === 0) bad.push(`helper-elevated: Windows helper step "${s.label}" has no ${WINDOWS_HELPER_RUN.trim()}`);
      for (const l of starts) {
        if (!/ -Wait\b/.test(l) || !/ -PassThru\b/.test(l)) {
          bad.push(`helper-elevated: "${s.label}": ${l.trim()} without -Wait -PassThru`);
        }
      }
      windowsRuns += starts.length;
    } else if (gate === "runner.os == 'macOS'") {
      macosRuns += count(run, MACOS_HELPER_RUN);
    }
  }
  if (windowsRuns === 0) bad.push('helper-elevated: no Windows helper run');
  if (macosRuns === 0) bad.push('helper-elevated: no macOS helper run');
  const ilf = blocks['inno-locked-file'];
  if (field(ilf, 4, 'runs-on') !== 'windows-latest') bad.push(`inno-locked-file.runs-on ${field(ilf, 4, 'runs-on')}`);
  if (count(ilf, INNO_ACTION) !== 2) bad.push('inno-locked-file: not two fixture Inno compiles (1.0.0, 1.0.1)');
  // FR-102a (SI-26): after M-1 the job reads each committed fixture and turns a mismatch into an error.
  for (const f of ['exit_code.txt', 'locked_line.txt', 'classes.json']) {
    if (!ilf.includes(`"$m1/${f}"`)) bad.push(`inno-locked-file: does not compare with inno-locked/${f} (FR-102a)`);
  }
  if (!ilf.includes('(M-1 changed)::')) bad.push('inno-locked-file: no ::error for an M-1 mismatch (FR-102a)');

  // FR-106, FR-107 (SI-29): sudo only in helper-elevated (check's own rule keeps it to Linux system packages).
  for (const j of ['changes', 'inno-locked-file']) if (blocks[j].includes('sudo')) bad.push(`${j}: sudo (FR-106)`);
  const elevatedText = ELEVATED_JOBS.map((j) => blocks[j]);
  for (const tok of ELEVATED_ONLY) {
    const inside = elevatedText.reduce((n, b) => n + count(b, tok), 0);
    if (count(wf, tok) !== inside) bad.push(`${tok} outside helper-elevated / inno-locked-file (FR-106)`);
  }
  const runsInside = elevatedText.reduce((n, b) => n + countMatches(b, HELPER_RUN), 0);
  if (countMatches(wf, HELPER_RUN) !== runsInside) {
    bad.push('a helper run with arguments outside helper-elevated / inno-locked-file (FR-106)');
  }
  // FR-103a, FR-107: the feature only in the two jobs and check's two named steps.
  let featureInCheck = 0;
  for (const [name, cmd] of Object.entries(CI_FEATURE_STEPS)) {
    const s = named('check', name);
    if (!s) continue;
    const line = scriptLines(s).find((l) => l.includes(CI_FEATURE));
    if (!line || !line.includes(cmd)) bad.push(`${name}: no ${cmd.trim()} ... ${CI_FEATURE} command`);
    featureInCheck += count(s.text, CI_FEATURE);
  }
  const featureInside = elevatedText.reduce((n, b) => n + count(b, CI_FEATURE), 0);
  if (count(wf, CI_FEATURE) !== featureInside + featureInCheck) {
    bad.push(`${CI_FEATURE} outside the two elevated jobs and check's two FR-103a steps`);
  }
  // FR-105: manual-elevation only in its compile-only step.
  const manual = named('check', MANUAL_STEP);
  if (!manual) bad.push(`no check step "${MANUAL_STEP}" (FR-105)`);
  else if (count(wf, MANUAL) !== count(manual.text, MANUAL)) bad.push(`${MANUAL} outside the step "${MANUAL_STEP}"`);
}

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
      'contract::tests::embedded_schemas_compile',
      'contract::tests::unsupported_schema_constructs_are_refused',
      'contract::tests::keyword_semantics_cases',
      'contract::tests::c1_cases_reproduce',
      'contract::tests::gen_feed_outputs_accepted',
      'contract::tests::ecma_pattern_translation',
      'contract::tests::trusted_comment_cases',
      'contract::tests::minisig_text_cases',
      'feed::tests::accept_rows_parse_through_the_client',
      'feed::tests::gen_feed_outputs_parse',
      'feed::tests::schema_version_dispatch',
      'feed::tests::model_of_full_v2',
      'feed::tests::model_range_cases',
      'feed::tests::asset_name_cases',
      'feed::tests::v1_entries_are_sanitized',
      'feed::tests::unknown_keys_do_not_change_the_model',
      'feed::tests::prerelease_sibling_is_ignored',
      'feed::tests::check_fresh_writes_cache',
      'feed::tests::check_now_sends_no_cache',
      'feed::tests::rejected_feed_keeps_last_good_copy',
      'feed::tests::broken_sibling_never_blocks_stable',
      'feed::tests::sibling_prefix_filter',
      'feed::tests::offline_returns_cached_flagged_stale',
      'feed::tests::cache_rules',
      'feed::tests::cache_write_fault_keeps_previous',
      'feed::tests::roster_follows_the_feed',
      'feed::tests::check_logs_one_line',
      'feed::tests::concurrent_checks_leave_one_whole_file',
      'feed::tests::fresh_lower_version_replaces_cache',
      'feed::tests::one_attempt_per_check',
      'feed::tests::check_through_proxy_writes_cache',
      'version::tests::resolve_table',
      'version::tests::parse_core_cases',
      'version::tests::reduce_class_version_cases',
      'version::tests::compare_cases',
      'version::tests::os_version_cases',
      'version::tests::needs_newer_os_table',
      'http::tests::url_policy_cases',
      'http::tests::redirects_are_rechecked',
      'http::tests::next_hop_cases',
      'http::tests::status_and_body_errors',
      'http::tests::https_only_refuses_before_connecting',
      'http::tests::config_for_dev_url_cases',
      'http::tests::agent_trusts_the_os_store',
      'http::tests::resolve_proxy_cases',
      'http::tests::loopback_proxy_only',
      'http::tests::proxy_path_connect',
      'http::tests::proxy_path_http',
      'http::tests::no_proxy_bypass',
      'http::tests::proxy_env_from_vars_cases',
      'http::tests::proxy_scheme_cases',
      'http::tests::chain_shares_one_deadline',
      'http::tests::production_values',
      'sysproxy::tests::windows_proxy_parser_cases',
      'sysproxy::tests::macos_proxy_parser_cases',
      'sysproxy::tests::linux_proxy_parser_cases',
      'sysproxy::tests::win_ie_strings_are_read_and_freed_once',
      'sysproxy::tests::linux_desktop_choice_cases',
      'sysproxy::tests::kioslaverc_path_cases',
      'sysproxy::tests::kde_file_read_cases',
      'fixture_server::tests::binds_loopback_port_zero',
      'fixture_server::tests::connect_to_remote_host_is_refused',
      'fixture_server::tests::replies_behave_as_scripted',
      'context::tests::http_config_is_carried',
      'ureq_is_confined_to_http_rs',
      'feed_model_seams_hold',
      'production_config_only_in_allowed_tests',
      'roster_and_hosts_absent_from_sources',
      'roster_scanner_reports_planted_names',
      'pac::tests::pac_helper_cases',
      'pac::tests::find_proxy_result_cases',
      'pac::tests::wpad_candidate_cases',
      'pac::tests::winhttp_answer_cases',
      'pac::tests::pac_clock_cases',
      'http::tests::pac_policy_cases',
      'http::tests::os_pac_walk',
      'http::tests::os_pac_failure_cases',
      'http::tests::no_response_class_cases',
      'http::tests::bounded_call_abandons_after_wait',
      'sysproxy::tests::parse_utc_offset_cases',
      'sysproxy::tests::offset_range_cases',
      'reader_dependencies_are_the_ruled_set',
      'detect::tests::candidate_table',
      'detect::tests::trailing_comma_cases',
      'detect::tests::installed_state_table',
      'detect::tests::duplicates_in_two_roots',
      'detect::tests::record_parsers',
      'detect::tests::records_through_fake_host',
      'detect::tests::outside_install_location_panics',
      'detect::tests::os_version_parsers',
      'detect::tests::detection_leaves_the_tree_unchanged',
      'detect::tests::host_read_mappers',
      'detect::tests::nested_copies_listed',
      'detect::tests::listing_caps_honoured',
      'detect::tests::listing_production_limits',
      'detect::tests::installed_elsewhere',
      'status::tests::feed_driven_detection',
      'status::tests::running_os_gates',
      'status::tests::golden_views',
      'status::tests::build_cases',
      'status::tests::concurrent_builds',
      'status::tests::listing_runs_once_per_root',
      'status::tests::release_page_url_cases',
      'status::tests::plugin_page_url_cases',
      'status::tests::description_cases',
      'notes::tests::blocks_cases',
      'notes_and_plist_crates_are_confined',
      'host_io_symbols_are_confined',
      'confinement_scanner_reports_plants',
      'test_sources_never_name_os_host_io',
      'test_source_scanner_reports_planted_os_host_io',
      'detect_status_notes_hold_no_write_or_listing_token',
      'token_scanner_reports_planted_read_dir',
      'test_sources_hold_no_real_location',
      'real_location_scanner_reports_plants',
      'opener_is_confined_and_not_granted_to_the_webview',
      'opener_guard_reports_plants',
      'install_identifiers_absent_from_sources',
      'install_identifier_scanner_reports_plants',
      'download::tests::good_artifact_verifies',
      'download::tests::rejections_never_reach_next_stage',
      'verify::tests::kat_verifies_and_one_byte_changes_fail',
      'verify::tests::kat_secret_message_verifies',
      'verify::tests::invalid_trusted_comment_is_named',
      'verify::tests::verify_steps_stop_at_first_failure',
      'verify::tests::signing_key_text_cases',
      'download::tests::progress_is_monotonic_and_ends_at_size',
      'download::tests::interrupted_download_resumes',
      'download::tests::partial_from_a_previous_run_resumes',
      'download::tests::stall_resumes',
      'download::tests::ignored_range_falls_back_to_full',
      'download::tests::truncate_failure_after_ignored_range',
      'download::tests::range_edge_cases',
      'download::tests::classify_head_table',
      'download::tests::retries_are_bounded',
      'download::tests::stall_and_deadline_are_named',
      'http::tests::streamed_policy_cases',
      'http::tests::streamed_body_cases',
      'download::tests::staging_errors_are_named',
      'download::tests::crash_after_each_step_recovers',
      'download::tests::prune_keeps_only_live_artifacts',
      'download::tests::same_artifact_twice_is_busy',
      'download::tests::busy_registry_is_released_on_error',
      'verified_artifact_has_no_public_constructor',
      'verified_artifact_scanner_reports_plants',
      'verify::tests::release_key_is_the_committed_key_and_not_a_test_key',
      'no_secret_key_in_app_tree',
      'no_secret_key_scanner_reports_plants',
      'phase7_tests_hold_no_real_host',
      'phase7_host_scanner_reports_plants',
      'core_has_no_signing_crate',
      'signing_crate_scan_reports_plants',
      'verify_crates_are_confined',
      'verify_confinement_scanner_reports_plants',
      'download_and_verify_hold_no_url_or_whole_read',
      'download_verify_scanner_reports_plants',
      'download::tests::request_for_cases',
      'fixture_server::tests::ranged_cases',
      'fixture_server::tests::script_advances_per_request',
      'fixture_server::tests::paced_delivers_all_bytes_in_chunks',
      'download::tests::two_artifacts_reach_one_consumer',
      'download::tests::channel_adapter_forwards_in_order',
      'download::tests::command_flow_cases',
      'download::tests::cached_verified_file_is_reverified',
      'download::tests::redirect_target_never_logged',
      'download::tests::writes_nothing_outside_downloads',
      'download::tests::fixture_url_seam_cases',
      'fixture_url_seam_is_test_only',
      'fixture_url_seam_scanner_reports_plants',
      'download::tests::staged_paths_cases',
      'download::tests::cancellation_cases',
      'download::tests::discard_cases',
      'download::tests::retrying_events_are_ordered_and_serialize',
      'download::tests::policy_has_no_overall_budget',
      'download::tests::slow_moving_download_is_not_an_error',
      'too_many_arguments_is_never_suppressed',
      'download::tests::prune_spares_in_flight_artifact',
      'download::tests::non_retryable_transport_errors',
      'extract::tests::concurrent_stage_is_safe',
      'extract::tests::file_hashes_match_written_bytes',
      'extract::tests::moduleinfo_version_must_match',
      'extract::tests::prune_removes_by_kind',
      'extract::tests::path_length_bounds',
      'extract::tests::trailing_zero_padding_is_accepted',
      'extract::tests::good_fixtures_match_committed_listings',
      'extract::tests::bad_fixtures_fail_with_expected_code',
      'extract::tests::kat_archive_stages',
      'extract::tests::real_shaped_round_trip',
      'extract::tests::dot_slash_round_trip',
      'extract::tests::exec_and_symlink_round_trip_logic',
      'extract::tests::crafted_archives_are_refused',
      'extract::tests::payload_and_layout_mismatches',
      'extract::tests::entry_binding_cases',
      'extract::tests::changed_archive_is_refused',
      'extract::tests::io_failures_leave_no_tree',
      'extract::tests::pax_xattr_records_are_ignored',
      'extract::tests::case_fold_collision',
      'extract::tests::discard_and_prune',
      'extract::tests::tree_mismatch_is_refused',
      'extract::tests::entry_path_rejects_every_bad_form',
      'extract::tests::work_dir_must_be_inside_staging',
      'extract::tests::stage_logs_one_line_per_outcome',
      'install::tests::install_update_remove_cycle',
      'install::tests::crash_after_each_step_recovers',
      'install::tests::manifest_outside_roots_refused',
      'install::tests::remove_keeps_user_files',
      'install::tests::update_preset_rules',
      'install::tests::manifest_reads_back_through_detect',
      'install::tests::unverified_never_installed',
      'install::tests::state_checks_before_download',
      'install::tests::driver_rechecks_state',
      'install::tests::io_failures_roll_back',
      'install::tests::changed_staged_file_refused',
      'install::tests::collisions_refused_before_write',
      'install::tests::remove_crash_resumes',
      'install::tests::startup_recovery',
      'install::tests::recovery_table_rows',
      'install::tests::concurrent_actions',
      'install::tests::sync_before_commit',
      'install::tests::progress_events',
      'install::tests::sandbox_guard_panics',
      'install::tests::artifact_released_after_success',
      'install::tests::logs_one_line_per_action',
      'install::tests::commands_run_over_the_driver',
      'install::tests::batch_runs_every_action',
      'install::tests::remove_deletes_bundle_whole',
      'install::tests::cut_before_new_in_rolls_back_in_process',
      'context::tests::assemble_runs_install_recovery',
      'status::tests::actions_per_state',
      'validate::tests::rejects_crafted_jobs',
      'validate::tests::performs_valid_jobs',
      'validate::tests::run_dir_ancestors',
      'validate::tests::run_dir_sddl',
      'ops::tests::exact_argv',
      'ops::tests::batch_of_three_middle_locked',
      'ops::tests::batch_timeout_stops_the_rest',
      'ops::tests::uninstall_only_job_has_no_run_dir',
      'ops::tests::run_dir_failure_fails_installers',
      'ops::tests::copy_error_is_named',
      'ops::tests::spawn_error_is_named',
      'ops::tests::classify_sees_whole_bounded_log',
      'ops::tests::uninstall_polls_until_gone',
      'ops::tests::uninstall_poll_times_out',
      'ops::tests::uninstall_refuses_a_plugin_binary_in_use',
      'ops::tests::uninstall_checks_a_free_plugin_binary_and_runs',
      'ops::tests::uninstaller_never_exits',
      'ops::tests::runs_the_copy_never_the_original',
      'ops::tests::pkg_paths_classified',
      'ops::tests::pkg_failure_paths',
      'ops::tests::run_dir_removed_after_job',
      'ops::tests::run_dir_refusals_spawn_nothing',
      'ops::tests::temp_io_refuses_outside_base',
      'result::tests::log_tail_bounds',
      'roots::tests::binary_roots_ignore_job_and_settings',
      'job::tests::no_field_adds_a_root',
      'classify::tests::recorded_locked_case',
      'ops::tests::recorded_locked_case_through_runner',
      'install::tests::recorded_locked_case_maps_to_locked',
      'install::tests::unrecognised_log_falls_back',
      'cli::tests::ci_roots_flag_is_usage_error',
      'cli::tests::malformed_hash_is_usage_error',
      'os::tests::read_bounded_stops_at_bound',
      'core_written_jobs_parse',
      'helper_usage_exits_2',
      'install::tests::batch_locked_middle_maps_each',
      'install::tests::refused_prompt_changes_nothing',
      'install::tests::one_launch_per_batch',
      'install::tests::drivers_per_os',
      'install::tests::gate_remove_basis',
      'install::tests::remove_elsewhere_refused',
      'install::tests::record_outside_sandbox_panics',
      'install::tests::windows_install_update_remove',
      'install::tests::macos_install_update_remove',
      'install::tests::launch_outcomes_map',
      'install::tests::pre_job_refusals',
      'install::tests::elevating_event_order',
      'install::tests::osascript_outcome_parses',
      'install::tests::windows_parameters_quote',
      'install::tests::job_files_written_and_removed',
      'install::tests::recover_removes_stale_jobs',
      'install::tests::launch_program_is_the_resolved_helper',
      'install::tests::helper_file_never_opened',
      'install::tests::timed_out_holds_guard',
      'install::tests::not_applied_detected',
      'install::tests::new_error_texts',
      'install::tests::job_hash_is_the_verified_digest',
      'install::tests::helper_resolved_beside_exe',
      'fixture_names_confined',
      'self_update::tests::url_policy',
      'self_update::tests::redirect_hops',
      'self_update::tests::proxy_choice_matches_core',
      'self_update::tests::codes_and_sentences',
      'self_update::tests::view_from_update',
      'self_update::tests::endpoint_matches_feed_tool',
      'self_update::tests::busy_rule',
      'self_update::tests::in_flight_flag',
      'self_update::tests::timeouts',
      'self_update::tests::no_context_defaults',
      'self_update::tests::packaging_rule',
      'self_update::tests::progress_events_serialize',
      'context::tests::release_for_restart_frees_the_lock',
      'install::tests::refuse_while_self_updating',
      'updater_config_pinned',
      'planted_updater_config_fails_the_guard',
      'self_update_confinement_holds',
      'planted_self_update_confinement_fails',
      'updater_tar_only_through_the_plugin',
      'updater_e2e_cfg_sites_are_counted',
      'planted_updater_e2e_cfg_site_fails',
      'harness_source_names_no_installer_or_root',
      'update_inputs_only_from_production',
      'planted_update_inputs_fail_the_pin',
      'install::batch::one_launch_windows',
      'install::batch::one_launch_macos',
      'install::batch::no_launch_linux',
      'install::batch::download_bound',
      'install::batch::production_bound_is_three',
      'install::batch::installs_after_downloads',
      'install::batch::locked_item_continues',
      'install::batch::notice_cases',
      'install::batch::download_failure_excluded',
      'install::batch::staging_full_excluded',
      'install::batch::state_changed_items',
      'install::batch::busy_items',
      'install::batch::input_checks',
      'install::batch::refused_prompt',
      'install::batch::elevation_busy',
      'install::batch::registrar_kill_once',
      'install::batch::progress_mapping',
      'install::batch::log_lines',
      'install::batch::batch_level_errors',
      'install::tests::orphan_remove_cases',
      'install::tests::not_manageable_is_not_orphan',
      'install::tests::manifest_heads_cases',
      'fixture_server::tests::held_reply_releases',
      'settings::tests::save_for_command_cases',
      'settings::tests::view_cases',
      'settings::tests::root_gate_cases',
      'platform::tests::graphics_env_cases',
      'platform::tests::apply_graphics_env_cases',
      'status::tests::orphans_cases',
      'no_process_enumeration',
      'one_set_var_site_before_threads',
      'no_cancel_setter',
      'plugin_commands_take_the_root_ticket',
      'job::tests::core_and_helper_op_caps_agree',
    ],
    windows: [
      'platform::windows::tests::default_roots_from_env',
      'version::tests::native_candidates_windows',
      'detect::tests::native_layout_windows',
      'detect::tests::locked_moduleinfo_windows',
      'detect::tests::host_facts_through_fake_io_windows',
      'detect::tests::listing_case_windows',
      'detect::tests::listing_skips_junction_windows',
      'download::tests::prune_skips_a_file_open_without_share_delete_windows',
      'download::tests::verified_file_has_no_zone_identifier_windows',
      'download::tests::staged_links_are_not_followed_windows',
      'extract::tests::symlink_entry_unsupported_windows',
      'validate::tests::verbatim_paths_windows',
      'roots::tests::production_roots_windows',
      'validate::tests::symlinked_parent_root_windows',
      'windows_parameters_parse_back_windows',
      'install::tests::native_wiring_windows',
      'os::tests::open_source_refuses_link_windows',
      'os::tests::in_use_sees_a_mapped_image_and_a_held_file',
    ],
    macos: [
      'platform::macos::tests::default_roots_from_env',
      'version::tests::native_candidates_macos',
      'sysproxy_cf::tests::from_cf_proxies_cases',
      'sysproxy::tests::bounded_run_abandons_and_reaps',
      'pac::tests::url_argument_cases',
      'pac::tests::script_world_cases',
      'pac::tests::script_failure_cases',
      'pac::tests::dns_bound_cases',
      'http::tests::pac_path_proxy',
      'http::tests::pac_path_direct',
      'http::tests::pac_list_walk',
      'http::tests::pac_per_hop',
      'http::tests::pac_fetched_once_never_via_proxy',
      'http::tests::wpad_discovery_walk',
      'http::tests::pac_fallback_to_static_proxy',
      'http::tests::pac_body_cap',
      'feed::tests::check_survives_pac_failure',
      'detect::tests::native_layout_macos',
      'detect::tests::au_plist_cases_macos',
      'detect::tests::pkg_receipt_macos',
      'detect::tests::host_facts_through_fake_io_macos',
      'detect::tests::probe_metadata_error_unix',
      'detect::tests::listing_skips_symlinked_dirs_unix',
      'detect::tests::listing_case_exact_unix',
      'download::tests::readonly_staging_dir_unix',
      'download::tests::verified_file_has_no_quarantine_macos',
      'http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix',
      'download::tests::staged_symlinks_are_not_followed',
      'extract::tests::prune_removes_real_link_unix',
      'extract::tests::posix_modes_and_links_unix',
      'extract::tests::special_bits_and_mtime_not_applied_unix',
      'extract::tests::escape_cases_unix',
      'extract::tests::symlinked_staging_root_unix',
      'extract::tests::staged_tree_has_no_xattrs_macos',
      'extract::tests::raw_temp_dir_staging_root_macos',
      'install::tests::install_update_remove_cycle_unix',
      'install::tests::crash_after_each_step_recovers_unix',
      'install::tests::manifest_outside_roots_refused_unix',
      'install::tests::io_failures_roll_back_unix',
      'install::tests::collisions_refused_before_write_unix',
      'install::tests::non_utf8_root_refused_unix',
      'install::tests::symlinked_plugin_root_unix',
      'install::tests::remove_bundle_link_unix',
      'roots::tests::production_roots_macos',
      'install::tests::native_wiring_macos',
      'validate::tests::symlinked_parent_root_unix',
      'os::tests::open_source_refuses_links_and_fifos_unix',
      'os::tests::run_dir_mode_unix',
      'os::tests::wait_timeout_leaves_child_unix',
      'os::tests::in_use_is_false_off_windows',
    ],
    linux: [
      'platform::linux::tests::default_roots_from_env',
      'version::tests::native_candidates_linux',
      'sysproxy::tests::bounded_run_abandons_and_reaps',
      'pac::tests::url_argument_cases',
      'pac::tests::script_world_cases',
      'pac::tests::script_failure_cases',
      'pac::tests::dns_bound_cases',
      'http::tests::pac_path_proxy',
      'http::tests::pac_path_direct',
      'http::tests::pac_list_walk',
      'http::tests::pac_per_hop',
      'http::tests::pac_fetched_once_never_via_proxy',
      'http::tests::wpad_discovery_walk',
      'http::tests::pac_fallback_to_static_proxy',
      'http::tests::pac_body_cap',
      'feed::tests::check_survives_pac_failure',
      'detect::tests::native_layout_linux',
      'detect::tests::linux_detection_linux',
      'detect::tests::host_facts_through_fake_io_linux',
      'detect::tests::probe_metadata_error_unix',
      'detect::tests::listing_skips_symlinked_dirs_unix',
      'detect::tests::listing_case_exact_unix',
      'download::tests::readonly_staging_dir_unix',
      'http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix',
      'download::tests::staged_symlinks_are_not_followed',
      'extract::tests::prune_removes_real_link_unix',
      'extract::tests::posix_modes_and_links_unix',
      'extract::tests::special_bits_and_mtime_not_applied_unix',
      'extract::tests::escape_cases_unix',
      'extract::tests::symlinked_staging_root_unix',
      'install::tests::install_update_remove_cycle_unix',
      'install::tests::crash_after_each_step_recovers_unix',
      'install::tests::manifest_outside_roots_refused_unix',
      'install::tests::io_failures_roll_back_unix',
      'install::tests::collisions_refused_before_write_unix',
      'install::tests::non_utf8_root_refused_unix',
      'install::tests::symlinked_plugin_root_unix',
      'install::tests::remove_bundle_link_unix',
      'install::tests::native_wiring_installs_linux',
      'validate::tests::symlinked_parent_root_unix',
      'os::tests::open_source_refuses_links_and_fifos_unix',
      'os::tests::run_dir_mode_unix',
      'os::tests::wait_timeout_leaves_child_unix',
      'os::tests::in_use_is_false_off_windows',
    ],
  },
  vitest: [
    'shows the title and the version',
    'shows "version unavailable" when app_version fails',
    'shows only the notice when another instance is running',
    'renders fresh-all-states',
    'renders cached-stale',
    'renders no-feed',
    'renders future-schema',
    'renders macos-intel-not-for-this-mac',
    'renders not-for-this-computer-linux',
    'renders needs-newer-os-macos',
    'renders needs-newer-os-linux',
    'renders needs-newer-os-windows',
    'renders macos-au-copies',
    'renders record-notices',
    'renders notes-rendering',
    'renders installed-elsewhere',
    'does not call status commands when another instance is running',
    'every status text is rendered by a scenario',
    'check now disables the button while pending',
    'check now calls check_now and re-renders',
    'check now recovers from a rejection',
    'shows the ten-minute note',
    'renders the open release page button',
    'open release page calls open_release_page once',
    'open release page shows a rejection',
    'renders the open plugin page button',
    'open plugin page calls open_release_page with the plugin page',
    'open plugin page shows a rejection',
    'renders the plugin description under the name',
    'no navigation path but the release page button',
    'shows the pending text until the first result',
    'check now stays disabled until the first result',
    'check now is enabled after a rejected first load',
    'renders install for not-installed linux',
    'renders update and remove for update-available linux',
    'install asks for confirmation naming the plugin and version',
    'install on an elsewhere copy names the other copy and says to remove it by hand',
    'install without an elsewhere copy shows no other-copy line',
    'update asks for confirmation naming both versions',
    'remove asks for confirmation first',
    'cancelling a confirmation invokes nothing',
    'confirmed install sends name, version, action and a channel',
    "disables the card's buttons while an action runs",
    'shows placing progress',
    'shows the outcome and the DAW notice after an install',
    'shows the outcome and the DAW notice after an update',
    'shows kept files after remove',
    'shows the error code on failure',
    'refreshes the status after an action and keeps the outcome',
    'renders install on windows and macos',
    'windows confirmation mentions the administrator prompt',
    'macos confirmation mentions the administrator password',
    'shows waiting for administrator permission',
    'refused permission shows nothing changed and keeps the actions',
    'locked shows close your daw and retry',
    'macos install shows the au rescan steps',
    'outside-root install shows the windows settings hint and no remove',
    'windows remove confirmation names the uninstaller',
    'stale busy shows installer still running',
    'not applied shows the change was not applied',
    'macos remove confirmation names the uninstaller',
    'renders the manager update line',
    'confirms before updating',
    'manager update confirmation names the admin prompt on macOS only',
    'shows a failed update by code',
    'shows no banner when the check fails',
    'shows downloading then installing progress',
    'offers the new .deb instead of an update on a system-package install',
    'a failed .deb release-page open shows its code',
    'disables plugin actions while the manager updates itself',
    're-enables plugin actions after a failed manager update',
    'checks for a manager update after plugin_status is rejected',
    'checks for a manager update after plugin_status succeeds',
    'does not check for a manager update when the context is locked',
    'update_all_confirmation_lists_every_update',
    'update_all_locked_text',
    'update_all_macos_notice',
    'update_all_linux_notice',
    'update_all_disables_actions',
    'orphan_card_renders_remove',
    'update_all_progress_lines',
    'update_all_malformed',
    'update_all_settle_rereads_status',
    'update_all_no_cancel_control',
    'settings_section_cases',
    'settings_root_change_rereads_status',
    'main_mounts_settings_on_linux',
    'main_skips_settings_on_macos',
    'main_rereads_status_after_root_change',
    'update_all_excludes_manager_line',
  ],
  node: [
    'bundle sidecar name per host tuple',
    'fixture inno script keeps the plugin directives',
    'write test job prints verbatim windows paths',
    'bundle release mode argv',
    'bundle release asset table',
    'e2e update server serves only its directory',
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
  if (wf.includes(LIVE_NETWORK)) bad.push(`manager-ci.yml: ${LIVE_NETWORK} (FR-084)`);
}

function checkJob(wf, bad) {
  const jobs = jobNames(wf);
  if (!util.isDeepStrictEqual(jobs, JOBS)) bad.push(`jobs ${JSON.stringify(jobs)}`);
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

  const node = step('uses: actions/setup-node@pinned');
  if (node && field(node.text, 10, 'node-version') !== "'24'") bad.push('setup-node: node-version is not \'24\'');
  const cache = step('uses: Swatinem/rust-cache@pinned');
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
  // Phase 10 PD-20: `npm run bundle` runs tauri build.
  for (const line of wf.split('\n')) {
    const build = line.includes('tauri build') || line.includes('npm run bundle');
    if (build && line.includes('dev-feed')) bad.push(`dev-feed on a tauri build line: ${line.trim()}`);
  }

  // Phase 11 PD-32: --features updater-e2e and the KRATE_E2E_* names, within check, only in clippy updater-e2e.
  for (const [name, cmd] of Object.entries(E2E_FEATURE_STEPS)) {
    const s = step(name);
    if (!s) continue;
    const line = scriptLines(s).find((l) => l.includes(E2E_FEATURE));
    if (!line || !line.includes(cmd)) bad.push(`${name}: no ${cmd.trim()} ... ${E2E_FEATURE} command`);
    const env = trimmed(nested(s.text, 8, 'env'));
    if (!util.isDeepStrictEqual(env, E2E_ENV)) bad.push(`${name}: env ${JSON.stringify(env)}, want the PD-32 dummies`);
    if (s.text.includes('dev-feed')) bad.push(`${name}: dev-feed`);
    for (const tok of [E2E_FEATURE, ...E2E_NAMES]) {
      const inStep = count(s.text, tok);
      const inCheck = steps.reduce((n, x) => n + count(x.text, tok), 0);
      if (inStep < 1) bad.push(`${name}: no ${tok}`);
      if (inCheck !== inStep) bad.push(`${tok} outside the step "${name}" in check`);
    }
  }
}

function checkPackageJson(text, bad) {
  if (text.includes(LIVE_NETWORK)) bad.push(`package.json: ${LIVE_NETWORK} (FR-084)`);
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
  if (!util.isDeepStrictEqual(req, EXPECTED_REQUIRED)) {
    bad.push('required-tests.json differs from plan §4.18 + Phase 5 plan §4.13');
  }
}

/** Every finding on the given file texts; [] when the shape holds. */
function checkManagerCi(files) {
  const bad = [];
  const wf = files.workflow.replace(/\r\n/g, '\n');
  checkWorkflowTop(wf, bad);
  const steps = checkJob(wf, bad);
  if (steps) checkSteps(wf, steps, bad);
  checkElevatedJobs(wf, bad);
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

/** [head, end) of the step named `name` in `lines` (its `- name:` line up to the next step item or the end). */
function stepSpan(lines, name) {
  const head = lines.findIndex((l) => {
    const m = /^ {6}- name:\s*(.*)$/.exec(l);
    return m !== null && unquote(stripComment(m[1])) === name;
  });
  if (head < 0) throw new Error(`mutation: no step ${name}`);
  let end = head + 1;
  while (end < lines.length && !/^ {6}- /.test(lines[end]) && !(lines[end].trim() !== '' && indentOf(lines[end]) < 6)) {
    end++;
  }
  return [head, end];
}

/** `text` without the step named `name`. */
function removeStep(text, name) {
  const lines = text.split('\n');
  const [head, end] = stepSpan(lines, name);
  return [...lines.slice(0, head), ...lines.slice(end)].join('\n');
}

/** `text` with the step named `name` moved to just before the step named `before`. */
function moveStepBefore(text, name, before) {
  const lines = text.split('\n');
  const [head, end] = stepSpan(lines, name);
  const moved = lines.slice(head, end);
  const rest = [...lines.slice(0, head), ...lines.slice(end)];
  const [at] = stepSpan(rest, before);
  return [...rest.slice(0, at), ...moved, ...rest.slice(at)].join('\n');
}

/** `text` without the lines from the one trimmed equal to `first` through the next one trimmed equal to `last`. */
function dropLines(text, first, last) {
  const lines = text.split('\n');
  const head = lines.findIndex((l) => l.trim() === first);
  if (head < 0 || lines.filter((l) => l.trim() === first).length !== 1) throw new Error(`mutation anchor: ${first}`);
  const end = lines.findIndex((l, i) => i > head && l.trim() === last);
  if (end < 0) throw new Error(`mutation: no ${last} after ${first}`);
  return [...lines.slice(0, head), ...lines.slice(end + 1)].join('\n');
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
    onWorkflow((w) => replaceOnce(w, 'npm run bundle 2>&1', 'npm run bundle -- --features dev-feed 2>&1')),
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
  // Phase 5 plan §4.12 (SI-4).
  [
    "the push path line - 'tools/feed-contract/**' removed",
    onWorkflow((w) =>
      replaceOnce(
        w,
        "  push:\n    branches: [main]\n    paths:\n      - 'apps/krate-manager/**'\n" +
          "      - '.github/workflows/manager-ci.yml'\n      - 'tools/lint-manager-version.js'\n" +
          "      - 'tools/feed-contract/**'\n",
        "  push:\n    branches: [main]\n    paths:\n      - 'apps/krate-manager/**'\n" +
          "      - '.github/workflows/manager-ci.yml'\n      - 'tools/lint-manager-version.js'\n",
      ),
    ),
    /on\.push\.paths/,
  ],
  ['the roster absent step removed', onWorkflow((w) => removeStep(w, 'roster absent')), /no step "roster absent"/],
  [
    'live-network added to a step',
    onWorkflow((w) =>
      replaceOnce(w, 'cargo test --workspace --locked 2>&1', 'cargo test --workspace --locked --features live-network 2>&1'),
    ),
    /manager-ci\.yml: live-network/,
  ],
  [
    'live-network added to a package.json script',
    (files) => {
      const pkg = JSON.parse(files.packageJson);
      pkg.scripts.test = `${pkg.scripts.test} && cargo test --features live-network`;
      return { ...files, packageJson: JSON.stringify(pkg, null, 2) };
    },
    /package\.json: live-network/,
  ],
  [
    'required-tests.json without fixture_server::tests::replies_behave_as_scripted',
    (files) => {
      const req = JSON.parse(files.required);
      req.cargo.all = req.cargo.all.filter((n) => n !== 'fixture_server::tests::replies_behave_as_scripted');
      return { ...files, required: JSON.stringify(req, null, 2) };
    },
    /required-tests\.json differs/,
  ],
  // Phase 11 (plan §4.11; PD-32, PD-33, PD-38; tasks T017).
  ...['clippy updater-e2e', 'clippy updater-refusals', 'updater refusals', 'updater-e2e absent'].map((name) => [
    `the ${name} step removed`,
    onWorkflow((w) => removeStep(w, name)),
    new RegExp(`no step "${name}"`),
  ]),
  [
    'clippy updater-e2e moved after clippy-guard',
    onWorkflow((w) => moveStepBefore(w, 'clippy updater-e2e', 'clippy updater-refusals')),
    /steps out of the plan §4\.16 order/,
  ],
  [
    "clippy updater-e2e's env: dropped",
    onWorkflow((w) =>
      replaceOnce(
        w,
        '      - name: clippy updater-e2e\n        env:\n' + E2E_ENV.map((l) => `          ${l}\n`).join(''),
        '      - name: clippy updater-e2e\n',
      ),
    ),
    /clippy updater-e2e: env null/,
  ],
  [
    `${E2E_FEATURE} added to clippy`,
    onWorkflow((w) => replaceOnce(w, CLIPPY_LINE, CLIPPY_LINE.replace('--locked', `${E2E_FEATURE} --locked`))),
    /--features updater-e2e outside the step "clippy updater-e2e" in check/,
  ],
  [
    'the harness count changed from 27',
    onWorkflow((w) => replaceOnce(w, "'harness: 27 ok, 0 not ok'", "'harness: 26 ok, 0 not ok'")),
    /updater refusals: no line "summary=/,
  ],
  [
    'the plant check dropped',
    onWorkflow((w) => dropLines(w, 'for plant in committed-key:E_HARNESS_KEY bind-any:E_HARNESS_BIND; do', 'done')),
    /updater refusals: no line "for plant in/,
  ],
  [
    'the marker count check dropped',
    onWorkflow((w) =>
      replaceOnce(w, '[ "$hits" = "0" ] && [ "$marker" -ge 1 ] && [ "$inb" = "0" ]; then', '[ "$hits" = "0" ] && [ "$inb" = "0" ]; then'),
    ),
    /updater-e2e absent: no line "if \[/,
  ],
  [
    `the updater refusals if: flipped against HARNESS_ON_WINDOWS (${HARNESS_ON_WINDOWS})`,
    onWorkflow((w) =>
      HARNESS_ON_WINDOWS
        ? replaceOnce(w, '      - name: updater refusals\n', "      - name: updater refusals\n        if: runner.os != 'Windows'\n")
        : replaceOnce(w, "      - name: updater refusals\n        if: runner.os != 'Windows'\n", '      - name: updater refusals\n'),
    ),
    /updater refusals: if: /,
  ],
];

// Phase 10 (plan §4.8.4, SC-031, SI-29): the gate, the changes script and FR-106's bans.
const CHECK_PLANT_ANCHOR = 'cargo test --workspace --locked 2>&1 | tee "$RUNNER_TEMP/cargo-test.txt"';
const plantInCheck = (line) => onWorkflow((w) => replaceOnce(w, CHECK_PLANT_ANCHOR, `${CHECK_PLANT_ANCHOR}\n          ${line}`));
const CLIPPY_LINE = 'cargo clippy --workspace --all-targets --locked -- -D warnings';

const ELEVATED_MUTATIONS = [
  [
    'needs removed from helper-elevated',
    onWorkflow((w) =>
      replaceOnce(
        w,
        '    name: helper-elevated (${{ matrix.os }})\n    needs: changes\n',
        '    name: helper-elevated (${{ matrix.os }})\n',
      ),
    ),
    /helper-elevated: needs null, want changes/,
  ],
  [
    'if removed from helper-elevated',
    onWorkflow((w) =>
      replaceOnce(
        w,
        `    needs: changes\n    if: ${GATE_IF}\n    runs-on: \${{ matrix.os }}\n`,
        '    needs: changes\n    runs-on: ${{ matrix.os }}\n',
      ),
    ),
    /helper-elevated: if null/,
  ],
  [
    'needs removed from inno-locked-file',
    onWorkflow((w) =>
      replaceOnce(
        w,
        '    name: inno-locked-file (windows-latest)\n    needs: changes\n',
        '    name: inno-locked-file (windows-latest)\n',
      ),
    ),
    /inno-locked-file: needs null, want changes/,
  ],
  [
    'if removed from inno-locked-file',
    onWorkflow((w) => replaceOnce(w, `    if: ${GATE_IF}\n    runs-on: windows-latest\n`, '    runs-on: windows-latest\n')),
    /inno-locked-file: if null/,
  ],
  [
    'FR-102a classes comparison removed from inno-locked-file',
    onWorkflow((w) => replaceOnce(w, 'Get-Content -LiteralPath "$m1/classes.json" -Raw', 'Get-Content -LiteralPath "classes.json" -Raw')),
    /inno-locked-file: does not compare with inno-locked\/classes\.json/,
  ],
  [
    'check gated by changes',
    onWorkflow((w) => replaceOnce(w, '  check:\n    name: check', '  check:\n    needs: changes\n    name: check')),
    /check: needs: changes \(check is not gated\)/,
  ],
  [
    'the workflow_dispatch trigger removed',
    onWorkflow((w) => replaceOnce(w, '  workflow_dispatch:\n', '')),
    /on: triggers/,
  ],
  [
    "a feature token in check's clippy step",
    onWorkflow((w) => replaceOnce(w, CLIPPY_LINE, CLIPPY_LINE.replace('--locked', `${CI_FEATURE} --locked`))),
    /--features ci-temp-roots outside the two elevated jobs/,
  ],
  [
    'manual-elevation outside its step',
    onWorkflow((w) => replaceOnce(w, CLIPPY_LINE, `${CLIPPY_LINE} && cargo check -p krate-manager --features ${MANUAL}`)),
    /manual-elevation outside the step/,
  ],
  [
    'sudo in inno-locked-file',
    onWorkflow((w) =>
      replaceOnce(w, '      - name: locked-dll builds\n        run: |\n', '      - name: locked-dll builds\n        run: |\n          sudo whoami\n'),
    ),
    /inno-locked-file: sudo/,
  ],
  [
    'a bash Windows helper step',
    onWorkflow((w) =>
      replaceOnce(
        w,
        "      - name: helper jobs (Windows)\n        if: runner.os == 'Windows'\n        shell: pwsh\n",
        "      - name: helper jobs (Windows)\n        if: runner.os == 'Windows'\n        shell: bash\n",
      ),
    ),
    /Windows helper step "helper jobs \(Windows\)" is not shell: pwsh/,
  ],
  [
    'ubuntu-latest in changes',
    onWorkflow((w) => replaceOnce(w, '    runs-on: ubuntu-24.04\n', '    runs-on: ubuntu-latest\n')),
    /changes\.runs-on ubuntu-latest/,
  ],
  ['pkgbuild in check', plantInCheck('pkgbuild --root "$RUNNER_TEMP/s" x.pkg'), /pkgbuild outside/],
  [
    'Minionguyjpro/Inno-Setup-Action in changes',
    onWorkflow((w) =>
      replaceOnce(w, '        with:\n          fetch-depth: 0\n', `        with:\n          fetch-depth: 0\n\n      - ${INNO_ACTION}\n`),
    ),
    /Minionguyjpro\/Inno-Setup-Action outside/,
  ],
  ['installer -pkg in check', plantInCheck('installer -pkg x.pkg -target /'), /installer -pkg outside/],
  [
    'a helper run with --job in check',
    plantInCheck('target/release/krate-manager-helper --job "$RUNNER_TEMP/j.json" --job-sha256 0'),
    /a helper run with arguments outside/,
  ],
  [
    'the pull_request branch removed',
    onWorkflow((w) => replaceOnce(w, CHANGES_LINES[2], 'from="$BEFORE"; to="$SHA"')),
    /changes: no line ".*pull_request/,
  ],
  [
    'the all-zero before fail-open removed',
    onWorkflow((w) => replaceOnce(w, CHANGES_LINES[3], 'if false; then')),
    /changes: no line "if \[ -z/,
  ],
  [
    'the failing git diff fail-open removed',
    onWorkflow((w) => replaceOnce(w, CHANGES_LINES[5], CHANGES_LINES[5].replace('"$to")', '"$to" || true)'))),
    /changes: no line "elif ! files/,
  ],
  [
    'Cargo.lock dropped from the changed-paths list',
    onWorkflow((w) => replaceOnce(w, '            apps/krate-manager/Cargo.lock\n', '')),
    /changes: changed-paths list/,
  ],
  [
    'the receipt forget step without always()',
    onWorkflow((w) => replaceOnce(w, `if: ${FORGET_IF}`, "if: runner.os == 'macOS'")),
    /the forget step if/,
  ],
];

test('manager-ci.yml keeps the Phase 4 shape', async (t) => {
  const files = readFiles();
  let pathGatedRan = false;

  await t.test('the real files give no finding', () => {
    assert.deepEqual(checkManagerCi(files), []);
  });

  await t.test('a CRLF copy of the real files gives no finding', () => {
    const crlf = { ...files, workflow: files.workflow.replace(/\n/g, '\r\n') };
    assert.deepEqual(checkManagerCi(crlf), []);
  });

  // SC-031 (plan §4.8.4 placement): a subtest, so it adds no top-level test to the feed-tools TAP plan.
  await t.test('elevated jobs are path-gated', async (t2) => {
    const wf = files.workflow;
    const own = [];
    checkElevatedJobs(wf, own);
    assert.deepEqual(own, []);
    const triggers = (nested(wf, 0, 'on') || []).map((l) => /^ {2}([A-Za-z0-9_-]+):/.exec(l)).filter(Boolean);
    assert.ok(triggers.some((m) => m[1] === 'workflow_dispatch'), 'workflow_dispatch is not a trigger');
    assert.deepEqual(jobNames(wf), JOBS);
    for (const j of ELEVATED_JOBS) {
      assert.equal(field(jobBlock(wf, j), 4, 'needs'), 'changes', `${j} needs`);
      assert.equal(field(jobBlock(wf, j), 4, 'if'), GATE_IF, `${j} if`);
    }
    assert.equal(field(jobBlock(wf, 'check'), 4, 'needs'), null, 'check needs');
    assert.equal(field(jobBlock(wf, 'check'), 4, 'if'), null, 'check if');
    const paths = stepBlocks(jobBlock(wf, 'changes')).find((s) => s.label === 'changed paths');
    assert.ok(paths, 'no changed paths step');
    assert.deepEqual(pathArray(runText(paths)), CHANGED_PATHS);
    for (const [name, mutate, expected] of ELEVATED_MUTATIONS) {
      await t2.test(`mutation: ${name}`, () => {
        const mutated = mutate(files);
        assert.notDeepEqual(mutated, files, 'the mutation changed nothing');
        const errs = checkManagerCi(mutated);
        assert.ok(errs.some((e) => expected.test(e)), `no finding matches ${expected}: ${JSON.stringify(errs)}`);
      });
    }
    pathGatedRan = true;
  });

  await t.test('manager-ci.yml does not name live-network (FR-084)', () => {
    assert.equal(count(files.workflow, LIVE_NETWORK), 0);
  });

  await t.test('package.json does not name live-network (FR-084)', () => {
    assert.equal(count(files.packageJson, LIVE_NETWORK), 0);
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

  assert.equal(pathGatedRan, true, 'elevated jobs are path-gated did not run');
});
