// node:test coverage of e2e-update-server.js (spec FR-071; plan §4.9).
import assert from 'node:assert/strict';
import { Buffer } from 'node:buffer';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { parseArgs, startServer } from './e2e-update-server.js';

/**
 * One request against the server on 127.0.0.1:`port`.
 * @param {number} port
 * @param {string} method
 * @param {string} requestPath sent as written, never normalised
 * @returns {Promise<{ status: number, body: Buffer }>}
 */
function request(port, method, requestPath) {
  return new Promise((resolve, reject) => {
    const req = http.request({ host: '127.0.0.1', port, method, path: requestPath, agent: false }, (res) => {
      /** @type {Buffer[]} */
      const chunks = [];
      res.on('data', (/** @type {Buffer} */ chunk) => chunks.push(chunk));
      res.on('end', () => {
        resolve({ status: res.statusCode ?? 0, body: Buffer.concat(chunks) });
      });
      res.on('error', reject);
    });
    req.on('error', reject);
    req.end();
  });
}

await test('e2e update server serves only its directory', async () => {
  const root = mkdtempSync(path.join(os.tmpdir(), 'km-e2e-server-'));
  const dir = path.join(root, 'serve');
  const log = path.join(root, 'requests.log');
  mkdirSync(dir);
  const latest = Buffer.from('{"version":"0.1.1"}\n');
  const versions = Buffer.from('{"schema_version":1}\n');
  const pkg = Buffer.from([0, 1, 2, 3, 255, 254, 10, 13]);
  writeFileSync(path.join(dir, 'latest.json'), latest);
  writeFileSync(path.join(dir, 'versions.json'), versions);
  writeFileSync(path.join(dir, 'pkg.bin'), pkg);
  mkdirSync(path.join(dir, 'sub'));
  writeFileSync(path.join(dir, 'sub', 'pkg.bin'), pkg);
  writeFileSync(path.join(root, 'outside.bin'), 'outside');

  // A symlink in the served dir that points out of it; symlinks need a privilege on Windows,
  // so the case runs wherever the link can be made (always on the Linux and macOS CI legs).
  let haveLink = true;
  try {
    symlinkSync(path.join(root, 'outside.bin'), path.join(dir, 'link.bin'));
  } catch {
    haveLink = false;
  }

  const server = await startServer({ dir, log });
  try {
    assert.equal(server.address, '127.0.0.1');
    const { port } = server;
    const release = '/o/r/releases/download/manager/v0.1.1';

    const got = await request(port, 'GET', '/latest.json');
    assert.equal(got.status, 200);
    assert.deepEqual(got.body, latest);
    const gotVersions = await request(port, 'GET', '/versions.json');
    assert.equal(gotVersions.status, 200);
    assert.deepEqual(gotVersions.body, versions);
    const gotPkg = await request(port, 'GET', `${release}/pkg.bin`);
    assert.equal(gotPkg.status, 200);
    assert.deepEqual(gotPkg.body, pkg);

    /** @type {[string, string][]} */
    const refused = [
      ['GET', `${release}/../latest.json`],
      ['GET', `${release}/%2e%2e/latest.json`],
      ['GET', `${release}/%2E%2E%2Flatest.json`],
      ['GET', `${release}/..%2flatest.json`],
      ['GET', '/sub/pkg.bin'],
      ['GET', `${release}/sub`],
      ['GET', `${release}/nope.bin`],
      ['GET', '/nope.json'],
      ['GET', '/pkg.bin'],
      ['GET', '/'],
      ['GET', '/o/r/releases/download/v0.1.1/pkg.bin'],
      ['POST', '/latest.json'],
      ['POST', `${release}/pkg.bin`],
    ];
    if (haveLink) refused.push(['GET', `${release}/link.bin`]);
    for (const [method, p] of refused) {
      const r = await request(port, method, p);
      assert.equal(r.status, 404, `${method} ${p}`);
      assert.equal(r.body.length, 0, `${method} ${p} body`);
    }

    const lines = readFileSync(log, 'utf8').split('\n');
    assert.equal(lines.pop(), '', 'the log ends with a newline');
    assert.deepEqual(lines, [
      `GET /latest.json 200 ${String(latest.length)}`,
      `GET /versions.json 200 ${String(versions.length)}`,
      `GET ${release}/pkg.bin 200 ${String(pkg.length)}`,
      ...refused.map(([method, p]) => `${method} ${p} 404 0`),
    ]);
  } finally {
    await server.close();
    rmSync(root, { recursive: true, force: true });
  }

  // The CLI form: every flag required once, no other flag; `--host` is refused (exit 2).
  const ok = ['--dir', 'd', '--port-file', 'p', '--log', 'l'];
  assert.deepEqual(parseArgs(ok), { dir: 'd', portFile: 'p', log: 'l' });
  /** @param {unknown} e */
  const exit2 = (e) => e instanceof Error && 'exitCode' in e && e.exitCode === 2;
  for (const argv of [
    ['--host', '0.0.0.0', ...ok],
    [...ok, '--host', '127.0.0.1'],
    [...ok, '--port', '8080'],
    ['--dir', 'd', '--port-file', 'p'],
    [...ok, '--dir', 'e'],
    [...ok, '--log'],
  ]) {
    assert.throws(() => parseArgs(argv), exit2, argv.join(' '));
  }
});
