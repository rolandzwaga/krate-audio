#!/usr/bin/env node
// Writes one helper job for the elevated CI jobs (spec FR-001, FR-021, FR-027, FR-100; plan §4.6).
// CI only: the job lands at `<S>/jobs/job-<32 hex>.json` with FR-001's keys in order (compact JSON),
// and `<job path> <sha256 of the job bytes>` is printed on stdout. Every id is an argument; this
// script names no plug-in and no fixture.
//
// Windows path form (FR-027, FR-021): the helper accepts only verbatim `\\?\` paths for `file` and
// `--job`, so on win32 `--state` and every `--file` are canonicalized with `fs.realpathSync.native`
// and written / printed in verbatim form. Elsewhere every path is used as given.
//
// usage: node scripts/write-test-job.js --state <S> (--op <kind> [--file <path> --sha256 <hex> | --app-id <id> | --pkg-id <id>])...
import { Buffer } from 'node:buffer';
import { randomBytes, createHash } from 'node:crypto';
import { mkdirSync, realpathSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const USAGE =
  'usage: node scripts/write-test-job.js --state <S> (--op <kind> [--file <path> --sha256 <hex> | --app-id <id> | --pkg-id <id>])...';

/**
 * Each op kind's keys after `op`, in FR-001's order, with the flag that supplies each.
 * @type {Record<string, [string, string][]>}
 */
const KINDS = {
  run_inno_installer: [['file', '--file'], ['sha256', '--sha256']],
  run_inno_uninstaller: [['app_id', '--app-id']],
  run_pkg_installer: [['file', '--file'], ['sha256', '--sha256']],
  uninstall_pkg: [['pkg_id', '--pkg-id']],
};
const OP_FLAGS = new Set(['--file', '--sha256', '--app-id', '--pkg-id']);

/** A malformed argument list (exit 2); any other failure is exit 1. */
class UsageError extends Error {}

/**
 * The Windows verbatim form of `p` on `win32`: `\\?\` + a drive path, `\\?\UNC\` + the rest of a
 * `\\server\share\…` path, an already verbatim `\\?\…` path unchanged. Any other platform: `p`.
 * @param {string} p
 * @param {string} platform a `process.platform` value
 * @returns {string}
 */
export function verbatim(p, platform) {
  if (platform !== 'win32' || p.startsWith('\\\\?\\')) return p;
  if (p.startsWith('\\\\')) return `\\\\?\\UNC\\${p.slice(2)}`;
  return `\\\\?\\${p}`;
}

/**
 * @param {string} message
 * @returns {never}
 */
function usage(message) {
  throw new UsageError(`${message}\n${USAGE}`);
}

/**
 * @param {string[]} argv
 * @returns {{ state: string, ops: { kind: string, values: Map<string, string> }[] }}
 */
function parseArgs(argv) {
  /** @type {string | undefined} */
  let state;
  /** @type {{ kind: string, values: Map<string, string> }[]} */
  const ops = [];
  for (let i = 0; i < argv.length; i += 2) {
    const flag = argv.at(i) ?? '';
    const value = argv.at(i + 1);
    if (value === undefined) usage(`missing value after ${flag}`);
    if (flag === '--state') {
      if (state !== undefined) usage('--state given twice');
      state = value;
    } else if (flag === '--op') {
      if (!Object.hasOwn(KINDS, value)) usage(`unknown op ${value}`);
      ops.push({ kind: value, values: new Map() });
    } else if (OP_FLAGS.has(flag)) {
      const op = ops.at(-1);
      if (op === undefined) usage(`${flag} before any --op`);
      if (op.values.has(flag)) usage(`${flag} given twice for one --op`);
      op.values.set(flag, value);
    } else {
      usage(`unknown argument ${flag}`);
    }
  }
  if (state === undefined) usage('--state is required');
  if (ops.length === 0) usage('at least one --op is required');
  for (const op of ops) {
    const want = (KINDS[op.kind] ?? []).map(([, flag]) => flag);
    const got = [...op.values.keys()];
    if (got.length !== want.length || !want.every((flag) => op.values.has(flag))) {
      usage(`--op ${op.kind} takes exactly ${want.join(' ')}, got ${got.join(' ') || 'none'}`);
    }
  }
  return { state, ops };
}

/**
 * Writes the job `argv` describes and returns the line to print, `<job path> <sha256>`.
 * @param {string[]} argv the arguments after the script name
 * @param {string} platform a `process.platform` value (forced in the test)
 * @returns {string}
 */
export function run(argv, platform) {
  const { state, ops } = parseArgs(argv);
  /** @param {string} p */
  const canonical = (p) => (platform === 'win32' ? realpathSync.native(p) : p);
  const job = {
    job_version: 1,
    ops: ops.map(({ kind, values }) => {
      /** @type {Record<string, string>} */
      const op = { op: kind };
      for (const [key, flag] of KINDS[kind] ?? []) {
        const value = values.get(flag) ?? '';
        op[key] = key === 'file' ? verbatim(canonical(value), platform) : value;
      }
      return op;
    }),
  };
  const bytes = Buffer.from(JSON.stringify(job), 'utf8');
  const jobs = path.join(canonical(state), 'jobs');
  mkdirSync(jobs, { recursive: true });
  const file = path.join(jobs, `job-${randomBytes(16).toString('hex')}.json`);
  writeFileSync(file, bytes, { flag: 'wx' });
  return `${verbatim(file, platform)} ${createHash('sha256').update(bytes).digest('hex')}`;
}

function main() {
  let line;
  try {
    line = run(process.argv.slice(2), process.platform);
  } catch (e) {
    process.stderr.write(`write-test-job: ${e instanceof Error ? e.message : String(e)}\n`);
    process.exit(e instanceof UsageError ? 2 : 1);
  }
  process.stdout.write(`${line}\n`);
}

const entry = process.argv.at(1);
if (entry !== undefined && import.meta.url === pathToFileURL(entry).href) main();
