#!/usr/bin/env node
// The loopback static server of the `updater-e2e` job (spec FR-071; plan §4.9). CI only.
//
// Binds 127.0.0.1 on a free port (there is no `--host`), writes the port to `--port-file` once it
// listens, and serves files that sit directly in `--dir`:
//   GET /latest.json, GET /versions.json                      -> <dir>/<that name>
//   GET /<owner>/<repo>/releases/download/<tag>/<v>/<name>    -> <dir>/<name>
// (the per-tag path of TA-24, the tag being `manager/v<version>`). The path is matched as sent,
// never decoded or normalised: every segment is `[A-Za-z0-9._-]+` and not `.` / `..`, so a `..`,
// a percent escape or a subdirectory is a 404. A name that is a symlink or not a regular file is a
// 404, so nothing outside `--dir` is ever read. Every other method or path is a 404 with no body.
// Each request appends `<method> <path> <status> <bytes>` to `--log`.
//
// usage: node scripts/e2e-update-server.js --dir <dir> --port-file <file> --log <file>
import { appendFileSync, lstatSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const USAGE = 'usage: node scripts/e2e-update-server.js --dir <dir> --port-file <file> --log <file>';
const HOST = '127.0.0.1';
const TOP_LEVEL = new Set(['latest.json', 'versions.json']);
const SEGMENT = /^[A-Za-z0-9._-]+$/;

/** A malformed argument list: exit 2. */
export class UsageError extends Error {
  exitCode = 2;
}

/**
 * The CLI arguments; each flag exactly once, nothing else (no `--host`, no `--port`).
 * @param {string[]} argv the arguments after the script name
 * @returns {{ dir: string, portFile: string, log: string }}
 */
export function parseArgs(argv) {
  /** @type {Record<string, string | undefined>} */
  const values = { '--dir': undefined, '--port-file': undefined, '--log': undefined };
  for (let i = 0; i < argv.length; i += 2) {
    const flag = argv.at(i) ?? '';
    const value = argv.at(i + 1);
    if (!Object.hasOwn(values, flag)) throw new UsageError(`unknown argument ${flag}\n${USAGE}`);
    if (value === undefined) throw new UsageError(`missing value after ${flag}\n${USAGE}`);
    if (values[flag] !== undefined) throw new UsageError(`${flag} given twice\n${USAGE}`);
    values[flag] = value;
  }
  const dir = values['--dir'];
  const portFile = values['--port-file'];
  const log = values['--log'];
  if (dir === undefined || portFile === undefined || log === undefined) {
    throw new UsageError(`--dir, --port-file and --log are required\n${USAGE}`);
  }
  return { dir, portFile, log };
}

/**
 * The file name a request path maps to, or `undefined` for a 404.
 * @param {string} requestPath the path as sent (query, if any, ignored)
 * @returns {string | undefined}
 */
function routeName(requestPath) {
  const q = requestPath.indexOf('?');
  const segs = (q === -1 ? requestPath : requestPath.slice(0, q)).split('/');
  if (segs.shift() !== '') return undefined;
  if (!segs.every((s) => SEGMENT.test(s) && s !== '.' && s !== '..')) return undefined;
  if (segs.length === 1) {
    const name = segs[0] ?? '';
    return TOP_LEVEL.has(name) ? name : undefined;
  }
  if (segs.length === 7 && segs[2] === 'releases' && segs[3] === 'download') return segs[6];
  return undefined;
}

/**
 * The bytes of `<dir>/<name>` if it is a regular file (not a symlink), else `undefined`.
 * @param {string} dir
 * @param {string} name
 * @returns {Buffer | undefined}
 */
function readServed(dir, name) {
  const file = path.join(dir, name);
  try {
    return lstatSync(file).isFile() ? readFileSync(file) : undefined;
  } catch {
    return undefined;
  }
}

/**
 * Starts the server in-process on 127.0.0.1, a free port.
 * @param {{ dir: string, log: string }} options
 * @returns {Promise<{ port: number, address: string, close: () => Promise<void> }>}
 */
export async function startServer({ dir, log }) {
  const server = http.createServer((req, res) => {
    const method = req.method ?? '';
    const requestPath = req.url ?? '';
    const name = method === 'GET' ? routeName(requestPath) : undefined;
    const body = name === undefined ? undefined : readServed(dir, name);
    const status = body === undefined ? 404 : 200;
    const bytes = body?.length ?? 0;
    appendFileSync(log, `${method} ${requestPath} ${String(status)} ${String(bytes)}\n`);
    res.writeHead(status, {
      'Content-Type': name?.endsWith('.json') && body !== undefined ? 'application/json' : 'application/octet-stream',
      'Content-Length': String(bytes),
    });
    res.end(body);
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen({ host: HOST, port: 0 }, () => {
      resolve(undefined);
    });
  });
  const bound = server.address();
  if (bound === null || typeof bound === 'string') throw new Error('the server has no TCP address');
  return {
    port: bound.port,
    address: bound.address,
    close: () =>
      new Promise((resolve, reject) => {
        server.close((e) => {
          if (e) reject(e);
          else resolve();
        });
        server.closeAllConnections();
      }),
  };
}

async function main() {
  let args;
  try {
    args = parseArgs(process.argv.slice(2));
  } catch (e) {
    process.stderr.write(`e2e-update-server: ${e instanceof Error ? e.message : String(e)}\n`);
    process.exit(2);
  }
  const { port } = await startServer({ dir: args.dir, log: args.log });
  // Temp file + rename, so a poller never reads a half-written port.
  const tmp = `${args.portFile}.tmp`;
  writeFileSync(tmp, `${String(port)}\n`);
  renameSync(tmp, args.portFile);
  process.stdout.write(`e2e-update-server: http://${HOST}:${String(port)}/ serving ${args.dir}\n`);
}

const entry = process.argv.at(1);
if (entry !== undefined && import.meta.url === pathToFileURL(entry).href) {
  main().catch((/** @type {unknown} */ e) => {
    process.stderr.write(`e2e-update-server: ${e instanceof Error ? e.message : String(e)}\n`);
    process.exit(1);
  });
}
