#!/usr/bin/env node
// Builds the Krate Manager bundle with its elevated helper as a Tauri sidecar (spec FR-092, FR-104;
// plan §4.6, PD-20): `cargo build -p krate-manager-helper --release --locked` (Windows and macOS only;
// no Linux helper binary is ever built, D18), copies it to
// `src-tauri/binaries/krate-manager-helper-<host tuple>[.exe]`, then runs `tauri build` with
// `src-tauri/tauri.helper.conf.json` (Windows, macOS) or plain `tauri build` (Linux).
// Child stdio is inherited and a failing step's exit code is this script's exit code.
//
// Release mode (Phase 11 spec FR-030 to FR-033; plan §4.8, PD-15, PD-20, PD-22): `--release` adds
// `src-tauri/tauri.release.conf.json` (and every file in KRATE_BUNDLE_EXTRA_CONFIG) to the `tauri build`
// argv; `--target universal-apple-darwin` (macOS) builds the helper for both Apple targets and joins
// them with `lipo -create`; `--bundles <list>` (Linux) and `--features updater-e2e` are passed on.
// After the build, each bundler output is copied to `target/release-assets/` under its D6 name; a
// pattern that matches zero or more than one file is `E_BUNDLE_ASSET` (exit 1).
//
// usage: node scripts/bundle.js [--release [--target universal-apple-darwin] [--bundles <list>] [--features updater-e2e]]
import { spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, statSync } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const USAGE =
  'usage: node scripts/bundle.js [--release [--target universal-apple-darwin] [--bundles <list>] [--features updater-e2e]]';
const HELPER = 'krate-manager-helper';
const APP_ROOT = path.resolve(import.meta.dirname, '..');
const UNIVERSAL = 'universal-apple-darwin';
const APPLE_TARGETS = ['aarch64-apple-darwin', 'x86_64-apple-darwin'];
const HELPER_CONFIG = 'src-tauri/tauri.helper.conf.json';
const RELEASE_CONFIG = 'src-tauri/tauri.release.conf.json';
const E2E_FEATURE = 'updater-e2e';

/**
 * @typedef {{ release: boolean, target?: 'universal-apple-darwin', bundles?: string[], features?: 'updater-e2e' }} BundleOptions
 * @typedef {{ dir: string, glob: string, name: string }} AssetRow
 */

/** A malformed argument list: exit code 2 (any other failure is exit 1). */
class UsageError extends Error {
  exitCode = 2;
}

/**
 * @param {string} message
 * @returns {never}
 */
function usage(message) {
  throw new UsageError(message);
}

/**
 * The sidecar file name Tauri's `externalBin` entry `binaries/krate-manager-helper` looks for:
 * the base name, `-`, the target tuple, and `.exe` on a Windows tuple.
 * @param {string} hostTuple a Rust target tuple, e.g. `x86_64-pc-windows-msvc`
 * @returns {string}
 */
export function sidecarName(hostTuple) {
  return `${HELPER}-${hostTuple}${exeSuffix(hostTuple)}`;
}

/**
 * The `tauri` CLI arguments for `platform` (a `process.platform` value): the helper config off
 * Linux, plain `build` on Linux (no helper is bundled there).
 * @param {string} platform
 * @returns {string[]}
 */
export function tauriArgs(platform) {
  return platform === 'linux' ? ['build'] : ['build', '--config', HELPER_CONFIG];
}

/**
 * Parses this script's arguments for `platform` (a `process.platform` value). No argument is the
 * Phase 10 path (`{ release: false }`). Throws a usage error (`exitCode` 2) for an unknown or repeated
 * argument, a missing value, `--target` off macOS or with any value but `universal-apple-darwin`,
 * `--bundles` off Linux or with an empty entry, `--features` with any value but `updater-e2e`, and
 * any option without `--release`.
 * @param {string[]} argv
 * @param {string} platform
 * @returns {BundleOptions}
 */
export function parseArgs(argv, platform) {
  /** @type {BundleOptions} */
  const opts = { release: false };
  let optionSeen = false;
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv.at(i) ?? '';
    if (arg === '--release') {
      if (opts.release) usage('--release given twice');
      opts.release = true;
      continue;
    }
    if (arg !== '--target' && arg !== '--bundles' && arg !== '--features') usage(`unknown argument ${arg}`);
    const value = argv.at(i + 1);
    if (value === undefined) usage(`missing value after ${arg}`);
    i += 1;
    optionSeen = true;
    if (arg === '--target') {
      if (opts.target !== undefined) usage('--target given twice');
      if (platform !== 'darwin') usage('--target is macOS only');
      if (value !== UNIVERSAL) usage(`--target must be ${UNIVERSAL}`);
      opts.target = UNIVERSAL;
    } else if (arg === '--bundles') {
      if (opts.bundles !== undefined) usage('--bundles given twice');
      if (platform !== 'linux') usage('--bundles is Linux only');
      const bundles = value.split(',');
      if (bundles.some((b) => b === '')) usage(`--bundles ${value} has an empty entry`);
      opts.bundles = bundles;
    } else {
      if (opts.features !== undefined) usage('--features given twice');
      if (value !== E2E_FEATURE) usage(`--features must be ${E2E_FEATURE}`);
      opts.features = E2E_FEATURE;
    }
  }
  if (optionSeen && !opts.release) usage('--target, --bundles and --features need --release');
  return opts;
}

/**
 * The `tauri` CLI arguments of a release build (plan §4.8): the helper config then the release
 * config (Windows, macOS; `--target` when given) or the release config alone (Linux; `--bundles`
 * when given), then `--config <file>` per extra config, then `--features updater-e2e` when set.
 * @param {string} platform
 * @param {BundleOptions} opts
 * @param {string[]} extra the files `extraConfigs` returned
 * @returns {string[]}
 */
export function releaseTauriArgs(platform, opts, extra) {
  const args =
    platform === 'linux'
      ? ['build', '--config', RELEASE_CONFIG]
      : ['build', '--config', HELPER_CONFIG, '--config', RELEASE_CONFIG];
  if (opts.target !== undefined) args.push('--target', opts.target);
  if (opts.bundles !== undefined) args.push('--bundles', opts.bundles.join(','));
  for (const file of extra) args.push('--config', file);
  if (opts.features !== undefined) args.push('--features', opts.features);
  return args;
}

/**
 * The three helper sidecar names a universal macOS build stages (D13): one per Apple target and
 * the `lipo -create` result Tauri bundles for `--target universal-apple-darwin`.
 * @returns {string[]}
 */
export function universalSidecars() {
  return [...APPLE_TARGETS, UNIVERSAL].map(sidecarName);
}

/**
 * The bundler outputs a release build copies, as `{ dir, glob, name }` rows (PD-15 dirs and globs,
 * D6 names as amended by SI-21: the `.deb` has no `.sig` row). `dir` is relative to the app root,
 * `target/` standing for the cargo target dir. On Linux, `opts.bundles` (when given) selects the
 * AppImage and `.deb` rows. The macOS rows always read the universal build's dir: a native-arch
 * build makes no universal archive, so its copy finds nothing (`E_BUNDLE_ASSET`).
 * @param {string} platform
 * @param {string} version
 * @param {BundleOptions} opts
 * @returns {AssetRow[]}
 */
export function assetTable(platform, version, opts) {
  const v = version;
  if (platform === 'win32') {
    const dir = 'target/release/bundle/nsis';
    const glob = `*_${v}_x64-setup.exe`;
    const name = `Krate-Manager-${v}-Windows-x64-setup.exe`;
    return [
      { dir, glob, name },
      { dir, glob: `${glob}.sig`, name: `${name}.sig` },
    ];
  }
  if (platform === 'darwin') {
    const dir = `target/${UNIVERSAL}/release/bundle/macos`;
    const glob = 'Krate Manager.app.tar.gz';
    const name = `Krate-Manager-${v}-macOS-universal.app.tar.gz`;
    return [
      { dir, glob, name },
      { dir, glob: `${glob}.sig`, name: `${name}.sig` },
    ];
  }
  /** @type {AssetRow[]} */
  const rows = [];
  const wants = (/** @type {string} */ bundle) => opts.bundles === undefined || opts.bundles.includes(bundle);
  if (wants('appimage')) {
    const dir = 'target/release/bundle/appimage';
    const glob = `*_${v}_amd64.AppImage`;
    const name = `Krate-Manager-${v}-Linux-x86_64.AppImage`;
    rows.push({ dir, glob, name }, { dir, glob: `${glob}.sig`, name: `${name}.sig` });
  }
  if (wants('deb')) {
    const dir = 'target/release/bundle/deb';
    rows.push({ dir, glob: `*_${v}_amd64.deb`, name: `Krate-Manager-${v}-Linux-amd64.deb` });
  }
  return rows;
}

/**
 * The extra `tauri build` config files: `KRATE_BUNDLE_EXTRA_CONFIG` split on `path.delimiter`
 * (unset or empty: none). Throws when an entry is not an existing file.
 * @param {Record<string, string | undefined>} env
 * @returns {string[]}
 */
export function extraConfigs(env) {
  const list = env.KRATE_BUNDLE_EXTRA_CONFIG ?? '';
  if (list === '') return [];
  return list.split(path.delimiter).map((file) => {
    const stat = statSync(file, { throwIfNoEntry: false });
    if (stat === undefined) throw new Error(`KRATE_BUNDLE_EXTRA_CONFIG entry ${file} does not exist`);
    if (!stat.isFile()) throw new Error(`KRATE_BUNDLE_EXTRA_CONFIG entry ${file} is not a file`);
    // Absolute, so the stat above, the `tauri` CLI (run in the app root) and the version read agree.
    return path.resolve(file);
  });
}

/**
 * The one name in `listing` that `glob` (`*` matches any run of characters, everything else is
 * literal) matches in full. Throws `E_BUNDLE_ASSET <glob> matched <n>` for zero or several (FR-032).
 * @param {string[]} listing the file names of the row's dir
 * @param {string} glob
 * @returns {string}
 */
export function resolveAsset(listing, glob) {
  const body = glob
    .split('*')
    .map((part) => part.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))
    .join('.*');
  const re = new RegExp(`^${body}$`, 'su');
  const matches = listing.filter((name) => re.test(name));
  const only = matches.at(0);
  if (matches.length !== 1 || only === undefined) {
    throw new Error(`E_BUNDLE_ASSET ${glob} matched ${String(matches.length)}`);
  }
  return only;
}

/**
 * @param {string} tuple
 * @returns {string} `.exe` for a Windows tuple, else empty
 */
function exeSuffix(tuple) {
  return tuple.includes('-windows-') ? '.exe' : '';
}

/**
 * @param {string} message
 * @returns {never}
 */
function fail(message) {
  process.stderr.write(`bundle: ${message}\n`);
  process.exit(1);
}

/**
 * Runs `command` in the app root with inherited stdio; exits with its code when it fails.
 * @param {string} command
 * @param {string[]} args
 */
function run(command, args) {
  const r = spawnSync(command, args, { cwd: APP_ROOT, stdio: 'inherit' });
  if (r.error !== undefined) fail(`cannot run ${command}: ${r.error.message}`);
  const status = r.status ?? 1;
  if (status !== 0) process.exit(status);
}

/** @returns {string} the tuple `rustc --print host-tuple` prints */
function hostTuple() {
  const r = spawnSync('rustc', ['--print', 'host-tuple'], {
    cwd: APP_ROOT,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  if (r.error !== undefined) fail(`cannot run rustc: ${r.error.message}`);
  const status = r.status ?? 1;
  if (status !== 0) process.exit(status);
  const tuple = r.stdout.trim();
  if (tuple === '') fail('rustc --print host-tuple printed nothing');
  return tuple;
}

/** @returns {string} the cargo target dir: CARGO_TARGET_DIR when set (cargo honours it), else `target` */
function cargoTargetDir() {
  return path.resolve(APP_ROOT, process.env.CARGO_TARGET_DIR ?? 'target');
}

/** @returns {string} the sidecar dir `tauri.helper.conf.json`'s `externalBin` names */
function binariesDir() {
  const binaries = path.join(APP_ROOT, 'src-tauri', 'binaries');
  mkdirSync(binaries, { recursive: true });
  return binaries;
}

/** Builds the helper for the host and stages it under its host-tuple sidecar name. */
function stageHostHelper() {
  const tuple = hostTuple();
  run('cargo', ['build', '-p', HELPER, '--release', '--locked']);
  const built = path.join(cargoTargetDir(), 'release', `${HELPER}${exeSuffix(tuple)}`);
  copyFileSync(built, path.join(binariesDir(), sidecarName(tuple)));
}

/**
 * Builds the helper for both Apple targets, stages each, and joins them into the universal
 * sidecar with `lipo -create -output` (FR-031, D13).
 */
function stageUniversalHelper() {
  const binaries = binariesDir();
  const staged = APPLE_TARGETS.map((target) => {
    run('cargo', ['build', '-p', HELPER, '--release', '--locked', '--target', target]);
    const to = path.join(binaries, sidecarName(target));
    copyFileSync(path.join(cargoTargetDir(), target, 'release', HELPER), to);
    return to;
  });
  run('lipo', ['-create', '-output', path.join(binaries, sidecarName(UNIVERSAL)), ...staged]);
}

/**
 * The version the bundler writes into its output names: `tauri.conf.json`'s `version`, overridden
 * by the last extra config that sets one (the CLI merges `--config` files in order; the e2e job's
 * extra config carries its old / new version, plan §4.10).
 * @param {string[]} extra
 * @returns {string}
 */
function bundleVersion(extra) {
  let version = '';
  for (const file of [path.join(APP_ROOT, 'src-tauri', 'tauri.conf.json'), ...extra]) {
    /** @type {unknown} */
    const doc = JSON.parse(readFileSync(file, 'utf8'));
    if (typeof doc === 'object' && doc !== null && 'version' in doc && typeof doc.version === 'string') {
      version = doc.version;
    }
  }
  if (version === '') fail('no version in src-tauri/tauri.conf.json');
  return version;
}

/**
 * Copies each `assetTable` row's single match to `target/release-assets/<D6 name>` (FR-032).
 * @param {AssetRow[]} rows
 */
function copyAssets(rows) {
  const out = path.join(APP_ROOT, 'target', 'release-assets');
  mkdirSync(out, { recursive: true });
  for (const row of rows) {
    const dir = path.join(cargoTargetDir(), ...row.dir.split('/').slice(1));
    // A missing bundle dir is an empty listing, so the refusal still names the glob.
    const listing = existsSync(dir) ? readdirSync(dir) : [];
    const from = path.join(dir, resolveAsset(listing, row.glob));
    copyFileSync(from, path.join(out, row.name));
    process.stdout.write(`bundle: ${from} -> ${path.join(out, row.name)}\n`);
  }
}

/**
 * The release build (plan §4.8).
 * @param {BundleOptions} opts
 */
function release(opts) {
  const extra = extraConfigs(process.env);
  if (process.platform !== 'linux') {
    if (opts.target === UNIVERSAL) stageUniversalHelper();
    else stageHostHelper();
  }
  const cli = path.join(APP_ROOT, 'node_modules', '@tauri-apps', 'cli', 'tauri.js');
  run(process.execPath, [cli, ...releaseTauriArgs(process.platform, opts, extra)]);
  copyAssets(assetTable(process.platform, bundleVersion(extra), opts));
}

function main() {
  /** @type {BundleOptions} */
  let opts;
  try {
    opts = parseArgs(process.argv.slice(2), process.platform);
  } catch (e) {
    if (!(e instanceof UsageError)) throw e;
    process.stderr.write(`bundle: ${e.message}\n${USAGE}\n`);
    process.exit(2);
  }
  if (opts.release) {
    try {
      release(opts);
    } catch (e) {
      fail(e instanceof Error ? e.message : String(e));
    }
    return;
  }
  if (process.platform !== 'linux') stageHostHelper();
  // The local `tauri` CLI entry point (`@tauri-apps/cli` package.json `bin.tauri`) run with this
  // Node: what `npx tauri` resolves to, without a shell (a `.cmd` shim needs one on Windows).
  const cli = path.join(APP_ROOT, 'node_modules', '@tauri-apps', 'cli', 'tauri.js');
  run(process.execPath, [cli, ...tauriArgs(process.platform)]);
}

const entry = process.argv.at(1);
if (entry !== undefined && import.meta.url === pathToFileURL(entry).href) main();
