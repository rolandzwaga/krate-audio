// Write and spawn guard for the feed-contract tests (spec FR-061, SC-015; plan §4.6, PD-15).
//
// WHY: this dev box has the user's real plug-ins, presets and repo checkout. A test that writes outside a temp
// directory, or spawns a tool that does, must fail loudly instead of trusting every author to get paths right.
// Every tools/feed-contract/tests/*.test.js requires this file as its first statement (hygiene.test.js checks it).
//
// What it does, once per process (globalThis[Symbol.for('krate.writeGuard')]):
//   - wraps every writing entry point of node:fs and node:fs/promises; a path that does not resolve under
//     realpath(os.tmpdir()) throws an Error with code 'E_WRITE_OUTSIDE_TMP';
//   - refuses any path holding a '..' segment (and a win32 '\\?\' / '\\.\' prefix) BEFORE resolving it, because
//     path.resolve collapses '<tmp>/L/../x' lexically while the kernel follows the link L first;
//   - resolves the nearest existing ancestor with realpath.native, so a link inside tmp that points outside is caught;
//   - checks both paths of link (a hard link from a repo file into tmp would let a later tmp write modify the repo file);
//   - turns child_process into an allowlist: only 'git', 'tar' and 'rsign' in fixed argument shapes, with a cwd
//     under tmp;
//     exec, execSync, fork and shell: true always throw.
// fd-based calls (write, fchmod, ftruncate, ...) are not wrapped: only a guarded open can hand out a writable fd.
//
// This is the one scanned file allowed to require fs and child_process before patching them (it is the patcher).
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const url = require('url');
const util = require('util');
const childProcess = require('child_process');

const GUARD_KEY = Symbol.for('krate.writeGuard');
const CODE = 'E_WRITE_OUTSIDE_TMP';
const SKIP = Symbol('skip');

if (!globalThis[GUARD_KEY]) {
  install();
}

function install() {
  const isWin = process.platform === 'win32';
  // Captured before anything is patched; neither is wrapped.
  const realpathNative = fs.realpathSync.native;
  const lstatSync = fs.lstatSync;
  const fold = (s) => (isWin ? s.toLowerCase() : s);
  const TMP = fold(realpathNative(os.tmpdir()));

  function guardError(op, subject, why) {
    const err = new Error(`write guard: ${op} refused (${why}): ${subject}`);
    err.code = CODE;
    return err;
  }

  // string | Buffer | Uint8Array | file: URL -> string; an fd, a FileHandle or an absent path -> SKIP.
  function toPathString(p) {
    if (typeof p === 'string') return p;
    if (Buffer.isBuffer(p)) return p.toString('utf8');
    if (p instanceof Uint8Array) return Buffer.from(p).toString('utf8');
    if (p instanceof URL) return url.fileURLToPath(p);
    if (p === undefined || p === null || typeof p === 'number') return SKIP;
    if (typeof p === 'object' && typeof p.fd === 'number') return SKIP;
    return String(p);
  }

  function rejectDotDot(s, op) {
    const segments = isWin ? s.split(/[\\/]/) : s.split('/');
    if (segments.includes('..')) throw guardError(op, s, "'..' segment");
    if (isWin) {
      const b = s.replace(/\//g, '\\');
      if (b.startsWith('\\\\?\\') || b.startsWith('\\\\.\\')) throw guardError(op, s, 'device or verbatim prefix');
    }
  }

  function exists(p) {
    try {
      return lstatSync(p, { throwIfNoEntry: false }) !== undefined;
    } catch {
      return true; // EPERM and the like: let realpath decide, and fail closed there.
    }
  }

  // Resolves like the write itself: the nearest existing ancestor through realpath (a final link is followed), then
  // the components that do not exist yet. null when the existing part cannot be resolved (a dangling link).
  function canonicalFollow(s) {
    let p = path.resolve(s);
    const rest = [];
    while (!exists(p)) {
      const parent = path.dirname(p);
      if (parent === p) return null;
      rest.unshift(path.basename(p));
      p = parent;
    }
    let real;
    try {
      real = realpathNative(p);
    } catch {
      return null;
    }
    return rest.length > 0 ? path.join(real, ...rest) : real;
  }

  // For operations on the directory entry itself, which never follow it (unlink, rm, rename, lchmod, ...).
  function canonicalEntry(s) {
    const r = path.resolve(s);
    const dir = canonicalFollow(path.dirname(r));
    return dir === null ? null : path.join(dir, path.basename(r));
  }

  function assertUnderTmp(p, op, { entry = false } = {}) {
    const s = toPathString(p);
    if (s === SKIP) return;
    rejectDotDot(s, op);
    const c = entry ? canonicalEntry(s) : canonicalFollow(s);
    if (c === null) throw guardError(op, s, 'path cannot be resolved');
    const f = fold(c);
    if (f === TMP || f.startsWith(TMP + path.sep)) return;
    throw guardError(op, s, 'outside os.tmpdir()');
  }

  // ---- fs -------------------------------------------------------------------------------------------------------

  const follow = (...idx) => (op, args) => idx.forEach((i) => assertUnderTmp(args[i], op));
  const entry = (...idx) => (op, args) => idx.forEach((i) => assertUnderTmp(args[i], op, { entry: true }));
  // mkdtemp appends six characters to the prefix: the created entry must land under tmp.
  const mkdtempCheck = (op, args) => {
    const s = toPathString(args[0]);
    if (s === SKIP) return;
    rejectDotDot(s, op);
    assertUnderTmp(s + 'XXXXXX', op, { entry: true });
  };
  const WRITE_BITS =
    fs.constants.O_WRONLY | fs.constants.O_RDWR | fs.constants.O_CREAT | fs.constants.O_TRUNC | fs.constants.O_APPEND;
  const isWriteFlag = (flags) => {
    if (flags === undefined || flags === null) return false;
    if (typeof flags === 'string') return flags !== 'r';
    if (typeof flags === 'number') return (flags & WRITE_BITS) !== 0;
    return true;
  };
  const openCheck = (op, args) => {
    if (isWriteFlag(args[1])) assertUnderTmp(args[0], op);
  };

  const FS_CHECKS = {
    writeFile: follow(0),
    appendFile: follow(0),
    mkdir: follow(0),
    mkdtemp: mkdtempCheck,
    mkdtempDisposable: mkdtempCheck,
    rename: entry(0, 1),
    copyFile: follow(1),
    cp: follow(1),
    rm: entry(0),
    rmdir: entry(0),
    unlink: entry(0),
    symlink: entry(1),
    link: (op, args) => {
      assertUnderTmp(args[0], op);
      assertUnderTmp(args[1], op, { entry: true });
    },
    truncate: follow(0),
    chmod: follow(0),
    lchmod: entry(0),
    chown: follow(0),
    lchown: entry(0),
    utimes: follow(0),
    lutimes: entry(0),
    open: openCheck,
  };

  function wrapSync(obj, name, check, label) {
    const orig = obj[name];
    if (typeof orig !== 'function') return;
    obj[name] = function guarded(...args) {
      check(label, args);
      return orig.apply(this, args);
    };
  }

  function wrapAsync(obj, name, check, label) {
    const orig = obj[name];
    if (typeof orig !== 'function') return;
    obj[name] = async function guarded(...args) {
      check(label, args);
      return orig.apply(this, args);
    };
  }

  for (const [base, check] of Object.entries(FS_CHECKS)) {
    wrapSync(fs, base, check, `fs.${base}`);
    wrapSync(fs, `${base}Sync`, check, `fs.${base}Sync`);
    wrapAsync(fs.promises, base, check, `fs.promises.${base}`);
  }
  wrapSync(fs, 'createWriteStream', follow(0), 'fs.createWriteStream');

  // ---- child_process --------------------------------------------------------------------------------------------

  const GIT_SUBCOMMANDS = new Set(['init', 'commit', 'update-ref', 'tag']);
  const GIT_ENV = new Set(['GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE']);
  const GIT_REPO_FLAG = /^(-C|--git-dir(=.*)?|--work-tree(=.*)?|--separate-git-dir(=.*)?)$/;

  function checkGit(op, args, cwd) {
    let i = 0;
    while (args[i] === '-c') {
      const v = args[i + 1];
      if (typeof v !== 'string' || !/^user\.(name|email)=/.test(v)) throw guardError(op, `git -c ${v}`, 'config');
      i += 2;
    }
    const sub = args[i];
    if (!GIT_SUBCOMMANDS.has(sub)) throw guardError(op, `git ${sub}`, 'subcommand not allowed');
    for (const a of args) {
      if (GIT_REPO_FLAG.test(a)) throw guardError(op, `git ${a}`, 'repository redirect');
    }
    // 'git init <dir>' creates a repository at <dir>: it must land under tmp as well.
    if (sub === 'init') {
      for (const a of args.slice(i + 1)) {
        if (a.startsWith('-')) continue;
        rejectDotDot(a, op);
        assertUnderTmp(path.resolve(cwd, a), op);
      }
    }
  }

  function checkTar(op, args, cwd) {
    const shapeOk = args.length === 4 && args[0] === '-xzf' && args[2] === '-C';
    if (!shapeOk) throw guardError(op, `tar ${args.join(' ')}`, "only ['-xzf', <source>, '-C', <dest>]");
    rejectDotDot(args[3], op);
    assertUnderTmp(path.resolve(cwd, args[3]), op);
  }

  // FR-071: rsign in exactly four shapes; 'P' is a path that must land under tmp, 'T' a comment without CR or LF.
  const RSIGN_SHAPES = [
    ['--version'],
    ['generate', '-W', '-p', 'P', '-s', 'P'],
    ['sign', '-W', '-s', 'P', '-t', 'T', '-x', 'P', 'P'],
    ['verify', '-p', 'P', '-x', 'P', 'P'],
  ];

  function checkRsign(op, args, cwd) {
    const shape = RSIGN_SHAPES.find(
      (sh) => sh.length === args.length && sh.every((x, i) => x === 'P' || x === 'T' || x === args[i]),
    );
    if (!shape) throw guardError(op, `rsign ${args.join(' ')}`, 'rsign shape');
    shape.forEach((x, i) => {
      if (x === 'T' && /[\r\n]/.test(args[i])) throw guardError(op, 'rsign -t <comment>', 'rsign shape');
      if (x === 'P') {
        rejectDotDot(args[i], op);
        assertUnderTmp(path.resolve(cwd, args[i]), op);
      }
    });
  }

  function checkSpawn(op, command, a, b) {
    let args = [];
    let options = b;
    if (Array.isArray(a)) args = a;
    else options = a;
    if (typeof options !== 'object' || options === null) options = {};
    if (options.shell) throw guardError(op, String(command), 'shell: true');
    const cwd = toPathString(options.cwd);
    if (cwd === SKIP) throw guardError(op, String(command), 'no cwd');
    assertUnderTmp(cwd, op);
    if (command !== 'git' && command !== 'tar' && command !== 'rsign') {
      throw guardError(op, String(command), 'command not allowed');
    }
    if (!args.every((x) => typeof x === 'string')) throw guardError(op, String(command), 'non-string argument');
    // The child inherits process.env when options.env is absent, so the effective environment is what counts.
    const env = options.env || process.env;
    for (const k of Object.keys(env)) {
      if (GIT_ENV.has(isWin ? k.toUpperCase() : k)) throw guardError(op, k, 'git repository variable in env');
    }
    if (command === 'git') checkGit(op, args, cwd);
    else if (command === 'tar') checkTar(op, args, cwd);
    else checkRsign(op, args, cwd);
  }

  function refuseAlways(name, why) {
    const label = `child_process.${name}`;
    const refused = function refused(...args) {
      throw guardError(label, String(args[0]), why);
    };
    refused[util.promisify.custom] = refused;
    childProcess[name] = refused;
  }

  refuseAlways('exec', 'shell command string');
  refuseAlways('execSync', 'shell command string');
  refuseAlways('fork', 'fork');

  for (const name of ['spawn', 'spawnSync', 'execFile', 'execFileSync']) {
    const orig = childProcess[name];
    const label = `child_process.${name}`;
    const guarded = function guarded(...args) {
      checkSpawn(label, args[0], args[1], args[2]);
      return orig.apply(this, args);
    };
    const custom = orig[util.promisify.custom];
    if (typeof custom === 'function') {
      guarded[util.promisify.custom] = function guardedPromise(...args) {
        checkSpawn(label, args[0], args[1], args[2]);
        return custom.apply(this, args);
      };
    }
    childProcess[name] = guarded;
  }

  globalThis[GUARD_KEY] = { tmp: TMP };
}
