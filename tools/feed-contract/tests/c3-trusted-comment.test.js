'use strict';
require('./helpers/write-guard.js');

// `C3 trusted comments` (spec SC-007; FR-020 to FR-022, FR-057; plan §4.2).
// Reads fixtures/trusted-comments.json (built by fixtures/make-fixtures.js) and checks the C3 grammar, the
// formatter round trip, the binding check, and the minisign text reader (`parseMinisigText`, `toLfMinisig`).

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const contract = require('../contract.js');
const { fixtureSignature } = require('../fixtures/make-fixtures.js');

const {
  ContractError,
  CODES,
  PLATFORM_KEYS,
  formatTrustedComment,
  parseTrustedComment,
  checkBinding,
  parseMinisigText,
  toLfMinisig,
  assetNameFor,
  isStableCore,
} = contract;

const FIXTURE = path.join(__dirname, '..', 'fixtures', 'trusted-comments.json');
const data = JSON.parse(fs.readFileSync(FIXTURE, 'utf8'));

function expectCode(fn, code, label) {
  assert.throws(
    fn,
    (err) => {
      assert.ok(err instanceof ContractError, `${label}: expected a ContractError, got ${err && err.name}`);
      assert.equal(err.code, code, `${label}: ${err.message}`);
      return true;
    },
    label,
  );
}

const show = (s) => JSON.stringify(s);

test('C3 trusted comments', async (t) => {
  await t.test('valid comments parse and round-trip through formatTrustedComment', () => {
    assert.ok(Array.isArray(data.valid) && data.valid.length > 0, 'no valid comments');
    for (const c of data.valid) {
      const parsed = parseTrustedComment(c);
      assert.deepEqual(Object.keys(parsed).sort(), ['asset', 'name', 'platform', 'version'], show(c));
      assert.equal(formatTrustedComment(parsed.name, parsed.version, parsed.platform, parsed.asset), c, show(c));
    }
  });

  await t.test('the valid set covers stable, prerelease and every platform key with its FR-017 asset name', () => {
    const parsed = data.valid.map((c) => parseTrustedComment(c));
    assert.ok(parsed.some((p) => isStableCore(p.version)), 'no stable version');
    assert.ok(parsed.some((p) => p.version === '1.2.0-beta.1'), 'no 1.2.0-beta.1 prerelease');
    for (const key of PLATFORM_KEYS) {
      assert.ok(parsed.some((p) => p.platform === key), `no comment for ${key}`);
    }
    for (const p of parsed) {
      assert.equal(p.asset, assetNameFor(p.name, p.version, p.platform), `asset name of ${show(p)}`);
    }
  });

  await t.test('every invalid comment throws its expected code', () => {
    assert.ok(Array.isArray(data.invalid) && data.invalid.length > 0, 'no invalid comments');
    for (const row of data.invalid) {
      assert.equal(typeof row.why, 'string', `row without why: ${show(row.comment)}`);
      assert.equal(CODES[row.code], 'degrade', `${row.code} is not a degrade-class code`);
      expectCode(() => parseTrustedComment(row.comment), row.code, `parse ${show(row.comment)} (${row.why})`);
    }
  });

  await t.test('the invalid set holds every FR-057 category', () => {
    const byComment = new Map(data.invalid.map((r) => [r.comment, r.code]));
    const has = (pred, code, label) =>
      assert.ok(
        data.invalid.some((r) => pred(r.comment) && r.code === code),
        `missing invalid row: ${label} -> ${code}`,
      );
    const fields = (c) => c.split(':');
    const ok6 = (c) => fields(c).length === 6 && c.startsWith('krate:1:');
    has((c) => fields(c).length === 5, 'E_TC_FIELDS', 'five fields');
    has((c) => fields(c).length === 7, 'E_TC_FIELDS', 'seven fields');
    has((c) => c.startsWith('krate:2:'), 'E_TC_FIELDS', 'prefix krate:2');
    has((c) => ok6(c) && fields(c)[4] === 'windows-aarch64', 'E_TC_PLATFORM', 'platform windows-aarch64');
    for (const v of ['1.2', '01.2.3', '1.2.3+b1', '1.2.4294967296']) {
      has((c) => ok6(c) && fields(c)[3] === v, 'E_TC_VERSION', `version ${v}`);
    }
    has((c) => ok6(c) && fields(c)[5].includes('/'), 'E_TC_ASSET', 'asset with /');
    has((c) => ok6(c) && fields(c)[5].includes('\\'), 'E_TC_ASSET', 'asset with \\');
    has((c) => ok6(c) && fields(c)[5] === '..', 'E_TC_ASSET', 'asset ..');
    has(
      (c) => fields(c).length === 7 && c.startsWith('krate:1:') && /\.(exe|pkg|tar\.gz)$/.test(c),
      'E_TC_FIELDS',
      'asset with :',
    );
    has((c) => ok6(c) && fields(c)[2] === '', 'E_TC_NAME', 'empty name');
    has((c) => c.includes('\r'), 'E_TC_FIELDS', 'embedded CR');
    assert.equal(byComment.size, data.invalid.length, 'duplicate invalid comments');
  });

  await t.test('formatTrustedComment rejects each invalid field value with the code parsing gives', () => {
    let exercised = 0;
    for (const row of data.invalid) {
      const f = row.comment.split(':');
      // Rows whose fault is the frame (field count below six, wrong prefix) have no field values to format.
      if (f.length < 6 || f[0] !== 'krate' || f[1] !== '1') continue;
      const args = [f[2], f[3], f[4], f.slice(5).join(':')];
      expectCode(() => formatTrustedComment(...args), row.code, `format ${show(args)} (${row.why})`);
      exercised += 1;
    }
    assert.ok(exercised >= 10, `only ${exercised} invalid rows were field-value rows`);
  });

  await t.test('checkBinding: a matching comment passes, each differing field is named', () => {
    const { comment, matches, mismatches } = data.binding;
    assert.doesNotThrow(() => checkBinding(comment, matches));
    assert.ok(Array.isArray(mismatches) && mismatches.length >= 4, 'binding mismatches missing');
    for (const m of mismatches) {
      assert.throws(
        () => checkBinding(comment, m.metadata),
        (err) => {
          assert.ok(err instanceof ContractError, m.why);
          assert.equal(err.code, 'E_TC_BINDING', m.why);
          assert.equal(err.field, m.field, m.why);
          return true;
        },
        m.why,
      );
    }
    const named = new Set(mismatches.map((m) => m.field));
    for (const field of ['name', 'version', 'platform', 'asset']) assert.ok(named.has(field), `no ${field} mismatch`);
  });

  await t.test('checkBinding: asset A comment against asset B metadata names platform', () => {
    const a = { name: 'Alpha', version: '1.2.0', platform: 'linux-x86_64', asset: 'Alpha-1.2.0-Linux-x64.tar.gz' };
    const b = { name: 'Alpha', version: '1.2.0', platform: 'windows-x86_64', asset: 'Alpha-1.2.0-Windows-x64.exe' };
    const commentA = formatTrustedComment(a.name, a.version, a.platform, a.asset);
    const err = (() => {
      try {
        checkBinding(commentA, b);
      } catch (e) {
        return e;
      }
      return null;
    })();
    assert.ok(err instanceof ContractError);
    assert.equal(err.code, 'E_TC_BINDING');
    assert.equal(err.field, 'platform');
    expectCode(() => checkBinding(commentA, { ...a, version: '1.2.1' }), 'E_TC_BINDING', 'version only');
    assert.throws(() => checkBinding(commentA, { ...a, version: '1.2.1' }), { field: 'version' });
    assert.throws(() => checkBinding(commentA, { ...a, asset: 'Alpha-1.2.0-Linux-arm64.tar.gz' }), { field: 'asset' });
  });

  await t.test('parseMinisigText reads a fixture signature, LF and CRLF alike', () => {
    const comment = data.valid[0];
    const lf = fixtureSignature(comment);
    const sig = parseMinisigText(lf);
    assert.equal(sig.algorithm, 'ED');
    assert.ok(Buffer.isBuffer(sig.keyId));
    assert.equal(sig.keyId.length, 8);
    assert.equal(sig.trustedComment, comment);
    assert.equal(sig.untrustedComment, 'krate fixture signature (not cryptographic)');
    const crlf = lf.replace(/\n/g, '\r\n');
    assert.deepEqual(parseMinisigText(crlf), sig);
    assert.equal(toLfMinisig(crlf), lf);
    assert.equal(toLfMinisig(lf), lf);
  });

  await t.test('parseMinisigText rejects malformed texts with E_SIGNATURE_TEXT', () => {
    const lines = fixtureSignature(data.valid[0]).split('\n').slice(0, 4);
    const join = (ls) => ls.join('\n') + '\n';
    const b64 = (buf) => buf.toString('base64');
    const blob = (alg, len) => Buffer.concat([Buffer.from(alg, 'latin1'), Buffer.alloc(len - 2)]);

    const canonicalBlob = lines[1];
    assert.ok(canonicalBlob.endsWith('AAA='), 'fixture blob encoding changed; rebuild the non-canonical probe');
    const nonCanonical = canonicalBlob.slice(0, -2) + 'B=';
    // Same bytes, different text: the only fault is that the base64 is not canonical.
    assert.deepEqual(Buffer.from(nonCanonical, 'base64'), Buffer.from(canonicalBlob, 'base64'));

    const bad = {
      'three lines': join(lines.slice(0, 3)),
      'five lines': join([...lines, lines[3]]),
      'missing trusted comment prefix': join([lines[0], lines[1], lines[2].replace(/^trusted comment: /, ''), lines[3]]),
      'missing untrusted comment prefix': join([lines[0].replace(/^untrusted comment: /, ''), ...lines.slice(1)]),
      'blob of 73 bytes': join([lines[0], b64(blob('ED', 73)), lines[2], lines[3]]),
      'legacy Ed blob': join([lines[0], b64(blob('Ed', 74)), lines[2], lines[3]]),
      'non-canonical base64': join([lines[0], nonCanonical, lines[2], lines[3]]),
      'global signature of 63 bytes': join([...lines.slice(0, 3), b64(Buffer.alloc(63))]),
      'empty line': join([lines[0], '', lines[2], lines[3]]),
    };
    for (const [label, text] of Object.entries(bad)) {
      expectCode(() => parseMinisigText(text), 'E_SIGNATURE_TEXT', label);
    }
    // Positive control: the untouched lines parse.
    assert.doesNotThrow(() => parseMinisigText(join(lines)));
  });
});

// --- KAT and the FR-024 secret-key scan (SC-013; FR-023, FR-024)

const CONTRACT_DIR = path.join(__dirname, '..');
const KAT_ASSET = 'KatPlugin-1.2.3-Linux-x64.tar.gz';
const KAT_MINISIG = path.join(CONTRACT_DIR, 'fixtures', 'kat', `${KAT_ASSET}.minisig`);

// FR-024 rules (a)-(c) that hold for one file, as letters; [] when the file is not a secret-key file.
// (a) the name ends in .key; (b) an encrypted-secret-key header line; (c) a line ending "secret key" whose next line
// decodes to the secret-key layout: 'Ed' at bytes 0-1 and 'B2' at bytes 4-5 (a signature blob starts 'ED').
const RULE_B = /^untrusted comment: (rsign|minisign) encrypted secret key/;
const RULE_C = /^untrusted comment: .*secret key\r?$/;
function secretKeyRules(file) {
  const rules = [];
  if (path.basename(file).endsWith('.key')) rules.push('a');
  const lines = fs.readFileSync(file, 'latin1').split('\n');
  if (lines.some((l) => RULE_B.test(l))) rules.push('b');
  const keyBlob = (i) => {
    if (!RULE_C.test(lines[i]) || i + 1 >= lines.length) return false;
    const blob = Buffer.from(lines[i + 1].replace(/\r$/, ''), 'base64');
    return blob.length >= 6 && blob.toString('latin1', 0, 2) === 'Ed' && blob.toString('latin1', 4, 6) === 'B2';
  };
  if (lines.some((_, i) => keyBlob(i))) rules.push('c');
  return rules;
}

// Every regular file under root, recursively (links are not followed).
function walkFiles(root) {
  const out = [];
  for (const d of fs.readdirSync(root, { withFileTypes: true })) {
    const p = path.join(root, d.name);
    if (d.isDirectory()) out.push(...walkFiles(p));
    else if (d.isFile()) out.push(p);
  }
  return out;
}

const findSecretKeyFiles = (root) => walkFiles(root).filter((f) => secretKeyRules(f).length > 0);

// Every string value of a `signature` key anywhere in a JSON document.
function signatureValues(node, out = []) {
  if (Array.isArray(node)) node.forEach((x) => signatureValues(x, out));
  else if (node !== null && typeof node === 'object') {
    for (const [k, v] of Object.entries(node)) {
      if (k === 'signature' && typeof v === 'string') out.push(v);
      else signatureValues(v, out);
    }
  }
  return out;
}

test('KAT binds and no secret key is committed', () => {
  // The KAT signature: ED, a C3 comment bound to the archive, LF only.
  const bytes = fs.readFileSync(KAT_MINISIG);
  assert.equal(bytes.includes(0x0d), false, 'the KAT .minisig contains a CR');
  const sig = parseMinisigText(bytes.toString('utf8'));
  assert.equal(sig.algorithm, 'ED');
  assert.doesNotThrow(() =>
    checkBinding(sig.trustedComment, {
      name: 'KatPlugin',
      version: '1.2.3',
      platform: 'linux-x86_64',
      asset: KAT_ASSET,
    }),
  );

  // The whole tools/feed-contract/ tree: no secret-key file, and the KAT .minisig is scanned and not flagged.
  const tree = walkFiles(CONTRACT_DIR);
  assert.ok(tree.includes(KAT_MINISIG), 'the scan did not visit the KAT .minisig');
  assert.deepEqual(secretKeyRules(KAT_MINISIG), [], 'the KAT .minisig is flagged');
  assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), []);

  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km1-keys-'));
  try {
    // Every fixture signature value, as its own file: none is flagged.
    const sigDir = path.join(tmp, 'signatures');
    fs.mkdirSync(sigDir);
    let n = 0;
    for (const set of ['feeds', 'release']) {
      for (const f of walkFiles(path.join(CONTRACT_DIR, 'fixtures', set)).filter((x) => x.endsWith('.json'))) {
        let doc;
        try {
          doc = JSON.parse(fs.readFileSync(f, 'utf8'));
        } catch {
          continue; // feeds/invalid/malformed-json.json is not JSON at all
        }
        for (const s of signatureValues(doc)) {
          fs.writeFileSync(path.join(sigDir, `sig-${n}.minisig`), s);
          n += 1;
        }
      }
    }
    assert.ok(n > 0, 'no fixture signature values found');
    assert.deepEqual(findSecretKeyFiles(sigDir), [], `${n} fixture signature values`);

    // Synthetic key-shaped files: each is flagged by exactly its rule, so a scan that flags nothing fails.
    const synth = path.join(tmp, 'synthetic');
    fs.mkdirSync(synth);
    const blobC = Buffer.concat([Buffer.from('EdScB2', 'latin1'), Buffer.alloc(100, 0x41)]).toString('base64');
    const cases = [
      ['x.key', 'not a key\n', 'a'],
      ['rule-b.txt', 'untrusted comment: rsign encrypted secret key\n', 'b'],
      ['rule-c.txt', `untrusted comment: some secret key\n${blobC}\n`, 'c'],
    ];
    for (const [name, content, rule] of cases) {
      const p = path.join(synth, name);
      fs.writeFileSync(p, content);
      assert.deepEqual(secretKeyRules(p), [rule], `${name} must be flagged by rule (${rule}) alone`);
    }
    assert.deepEqual(
      findSecretKeyFiles(synth).map((p) => path.basename(p)).sort(),
      cases.map(([name]) => name).sort(),
    );
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});
