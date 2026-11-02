'use strict';
require('./helpers/write-guard.js');

// tools/minisign.js: the KAT, the round trip, rsign2 interop and the verify CLI (spec FR-001 to FR-008, SC-001,
// SC-002, SC-003, SC-023; plan §4.1, §7.1), and secret-key decryption with the decrypt-key CLI (FR-009 to FR-009b,
// SC-027, SC-028).
// Repo fixtures are only read; every changed copy and every file rsign reads is written under an fs.mkdtemp dir.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const contract = require('../contract.js');
const minisign = require('../../minisign.js');
const { rsignGate, rsignGenerate, rsignSign, rsignVerify } = require('./helpers/external-tools.js');

const KAT_DIR = path.join(__dirname, '..', 'fixtures', 'kat');
const KAT_ASSET = 'KatPlugin-1.2.3-Linux-x64.tar.gz';
const KAT_TARBALL = path.join(KAT_DIR, KAT_ASSET);
const KAT_SIG = `${KAT_TARBALL}.minisig`;
const KAT_PUB = path.join(KAT_DIR, 'kat.pub');
const KAT_TC = `krate:1:KatPlugin:1.2.3:linux-x86_64:${KAT_ASSET}`;
const KAT_KEY_ID = '440FA75168B852F7';

// The FR-064 encrypted secret-key KAT, made by rsign2 0.6.7 with the published test password.
const KS_DIR = path.join(__dirname, '..', 'fixtures', 'phase2', 'kat-secret');
const KS_KEY = path.join(KS_DIR, 'kat.key');
const KS_PUB = path.join(KS_DIR, 'kat.pub');
const KS_PASSWORD = 'krate-kat-test-password';
const KS_KEY_ID = '63861317F7D60239';

const read = (p) => fs.readFileSync(p, 'utf8');

function tempDir(name) {
  return fs.mkdtempSync(path.join(os.tmpdir(), `km2-${name}-`));
}

function withTemp(t, name) {
  const dir = tempDir(name);
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

// A temp copy of the KAT archive, optionally with byte `flipAt` XORed.
function tarballCopy(dir, flipAt) {
  const bytes = fs.readFileSync(KAT_TARBALL);
  if (flipAt !== undefined) bytes[flipAt] ^= 0x01;
  const p = path.join(dir, KAT_ASSET);
  fs.writeFileSync(p, bytes);
  return p;
}

// The .minisig text with decoded line `lineIndex` (0-based) changed by `edit(buf)` and re-encoded.
function editSigLine(text, lineIndex, edit) {
  const lines = text.split('\n');
  const buf = Buffer.from(lines[lineIndex], 'base64');
  edit(buf);
  lines[lineIndex] = buf.toString('base64');
  return lines.join('\n');
}

function keyIdHex(keyId) {
  return Buffer.from(keyId).reverse().toString('hex').toUpperCase();
}

function captureIo() {
  const io = { out: '', err: '', env: {} };
  io.stdout = { write: (s) => (io.out += s) };
  io.stderr = { write: (s) => (io.err += s) };
  return io;
}

const C3 = (asset) => contract.formatTrustedComment('KatPlugin', '1.2.3', 'linux-x86_64', asset);

test('minisign.js verifies the rsign2 KAT', async (t) => {
  const sig = read(KAT_SIG);
  const pub = read(KAT_PUB);
  assert.deepEqual(minisign.verifyFile(KAT_TARBALL, sig, pub), { trustedComment: KAT_TC, keyId: KAT_KEY_ID });

  await t.test('one archive byte flipped -> E_MINISIG_SIGNATURE', (t) => {
    const dir = withTemp(t, 'kat-archive');
    const file = tarballCopy(dir, 100);
    assert.throws(() => minisign.verifyFile(file, sig, pub), { code: 'E_MINISIG_SIGNATURE' });
  });

  await t.test('one byte of the decoded line-2 signature flipped -> E_MINISIG_SIGNATURE', (t) => {
    const dir = withTemp(t, 'kat-sig');
    const file = tarballCopy(dir);
    const bad = editSigLine(sig, 1, (b) => (b[40] ^= 0x01)); // bytes 10..73 are the signature
    fs.writeFileSync(`${file}.minisig`, bad);
    assert.throws(() => minisign.verifyFile(file, read(`${file}.minisig`), pub), { code: 'E_MINISIG_SIGNATURE' });
  });

  await t.test('one key-id byte flipped -> E_MINISIG_KEY_ID', (t) => {
    const dir = withTemp(t, 'kat-keyid');
    const file = tarballCopy(dir);
    const bad = editSigLine(sig, 1, (b) => (b[5] ^= 0x01)); // bytes 2..9 are the key id
    fs.writeFileSync(`${file}.minisig`, bad);
    assert.throws(() => minisign.verifyFile(file, read(`${file}.minisig`), pub), { code: 'E_MINISIG_KEY_ID' });
  });

  await t.test('one trusted-comment character changed -> E_MINISIG_GLOBAL', (t) => {
    const dir = withTemp(t, 'kat-tc');
    const file = tarballCopy(dir);
    const lines = sig.split('\n');
    assert.ok(lines[2].includes(':1.2.3:'));
    lines[2] = lines[2].replace(':1.2.3:', ':1.2.4:');
    fs.writeFileSync(`${file}.minisig`, lines.join('\n'));
    assert.throws(() => minisign.verifyFile(file, read(`${file}.minisig`), pub), { code: 'E_MINISIG_GLOBAL' });
  });
});

test('minisign.js sign and verify round trip', async (t) => {
  const dir = withTemp(t, 'roundtrip');
  const kp = minisign.generateKeyPair();
  assert.ok(Buffer.isBuffer(kp.keyId) && kp.keyId.length === 8, 'an 8-byte key id');
  const pub = minisign.formatPublicKey(kp);
  const data = crypto.randomBytes(1 << 20);
  const file = path.join(dir, 'data.bin');
  fs.writeFileSync(file, data);
  const tc = C3('data.bin');
  const sig = minisign.signFile(file, kp, { trustedComment: tc });

  await t.test('signFile output verifies, has the D7 form and passes parseMinisigText', () => {
    assert.deepEqual(minisign.verifyFile(file, sig, pub), { trustedComment: tc, keyId: keyIdHex(kp.keyId) });
    assert.deepEqual(minisign.verifyBuffer(data, sig, pub), { trustedComment: tc, keyId: keyIdHex(kp.keyId) });
    assert.ok(!sig.includes('\r'), 'LF only');
    assert.ok(sig.endsWith('\n') && !sig.endsWith('\n\n'), 'one final LF');
    const lines = sig.split('\n');
    assert.equal(lines.length, 5);
    assert.equal(lines[0], 'untrusted comment: signature from krate minisign.js');
    const parsed = contract.parseMinisigText(sig);
    assert.equal(parsed.trustedComment, tc);
    assert.ok(parsed.keyId.equals(kp.keyId));
    // Ed25519 is deterministic: the streamed file hash equals the in-memory one.
    assert.equal(minisign.signBuffer(data, kp, { trustedComment: tc }), sig);
  });

  await t.test('the public key text round-trips the key id', () => {
    const lines = pub.split('\n');
    assert.equal(lines.length, 3);
    assert.equal(lines[0], `untrusted comment: minisign public key: ${keyIdHex(kp.keyId)}`);
    assert.equal(lines[2], '');
    const parsed = minisign.parsePublicKey(pub);
    assert.ok(parsed.keyId.equals(kp.keyId));
    assert.equal(parsed.publicKey.asymmetricKeyType, 'ed25519');
  });

  await t.test('an untrusted comment of the caller is kept; a CR or LF in a comment is a TypeError', () => {
    const s = minisign.signBuffer(data, kp, { trustedComment: tc, untrustedComment: 'made by a test' });
    assert.equal(s.split('\n')[0], 'untrusted comment: made by a test');
    assert.throws(() => minisign.signBuffer(data, kp, { trustedComment: 'a\nb' }), TypeError);
    assert.throws(() => minisign.signBuffer(data, kp, { trustedComment: 'a\rb' }), TypeError);
    assert.throws(() => minisign.signBuffer(data, kp, { trustedComment: tc, untrustedComment: 'x\ny' }), TypeError);
  });

  await t.test("a second pair's public key -> E_MINISIG_KEY_ID", () => {
    const other = minisign.formatPublicKey(minisign.generateKeyPair());
    assert.throws(() => minisign.verifyFile(file, sig, other), { code: 'E_MINISIG_KEY_ID' });
  });

  await t.test("a second pair given the first pair's key id -> E_MINISIG_SIGNATURE", () => {
    const kp2 = minisign.generateKeyPair();
    const other = minisign.formatPublicKey({ keyId: kp.keyId, publicKey: kp2.publicKey });
    assert.throws(() => minisign.verifyFile(file, sig, other), { code: 'E_MINISIG_SIGNATURE' });
  });

  await t.test("a legacy 'Ed' signature algorithm -> E_SIGNATURE_TEXT", () => {
    const legacy = editSigLine(sig, 1, (b) => (b[1] = 0x64));
    assert.throws(() => minisign.verifyFile(file, legacy, pub), { code: 'E_SIGNATURE_TEXT' });
  });

  await t.test('a CRLF .minisig file and a CRLF public key verify', () => {
    const sigPath = path.join(dir, 'data.bin.minisig');
    const pubPath = path.join(dir, 'crlf.pub');
    fs.writeFileSync(sigPath, sig.replace(/\n/g, '\r\n'));
    fs.writeFileSync(pubPath, pub.replace(/\n/g, '\r\n'));
    assert.ok(read(sigPath).includes('\r\n'));
    assert.deepEqual(minisign.verifyFile(file, read(sigPath), read(pubPath)), {
      trustedComment: tc,
      keyId: keyIdHex(kp.keyId),
    });
  });

  await t.test('four malformed public keys -> E_MINISIG_PUBKEY', () => {
    const [comment, b64] = pub.split('\n');
    const raw = Buffer.from(b64, 'base64');
    assert.equal(raw.length, 42);
    const ed = Buffer.from(raw);
    ed[1] = 0x44; // 'ED' in place of 'Ed'
    const cases = {
      'first line not untrusted comment': `comment: minisign public key\n${b64}\n`,
      'trailing extra =': `${comment}\n${b64}=\n`,
      'non-alphabet character': `${comment}\n${b64.slice(0, 20)}*${b64.slice(21)}\n`,
      '41 bytes': `${comment}\n${raw.subarray(0, 41).toString('base64')}\n`,
      "42 bytes with 'ED'": `${comment}\n${ed.toString('base64')}\n`,
    };
    for (const [name, text] of Object.entries(cases)) {
      assert.throws(() => minisign.parsePublicKey(text), { code: 'E_MINISIG_PUBKEY' }, name);
      assert.throws(() => minisign.verifyFile(file, sig, text), { code: 'E_MINISIG_PUBKEY' }, name);
    }
  });
});

test('minisign.js and rsign2 interoperate', async (t) => {
  if (!rsignGate(t)) return;

  await t.test('(a) rsign verify accepts a minisign.js key and signature and prints its trusted comment', (t) => {
    const dir = withTemp(t, 'interop-a');
    const kp = minisign.generateKeyPair();
    const pubPath = path.join(dir, 'mjs.pub');
    const file = path.join(dir, KAT_ASSET);
    const sigPath = `${file}.minisig`;
    fs.writeFileSync(pubPath, minisign.formatPublicKey(kp));
    fs.writeFileSync(file, crypto.randomBytes(4096));
    const tc = C3(KAT_ASSET);
    fs.writeFileSync(sigPath, minisign.signFile(file, kp, { trustedComment: tc }));
    const r = rsignVerify(dir, pubPath, sigPath, file);
    assert.equal(r.status, 0, `rsign verify: ${r.stderr}`);
    assert.ok(r.stderr.includes(`Trusted comment: ${tc}`), `rsign stderr: ${JSON.stringify(r.stderr)}`);
  });

  await t.test('(b) verifyFile accepts an rsign generate -W key and an rsign sign -W -t signature', (t) => {
    const dir = withTemp(t, 'interop-b');
    const { pub, sec } = rsignGenerate(dir);
    const file = path.join(dir, KAT_ASSET);
    const sigPath = `${file}.minisig`;
    fs.writeFileSync(file, crypto.randomBytes(4096));
    const tc = C3(KAT_ASSET);
    rsignSign(dir, sec, tc, sigPath, file);
    const got = minisign.verifyFile(file, read(sigPath), read(pub));
    assert.equal(got.trustedComment, tc);
    assert.equal(got.keyId, keyIdHex(minisign.parsePublicKey(read(pub)).keyId));
  });
});

test('minisign.js verify CLI prints and exits by contract', async (t) => {
  await t.test('the KAT verifies: exact stdout, exit 0', () => {
    const io = captureIo();
    assert.equal(minisign.main(['verify', '-p', KAT_PUB, '-x', KAT_SIG, KAT_TARBALL], io), 0, io.err);
    assert.equal(io.out, `verified: ${KAT_TC}\n`);
    assert.equal(io.err, '');
  });

  await t.test('one flipped archive byte: exit 1 with the code', (t) => {
    const dir = withTemp(t, 'cli');
    const file = tarballCopy(dir, 200);
    const io = captureIo();
    assert.equal(minisign.main(['verify', '-p', KAT_PUB, '-x', KAT_SIG, file], io), 1);
    assert.ok(io.err.startsWith('minisign: E_MINISIG_SIGNATURE:'), io.err);
    assert.equal(io.out, '');
  });

  await t.test('-x missing: exit 2 with the usage line', () => {
    const io = captureIo();
    assert.equal(minisign.main(['verify', '-p', KAT_PUB, KAT_TARBALL], io), 2);
    assert.ok(io.err.startsWith('usage: node tools/minisign.js'), io.err);
    assert.equal(io.out, '');
  });

  await t.test('an unknown subcommand: exit 2 with the usage line', () => {
    const io = captureIo();
    assert.equal(minisign.main(['frobnicate', '-p', KAT_PUB], io), 2);
    assert.ok(io.err.startsWith('usage: node tools/minisign.js'), io.err);
    assert.equal(io.out, '');
  });
});

// --- Secret-key decryption (SC-027) and the decrypt-key CLI (SC-028)

// Decoded byte offsets of a minisign secret-key file (spec External facts): sig_alg 0-1, kdf_alg 2-3, chk_alg 4-5,
// salt 6-37, opslimit 38-45, memlimit 46-53, keynum_sk 54-157 (key id 8 | secret key 64 | checksum 32).
const SK_KDF = 2;
const SK_MEMLIMIT = 46;
const SK_KEYNUM = 54;
const SK_BYTES = 158;

// The key file text with its decoded line 2 changed by `edit(buf)` and re-encoded.
function editKeyBlob(text, edit) {
  const lines = text.split('\n');
  const buf = Buffer.from(lines[1], 'base64');
  edit(buf);
  lines[1] = buf.toString('base64');
  return lines.join('\n');
}

// The raw 32-byte seed and 32-byte public key of an Ed25519 private KeyObject (RFC 8410 DER headers: 16 and 12 bytes).
function rawKeyParts(privateKey) {
  return {
    seed: privateKey.export({ format: 'der', type: 'pkcs8' }).subarray(16),
    pk: crypto.createPublicKey(privateKey).export({ format: 'der', type: 'spki' }).subarray(12),
  };
}

// An unencrypted-form secret-key file (plan PD-16: kdf_alg, salt and limits zero, plaintext checksum over
// sig_alg || key id || secret key, made with the module's own BLAKE2b-256, which the anchors below pin).
function unencryptedKeyText(keyId, sk64) {
  const ed = Buffer.from('Ed', 'latin1');
  const chk = minisign._internal.blake2b(Buffer.concat([ed, keyId, sk64]), 32);
  const raw = Buffer.concat([ed, Buffer.alloc(2), Buffer.from('B2', 'latin1'), Buffer.alloc(48), keyId, sk64, chk]);
  assert.equal(raw.length, SK_BYTES);
  return `untrusted comment: test-made unencrypted key\n${raw.toString('base64')}\n`;
}

test('minisign.js decrypts the rsign2 secret-key KAT', async (t) => {
  const keyText = read(KS_KEY);
  const pubText = read(KS_PUB);
  const katPub = minisign.parsePublicKey(pubText);
  const sk = minisign.decryptSecretKey(keyText, KS_PASSWORD);
  assert.ok(sk.keyId.equals(katPub.keyId), "the decrypted key id is not kat.pub's");
  assert.equal(keyIdHex(sk.keyId), KS_KEY_ID);
  assert.equal(sk.privateKey.asymmetricKeyType, 'ed25519');
  const { seed, pk } = rawKeyParts(sk.privateKey);
  const sk64 = Buffer.concat([seed, pk]);

  await t.test('a signature made with the decrypted key verifies against kat.pub', () => {
    const data = Buffer.from('krate decrypted-key signature check\n', 'utf8');
    const tc = C3(KAT_ASSET);
    const sig = minisign.signBuffer(data, sk, { trustedComment: tc });
    assert.deepEqual(minisign.verifyBuffer(data, sig, pubText), { trustedComment: tc, keyId: KS_KEY_ID });
  });

  await t.test('the Tauri single-line form decrypts to the same key id', () => {
    const tauri = Buffer.from(keyText, 'utf8').toString('base64');
    assert.ok(!/[\r\n]/.test(tauri));
    assert.ok(minisign.decryptSecretKey(tauri, KS_PASSWORD).keyId.equals(katPub.keyId));
  });

  await t.test('an unencrypted-form key decrypts with any password', () => {
    const text = unencryptedKeyText(sk.keyId, sk64);
    for (const pw of ['', 'anything', KS_PASSWORD]) {
      const got = minisign.decryptSecretKey(text, pw);
      assert.ok(got.keyId.equals(katPub.keyId));
      assert.ok(rawKeyParts(got.privateKey).seed.equals(seed), 'a different seed');
    }
  });

  await t.test('a wrong password -> E_MINISIG_PASSWORD, naming neither password', () => {
    const wrong = 'not-the-kat-password';
    assert.throws(
      () => minisign.decryptSecretKey(keyText, wrong),
      (err) => {
        assert.equal(err.code, 'E_MINISIG_PASSWORD');
        assert.ok(!err.message.includes(wrong), 'the message names the wrong password');
        assert.ok(!err.message.includes(KS_PASSWORD), 'the message names the right password');
        return true;
      },
    );
  });

  await t.test('one flipped byte of the encrypted keynum_sk -> E_MINISIG_PASSWORD', () => {
    for (const at of [SK_KEYNUM, SK_KEYNUM + 40, SK_BYTES - 1]) {
      const bad = editKeyBlob(keyText, (b) => (b[at] ^= 0x01));
      assert.throws(() => minisign.decryptSecretKey(bad, KS_PASSWORD), { code: 'E_MINISIG_PASSWORD' }, `byte ${at}`);
    }
  });

  await t.test('a truncated file -> E_MINISIG_SECRET_KEY', () => {
    const [comment, b64] = keyText.split('\n');
    const cases = {
      'line 2 cut to 100 characters': `${comment}\n${b64.slice(0, 100)}\n`,
      'line 2 missing': `${comment}\n`,
      'the last 30 characters cut': keyText.slice(0, keyText.length - 30),
    };
    for (const [name, text] of Object.entries(cases)) {
      assert.throws(() => minisign.decryptSecretKey(text, KS_PASSWORD), { code: 'E_MINISIG_SECRET_KEY' }, name);
    }
  });

  await t.test('an unknown kdf_alg -> E_MINISIG_SECRET_KEY', () => {
    const bad = editKeyBlob(keyText, (b) => b.write('Xy', SK_KDF, 'latin1'));
    assert.throws(() => minisign.decryptSecretKey(bad, KS_PASSWORD), { code: 'E_MINISIG_SECRET_KEY' });
  });

  await t.test('(a) kat.key with CRLF line ends decrypts to the same key id', () => {
    const crlf = keyText.replace(/\n/g, '\r\n');
    assert.ok(crlf.includes('\r\n'));
    assert.ok(minisign.decryptSecretKey(crlf, KS_PASSWORD).keyId.equals(katPub.keyId));
  });

  await t.test('(b) a memlimit of 2^30 + 1 -> E_MINISIG_SECRET_KEY without calling scrypt', () => {
    const bad = editKeyBlob(keyText, (b) => b.writeBigUInt64LE(2n ** 30n + 1n, SK_MEMLIMIT));
    const original = crypto.scryptSync;
    let calls = 0;
    crypto.scryptSync = (...args) => {
      calls += 1;
      return original(...args);
    };
    try {
      // Positive control: the wrapper sees the one scrypt call of a good key, so the 0 below is not vacuous.
      assert.ok(minisign.decryptSecretKey(keyText, KS_PASSWORD).keyId.equals(katPub.keyId));
      assert.equal(calls, 1, 'the wrapper did not see the scrypt call of the good key');
      calls = 0;
      assert.throws(() => minisign.decryptSecretKey(bad, KS_PASSWORD), { code: 'E_MINISIG_SECRET_KEY' });
      assert.equal(calls, 0, 'scrypt ran for a key above the memory limit');
    } finally {
      crypto.scryptSync = original;
    }
  });

  await t.test("(c) an unencrypted key whose public half is not its seed's -> E_MINISIG_SECRET_KEY", () => {
    const otherPk = rawKeyParts(minisign.generateKeyPair().privateKey).pk;
    const text = unencryptedKeyText(sk.keyId, Buffer.concat([seed, otherPk]));
    assert.throws(() => minisign.decryptSecretKey(text, ''), { code: 'E_MINISIG_SECRET_KEY' });
  });

  await t.test('BLAKE2b anchors: blake2b(x, 64) equals OpenSSL blake2b512 at the block boundaries', () => {
    for (const n of [0, 1, 127, 128, 129, 1000]) {
      const data = crypto.randomBytes(n);
      const want = crypto.createHash('blake2b512').update(data).digest();
      assert.ok(minisign._internal.blake2b(data, 64).equals(want), `length ${n}`);
    }
  });

  await t.test('BLAKE2b anchor: the RFC 7693 Appendix A vector (BLAKE2b-512 of "abc")', () => {
    const want =
      'ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1' +
      '7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923';
    assert.equal(minisign._internal.blake2b(Buffer.from('abc', 'latin1'), 64).toString('hex'), want);
  });

  await t.test('scrypt-parameter known answer and the PD-2 limits', () => {
    const { rawScryptParams } = minisign._internal;
    assert.deepEqual(rawScryptParams(33554432n, 1048576n), { N: 32768, r: 8, p: 1 });
    assert.throws(() => rawScryptParams(2n ** 30n + 1n, 1048576n), { code: 'E_MINISIG_SECRET_KEY' }, 'memlimit');
    // memlimit 1 GiB with a huge opslimit: N = 2^20, p = 2^15, cost 2^45 > 2 GiB.
    assert.throws(() => rawScryptParams(2n ** 30n, 2n ** 40n), { code: 'E_MINISIG_SECRET_KEY' }, 'cost');
  });

  await t.test('rsign2 signs with the decrypt-key output; rsign verify accepts it against kat.pub', (t) => {
    if (!rsignGate(t)) return;
    const dir = withTemp(t, 'dk-rsign');
    const out = path.join(dir, 'k.key');
    const io = captureIo();
    io.env = { KRATE_SIGNING_KEY: keyText, KRATE_SIGNING_KEY_PASSWORD: KS_PASSWORD, RUNNER_TEMP: dir };
    assert.equal(minisign.main(['decrypt-key', '--out', out], io), 0, io.err);
    const pubCopy = path.join(dir, 'kat.pub');
    fs.writeFileSync(pubCopy, pubText);
    const file = path.join(dir, KAT_ASSET);
    fs.writeFileSync(file, crypto.randomBytes(4096));
    const sigPath = `${file}.minisig`;
    const tc = C3(KAT_ASSET);
    rsignSign(dir, out, tc, sigPath, file);
    const r = rsignVerify(dir, pubCopy, sigPath, file);
    assert.equal(r.status, 0, `rsign verify: ${r.stderr}`);
    assert.ok(r.stderr.includes(`Trusted comment: ${tc}`), `rsign stderr: ${JSON.stringify(r.stderr)}`);
    assert.deepEqual(minisign.verifyFile(file, read(sigPath), pubText), { trustedComment: tc, keyId: KS_KEY_ID });
  });
});

test('minisign.js decrypt-key writes only under RUNNER_TEMP and cleans up', async (t) => {
  const keyText = read(KS_KEY);
  // Exactly what mkdtemp returns, not realpath'd: macOS goes through /var -> /private/var, the Windows runner through
  // its 8.3 RUNNER~1 name (plan PD-19).
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km2-dk-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const goodEnv = () => ({ KRATE_SIGNING_KEY: keyText, KRATE_SIGNING_KEY_PASSWORD: KS_PASSWORD, RUNNER_TEMP: tmp });
  const without = (env, name) => Object.fromEntries(Object.entries(env).filter(([k]) => k !== name));
  const WRONG_PASSWORD = 'wrong-password';
  const outputs = [];
  const run = (argv, env) => {
    const io = captureIo();
    io.env = env;
    const code = minisign.main(argv, io);
    outputs.push(io.out, io.err);
    return { code, out: io.out, err: io.err };
  };
  let cases = 0;
  const caseDir = () => {
    cases += 1;
    const d = path.join(tmp, `case-${cases}`);
    fs.mkdirSync(d);
    return d;
  };
  // Exit 1 with the code, empty stdout, and the target directory holding exactly `before` (no out, no temp file).
  const refused = (r, code, dir, before = []) => {
    assert.equal(r.code, 1, r.err);
    assert.ok(r.err.startsWith(`minisign: ${code}:`), r.err);
    assert.ok(!r.err.includes('E_WRITE_OUTSIDE_TMP'), 'refused by the write guard, not by decrypt-key');
    assert.equal(r.out, '');
    assert.deepEqual(fs.readdirSync(dir).sort(), before, 'the target directory holds an out or temp file');
  };

  const out = path.join(tmp, 'k.key');
  const ok = run(['decrypt-key', '--out', out], goodEnv());
  assert.equal(ok.code, 0, ok.err);
  assert.equal(ok.out, `decrypted: key id ${KS_KEY_ID}\n`);
  assert.equal(ok.err, '');
  assert.deepEqual(fs.readdirSync(tmp), ['k.key'], 'a temp file is left beside the key');
  const outText = read(out);
  const got = minisign.decryptSecretKey(outText, 'anything');
  assert.equal(keyIdHex(got.keyId), KS_KEY_ID);
  const { seed, pk } = rawKeyParts(minisign.decryptSecretKey(keyText, KS_PASSWORD).privateKey);
  assert.ok(rawKeyParts(got.privateKey).seed.equals(seed), 'the written key is not the KAT key');

  await t.test('the written file has the PD-16 unencrypted layout', () => {
    const lines = outText.split('\n');
    assert.equal(lines.length, 3);
    assert.equal(lines[0], 'untrusted comment: krate decrypted secret key (runner temp only)');
    assert.equal(lines[2], '');
    const raw = Buffer.from(lines[1], 'base64');
    assert.equal(raw.length, SK_BYTES);
    assert.equal(raw.toString('latin1', 0, 6), 'Ed\0\0B2');
    assert.ok(raw.subarray(6, SK_KEYNUM).equals(Buffer.alloc(SK_KEYNUM - 6)), 'salt and limits are not zero');
  });

  await t.test(
    'the key file is mode 0600',
    { skip: process.platform === 'win32' ? 'POSIX file modes; win32 has none' : false },
    () => {
      assert.equal(fs.statSync(out).mode & 0o777, 0o600);
    },
  );

  await t.test('a wrong password -> E_MINISIG_PASSWORD, nothing written', () => {
    const dir = caseDir();
    const env = { ...goodEnv(), KRATE_SIGNING_KEY_PASSWORD: WRONG_PASSWORD };
    refused(run(['decrypt-key', '--out', path.join(dir, 'k.key')], env), 'E_MINISIG_PASSWORD', dir);
  });

  await t.test('--out outside RUNNER_TEMP, or with a .. segment -> E_INPUT, nothing written', (t) => {
    const outside = withTemp(t, 'dk-outside');
    refused(run(['decrypt-key', '--out', path.join(outside, 'k.key')], goodEnv()), 'E_INPUT', outside);
    const dir = caseDir();
    fs.mkdirSync(path.join(dir, 'sub'));
    const dotdot = [dir, 'sub', '..', 'k.key'].join(path.sep); // path.join would collapse the '..'
    refused(run(['decrypt-key', '--out', dotdot], goodEnv()), 'E_INPUT', dir, ['sub']);
    assert.deepEqual(fs.readdirSync(path.join(dir, 'sub')), []);
  });

  await t.test('the password variable absent or empty -> E_INPUT naming it', () => {
    const envs = [without(goodEnv(), 'KRATE_SIGNING_KEY_PASSWORD'), { ...goodEnv(), KRATE_SIGNING_KEY_PASSWORD: '' }];
    for (const env of envs) {
      const dir = caseDir();
      const r = run(['decrypt-key', '--out', path.join(dir, 'k.key')], env);
      refused(r, 'E_INPUT', dir);
      assert.ok(r.err.includes('KRATE_SIGNING_KEY_PASSWORD'), r.err);
    }
  });

  await t.test('the key variable absent -> E_INPUT naming it', () => {
    const dir = caseDir();
    const r = run(['decrypt-key', '--out', path.join(dir, 'k.key')], without(goodEnv(), 'KRATE_SIGNING_KEY'));
    refused(r, 'E_INPUT', dir);
    assert.ok(r.err.includes('KRATE_SIGNING_KEY'), r.err);
  });

  await t.test('a missing parent directory -> E_INPUT, the parent is not created', () => {
    const dir = caseDir();
    refused(run(['decrypt-key', '--out', path.join(dir, 'missing', 'k.key')], goodEnv()), 'E_INPUT', dir);
  });

  await t.test('an existing entry at --out -> E_INPUT, the entry is unchanged', () => {
    const dir = caseDir();
    const existing = path.join(dir, 'k.key');
    fs.writeFileSync(existing, 'existing\n');
    refused(run(['decrypt-key', '--out', existing], goodEnv()), 'E_INPUT', dir, ['k.key']);
    assert.equal(read(existing), 'existing\n');
  });

  await t.test(
    '--out through a symlinked directory pointing outside RUNNER_TEMP -> E_INPUT',
    { skip: process.platform === 'win32' ? 'POSIX symlinks; win32 needs a privilege for them' : false },
    (t) => {
      const outside = withTemp(t, 'dk-link-target');
      const dir = caseDir();
      fs.symlinkSync(outside, path.join(dir, 'link'), 'dir');
      refused(run(['decrypt-key', '--out', path.join(dir, 'link', 'k.key')], goodEnv()), 'E_INPUT', dir, ['link']);
      assert.deepEqual(fs.readdirSync(outside), [], 'the link target received a file');
    },
  );

  await t.test('usage errors exit 2', () => {
    for (const argv of [['decrypt-key'], ['decrypt-key', '--out'], ['decrypt-key', '--out', out, '--out', out]]) {
      const r = run(argv, goodEnv());
      assert.equal(r.code, 2, r.err);
      assert.ok(r.err.startsWith('usage: node tools/minisign.js'), r.err);
    }
  });

  await t.test('no output names a password, a base64 key line or the secret key', () => {
    const secrets = [
      KS_PASSWORD,
      WRONG_PASSWORD,
      keyText.split('\n')[1],
      outText.split('\n')[1],
      seed.toString('hex'),
      seed.toString('hex').toUpperCase(),
      seed.toString('base64'),
      Buffer.concat([seed, pk]).toString('hex'),
    ];
    assert.ok(outputs.length >= 20, `${outputs.length} captured outputs`);
    outputs.forEach((s, i) => {
      secrets.forEach((secret, j) => assert.ok(!s.includes(secret), `output ${i} contains secret ${j}`));
    });
  });
});
