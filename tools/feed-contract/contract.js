// Krate feed contract: shared rules for C1 (versions.json v2), C2 (release.json) and C3 (trusted comments).
//
// WHY: gen-feed.js (Phase 1), gen-release-manifest.js (Phase 2) and the fixture builder must apply one set of rules,
// and Phase 5 / 7 port them to Rust from CONTRACT.md. Every rule lives here once; the schema files carry the same
// pattern text (spec FR-001 to FR-022, FR-046; plan §3, §4.2).
//
// Every function that rejects throws a ContractError (first failure, fixed check order) whose `code` is a key of
// CODES, except validateFeed / validateRelease, which return every finding. No I/O beyond loading the three schema
// files as modules, no network, no crypto: signatures are parsed here, verified by Phase 2's tools/minisign.js and
// Phase 7's Rust.
'use strict';
// Ajv 8.20.0, JSON Schema 2020-12 class: https://ajv.js.org/json-schema.html#draft-2020-12
const Ajv2020 = require('ajv/dist/2020');

class ContractError extends Error {
  /** @param {string} code  e.g. 'E_TC_BINDING'
   *  @param {string} message  names the plugin, file and field where known
   *  @param {{plugin?: string, file?: string, field?: string, instancePath?: string, keyword?: string}} [where] */
  constructor(code, message, where = {}) {
    super(message);
    this.name = 'ContractError';
    this.code = code;
    Object.assign(this, where);
  }
}

// --- error codes (plan §3.2, PD-17): code -> gen-feed class
const CODES = Object.freeze({
  E_USAGE: 'usage',
  E_INPUT: 'abort',
  E_PLUGIN_NAME: 'abort',
  E_V1_TYPE: 'abort',
  E_DUPLICATE_PLUGIN: 'abort',
  E_TAG_FORMAT: 'abort',
  E_URL_NOT_HTTPS: 'abort-or-degrade',
  E_URL_LATEST: 'abort-or-degrade',
  E_URL_NOT_PER_TAG: 'degrade',
  E_OUTPUT_SCHEMA: 'abort',
  E_MANIFEST_SCHEMA: 'degrade',
  E_MANIFEST_DUPLICATE: 'degrade', // spec Clarifications 2026-10-30 Q-A (PD-8): degrade
  E_MANIFEST_MISMATCH: 'degrade',
  E_SIGNATURE_TEXT: 'degrade',
  E_TC_FIELDS: 'degrade',
  E_TC_NAME: 'degrade',
  E_TC_VERSION: 'degrade',
  E_TC_PLATFORM: 'degrade',
  E_TC_ASSET: 'degrade',
  E_TC_BINDING: 'degrade',
  E_INSTALL_VALUE: 'degrade',
  E_INSTALL_DUPLICATE: 'degrade',
  E_LAYOUT: 'degrade',
  E_LISTING_PATH: 'degrade',
  E_LISTING_ABSOLUTE: 'degrade',
  E_LISTING_DRIVE: 'degrade',
  E_LISTING_BACKSLASH: 'degrade',
  E_LISTING_DOTDOT: 'degrade',
  E_LISTING_DUPLICATE: 'degrade',
  E_LISTING_ROOT_SYMLINK: 'degrade',
  E_LISTING_SYMLINK_FORM: 'degrade',
  E_LISTING_SYMLINK_ESCAPE: 'degrade',
  E_LISTING_SYMLINK_CHAIN: 'degrade',
  E_LISTING_THROUGH_SYMLINK: 'degrade',
  E_LISTING_TYPE: 'degrade',
  E_LISTING_CHECKSUM: 'degrade',
  E_LISTING_TRUNCATED: 'degrade',
  E_LISTING_CORRUPT: 'degrade',
  E_LISTING_NOT_CANONICAL: 'degrade',
  E_LISTING_COUNT: 'degrade',
  E_LISTING_DIGEST: 'degrade',
  E_LISTING_NO_POSIX_MODES: 'none',
  E_SCHEMA: 'none',
  E_MINISIG_PUBKEY: 'none',
  E_MINISIG_SECRET_KEY: 'none',
  E_MINISIG_PASSWORD: 'none',
  E_MINISIG_KEY_ID: 'none',
  E_MINISIG_SIGNATURE: 'none',
  E_MINISIG_GLOBAL: 'none',
  E_FACTS_NO_MACHO: 'none',
  E_FACTS_ARCH: 'none',
  E_FACTS_ARCH_MISMATCH: 'none',
  E_FACTS_NO_MINOS: 'none',
  E_FACTS_MACHO_CORRUPT: 'none',
  E_FACTS_NO_GLIBC: 'none',
  E_FACTS_LISTING_MISMATCH: 'none',
  E_FACTS_LIPO_MISMATCH: 'none',
  E_FACTS_STALE: 'none',
  E_FACTS_MISSING: 'none',
  E_ASSET_MISSING: 'none',
  E_VERSION_MISMATCH: 'none',
  E_SIGNING_CONFIG: 'none',
  E_BACKFILL_LIST_TRUNCATED: 'none',
  E_BACKFILL_ASSET_MISSING: 'none',
  E_BACKFILL_IMMUTABLE: 'none',
  E_BACKFILL_PARTIAL: 'none',
  E_BACKFILL_SIZE: 'none',
  E_BACKFILL_DIGEST: 'none',
  E_BACKFILL_ASSETS_CHANGED: 'none',
  E_SIGN_KEY_MISMATCH: 'none',
});

// --- platform keys and asset names
const PLATFORM_KEYS = Object.freeze([
  'windows-x86_64',
  'darwin-aarch64',
  'darwin-x86_64',
  'darwin-universal',
  'linux-x86_64',
]);
// .github/workflows/release.yml:524-526
const ASSET_SUFFIX = Object.freeze({ windows: '-Windows-x64.exe', darwin: '-macOS.pkg', linux: '-Linux-x64.tar.gz' });

// --- patterns (written once; the schema files carry the same text)
const CORE_PART = '(0|[1-9][0-9]{0,8})'; // FR-004: at most 9 digits, so every part fits a uint32_t
const CORE = `${CORE_PART}\\.${CORE_PART}\\.${CORE_PART}`;
const PRE_ID = '(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'; // SemVer 2.0.0 §9 identifier
const PRE = `${PRE_ID}(\\.${PRE_ID})*`;
const RESERVED_STEM =
  '^([Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][0-9]|[Ll][Pp][Tt][0-9])(\\..*)?$'; // PD-14

const CORE_RE = new RegExp(`^${CORE}$`, 'u');
const SEMVER_RE = new RegExp(`^${CORE}(-${PRE})?$`, 'u'); // bounded core + optional prerelease, no build metadata
const PLUGIN_NAME_RE = new RegExp('^[A-Z][A-Za-z0-9]{0,63}$', 'u'); // FR-002
const RESERVED_STEM_RE = new RegExp(RESERVED_STEM, 'u');
const ASSET_NAME_RE = new RegExp('^[A-Za-z0-9][A-Za-z0-9._-]*$', 'u'); // FR-020
const URL_RE = new RegExp('^https://[^\\s]+$', 'u'); // FR-005
const URL_LATEST = '/releases/latest/';
// Unbounded SemVer 2.0.0 (no build metadata) for precedence; the bounded forms above decide validity.
const SEMVER_ANY_RE = new RegExp(`^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-${PRE})?$`, 'u');

// FR-008 (as amended by PD-14)
const INSTALL_PATTERNS = Object.freeze({
  vst3_bundle: new RegExp('^[A-Za-z0-9][A-Za-z0-9 _.-]*\\.vst3$', 'u'),
  au_component: new RegExp('^[A-Za-z0-9][A-Za-z0-9 _.-]*\\.component$', 'u'),
  preset_subdir: new RegExp('^[A-Za-z0-9]([A-Za-z0-9 _-]*[A-Za-z0-9_-])?$', 'u'),
  windows_uninstall_key: new RegExp(
    '^\\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\\}_is1$',
    'u',
  ),
  macos_pkg_id: new RegExp('^com\\.krateaudio\\.[a-z0-9]+$', 'u'),
});
const NAMED_INSTALL_FIELDS = Object.freeze(['vst3_bundle', 'au_component', 'preset_subdir']); // reserved-stem check
// FR-014 / PD-13: compared after ASCII case folding, except windows_uninstall_key (exact).
const UNIQUE_INSTALL_FIELDS = Object.freeze(['vst3_bundle', 'au_component', 'windows_uninstall_key', 'macos_pkg_id']);
const CASE_FOLDED_FIELDS = new Set(['vst3_bundle', 'au_component', 'macos_pkg_id']);

const isObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const show = (v) => JSON.stringify(v);

// --- versions

/** SemVer 2.0.0 without build metadata; numeric parts as BigInt. null when `str` is not one. */
function parseSemVer(str) {
  if (typeof str !== 'string') return null;
  const m = SEMVER_ANY_RE.exec(str);
  if (!m) return null;
  const dash = str.indexOf('-');
  const pre = dash < 0 ? [] : str.slice(dash + 1).split('.').map((id) => (/^[0-9]+$/.test(id) ? BigInt(id) : id));
  return { major: BigInt(m[1]), minor: BigInt(m[2]), patch: BigInt(m[3]), pre };
}

function toSemVer(v) {
  const parsed = typeof v === 'string' ? parseSemVer(v) : v;
  if (!parsed) throw new TypeError(`compareSemVer: not a SemVer 2.0.0 version: ${show(v)}`);
  return parsed;
}

const cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

/** Precedence per https://semver.org/spec/v2.0.0.html §11. Takes strings or parseSemVer results. */
function compareSemVer(a, b) {
  const x = toSemVer(a);
  const y = toSemVer(b);
  for (const k of ['major', 'minor', 'patch']) {
    const c = cmp(x[k], y[k]);
    if (c !== 0) return c;
  }
  if (x.pre.length === 0 || y.pre.length === 0) return cmp(y.pre.length, x.pre.length); // no prerelease ranks higher
  for (let i = 0; i < Math.min(x.pre.length, y.pre.length); i += 1) {
    const p = x.pre[i];
    const q = y.pre[i];
    const pNum = typeof p === 'bigint';
    const qNum = typeof q === 'bigint';
    if (pNum !== qNum) return pNum ? -1 : 1; // numeric identifiers rank below alphanumeric ones
    const c = cmp(p, q); // BigInt numerically, strings in ASCII order
    if (c !== 0) return c;
  }
  return cmp(x.pre.length, y.pre.length);
}

/** FR-004: the bounded three-part core, nothing else. */
function isStableCore(str) {
  return typeof str === 'string' && CORE_RE.test(str);
}

/** FR-002 plus PD-14: the plugin-key pattern, and not a Windows reserved device name. */
function isPluginName(str) {
  return typeof str === 'string' && PLUGIN_NAME_RE.test(str) && !RESERVED_STEM_RE.test(str);
}

// --- URL and install values

/** FR-005: https, no whitespace, never a /releases/latest/ link. */
function checkUrl(url, { field = 'url' } = {}) {
  if (typeof url !== 'string' || !URL_RE.test(url)) {
    throw new ContractError('E_URL_NOT_HTTPS', `${field}: not an https URL without whitespace: ${show(url)}`, {
      field,
    });
  }
  if (url.includes(URL_LATEST)) {
    throw new ContractError('E_URL_LATEST', `${field}: a ${URL_LATEST} URL is never allowed: ${url}`, { field });
  }
}

/**
 * FR-008 (PD-14): every present install value is one safe name. Throws E_INSTALL_VALUE naming the field.
 * Required-ness is the schema's job; this checks only the values that are there. `install` that is not an object is
 * left to the schema too (the caller's shape check runs first).
 */
function checkInstallValues(install) {
  if (!isObject(install)) return;
  for (const field of Object.keys(INSTALL_PATTERNS)) {
    if (!Object.prototype.hasOwnProperty.call(install, field)) continue;
    const value = install[field];
    const where = { field: `install.${field}` };
    const fail = (why) => {
      throw new ContractError('E_INSTALL_VALUE', `install.${field}: ${why}: ${show(value)}`, where);
    };
    if (typeof value !== 'string') fail('not a string');
    if (/[/\\:]/u.test(value)) fail('contains a path separator or a colon');
    if (value.includes('..')) fail("contains '..'");
    if (field === 'preset_subdir' && value.endsWith(' ')) fail('ends in a space');
    if (NAMED_INSTALL_FIELDS.includes(field) && RESERVED_STEM_RE.test(value)) fail('a Windows reserved device name');
    if (!INSTALL_PATTERNS[field].test(value)) fail('outside its FR-008 pattern');
  }
}

/**
 * FR-014 (PD-13): pairs of different plugins that share an install value. An entry and its own prerelease sibling
 * are one plugin; an entry without `install` takes no part. `value` is the first plugin's spelling.
 * @returns {Array<{field: string, value: string, plugins: [string, string]}>}
 */
function findInstallDuplicates(plugins) {
  const found = [];
  if (!isObject(plugins)) return found;
  const seen = new Map(UNIQUE_INSTALL_FIELDS.map((f) => [f, new Map()])); // field -> folded value -> {plugin, value}
  for (const [plugin, entry] of Object.entries(plugins)) {
    if (!isObject(entry)) continue;
    const installs = [entry.install, isObject(entry.prerelease) ? entry.prerelease.install : undefined];
    for (const field of UNIQUE_INSTALL_FIELDS) {
      const mine = new Set();
      for (const install of installs) {
        if (!isObject(install) || typeof install[field] !== 'string') continue;
        const value = install[field];
        const key = CASE_FOLDED_FIELDS.has(field) ? asciiLower(value) : value;
        if (mine.has(key)) continue;
        mine.add(key);
        const first = seen.get(field).get(key);
        if (first) found.push({ field, value: first.value, plugins: [first.plugin, plugin] });
        else seen.get(field).set(key, { plugin, value });
      }
    }
  }
  return found;
}

const asciiLower = (s) => s.replace(/[A-Z]/gu, (c) => c.toLowerCase());

/** The defined PLATFORM_KEYS present in an entry's (or sibling's) `platforms`, in PLATFORM_KEYS order. */
function definedPlatformKeys(entryOrSibling) {
  const platforms = isObject(entryOrSibling) ? entryOrSibling.platforms : undefined;
  if (!isObject(platforms)) return [];
  return PLATFORM_KEYS.filter((k) => Object.prototype.hasOwnProperty.call(platforms, k));
}

// --- C3 trusted comments (FR-020 to FR-022)

const TC_FIELDS = Object.freeze(['name', 'version', 'platform', 'asset']);

function tcFieldError(field, value) {
  const code = { name: 'E_TC_NAME', version: 'E_TC_VERSION', platform: 'E_TC_PLATFORM', asset: 'E_TC_ASSET' }[field];
  return new ContractError(code, `trusted comment: invalid ${field} ${show(value)}`, { field });
}

const TC_FIELD_OK = Object.freeze({
  name: (v) => isPluginName(v),
  version: (v) => SEMVER_RE.test(v),
  platform: (v) => PLATFORM_KEYS.includes(v),
  asset: (v) => ASSET_NAME_RE.test(v) && v !== '.' && v !== '..',
});

/** `krate:1:<Name>:<version>:<platform-key>:<asset-file-name>` -> its four values; throws E_TC_*. */
function parseTrustedComment(str) {
  if (typeof str !== 'string') {
    throw new ContractError('E_TC_FIELDS', `trusted comment: not a string: ${show(str)}`);
  }
  if (/[\r\n]/u.test(str)) {
    throw new ContractError('E_TC_FIELDS', `trusted comment: holds a CR or LF: ${show(str)}`);
  }
  const f = str.split(':');
  if (f.length !== 6) {
    throw new ContractError('E_TC_FIELDS', `trusted comment: ${f.length} ':' fields, not 6: ${show(str)}`);
  }
  if (f[0] !== 'krate' || f[1] !== '1') {
    throw new ContractError('E_TC_FIELDS', `trusted comment: prefix is not krate:1: ${show(str)}`);
  }
  const out = { name: f[2], version: f[3], platform: f[4], asset: f[5] };
  for (const field of TC_FIELDS) {
    if (!TC_FIELD_OK[field](out[field])) throw tcFieldError(field, out[field]);
  }
  return out;
}

/** The C3 comment for these values; throws the code parsing the result would (so a ':' or CR is E_TC_FIELDS). */
function formatTrustedComment(name, version, platform, asset) {
  const values = { name, version, platform, asset };
  for (const field of TC_FIELDS) {
    if (typeof values[field] !== 'string') throw tcFieldError(field, values[field]);
  }
  const comment = ['krate', '1', name, version, platform, asset].join(':');
  parseTrustedComment(comment);
  return comment;
}

/** FR-022: the comment must parse and match `expected` field by field; names the first differing field. */
function checkBinding(trustedComment, expected) {
  const got = parseTrustedComment(trustedComment);
  for (const field of TC_FIELDS) {
    if (got[field] !== expected[field]) {
      throw new ContractError(
        'E_TC_BINDING',
        `trusted comment ${field} is ${show(got[field])}, expected ${show(expected[field])}`,
        { field },
      );
    }
  }
}

// --- minisign text (D7; https://jedisct1.github.io/minisign/ "Signature format", minisign 0.12)

const UNTRUSTED_PREFIX = 'untrusted comment: ';
const TRUSTED_PREFIX = 'trusted comment: ';
const SIG_BLOB_BYTES = 74; // 'ED' + 8-byte key id + 64-byte signature
const GLOBAL_SIG_BYTES = 64;

function sigError(why) {
  return new ContractError('E_SIGNATURE_TEXT', `signature text: ${why}`);
}

function decodeCanonicalBase64(s, what) {
  const buf = Buffer.from(s, 'base64');
  if (buf.toString('base64') !== s) throw sigError(`${what} is not canonical base64`);
  return buf;
}

/** Four lines, LF or CRLF, optional final line end. No cryptography. */
function parseMinisigText(text) {
  if (typeof text !== 'string') throw sigError('not a string');
  const lines = text.split(/\r?\n/u);
  if (lines[lines.length - 1] === '') lines.pop();
  if (lines.length !== 4) throw sigError(`${lines.length} lines, not 4`);
  if (lines.some((l) => l === '' || l.includes('\r'))) throw sigError('an empty line or a stray CR');
  if (!lines[0].startsWith(UNTRUSTED_PREFIX)) throw sigError(`line 1 does not start ${show(UNTRUSTED_PREFIX)}`);
  if (!lines[2].startsWith(TRUSTED_PREFIX)) throw sigError(`line 3 does not start ${show(TRUSTED_PREFIX)}`);
  const blob = decodeCanonicalBase64(lines[1], 'line 2');
  if (blob.length !== SIG_BLOB_BYTES) throw sigError(`line 2 decodes to ${blob.length} bytes, not ${SIG_BLOB_BYTES}`);
  if (blob[0] !== 0x45 || blob[1] !== 0x44) throw sigError("line 2 algorithm is not 'ED'");
  const global = decodeCanonicalBase64(lines[3], 'line 4');
  if (global.length !== GLOBAL_SIG_BYTES) {
    throw sigError(`line 4 decodes to ${global.length} bytes, not ${GLOBAL_SIG_BYTES}`);
  }
  return {
    untrustedComment: lines[0].slice(UNTRUSTED_PREFIX.length),
    algorithm: 'ED',
    keyId: Buffer.from(blob.subarray(2, 10)),
    trustedComment: lines[2].slice(TRUSTED_PREFIX.length),
  };
}

/** D7: writers store LF; CRLF -> LF changes no signed byte. */
function toLfMinisig(text) {
  return text.replace(/\r\n/gu, '\n');
}

// --- schema validation (FR-001 to FR-014; plan §3.1, PD-3)

// Compiled once at load in strict mode, so a strict-mode violation in a schema file fails every require of this module.
const ajv = new Ajv2020({ strict: true, allErrors: true });
const VALIDATORS = Object.freeze({
  v2: ajv.compile(require('./versions.v2.schema.json')),
  v1: ajv.compile(require('./versions.v1.schema.json')),
  release: ajv.compile(require('./release.schema.json')),
});

/** RFC 6901 JSON Pointer from path segments, the form Ajv uses for instancePath. */
const pointer = (...segments) =>
  segments.map((s) => '/' + String(s).replace(/~/gu, '~0').replace(/\//gu, '~1')).join('');

function ajvFindings(validate, doc) {
  if (validate(doc)) return [];
  return validate.errors.map((e) => ({
    code: 'E_SCHEMA',
    instancePath: e.instancePath,
    keyword: e.keyword,
    message: `${e.instancePath || '/'} ${e.message}`,
  }));
}

function finding(err, instancePath, extra = {}) {
  if (!(err instanceof ContractError)) throw err;
  return { code: err.code, instancePath, message: `${instancePath}: ${err.message}`, ...extra };
}

/** The code rules of a v2 feed that JSON Schema cannot state: FR-007 blob and C3 comment, FR-008 values, FR-014. */
function feedCodeFindings(doc) {
  const out = [];
  if (!isObject(doc) || !isObject(doc.plugins)) return out;
  for (const [plugin, entry] of Object.entries(doc.plugins)) {
    if (!isObject(entry)) continue;
    const parts = [[entry, pointer('plugins', plugin)]];
    if (isObject(entry.prerelease)) parts.push([entry.prerelease, pointer('plugins', plugin, 'prerelease')]);
    for (const [part, base] of parts) {
      if (isObject(part.platforms)) {
        for (const [key, platform] of Object.entries(part.platforms)) {
          if (!isObject(platform) || typeof platform.signature !== 'string') continue; // the schema reports it
          const at = `${base}${pointer('platforms', key, 'signature')}`;
          if (platform.signature.includes('\r')) {
            out.push(finding(sigError('holds a CR (LF line ends only, D7)'), at));
            continue;
          }
          let parsed;
          try {
            parsed = parseMinisigText(platform.signature);
          } catch (err) {
            out.push(finding(err, at));
            continue;
          }
          // C3's platform field names only the defined keys; a reader ignores any other key (D1), so its comment is
          // not judged by today's grammar.
          if (!PLATFORM_KEYS.includes(key)) continue;
          try {
            parseTrustedComment(parsed.trustedComment);
          } catch (err) {
            out.push(finding(err, at));
          }
        }
      }
      try {
        checkInstallValues(part.install);
      } catch (err) {
        out.push(finding(err, `${base}${pointer(...err.field.split('.'))}`, { field: err.field }));
      }
    }
  }
  for (const dup of findInstallDuplicates(doc.plugins)) {
    const [first, second] = dup.plugins;
    const entry = doc.plugins[second];
    const fold = (v) => (typeof v === 'string' && CASE_FOLDED_FIELDS.has(dup.field) ? asciiLower(v) : v);
    const inEntry = isObject(entry.install) && fold(entry.install[dup.field]) === fold(dup.value);
    const instancePath = inEntry
      ? pointer('plugins', second, 'install', dup.field)
      : pointer('plugins', second, 'prerelease', 'install', dup.field);
    out.push({
      code: 'E_INSTALL_DUPLICATE',
      instancePath,
      field: `install.${dup.field}`,
      message:
        `install.${dup.field} ${show(dup.value)} is shared by plugins ${show(first)} and ${show(second)} (FR-014)`,
    });
  }
  return out;
}

/**
 * C1 (v2) or the v1 compatibility floor. Ajv findings come back as {code: 'E_SCHEMA', instancePath, keyword,
 * message}; under v2 the code rules follow (E_SIGNATURE_TEXT, E_TC_*, E_INSTALL_VALUE, E_INSTALL_DUPLICATE).
 * @returns {{valid: boolean, errors: Array<{code: string, instancePath: string, keyword?: string, message: string}>}}
 */
function validateFeed(doc, { schema = 'v2' } = {}) {
  if (schema !== 'v2' && schema !== 'v1') {
    throw new RangeError(`validateFeed: schema must be 'v2' or 'v1': ${show(schema)}`);
  }
  const errors = ajvFindings(VALIDATORS[schema], doc);
  if (schema === 'v2') errors.push(...feedCodeFindings(doc));
  return { valid: errors.length === 0, errors };
}

/**
 * PD-10 backstop (plan §4.2, §4.5.4 step 4): one assembled entry (with its sibling) validated on its own as the
 * one-plugin C1 document {schema_version: 2, plugins: {[name]: entry}}. Throws E_MANIFEST_SCHEMA carrying the first
 * finding's instancePath (and keyword, for an Ajv finding); returns nothing when the entry passes.
 */
function checkAssembledEntry(name, entry) {
  const result = validateFeed({ schema_version: 2, plugins: { [name]: entry } }, { schema: 'v2' });
  if (result.valid) return;
  const [first] = result.errors;
  throw new ContractError(
    'E_MANIFEST_SCHEMA',
    `plugin ${show(name)}: the assembled entry fails C1 at ${first.instancePath || '/'}: ${first.message}`,
    { plugin: name, instancePath: first.instancePath, ...(first.keyword !== undefined ? { keyword: first.keyword } : {}) },
  );
}

/** C2 schema only (release.schema.json); checkReleaseManifest adds the cross-field rules. */
function validateRelease(doc) {
  const errors = ajvFindings(VALIDATORS.release, doc);
  return { valid: errors.length === 0, errors };
}

/**
 * C2 in full (FR-015 to FR-017): throws the first failure as a ContractError carrying `plugin` (the manifest's name
 * when it is a string), `file` (when given) and `field`; returns nothing when the manifest passes. Order (plan §4.2):
 * shape -> checkInstallValues -> checkUrl per asset -> minisign text per asset -> C2 schema -> tag, asset-name,
 * vst3_bundle, au_component naming rules -> per-tag URL -> trusted-comment binding -> payload listing (canonical form,
 * count, digest) -> C4 layout. The value checks run before the schema so the specific code wins (FR-046).
 */
function checkReleaseManifest(doc, { repo, file } = {}) {
  if (typeof repo !== 'string') throw new TypeError(`checkReleaseManifest: repo must be a string: ${show(repo)}`);
  // Lazy: listing.js requires this module, so a top-level require would hand it an unfinished exports object.
  const listing = require('./listing.js');
  const plugin = isObject(doc) && typeof doc.name === 'string' ? doc.name : undefined;
  const fail = (code, field, message, extra = {}) => {
    const who = [file, plugin].filter((s) => s !== undefined).join(': ');
    const where = { ...(plugin !== undefined ? { plugin } : {}), ...(file !== undefined ? { file } : {}), field };
    return new ContractError(code, `${who ? `${who}: ` : ''}${field}: ${message}`, { ...where, ...extra });
  };
  /** Runs fn; a ContractError it throws comes back with this manifest's plugin, file and field. */
  const at = (field, fn) => {
    try {
      return fn();
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      const named = typeof err.field === 'string' && err.field.startsWith(`${field}.`) ? err.field : field; // install.x
      const message = err.message.startsWith(`${named}: `) ? err.message.slice(named.length + 2) : err.message;
      throw fail(err.code, named, message);
    }
  };

  // 1. shape: the loops below need objects where objects belong; anything else is the schema's to name.
  if (!isObject(doc)) throw fail('E_MANIFEST_SCHEMA', '(root)', 'not a JSON object');
  if (doc.install !== undefined && !isObject(doc.install)) throw fail('E_MANIFEST_SCHEMA', 'install', 'not an object');
  if (doc.assets !== undefined && !isObject(doc.assets)) throw fail('E_MANIFEST_SCHEMA', 'assets', 'not an object');
  const assets = Object.entries(doc.assets ?? {});
  for (const [key, asset] of assets) {
    if (!isObject(asset)) throw fail('E_MANIFEST_SCHEMA', `assets.${key}`, 'not an object');
  }

  // 2.-4. value checks with their own codes.
  at('install', () => checkInstallValues(doc.install));
  for (const [key, asset] of assets) {
    const field = `assets.${key}.url`;
    if (asset.url !== undefined) at(field, () => checkUrl(asset.url, { field }));
  }
  const comments = new Map();
  for (const [key, asset] of assets) {
    if (asset.signature === undefined) continue;
    const field = `assets.${key}.signature`;
    if (typeof asset.signature === 'string' && asset.signature.includes('\r')) {
      throw fail('E_SIGNATURE_TEXT', field, 'holds a CR (LF line ends only, D7)');
    }
    comments.set(key, at(field, () => parseMinisigText(asset.signature)).trustedComment);
  }

  // 5. the C2 schema.
  const schema = validateRelease(doc);
  if (!schema.valid) {
    const [first] = schema.errors;
    // The dotted field form the other checks use; instancePath and keyword stay as Ajv gave them.
    const field = first.instancePath.split('/').slice(1).map((s) => s.replace(/~1/gu, '/').replace(/~0/gu, '~')).join('.');
    const message = first.message.slice(first.message.indexOf(' ') + 1); // drop ajvFindings' "<path> " prefix
    throw fail('E_MANIFEST_SCHEMA', field || '(root)', message, {
      instancePath: first.instancePath,
      keyword: first.keyword,
    });
  }

  // 6. FR-017 naming rules (the schema guarantees every field below exists with its type).
  const { name, version, tag, install } = doc;
  const expectedTag = `${name.toLowerCase()}/v${version}`;
  if (tag !== expectedTag) throw fail('E_MANIFEST_SCHEMA', 'tag', `is ${show(tag)}, expected ${show(expectedTag)}`);
  for (const [key, asset] of assets) {
    const expected = assetNameFor(name, version, key);
    if (asset.name !== expected) {
      throw fail('E_MANIFEST_SCHEMA', `assets.${key}.name`, `is ${show(asset.name)}, expected ${show(expected)}`);
    }
  }
  for (const [field, suffix] of [
    ['vst3_bundle', '.vst3'],
    ['au_component', '.component'],
  ]) {
    if (install[field] !== undefined && install[field] !== name + suffix) {
      throw fail('E_MANIFEST_SCHEMA', `install.${field}`, `is ${show(install[field])}, expected ${show(name + suffix)}`);
    }
  }

  // 7. per-tag URL.
  for (const [key, asset] of assets) {
    const expected = perTagUrl(repo, tag, asset.name);
    if (asset.url !== expected) {
      throw fail('E_URL_NOT_PER_TAG', `assets.${key}.url`, `is ${show(asset.url)}, expected ${show(expected)}`);
    }
  }

  // 8. each trusted comment binds its asset: formatTrustedComment(name, version, key, asset.name), field by field.
  for (const [key, asset] of assets) {
    at(`assets.${key}.signature`, () =>
      checkBinding(comments.get(key), { name, version, platform: key, asset: asset.name }),
    );
  }

  // 9.-10. the C4 payload of the tar.gz asset.
  for (const [key, asset] of assets) {
    if (asset.format !== 'tar.gz') continue;
    const { files, sha256, list } = asset.payload;
    const base = `assets.${key}.payload`;
    at(`${base}.list`, () => listing.checkCanonicalListing(list));
    if (files !== list.length) {
      throw fail('E_LISTING_COUNT', `${base}.files`, `is ${files}, the list holds ${list.length} lines`);
    }
    const actual = listing.digest(list);
    if (sha256 !== actual) throw fail('E_LISTING_DIGEST', `${base}.sha256`, `is ${sha256}, digest(list) is ${actual}`);
    at(`${base}.list`, () => listing.checkLinuxLayout(list, { vst3Bundle: install.vst3_bundle }));
  }
}

// --- names and URLs

/** FR-017 asset name for a defined platform key (release.yml:524-526). */
function assetNameFor(name, version, key) {
  if (!PLATFORM_KEYS.includes(key)) throw new RangeError(`assetNameFor: not a defined platform key: ${show(key)}`);
  return `${name}-${version}${ASSET_SUFFIX[key.slice(0, key.indexOf('-'))]}`;
}

/** FR-017 per-tag download URL (the tag's '/' stays literal). */
function perTagUrl(repo, tag, asset) {
  return `https://github.com/${repo}/releases/download/${tag}/${asset}`;
}

module.exports = {
  ContractError,
  CODES,
  PLATFORM_KEYS,
  ASSET_SUFFIX,
  parseSemVer,
  compareSemVer,
  isStableCore,
  validateFeed,
  validateRelease,
  checkAssembledEntry,
  checkReleaseManifest,
  checkInstallValues,
  checkUrl,
  findInstallDuplicates,
  isPluginName,
  definedPlatformKeys,
  formatTrustedComment,
  parseTrustedComment,
  checkBinding,
  parseMinisigText,
  toLfMinisig,
  assetNameFor,
  perTagUrl,
};
