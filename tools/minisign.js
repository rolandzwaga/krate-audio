#!/usr/bin/env node
// minisign signatures in Node: key pairs, public-key text, signing and verification over node:crypto.
//
// WHY: the release pipeline (gen-release-manifest.js, backfill-sign.js) and the tests must sign and verify minisign
// signatures without a native tool or a new npm dependency. CI signs with rsign2; this module verifies those
// signatures, makes throwaway in-memory key pairs for tests and dry runs, and is proven against rsign2 both ways
// (spec specs/krate-manager-phase2/spec.md FR-001 to FR-009b; plan §4.1).
// The signing secret is an rsign2 / tauri key encrypted with a password, and rsign2 reads a password only from a
// terminal, so CI decrypts it here (decrypt-key) into an unencrypted key under $RUNNER_TEMP that `rsign sign -W` reads.
// Format: https://jedisct1.github.io/minisign/ (minisign 0.12): public key = base64('Ed' || key id (8) || pk (32));
// signature line 2 = base64('ED' || key id || ed25519(Blake2b-512(data))), line 4 = base64(ed25519(signature ||
// trusted comment)). Only the prehashed 'ED' form is written or accepted (legacy 'Ed' is E_SIGNATURE_TEXT).
// Secret key line 2 = base64('Ed' || kdf_alg 'Sc' or 00 00 || 'B2' || salt (32) || opslimit (8, LE) || memlimit (8, LE)
// || keynum_sk), keynum_sk = key id (8) || seed (32) || pk (32) || Blake2b-256 checksum (32), XORed with a 104-byte
// scrypt keystream when kdf_alg is 'Sc' (minisign 0.9.1 src/secret_key.rs, src/helpers.rs).
// No network, no child process; keys of generateKeyPair exist only as in-memory KeyObjects, and no function here
// generates, encrypts or writes a secret-key file other than decrypt-key's output (FR-009).
//
// Usage:
//   node tools/minisign.js verify -p <public key file> -x <.minisig file> <file>
//     prints `verified: <trusted comment>`; it never checks the C3 binding (callers run contract.checkBinding)
//   KRATE_SIGNING_KEY=<key text> KRATE_SIGNING_KEY_PASSWORD=<password> node tools/minisign.js decrypt-key --out <file>
//     writes the unencrypted key (mode 0600) to <file>, which must be a new entry under $RUNNER_TEMP (the OS temp dir
//     when unset), and prints `decrypted: key id <KEYID>`; the key and password come only from the environment
// Exit codes: 0 verified / decrypted, 1 rejected or unreadable input (stderr `minisign: <CODE>: <message>`), 2 usage.
// The codes are in tools/feed-contract/CONTRACT.md §7 and contract.js CODES.
// Exports main(argv, io), generateKeyPair, formatPublicKey, parsePublicKey, signFile, signBuffer, verifyFile,
// verifyBuffer, decryptSecretKey, and _internal {blake2b, rawScryptParams} for the SC-027 anchors only (plan PD-18).
'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { ContractError, parseMinisigText } = require('./feed-contract/contract.js');

const UNTRUSTED_PREFIX = 'untrusted comment: ';
const TRUSTED_PREFIX = 'trusted comment: ';
const DEFAULT_UNTRUSTED = 'signature from krate minisign.js';
const PUBKEY_ALG = Buffer.from('Ed', 'latin1');
const SIG_ALG = Buffer.from('ED', 'latin1');
const KEY_ID_BYTES = 8;
const PUBKEY_BLOB_BYTES = 42; // 'Ed' + key id + 32-byte Ed25519 public key
// DER header of an Ed25519 SubjectPublicKeyInfo (RFC 8410); the raw 32-byte key follows it.
const SPKI_PREFIX = Buffer.from('302a300506032b6570032100', 'hex');
const HASH_CHUNK = 1 << 20;
const USAGE =
  'usage: node tools/minisign.js verify -p <public key file> -x <.minisig file> <file>\n' +
  '       node tools/minisign.js decrypt-key --out <file>   (key and password from the environment)\n';

// Secret-key layout (spec External facts; minisign 0.9.1 src/secret_key.rs, src/constants.rs).
const SECRET_KEY_BYTES = 158;
const KDF_SCRYPT = Buffer.from('Sc', 'latin1');
const KDF_NONE = Buffer.alloc(2);
const CHK_ALG = Buffer.from('B2', 'latin1');
const SALT_OFFSET = 6;
const OPSLIMIT_OFFSET = 38;
const MEMLIMIT_OFFSET = 46;
const KEYNUM_OFFSET = 54;
const KEYNUM_BYTES = 104; // key id 8 | seed 32 | pk 32 | checksum 32
const CHECKSUM_BYTES = 32;
// DER header of an Ed25519 PKCS#8 private key (RFC 8410); the raw 32-byte seed follows it.
const PKCS8_PREFIX = Buffer.from('302e020100300506032b657004220420', 'hex');
// minisign's own limits (constants.rs MEMLIMIT_MAX, N_LOG2_MAX) and the spec's 2 GiB scrypt cost ceiling (plan PD-2).
const MEMLIMIT_MAX = 1073741824n;
const N_LOG2_MAX = 20n;
const SCRYPT_COST_MAX = 2n ** 31n;
const DECRYPTED_COMMENT = 'krate decrypted secret key (runner temp only)';
const SIGNING_KEY_VAR = 'KRATE_SIGNING_KEY';
const SIGNING_PASSWORD_VAR = 'KRATE_SIGNING_KEY_PASSWORD';

/** The key id as rsign prints it: the 8 stored bytes reversed, upper-case hex. */
function keyIdHex(keyId) {
  return Buffer.from(keyId).reverse().toString('hex').toUpperCase();
}

function pubkeyError(why) {
  return new ContractError('E_MINISIG_PUBKEY', `public key: ${why}`);
}

function checkKeyId(keyId) {
  if (!Buffer.isBuffer(keyId) || keyId.length !== KEY_ID_BYTES) {
    throw new TypeError(`keyId must be a ${KEY_ID_BYTES}-byte Buffer`);
  }
}

function checkComment(value, what) {
  if (typeof value !== 'string') throw new TypeError(`${what} must be a string`);
  if (/[\r\n]/u.test(value)) throw new TypeError(`${what} must not contain a CR or LF`);
}

/** A fresh Ed25519 pair and a random 8-byte key id, in memory only (FR-002). */
function generateKeyPair() {
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
  return { keyId: crypto.randomBytes(KEY_ID_BYTES), publicKey, privateKey };
}

/** 'untrusted comment: minisign public key: <KEYID>\n<base64('Ed' || keyId || pk)>\n' (FR-002). */
function formatPublicKey({ keyId, publicKey }) {
  checkKeyId(keyId);
  const raw = publicKey.export({ format: 'der', type: 'spki' }).subarray(SPKI_PREFIX.length);
  const blob = Buffer.concat([PUBKEY_ALG, keyId, raw]);
  return `${UNTRUSTED_PREFIX}minisign public key: ${keyIdHex(keyId)}\n${blob.toString('base64')}\n`;
}

/** Two lines, LF or CRLF, optional final line end -> {keyId, publicKey}; anything else is E_MINISIG_PUBKEY (FR-003). */
function parsePublicKey(text) {
  if (typeof text !== 'string') throw pubkeyError('not a string');
  const lines = text.split(/\r?\n/u);
  if (lines[lines.length - 1] === '') lines.pop();
  if (lines.length !== 2) throw pubkeyError(`${lines.length} lines, not 2`);
  if (!lines[0].startsWith(UNTRUSTED_PREFIX)) throw pubkeyError(`line 1 does not start '${UNTRUSTED_PREFIX}'`);
  const blob = Buffer.from(lines[1], 'base64');
  if (lines[1] === '' || blob.toString('base64') !== lines[1]) throw pubkeyError('line 2 is not canonical base64');
  if (blob.length !== PUBKEY_BLOB_BYTES) {
    throw pubkeyError(`line 2 decodes to ${blob.length} bytes, not ${PUBKEY_BLOB_BYTES}`);
  }
  if (!blob.subarray(0, 2).equals(PUBKEY_ALG)) throw pubkeyError("line 2 algorithm is not 'Ed'");
  let publicKey;
  try {
    publicKey = crypto.createPublicKey({
      key: Buffer.concat([SPKI_PREFIX, blob.subarray(2 + KEY_ID_BYTES)]),
      format: 'der',
      type: 'spki',
    });
  } catch {
    throw pubkeyError('line 2 is not an Ed25519 public key');
  }
  return { keyId: Buffer.from(blob.subarray(2, 2 + KEY_ID_BYTES)), publicKey };
}

/** Blake2b-512 of a file, read in 1 MiB chunks so the asset size never bounds memory. */
function hashFile(filePath) {
  const hash = crypto.createHash('blake2b512');
  let fd;
  try {
    fd = fs.openSync(filePath, 'r');
    const chunk = Buffer.allocUnsafe(HASH_CHUNK);
    for (;;) {
      const n = fs.readSync(fd, chunk, 0, HASH_CHUNK, null);
      if (n === 0) break;
      hash.update(chunk.subarray(0, n));
    }
  } catch (err) {
    throw new ContractError('E_INPUT', `cannot read ${filePath}: ${err.code || err.message}`);
  } finally {
    if (fd !== undefined) fs.closeSync(fd);
  }
  return hash.digest();
}

function hashBuffer(data) {
  return crypto.createHash('blake2b512').update(data).digest();
}

function signHash(prehash, { keyId, privateKey }, { trustedComment, untrustedComment = DEFAULT_UNTRUSTED } = {}) {
  checkKeyId(keyId);
  checkComment(trustedComment, 'trustedComment');
  checkComment(untrustedComment, 'untrustedComment');
  const sig = crypto.sign(null, prehash, privateKey);
  const global = crypto.sign(null, Buffer.concat([sig, Buffer.from(trustedComment, 'utf8')]), privateKey);
  return (
    `${UNTRUSTED_PREFIX}${untrustedComment}\n` +
    `${Buffer.concat([SIG_ALG, keyId, sig]).toString('base64')}\n` +
    `${TRUSTED_PREFIX}${trustedComment}\n` +
    `${global.toString('base64')}\n`
  );
}

/** The four-line .minisig text (LF, final LF) over `data` (FR-004). */
function signBuffer(data, sk, opts) {
  return signHash(hashBuffer(data), sk, opts);
}

/** signBuffer over a file, hashed as a stream (FR-004). */
function signFile(filePath, sk, opts) {
  return signHash(hashFile(filePath), sk, opts);
}

// FR-005 order: text shape, public key, key id, signature over the prehash, global signature.
function verifyWith(prehashOf, minisigText, publicKeyText) {
  const parsed = parseMinisigText(minisigText);
  const { keyId, publicKey } = parsePublicKey(publicKeyText);
  if (!parsed.keyId.equals(keyId)) {
    throw new ContractError(
      'E_MINISIG_KEY_ID',
      `signature key id ${keyIdHex(parsed.keyId)} is not the public key's ${keyIdHex(keyId)}`,
    );
  }
  // parseMinisigText checked both base64 lines; it returns no signature bytes, so lines 2 and 4 are decoded here.
  const lines = minisigText.split(/\r?\n/u);
  const sig = Buffer.from(lines[1], 'base64').subarray(2 + KEY_ID_BYTES);
  const global = Buffer.from(lines[3], 'base64');
  if (!crypto.verify(null, prehashOf(), publicKey, sig)) {
    throw new ContractError('E_MINISIG_SIGNATURE', `signature does not match the data (key id ${keyIdHex(keyId)})`);
  }
  const signedComment = Buffer.concat([sig, Buffer.from(parsed.trustedComment, 'utf8')]);
  if (!crypto.verify(null, signedComment, publicKey, global)) {
    throw new ContractError('E_MINISIG_GLOBAL', 'global signature does not match the trusted comment');
  }
  return { trustedComment: parsed.trustedComment, keyId: keyIdHex(keyId) };
}

/** -> {trustedComment, keyId: <KEYID hex>}; throws the first failure as a ContractError (FR-005). */
function verifyBuffer(data, minisigText, publicKeyText) {
  return verifyWith(() => hashBuffer(data), minisigText, publicKeyText);
}

/** verifyBuffer over a file, hashed as a stream after the cheap checks pass (FR-005). */
function verifyFile(filePath, minisigText, publicKeyText) {
  return verifyWith(() => hashFile(filePath), minisigText, publicKeyText);
}

// --- BLAKE2b with a chosen digest length (RFC 7693; plan PD-1)
// OpenSSL offers only the 64-byte BLAKE2b (Node v24.7.0 refuses createHash('blake2b512', {outputLength: 32})), and
// BLAKE2b-256 is not a truncation of it (the length is in the parameter block, RFC 7693 §2.5). minisign's secret-key
// checksum is BLAKE2b-256, so this in-file version computes it; the file prehash stays OpenSSL's blake2b512.

const MASK64 = 0xffffffffffffffffn;
const B2_IV = [
  0x6a09e667f3bcc908n,
  0xbb67ae8584caa73bn,
  0x3c6ef372fe94f82bn,
  0xa54ff53a5f1d36f1n,
  0x510e527fade682d1n,
  0x9b05688c2b3e6c1fn,
  0x1f83d9abfb41bd6bn,
  0x5be0cd19137e2179n,
];
// RFC 7693 §2.7; rounds 10 and 11 reuse rows 0 and 1.
const B2_SIGMA = [
  [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
  [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
  [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
  [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
  [9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13],
  [2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9],
  [12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11],
  [13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10],
  [6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5],
  [10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0],
];

const rotr64 = (x, n) => ((x >> n) | (x << (64n - n))) & MASK64;

// RFC 7693 §3.2 compression F of one 128-byte block into the state h; t is the byte count so far, last the final flag.
function blake2bCompress(h, block, t, last) {
  const v = [...h, ...B2_IV];
  const m = [];
  for (let i = 0; i < 16; i += 1) m.push(block.readBigUInt64LE(i * 8));
  v[12] ^= t & MASK64;
  v[13] ^= t >> 64n;
  if (last) v[14] ^= MASK64;
  const g = (a, b, c, d, x, y) => {
    v[a] = (v[a] + v[b] + x) & MASK64;
    v[d] = rotr64(v[d] ^ v[a], 32n);
    v[c] = (v[c] + v[d]) & MASK64;
    v[b] = rotr64(v[b] ^ v[c], 24n);
    v[a] = (v[a] + v[b] + y) & MASK64;
    v[d] = rotr64(v[d] ^ v[a], 16n);
    v[c] = (v[c] + v[d]) & MASK64;
    v[b] = rotr64(v[b] ^ v[c], 63n);
  };
  for (let round = 0; round < 12; round += 1) {
    const s = B2_SIGMA[round % 10];
    g(0, 4, 8, 12, m[s[0]], m[s[1]]);
    g(1, 5, 9, 13, m[s[2]], m[s[3]]);
    g(2, 6, 10, 14, m[s[4]], m[s[5]]);
    g(3, 7, 11, 15, m[s[6]], m[s[7]]);
    g(0, 5, 10, 15, m[s[8]], m[s[9]]);
    g(1, 6, 11, 12, m[s[10]], m[s[11]]);
    g(2, 7, 8, 13, m[s[12]], m[s[13]]);
    g(3, 4, 9, 14, m[s[14]], m[s[15]]);
  }
  for (let i = 0; i < 8; i += 1) h[i] ^= v[i] ^ v[i + 8];
}

/** Unkeyed BLAKE2b of `data` (a Buffer) with an `outLen`-byte digest, 1 to 64 (RFC 7693 §3.3). */
function blake2b(data, outLen) {
  if (!Buffer.isBuffer(data)) throw new TypeError('data must be a Buffer');
  if (!Number.isInteger(outLen) || outLen < 1 || outLen > 64) throw new TypeError('outLen must be an integer 1..64');
  const h = [...B2_IV];
  h[0] ^= 0x01010000n ^ BigInt(outLen); // parameter block: depth 1, fanout 1, key length 0, digest length
  let offset = 0;
  while (data.length - offset > 128) {
    blake2bCompress(h, data.subarray(offset, offset + 128), BigInt(offset + 128), false);
    offset += 128;
  }
  const last = Buffer.alloc(128);
  data.copy(last, 0, offset);
  blake2bCompress(h, last, BigInt(data.length), true);
  const out = Buffer.alloc(64);
  h.forEach((word, i) => out.writeBigUInt64LE(word, i * 8));
  return out.subarray(0, outLen);
}

// --- Secret-key decryption (FR-009a; plan §4.1, PD-1 to PD-3)

function secretKeyError(why) {
  return new ContractError('E_MINISIG_SECRET_KEY', `secret key: ${why}`);
}

/**
 * minisign's raw_scrypt_params (minisign 0.9.1 src/helpers.rs:45-74), ported in BigInt, with the plan PD-2 limits:
 * memlimit above 1 GiB, log2(N) above 20 or a cost 128 * N * r * p above 2 GiB is E_MINISIG_SECRET_KEY.
 * @param {bigint|number} memlimit  kdf_memlimit of the key file
 * @param {bigint|number} opslimit  kdf_opslimit of the key file
 * @returns {{N: number, r: number, p: number}}
 */
function rawScryptParams(memlimit, opslimit) {
  const mem = BigInt(memlimit);
  if (mem > MEMLIMIT_MAX) throw secretKeyError(`kdf memlimit ${mem} is above ${MEMLIMIT_MAX}`);
  const ops = BigInt(opslimit) < 32768n ? 32768n : BigInt(opslimit);
  const r = 8n;
  const smallestLog2Above = (maxn) => {
    let nLog2 = 1n;
    while (nLog2 < 63n && (1n << nLog2) <= maxn / 2n) nLog2 += 1n;
    return nLog2;
  };
  let nLog2;
  let p;
  if (ops < mem / 32n) {
    p = 1n;
    nLog2 = smallestLog2Above(ops / (r * 4n));
  } else {
    nLog2 = smallestLog2Above(mem / (r * 128n));
    const maxrp = (ops / 4n) / (1n << nLog2);
    p = (maxrp < 0x3fffffffn ? maxrp : 0x3fffffffn) / r;
  }
  if (nLog2 > N_LOG2_MAX) throw secretKeyError(`scrypt log2(N) ${nLog2} is above ${N_LOG2_MAX}`);
  const n = 1n << nLog2;
  const cost = 128n * n * r * p;
  if (cost > SCRYPT_COST_MAX) throw secretKeyError(`scrypt cost ${cost} bytes is above ${SCRYPT_COST_MAX}`);
  return { N: Number(n), r: Number(r), p: Number(p) };
}

const isCanonicalBase64 = (s) => s !== '' && Buffer.from(s, 'base64').toString('base64') === s;

// The two lines of a secret-key file: its text (LF or CRLF, optional final line end), or the single base64 line a
// TAURI_SIGNING_PRIVATE_KEY secret holds (the file text base64-encoded once more, probes/results.md).
function secretKeyLines(keyText) {
  if (typeof keyText !== 'string') throw secretKeyError('not a string');
  let text = keyText.replace(/\r?\n$/u, '');
  if (!text.startsWith(UNTRUSTED_PREFIX) && !/[\r\n]/u.test(text) && isCanonicalBase64(text)) {
    text = Buffer.from(text, 'base64').toString('utf8').replace(/\r?\n$/u, '');
  }
  const lines = text.split(/\r?\n/u);
  if (lines.length !== 2) throw secretKeyError(`${lines.length} lines, not 2`);
  if (!lines[0].startsWith(UNTRUSTED_PREFIX)) throw secretKeyError(`line 1 does not start '${UNTRUSTED_PREFIX}'`);
  if (!isCanonicalBase64(lines[1])) throw secretKeyError('line 2 is not canonical base64');
  return lines;
}

/**
 * Decrypts a minisign / rsign2 / tauri secret-key file with its password (FR-009a). A key whose kdf_alg is two zero
 * bytes is unencrypted and the password is not used. A checksum mismatch of an encrypted key is E_MINISIG_PASSWORD
 * (wrong password or damaged key); any other malformed input is E_MINISIG_SECRET_KEY. No message names the password or
 * a key byte; the decrypted bytes are zeroed once the KeyObject exists.
 * @param {string} keyText
 * @param {string} password
 * @returns {{keyId: Buffer, privateKey: crypto.KeyObject}}
 */
function decryptSecretKey(keyText, password) {
  if (typeof password !== 'string') throw new TypeError('password must be a string');
  const raw = Buffer.from(secretKeyLines(keyText)[1], 'base64');
  if (raw.length !== SECRET_KEY_BYTES) {
    throw secretKeyError(`line 2 decodes to ${raw.length} bytes, not ${SECRET_KEY_BYTES}`);
  }
  const sigAlg = raw.subarray(0, 2);
  const kdfAlg = raw.subarray(2, 4);
  if (!sigAlg.equals(PUBKEY_ALG)) throw secretKeyError("signature algorithm is not 'Ed'");
  const encrypted = kdfAlg.equals(KDF_SCRYPT);
  if (!encrypted && !kdfAlg.equals(KDF_NONE)) throw secretKeyError("kdf algorithm is neither 'Sc' nor none");
  if (!raw.subarray(4, 6).equals(CHK_ALG)) throw secretKeyError("checksum algorithm is not 'B2'");
  const blob = Buffer.from(raw.subarray(KEYNUM_OFFSET));
  const scratch = [blob];
  try {
    if (encrypted) {
      const { N, r, p } = rawScryptParams(raw.readBigUInt64LE(MEMLIMIT_OFFSET), raw.readBigUInt64LE(OPSLIMIT_OFFSET));
      const salt = raw.subarray(SALT_OFFSET, OPSLIMIT_OFFSET);
      const stream = crypto.scryptSync(Buffer.from(password, 'utf8'), salt, KEYNUM_BYTES, {
        N,
        r,
        p,
        maxmem: 2 * 128 * N * r,
      });
      scratch.push(stream);
      for (let i = 0; i < KEYNUM_BYTES; i += 1) blob[i] ^= stream[i];
    }
    const keyId = blob.subarray(0, KEY_ID_BYTES);
    const seed = blob.subarray(KEY_ID_BYTES, KEY_ID_BYTES + 32);
    const pkHalf = blob.subarray(KEY_ID_BYTES + 32, KEY_ID_BYTES + 64);
    const checksumInput = Buffer.concat([sigAlg, blob.subarray(0, KEY_ID_BYTES + 64)]);
    scratch.push(checksumInput);
    if (!blake2b(checksumInput, CHECKSUM_BYTES).equals(blob.subarray(KEY_ID_BYTES + 64))) {
      if (encrypted) {
        throw new ContractError('E_MINISIG_PASSWORD', 'secret key: checksum mismatch (wrong password or damaged key)');
      }
      throw secretKeyError('checksum mismatch (the unencrypted key is damaged)');
    }
    const der = Buffer.concat([PKCS8_PREFIX, seed]);
    scratch.push(der);
    const privateKey = crypto.createPrivateKey({ key: der, format: 'der', type: 'pkcs8' });
    const derived = crypto.createPublicKey(privateKey).export({ format: 'der', type: 'spki' });
    if (!derived.subarray(SPKI_PREFIX.length).equals(pkHalf)) {
      throw secretKeyError("the public half does not match the seed's public key");
    }
    return { keyId: Buffer.from(keyId), privateKey };
  } finally {
    for (const b of scratch) b.fill(0);
  }
}

// --- CLI (FR-007, FR-009b)

function usageError(why) {
  return new ContractError('E_USAGE', why);
}

function inputError(why) {
  return new ContractError('E_INPUT', why);
}

function parseVerifyArgs(args) {
  const opts = new Map();
  const positional = [];
  for (let i = 0; i < args.length; i += 1) {
    const a = args[i];
    if (a === '-p' || a === '-x') {
      if (opts.has(a)) throw usageError(`${a} given twice`);
      if (i + 1 >= args.length) throw usageError(`${a} needs a value`);
      opts.set(a, args[i + 1]);
      i += 1;
    } else if (a.startsWith('-')) {
      throw usageError(`unknown option ${a}`);
    } else {
      positional.push(a);
    }
  }
  for (const flag of ['-p', '-x']) if (!opts.has(flag)) throw usageError(`${flag} is required`);
  if (positional.length !== 1) throw usageError(`one <file> argument expected, got ${positional.length}`);
  return { pub: opts.get('-p'), sig: opts.get('-x'), file: positional[0] };
}

function readText(filePath, what) {
  try {
    return fs.readFileSync(filePath, 'utf8');
  } catch (err) {
    throw new ContractError('E_INPUT', `cannot read ${what} ${filePath}: ${err.code || err.message}`);
  }
}

// The value of an environment variable; absent or empty is E_INPUT naming the variable, never a value (FR-009b).
function envValue(env, name) {
  const value = env ? env[name] : undefined;
  if (typeof value !== 'string' || value === '') throw inputError(`${name} is absent or empty`);
  return value;
}

const foldCase = (s) => (process.platform === 'win32' ? s.toLowerCase() : s);

/**
 * Plan PD-19: the canonical path `out` names, or E_INPUT. `out` holds no '..' segment, its parent directory exists, the
 * realpath of that parent joined with the basename is the canonical root (realpath of RUNNER_TEMP, or the OS temp dir)
 * or sits under it (case-folded on win32), and nothing exists there yet. Links in the parent chain are followed, so a
 * linked directory under the root that points outside it is refused.
 */
function containedOutPath(out, runnerTemp) {
  const segments = process.platform === 'win32' ? out.split(/[\\/]/u) : out.split('/');
  if (segments.includes('..')) throw inputError(`--out ${out}: a '..' segment is not allowed`);
  const rootPath = runnerTemp || os.tmpdir();
  let root;
  try {
    root = foldCase(fs.realpathSync.native(rootPath));
  } catch (err) {
    throw inputError(`the temp root ${rootPath} cannot be resolved (${err.code || err.message})`);
  }
  const abs = path.resolve(out);
  let parent;
  try {
    parent = fs.realpathSync.native(path.dirname(abs));
  } catch (err) {
    throw inputError(`--out ${out}: the parent directory cannot be resolved (${err.code || err.message})`);
  }
  if (!fs.lstatSync(parent).isDirectory()) throw inputError(`--out ${out}: the parent is not a directory`);
  const target = path.join(parent, path.basename(abs));
  const folded = foldCase(target);
  const prefix = root.endsWith(path.sep) ? root : root + path.sep;
  if (folded !== root && !folded.startsWith(prefix)) {
    throw inputError(`--out ${out} is not under the temp root ${rootPath}`);
  }
  if (fs.lstatSync(target, { throwIfNoEntry: false }) !== undefined) {
    throw inputError(`--out ${out}: an entry already exists there`);
  }
  return target;
}

// Plan PD-16: the layout minisign's generate_unencrypted_keypair writes (kdf_alg, salt and limits zero, the plaintext
// checksum), which `rsign sign -W` reads without a password.
function unencryptedKeyText({ keyId, privateKey }) {
  const seed = privateKey.export({ format: 'der', type: 'pkcs8' }).subarray(PKCS8_PREFIX.length);
  const pk = crypto.createPublicKey(privateKey).export({ format: 'der', type: 'spki' }).subarray(SPKI_PREFIX.length);
  const keynum = Buffer.concat([keyId, seed, pk]);
  const checksumInput = Buffer.concat([PUBKEY_ALG, keynum]);
  const checksum = blake2b(checksumInput, CHECKSUM_BYTES);
  const zeroKdfFields = Buffer.alloc(KEYNUM_OFFSET - SALT_OFFSET); // salt, opslimit, memlimit
  const raw = Buffer.concat([PUBKEY_ALG, KDF_NONE, CHK_ALG, zeroKdfFields, keynum, checksum]);
  const text = `${UNTRUSTED_PREFIX}${DECRYPTED_COMMENT}\n${raw.toString('base64')}\n`;
  for (const b of [seed, keynum, checksumInput, raw]) b.fill(0);
  return text;
}

// Writes `text` to `target` (mode 0600) through a 'wx' temp file in the same directory and a rename; the temp file is
// removed on every failure, and `target` never exists before the rename (FR-009b; as tools/gen-feed.js writeAtomic).
function writeKeyAtomic(target, text) {
  const tmp = path.join(
    path.dirname(target),
    `.${path.basename(target)}.${process.pid}.${crypto.randomBytes(4).toString('hex')}.tmp`,
  );
  let renamed = false;
  try {
    fs.writeFileSync(tmp, text, { flag: 'wx', mode: 0o600 });
    fs.renameSync(tmp, target);
    renamed = true;
  } catch (err) {
    throw inputError(`--out ${target}: cannot write (${err.code || err.message})`);
  } finally {
    if (!renamed) {
      try {
        fs.rmSync(tmp, { force: true });
      } catch {
        // The temp file was never created (or cannot be touched); the E_INPUT above is the report.
      }
    }
  }
}

function parseDecryptKeyArgs(args) {
  if (args.length !== 2 || args[0] !== '--out') throw usageError('decrypt-key takes exactly --out <file>');
  return args[1];
}

// FR-009b: KRATE_SIGNING_KEY + KRATE_SIGNING_KEY_PASSWORD (io.env only) -> the unencrypted key at --out.
function decryptKeyCommand(args, io) {
  const out = parseDecryptKeyArgs(args);
  const env = io.env || {};
  const keyText = envValue(env, SIGNING_KEY_VAR);
  const password = envValue(env, SIGNING_PASSWORD_VAR);
  const target = containedOutPath(out, env.RUNNER_TEMP);
  const sk = decryptSecretKey(keyText, password);
  writeKeyAtomic(target, unencryptedKeyText(sk));
  io.stdout.write(`decrypted: key id ${keyIdHex(sk.keyId)}\n`);
  return 0;
}

/**
 * @param {string[]} argv
 * @param {{stdout: {write(s: string): void}, stderr: {write(s: string): void}, env: object}} io
 * @returns {0|1|2}
 */
function main(argv, io) {
  try {
    if (argv[0] === 'decrypt-key') return decryptKeyCommand(argv.slice(1), io);
    if (argv[0] !== 'verify') {
      throw usageError(argv.length === 0 ? 'no subcommand' : `unknown subcommand ${argv[0]}`);
    }
    const { pub, sig, file } = parseVerifyArgs(argv.slice(1));
    const result = verifyFile(file, readText(sig, '-x'), readText(pub, '-p'));
    io.stdout.write(`verified: ${result.trustedComment}\n`);
    return 0;
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    if (err.code === 'E_USAGE') {
      io.stderr.write(USAGE);
      io.stderr.write(`minisign: E_USAGE: ${err.message}\n`);
      return 2;
    }
    io.stderr.write(`minisign: ${err.code}: ${err.message}\n`);
    return 1;
  }
}

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);

module.exports = {
  main,
  generateKeyPair,
  formatPublicKey,
  parsePublicKey,
  signFile,
  signBuffer,
  verifyFile,
  verifyBuffer,
  decryptSecretKey,
  // Plan PD-18: exported for the SC-027 anchors only; no tool or production caller uses it.
  _internal: { blake2b, rawScryptParams },
};
