#!/usr/bin/env node
// ==============================================================================
// guard-ci-gates.js  —  PreToolUse hook (Bash)
// ==============================================================================
// Runs the cheap, deterministic CI gates before `git commit` and blocks when any
// of them fails. Every check here costs milliseconds locally and a full CI
// round-trip (~7-40 min) to discover remotely, and NONE of them is exercised by
// a local build, test run, pluginval pass or clang-tidy sweep. That gap is the
// whole reason this hook exists -- a completely green local workflow has twice
// pushed a branch that CI rejected on the first job:
//
//   1. symbols.json (the ODR index) left stale after adding/renaming types.
//   2. A phaser/flanger test pinning a DSP render with a bit-exact digest over
//      raw float bits, which cannot pass on GCC or Apple Clang no matter what.
//
// Keep GENERATORS and LINTS in step with the "Generated Artifacts In Sync" and
// lint steps in .github/workflows/ci.yml. A gate added to CI but not here is a
// gate that only fires after push.
//
// Only runs --check / read-only forms, never writes. Reads the PreToolUse JSON
// envelope on stdin; exit 2 blocks. Fails open on any parse/IO/spawn problem so
// the hook can never wedge the session.
// ==============================================================================
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// Generators whose output CI compares against the committed copy.
const GENERATORS = [
    { script: 'gen-repo-map.js', artifact: 'specs/_architecture_/repo-map.json' },
    { script: 'gen-symbols.js', artifact: 'specs/_architecture_/symbols.json' },
    { script: 'gen-specs-index.js', artifact: 'specs/INDEX.md' },
];

// Deterministic architecture-invariant lints (prose rules turned into checks).
const LINTS = [
    { script: 'lint-layers.js', what: 'DSP layer-dependency violation' },
    { script: 'lint-odr.js', what: 'duplicate symbol (ODR hazard)' },
    { script: 'lint-arch-guarded-includes.js', what: 'arch-guarded krate include' },
    { script: 'lint-float-bit-goldens.js', what: 'bit-exact float golden digest' },
    { script: 'lint-midi-timing-goldens.js', what: 'byte-exact MIDI timing golden' },
    { script: 'lint-platform-type-literals.js', what: 'hardcoded platform-type constant in a test' },
    { script: 'lint-allocation-operator-overrides.js', what: 'hand-rolled global operator new/delete' },
    { script: 'lint-simd-aligned-loadstore.js', what: 'aligned hn::Load/Store (faults on AVX-512 runners)' },
    { script: 'lint-plugin-roster.js', what: 'plugin missing from a CI/tooling roster' },
    { script: 'lint-serial-test-runs.js', what: 'serial test run in agent instructions (use tools/run-gate.js)' },
    { script: 'lint-wallclock-tags.js', what: 'wall-clock-asserting test case without [wallclock]' },
    { script: 'lint-serial-test-runs.js', args: ['--self-test'], what: 'serial-test-run lint self-test' },
    { script: 'lint-plugin-editor-baseline.js', what: 'plugin editor missing a shared view (update banner, check button, version label)' },
    { script: 'lint-nonfinite-symbols.js', what: 'std::isnan/isinf/isfinite or a non-finite constant in fast-math DSP code' },
    { script: 'lint-finite-bitmask.js', what: 'hand-written finiteness bitmask (Apple clang folds it)' },
    { script: 'lint-ieee-flag-sets.js', what: '-fno-fast-math without -ffp-contract=off on the same CMake line' },
    { script: 'lint-apple-globals.js', what: 'identifier that compiles everywhere except macOS (MAC macro, MacTypes globals)' },
    { script: 'lint-musical-time-truncation.js', args: ['--self-test'], what: 'musical-time truncation lint self-test' },
    { script: 'lint-musical-time-truncation.js', what: 'truncating cast of ppq / projectTimeMusic / stepFraction (use std::llround)' },
    { script: 'lint-unordered-serialization.js', args: ['--self-test'], what: 'unordered-serialization lint self-test' },
    { script: 'lint-unordered-serialization.js', what: 'serializer iterating a std::unordered_map/set (platform-dependent bytes)' },
    { script: 'lint-allocation-probes.js', args: ['--self-test'], what: 'allocation-probe lint self-test' },
    { script: 'lint-allocation-probes.js', what: 'allocation probe the optimiser may elide (use TestHelpers::allocateObservably)' },
    { script: 'lint-ci-runner-pins.js', args: ['--self-test'], what: 'CI runner pin lint self-test' },
    { script: 'lint-ci-runner-pins.js', what: 'CI job building/testing on a floating ubuntu-latest / windows-latest image' },
    { script: 'lint-manager-version.js', what: 'Krate Manager version drift (manager-ci.yml)' },
    { script: 'lint-released-ports.js', args: ['--self-test'], what: 'released-port lint self-test' },
    { script: 'lint-released-ports.js', what: 'socket released after its URL/port was taken (port reuse race)' },
    { script: 'lint-gate-mirrors-ci.js', args: ['--self-test'], what: 'gate-mirrors-CI lint self-test' },
    { script: 'lint-gate-mirrors-ci.js', what: 'a workflow check this commit gate does not run' },
];

// Node test suites of the other workflows (2026-11-13: an action-pin commit edited .github/workflows/*.yml and the PR
// failed instantly in feed-tools.yml's node --test, which no gate above runs). A suite runs only when a staged path
// matches the workflow's own on.push.paths filter, read from the workflow file, so the hook triggers exactly when CI
// does (the feed-contract suite costs ~25 s).
// `deps`: directories whose untracked node_modules the suite requires (ajv etc.); an exported staged tree gets a link to
// the checkout's copy.
const NODE_SUITES = [
    { workflow: 'feed-tools.yml', cwd: '.', deps: ['tools/feed-contract'],
      args: ['--test', '--test-reporter=dot', 'tools/feed-contract/tests/*.test.js'] },
    { workflow: 'manager-ci.yml', cwd: 'apps/krate-manager', deps: ['apps/krate-manager'],
      args: ['--test', '--test-reporter=dot', 'scripts/*.test.js'] },
];

/// Links <root>/<dep>/node_modules into an exported tree; returns the links made (removed before the tree is).
function linkNodeModules(root, dir, suites) {
    const links = [];
    if (path.resolve(dir) === path.resolve(root)) return links;
    for (const dep of new Set(suites.flatMap((s) => s.deps))) {
        const target = path.join(root, dep, 'node_modules');
        const link = path.join(dir, dep, 'node_modules');
        if (!fs.existsSync(target) || fs.existsSync(link)) continue;
        try {
            fs.symlinkSync(target, link, 'junction');
            links.push(link);
        } catch { /* the suite then reports the missing module itself */ }
    }
    return links;
}

/// The `- '<glob>'` entries of on.push.paths in a workflow, as regexes (empty when there are none).
function pushPathFilters(root, workflow) {
    let lines;
    try {
        lines = fs.readFileSync(path.join(root, '.github', 'workflows', workflow), 'utf8').split(/\r?\n/);
    } catch {
        return [];
    }
    const out = [];
    let inOn = false, inPush = false, inPaths = false;
    for (const line of lines) {
        if (/^\S/.test(line)) { inOn = /^on:/.test(line); inPush = inPaths = false; continue; }
        if (!inOn) continue;
        if (/^ {2}\S/.test(line)) { inPush = /^ {2}push:/.test(line); inPaths = false; continue; }
        if (!inPush) continue;
        if (/^ {4}\S/.test(line)) { inPaths = /^ {4}paths:/.test(line); continue; }
        const m = inPaths && /^ {6}- '([^']+)'/.exec(line);
        if (!m) continue;
        const re = m[1].replace(/[.+?^${}()|[\]\\]/g, '\\$&').replace(/\*\*/g, '\0').replace(/\*/g, '[^/]*').replace(/\0/g, '.*');
        out.push(new RegExp(`^${re}$`));
    }
    return out;
}

function stagedPaths(root) {
    try {
        return execFileSync('git', ['diff', '--cached', '--name-only'], { cwd: root, maxBuffer: 64 << 20 })
            .toString().split(/\r?\n/).filter(Boolean);
    } catch {
        return [];
    }
}

/// Run a node --test suite; return its failure tail, or null when it passed or could not be run.
function runNodeSuite(dir, suite) {
    try {
        execFileSync(process.execPath, suite.args, { cwd: path.join(dir, suite.cwd), stdio: 'pipe', timeout: 240000 });
        return null;
    } catch (err) {
        if (typeof err.status !== 'number' || err.status === 0) return null;
        const out = `${err.stdout || ''}${err.stderr || ''}`.toString().trim().split(/\r?\n/);
        return out.slice(-40).join('\n') || `node ${suite.args.join(' ')} exited ${err.status}`;
    }
}

function readStdin() {
    return new Promise((resolve) => {
        let data = '';
        process.stdin.setEncoding('utf8');
        process.stdin.on('data', (c) => (data += c));
        process.stdin.on('end', () => resolve(data));
        if (process.stdin.isTTY) resolve('');
    });
}

function repoRoot() {
    // hook lives at <root>/tools/hooks/guard-ci-gates.js
    return path.resolve(__dirname, '..', '..');
}

/// The tree the gates judge: what is being COMMITTED. With unstaged edits or untracked files in the working tree
/// (an active phase's work in progress), the generators would index files that are not in the commit and block it
/// for them (2026-10-26: Profundum Phase 14's untracked header made symbols.json "stale" for an unrelated tooling
/// commit). Then the gates run on an export of the index instead; specs/**/evidence and artifacts are left out
/// (228 MB of 295, and no generator or lint reads them). A clean tree runs in place.
function stagedTree(root) {
    const inPlace = { dir: root, cleanup: () => {} };
    let status;
    try {
        status = execFileSync('git', ['status', '--porcelain', '--untracked-files=all'], { cwd: root, maxBuffer: 64 << 20 }).toString();
    } catch {
        return inPlace;
    }
    const dirty = status.split(/\r?\n/).some((l) => l.length > 3 && (l.startsWith('??') || l[1] !== ' '));
    if (!dirty) return inPlace;
    try {
        const base = fs.existsSync('F:/tmp') ? 'F:/tmp' : require('os').tmpdir();
        const dir = fs.mkdtempSync(path.join(base, 'ci-gate-index-'));
        const entries = execFileSync('git', ['ls-files', '-s', '-z'], { cwd: root, maxBuffer: 64 << 20 }).toString().split('\0');
        const files = entries
            .filter((e) => e && !e.startsWith('160000 '))               // submodules (extern/vst3sdk) have no blob
            .map((e) => e.slice(e.indexOf('\t') + 1))
            .filter((f) => !/^specs\/.*\/(evidence|artifacts)\//.test(f));
        execFileSync('git', ['checkout-index', '-z', '--stdin', `--prefix=${dir.replace(/\\/g, '/')}/`],
            { cwd: root, input: files.join('\0'), maxBuffer: 64 << 20, timeout: 120000 });
        return { dir, cleanup: () => fs.rmSync(dir, { recursive: true, force: true }) };
    } catch {
        return inPlace;
    }
}

/// True when the command is a real `git commit`. Excludes the read-only
/// inspection forms, which have no reason to be gated.
function isGitCommit(cmd) {
    // `commit` must be the git subcommand (after optional global options), not any word on the line:
    // `git add .claude/workflows/pre-commit-audit.js` was blocked as a commit (2026-10-26).
    if (!/\bgit(?:\s+(?:-[Cc]\s+\S+|--?[\w-]+(?:=\S+)?))*\s+commit\b/.test(cmd)) return false;
    // `git log`, `git show <commit>`, `git rev-parse` etc. can mention "commit"
    // in a path or message without being a commit themselves.
    if (/\bgit\s+(log|show|diff|rev-parse|rev-list|status|cherry)\b/.test(cmd)) return false;
    return true;
}

/// Run a tools/ script and return its failure output, or null when it passed or
/// could not be run at all (which must never block).
function runGate(root, script, args, env) {
    const scriptPath = path.join(root, 'tools', script);
    if (!fs.existsSync(scriptPath)) return null; // script removed -> nothing to check

    try {
        execFileSync(process.execPath, [scriptPath, ...args], {
            cwd: root,
            stdio: 'pipe',
            timeout: 120000,
            env: env || process.env,
        });
        return null;
    } catch (err) {
        // A non-zero exit means the gate failed. Anything else (spawn failure,
        // timeout) is a hook problem, not a repo problem -- do not block on it.
        if (typeof err.status !== 'number' || err.status === 0) return null;
        const out = `${err.stdout || ''}${err.stderr || ''}`.toString().trim();
        return out || `${script} exited ${err.status}`;
    }
}

(async () => {
    let payload;
    try {
        payload = JSON.parse(await readStdin());
    } catch {
        process.exit(0);
    }

    const cmd = (payload && payload.tool_input && payload.tool_input.command) || '';
    if (!cmd || !isGitCommit(cmd)) process.exit(0);

    const root = repoRoot();
    const staged = stagedPaths(root);
    const suites = NODE_SUITES.filter((s) => {
        const filters = pushPathFilters(root, s.workflow);
        return staged.some((p) => filters.some((re) => re.test(p)));
    });
    const tree = stagedTree(root);
    const stale = [];
    const lintFailures = [];
    const suiteFailures = [];
    const links = linkNodeModules(root, tree.dir, suites);

    try {
        for (const { script, artifact } of GENERATORS) {
            if (runGate(tree.dir, script, ['--check']) !== null) stale.push({ script, artifact });
        }
        // An exported tree has no .git; lints that list files with `git ls-files` (lint-ieee-flag-sets) read the
        // checkout's index through GIT_DIR, which names exactly the exported (staged) files. Lints only.
        let lintEnv;
        if (path.resolve(tree.dir) !== path.resolve(root)) {
            try {
                const gitDir = execFileSync('git', ['rev-parse', '--absolute-git-dir'], { cwd: root }).toString().trim();
                lintEnv = { ...process.env, GIT_DIR: gitDir, GIT_WORK_TREE: tree.dir };
            } catch { /* no git: the lint reports it */ }
        }
        for (const { script, args = [], what } of LINTS) {
            const out = runGate(tree.dir, script, args, lintEnv);
            if (out !== null) lintFailures.push({ script: [script, ...args].join(' '), what, out });
        }
        for (const suite of suites) {
            const out = runNodeSuite(tree.dir, suite);
            if (out !== null) suiteFailures.push({ suite, out });
        }
    } finally {
        // Unlink first: the tree's recursive delete must never reach the checkout's node_modules through a link.
        for (const link of links) {
            try { fs.unlinkSync(link); } catch { try { fs.rmdirSync(link); } catch { /* left for the tree cleanup */ } }
        }
        if (links.every((l) => !fs.existsSync(l))) tree.cleanup();
    }

    if (stale.length === 0 && lintFailures.length === 0 && suiteFailures.length === 0) process.exit(0);

    let msg = 'BLOCKED: cheap CI gates fail on this tree, so the push will go red.\n\n';

    if (stale.length > 0) {
        msg +=
            'Generated artifacts are stale ("Generated Artifacts In Sync" job).\n' +
            'Regenerate and stage them with this commit:\n\n' +
            stale.map((s) => `  ${s.artifact}  ->  node tools/${s.script}`).join('\n') +
            '\n\nsymbols.json goes stale whenever a class or struct is added, renamed\n' +
            'or removed under dsp/ or plugins/.\n\n';
    }

    for (const f of lintFailures) {
        msg += `Lint failed: ${f.what}  (node tools/${f.script})\n\n${f.out}\n\n`;
    }

    for (const f of suiteFailures) {
        msg += `${f.suite.workflow} node tests failed  (cd ${f.suite.cwd} && node ${f.suite.args.join(' ')})\n\n${f.out}\n\n`;
    }

    process.stderr.write(msg);
    process.exit(2);
})();
