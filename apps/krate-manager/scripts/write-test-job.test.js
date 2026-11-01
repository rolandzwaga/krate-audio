// node:test coverage of write-test-job.js (spec FR-001, FR-027, FR-100; plan §4.6).
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, readdirSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { run, verbatim } from './write-test-job.js';

await test('write test job prints verbatim windows paths', () => {
  // `verbatim` rows: drive, UNC, already verbatim, and every non-win32 platform unchanged.
  assert.equal(verbatim('C:\\Users\\u\\a.exe', 'win32'), '\\\\?\\C:\\Users\\u\\a.exe');
  assert.equal(verbatim('D:\\a\\_temp\\real', 'win32'), '\\\\?\\D:\\a\\_temp\\real');
  assert.equal(verbatim('\\\\server\\share\\x', 'win32'), '\\\\?\\UNC\\server\\share\\x');
  assert.equal(verbatim('\\\\?\\C:\\x\\y.exe', 'win32'), '\\\\?\\C:\\x\\y.exe');
  assert.equal(verbatim('\\\\?\\UNC\\server\\share\\x', 'win32'), '\\\\?\\UNC\\server\\share\\x');
  for (const platform of ['linux', 'darwin']) {
    assert.equal(verbatim('C:\\x\\y.exe', platform), 'C:\\x\\y.exe');
    assert.equal(verbatim('\\\\server\\share\\x', platform), '\\\\server\\share\\x');
    assert.equal(verbatim('/tmp/x/y.pkg', platform), '/tmp/x/y.pkg');
  }

  // A job written into a temp state dir with the platform forced to win32.
  const state = mkdtempSync(path.join(os.tmpdir(), 'km-write-test-job-'));
  try {
    const staging = path.join(state, 'staging');
    mkdirSync(staging);
    const exe = path.join(staging, 'a.exe');
    const pkg = path.join(staging, 'b.pkg');
    writeFileSync(exe, 'exe');
    writeFileSync(pkg, 'pkg');
    const sha = 'a'.repeat(64);
    const appId = '{01234567-89AB-CDEF-0123-456789ABCDEF}';
    const line = run(
      [
        '--state', state,
        '--op', 'run_inno_installer', '--file', exe, '--sha256', sha,
        '--op', 'run_inno_uninstaller', '--app-id', appId,
        '--op', 'run_pkg_installer', '--sha256', sha, '--file', pkg,
        '--op', 'uninstall_pkg', '--pkg-id', 'com.krateaudio.alpha',
      ],
      'win32',
    );

    const jobsDir = path.join(realpathSync.native(state), 'jobs');
    const names = readdirSync(jobsDir);
    assert.equal(names.length, 1, `one job file, got ${names.join(', ')}`);
    const name = names[0] ?? '';
    assert.match(name, /^job-[0-9a-f]{32}\.json$/);
    const written = path.join(jobsDir, name);
    const bytes = readFileSync(written);

    const space = line.lastIndexOf(' ');
    const printedPath = line.slice(0, space);
    const printedSha = line.slice(space + 1);
    assert.equal(printedPath, verbatim(written, 'win32'));
    assert.ok(printedPath.startsWith('\\\\?\\'), printedPath);
    assert.equal(printedSha, createHash('sha256').update(bytes).digest('hex'));

    const text = bytes.toString('utf8');
    assert.ok(text.startsWith('{"job_version":1,"ops":[{"op":"run_inno_installer","file":'), text);
    /** @type {unknown} */
    const parsed = JSON.parse(text);
    const job = /** @type {{job_version: number, ops: Record<string, string>[]}} */ (parsed);
    assert.deepEqual(Object.keys(job), ['job_version', 'ops']);
    assert.equal(job.job_version, 1);
    assert.deepEqual(
      job.ops.map((op) => Object.keys(op)),
      [['op', 'file', 'sha256'], ['op', 'app_id'], ['op', 'file', 'sha256'], ['op', 'pkg_id']],
    );
    const [inno, uninst, pkgOp, forget] = job.ops;
    assert.deepEqual(inno, {
      op: 'run_inno_installer',
      file: verbatim(realpathSync.native(exe), 'win32'),
      sha256: sha,
    });
    assert.ok(inno.file.startsWith('\\\\?\\'), inno.file);
    assert.deepEqual(uninst, { op: 'run_inno_uninstaller', app_id: appId });
    assert.deepEqual(pkgOp, {
      op: 'run_pkg_installer',
      file: verbatim(realpathSync.native(pkg), 'win32'),
      sha256: sha,
    });
    assert.deepEqual(forget, { op: 'uninstall_pkg', pkg_id: 'com.krateaudio.alpha' });

    // Malformed argument lists are refused before anything is written.
    for (const bad of [
      [],
      ['--state', state],
      ['--state', state, '--op', 'run_shell', '--file', exe, '--sha256', sha],
      ['--state', state, '--op', 'run_inno_installer', '--file', exe],
      ['--state', state, '--op', 'run_inno_uninstaller', '--pkg-id', 'com.krateaudio.alpha'],
      ['--state', state, '--op', 'uninstall_pkg', '--pkg-id', 'a', '--pkg-id', 'b'],
      ['--op', 'uninstall_pkg', '--pkg-id', 'a'],
      ['--state', state, '--file', exe],
    ]) {
      assert.throws(() => run(bad, 'win32'), /usage/, bad.join(' '));
    }
    assert.equal(readdirSync(jobsDir).length, 1);
  } finally {
    rmSync(state, { recursive: true, force: true });
  }
});
