// The only test-side spawner of real tools for the feed-contract tests (plan §4.12.2, PD-5; spec FR-071, D13).
//
// WHY: SC-003, SC-008 (POSIX half), SC-025 and SC-027 run rsign2 and tar. hygiene.test.js flags the child-process
// module in every scanned file, so the spawns live here, in one exempt helper (CHILD_EXEMPT), in exactly the argument
// shapes the write guard admits (FR-071). Every spawn runs with cwd a temp dir, stdin ignored and output piped.
//
// Callers load write-guard.js first (every test file's first statement) and pass paths under their own temp dir:
// the guard refuses any path argument outside os.tmpdir(), so a repo fixture an rsign call reads is copied first.
'use strict';
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const childProcess = require('node:child_process');

const RSIGN_VERSION = 'rsign2 0.6.7';

function run(cmd, args, cwd) {
  return childProcess.spawnSync(cmd, args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
}

// Throws unless the spawn started and exited 0; the message carries the tool's stderr.
function checked(label, r) {
  if (r.error) throw new Error(`${label}: ${r.error.code || r.error.message}`);
  if (r.status !== 0) throw new Error(`${label}: exit ${r.status}: ${String(r.stderr).trim()}`);
  return r;
}

/**
 * True when `rsign --version` prints exactly rsign2 0.6.7. Otherwise, with CI unset, skips the test (the found version
 * or the spawn error code as the reason) and returns false; with CI set, throws (D13: CI never skips an rsign case).
 * @param {{skip: (msg: string) => void}} t  the node:test context
 */
function rsignGate(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'km2-rsign-gate-'));
  let found;
  try {
    const r = run('rsign', ['--version'], dir);
    if (r.error) found = r.error.code || r.error.message;
    else if (/^rsign2 0\.6\.7\r?\n$/.test(r.stdout)) return true;
    else found = JSON.stringify(String(r.stdout).trim()) + ` (exit ${r.status})`;
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
  const msg = `${RSIGN_VERSION} required, found ${found}`;
  if (process.env.CI) throw new Error(msg);
  t.skip(msg);
  return false;
}

/** `rsign generate -W` into dir; returns the public and secret key paths. */
function rsignGenerate(dir) {
  const pub = path.join(dir, 'rsign.pub');
  const sec = path.join(dir, 'rsign.key');
  checked('rsign generate', run('rsign', ['generate', '-W', '-p', pub, '-s', sec], dir));
  return { pub, sec };
}

/** `rsign sign -W` of file with the trusted comment, writing the signature to sig. */
function rsignSign(dir, sec, comment, sig, file) {
  checked('rsign sign', run('rsign', ['sign', '-W', '-s', sec, '-t', comment, '-x', sig, file], dir));
}

/** `rsign verify`; a non-zero status is returned, not thrown (rsign prints its verdict on stderr, PD-17). */
function rsignVerify(dir, pub, sig, file) {
  const r = run('rsign', ['verify', '-p', pub, '-x', sig, file], dir);
  if (r.error) throw new Error(`rsign verify: ${r.error.code || r.error.message}`);
  return { status: r.status, stdout: r.stdout, stderr: r.stderr };
}

/** `tar -xzf src -C dest`, on the POSIX legs only (plan §4.12.2); dest is also the cwd. */
function extractTarGz(src, dest) {
  if (process.platform === 'win32') throw new Error('extractTarGz runs on POSIX only');
  checked('tar -xzf', run('tar', ['-xzf', src, '-C', dest], dest));
}

module.exports = { RSIGN_VERSION, rsignGate, rsignGenerate, rsignSign, rsignVerify, extractTarGz };
