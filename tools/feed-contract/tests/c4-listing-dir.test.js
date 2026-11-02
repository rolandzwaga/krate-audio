'use strict';
require('./helpers/write-guard.js');

// `C4 listings from extracted trees` (spec SC-009; FR-032, D4; plan §4.3, §7.1).
// POSIX: each good archive is extracted with the system tar into an fs.mkdtemp directory, and listingFromDirectory
// of that tree must equal the committed .listing (so the archive listing and the on-disk listing agree, symlinks and
// exec bits included, and macOS's /var -> /private/var never leaks into a path).
// win32: NTFS carries no POSIX modes, so the same subtests assert the E_LISTING_NO_POSIX_MODES refusal instead; the
// subtest names are identical on every OS, so `# tests` matches across the three CI legs (SC-018).
// The POSIX branch is PENDING-CI: feed-tools.yml node-test (ubuntu-latest) and node-test (macos-latest).

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const childProcess = require('node:child_process');

const { ContractError } = require('../contract.js');
const { listingFromDirectory, toText } = require('../listing.js');

const GOOD_DIR = path.join(__dirname, '..', 'fixtures', 'listings', 'good');

const GOOD = [
  'real-shaped',
  'exec-and-symlink',
  'long-name',
  'long-link',
  'pax-path',
  'pax-global',
  'implicit-dirs',
  'dot-slash',
  'ustar-prefix',
];

test('C4 listings from extracted trees', async (t) => {
  for (const name of GOOD) {
    await t.test(`extracted ${name} lists as its committed .listing`, () => {
      const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'c4-dir-'));
      try {
        if (process.platform === 'win32') {
          assert.throws(
            () => listingFromDirectory(tmp),
            (err) => err instanceof ContractError && err.code === 'E_LISTING_NO_POSIX_MODES',
          );
          return;
        }
        const archive = path.resolve(GOOD_DIR, `${name}.tar.gz`);
        const r = childProcess.spawnSync('tar', ['-xzf', archive, '-C', tmp], { cwd: tmp, encoding: 'utf8' });
        assert.equal(r.status, 0, `tar -xzf ${name}.tar.gz: ${r.error || r.stderr}`);

        const expected = fs.readFileSync(path.join(GOOD_DIR, `${name}.listing`), 'utf8');
        const lines = listingFromDirectory(tmp);
        const special = expected.split('\n').filter((x) => x.startsWith('l ') || x.startsWith('f 0755 '));
        for (const line of special) assert.ok(lines.includes(line), `${name}: on-disk listing lacks ${line}`);
        assert.deepEqual(
          lines.filter((x) => x.includes('/private/')),
          [],
          `${name}: a realpath leaked into the listing`,
        );
        assert.equal(toText(lines), expected, `${name}: on-disk listing differs from ${name}.listing`);
      } finally {
        fs.rmSync(tmp, { recursive: true, force: true });
      }
    });
  }
});
