#!/usr/bin/env node
// Builds the Krate Manager bundle with its elevated helper as a Tauri sidecar (spec FR-092, FR-104;
// plan §4.6, PD-20): `cargo build -p krate-manager-helper --release --locked` (Windows and macOS only;
// no Linux helper binary is ever built, D18), copies it to
// `src-tauri/binaries/krate-manager-helper-<host tuple>[.exe]`, then runs `tauri build` with
// `src-tauri/tauri.helper.conf.json` (Windows, macOS) or plain `tauri build` (Linux).
// Child stdio is inherited and a failing step's exit code is this script's exit code.
//
// usage: node scripts/bundle.js
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const USAGE = 'usage: node scripts/bundle.js';
const HELPER = 'krate-manager-helper';
const APP_ROOT = path.resolve(import.meta.dirname, '..');

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
  return platform === 'linux' ? ['build'] : ['build', '--config', 'src-tauri/tauri.helper.conf.json'];
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

function main() {
  if (process.argv.length > 2) {
    process.stderr.write(`${USAGE}\n`);
    process.exit(2);
  }
  if (process.platform !== 'linux') {
    const tuple = hostTuple();
    run('cargo', ['build', '-p', HELPER, '--release', '--locked']);
    // cargo honours CARGO_TARGET_DIR, so the built helper is read from there when it is set.
    const targetDir = path.resolve(APP_ROOT, process.env.CARGO_TARGET_DIR ?? 'target');
    const built = path.join(targetDir, 'release', `${HELPER}${exeSuffix(tuple)}`);
    const binaries = path.join(APP_ROOT, 'src-tauri', 'binaries');
    mkdirSync(binaries, { recursive: true });
    copyFileSync(built, path.join(binaries, sidecarName(tuple)));
  }
  // The local `tauri` CLI entry point (`@tauri-apps/cli` package.json `bin.tauri`) run with this
  // Node: what `npx tauri` resolves to, without a shell (a `.cmd` shim needs one on Windows).
  const cli = path.join(APP_ROOT, 'node_modules', '@tauri-apps', 'cli', 'tauri.js');
  run(process.execPath, [cli, ...tauriArgs(process.platform)]);
}

const entry = process.argv.at(1);
if (entry !== undefined && import.meta.url === pathToFileURL(entry).href) main();
