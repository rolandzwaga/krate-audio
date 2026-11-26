// ==============================================================================
// commit-root.js  —  which checkout does this `git commit` commit?
// ==============================================================================
// The commit hooks (guard-ci-gates, guard-portability, guard-changelog) judge a
// tree before it is committed. They used to take the repo root from their own
// file location, which is the checkout the SESSION runs in. A commit made in a
// git worktree (a parallel fix lane) was then judged against the wrong tree and
// the wrong index.
//
// The root is the top level of the directory the commit runs in. Starting from
// the tool call's cwd (the hook envelope's `cwd`), in the order the shell and
// git apply them:
//   1. every `cd <dir>` / `pushd <dir>` before the commit, also inside ( ) or { }
//   2. `git -C <dir> ... commit`      (an agent committing in a lane)
//   3. `--work-tree <dir>`, or `GIT_WORK_TREE=<dir>` in front of the command
// and the checkout this file lives in when that is not a git checkout.
// Not followed: a cd in an EARLIER tool call (the envelope's cwd covers it when
// the shell kept it), `export GIT_WORK_TREE=...`, a bare `--git-dir`.
// ==============================================================================
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ARG = String.raw`(?:"[^"]*"|'[^']*'|[^\s;&|]+)`;
// `git <global options> commit`. Options that take a separate value are named, so the value is not read as the subcommand.
const GIT_COMMIT = new RegExp(String.raw`\bgit((?:\s+(?:(?:-[Cc]|--work-tree|--git-dir|--namespace)\s+${ARG}|--?[\w-]+(?:=${ARG})?))*)\s+commit\b`);
const DASH_C = new RegExp(String.raw`(?:^|\s)-C\s+(${ARG})`, 'g');
const WORK_TREE = new RegExp(String.raw`(?:^|\s)--work-tree(?:=|\s+)(${ARG})`);
// Where a command starts: the line, after && || ; or inside ( ... ) and { ... }. `$(` is a command substitution: its cd
// does not move the commit.
const START = String.raw`(?:^|&&|\|\||;|\n|(?<!\$)\(|\{)\s*`;
// bash cd / pushd and their PowerShell forms (a commit can come through either shell tool).
const CD = new RegExp(String.raw`${START}(?:cd|pushd|chdir|sl|Set-Location|Push-Location)\s+(?:-(?:Literal)?Path\s+)?(${ARG})\s*(?=&&|;|\n)`, 'g');
const ENV_WORK_TREE = new RegExp(String.raw`${START}(?:env\s+)?(?:[A-Za-z_]\w*=${ARG}?\s+)*?GIT_WORK_TREE=(${ARG})\s+(?:[A-Za-z_]\w*=${ARG}?\s+)*$`);

/// The checkout this hook file belongs to.
function hookRoot() {
    return path.resolve(__dirname, '..', '..');
}

function unquote(s) {
    return /^(".*"|'.*')$/.test(s) ? s.slice(1, -1) : s;
}

/// Git Bash writes drive paths as /f/dir; node on Windows would read that as <current drive>:\f\dir.
function fromShellPath(p) {
    if (process.platform === 'win32') {
        const m = /^\/([a-zA-Z])(\/.*)?$/.exec(p);
        if (m) return `${m[1]}:${m[2] || '/'}`;
    }
    return p;
}

/// The directory the `git commit` in `cmd` runs in, or null when the command is not a commit.
function commitDir(cmd, cwd) {
    const m = GIT_COMMIT.exec(cmd || '');
    if (!m) return null;
    let dir = cwd || process.cwd();
    const step = (arg) => { dir = path.resolve(dir, fromShellPath(unquote(arg))); };
    const before = cmd.slice(0, m.index);
    for (const cd of before.matchAll(CD)) step(cd[1]);
    for (const c of m[1].matchAll(DASH_C)) step(c[1]);
    const workTree = WORK_TREE.exec(m[1]) || ENV_WORK_TREE.exec(before);
    if (workTree) step(workTree[1]);
    return dir;
}

function topLevel(dir) {
    if (!dir || !fs.existsSync(dir)) return null;
    try {
        const out = execFileSync('git', ['rev-parse', '--show-toplevel'], { cwd: dir, stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim();
        return out ? path.resolve(out) : null;
    } catch {
        return null;
    }
}

/// The root of the checkout a hook must judge for this tool call.
function commitRoot(payload, cmd) {
    const cwd = payload && typeof payload.cwd === 'string' && payload.cwd ? payload.cwd : null;
    return topLevel(commitDir(cmd, cwd) || cwd) || hookRoot();
}

/// A path argument of the commit command (a -F message file), resolved where the commit runs.
function resolveArg(cmd, cwd, arg) {
    return path.resolve(commitDir(cmd, cwd) || cwd || process.cwd(), fromShellPath(unquote(arg)));
}

module.exports = { commitRoot, commitDir, hookRoot, resolveArg, ARG };
