// ==============================================================================
// commit-root.js  —  which checkout does this `git commit` commit?
// ==============================================================================
// The commit hooks (guard-ci-gates, guard-portability, guard-changelog) judge a
// tree before it is committed. They used to take the repo root from their own
// file location, which is the checkout the SESSION runs in. A commit made in a
// git worktree (a parallel fix lane) was then judged against the wrong tree and
// the wrong index.
//
// The root is the top level of the directory the commit runs in:
//   1. `git -C <dir> ... commit`      (an agent committing in a lane)
//   2. the last `cd <dir>` before it  (`cd <lane> && git commit ...`)
//   3. the tool call's cwd            (the hook envelope's `cwd`)
//   4. the checkout this file lives in, when none of those is a git checkout.
// ==============================================================================
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ARG = String.raw`(?:"[^"]*"|'[^']*'|[^\s;&|]+)`;
const GIT_COMMIT = new RegExp(String.raw`\bgit((?:\s+(?:-[Cc]\s+${ARG}|--?[\w-]+(?:=\S+)?))*)\s+commit\b`);
const DASH_C = new RegExp(String.raw`(?:^|\s)-C\s+(${ARG})`, 'g');
const CD = new RegExp(String.raw`(?:^|&&|\|\||;|\n)\s*cd\s+(${ARG})\s*(?=&&|;|\n)`, 'g');

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
    for (const cd of cmd.slice(0, m.index).matchAll(CD)) step(cd[1]);
    for (const c of m[1].matchAll(DASH_C)) step(c[1]);
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

module.exports = { commitRoot, commitDir, hookRoot };
