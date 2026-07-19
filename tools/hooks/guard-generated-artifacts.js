#!/usr/bin/env node
// ==============================================================================
// guard-generated-artifacts.js  —  PreToolUse hook (Bash)
// ==============================================================================
// The "Generated Artifacts In Sync" CI job regenerates repo-map.json,
// symbols.json and specs/INDEX.md and fails if the committed copies differ. That
// check is instant locally and takes a full CI round-trip to discover remotely,
// so this hook runs it before `git commit` and blocks when anything is stale.
//
// The failure it exists to prevent: adding or renaming a class anywhere under
// dsp/ or plugins/ leaves symbols.json (the ODR index) out of date. Nothing in
// the local build or test run notices; the first sign is a red CI job on push.
//
// Only runs the --check generators, never writes. The generator to run is named
// in the block message so the fix is one copy-paste.
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks. Fails open on any
// parse/IO/spawn problem so the hook can never wedge the session.
// ==============================================================================
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// Generators whose output CI compares against the committed copy. Keep in step
// with the "Generated Artifacts In Sync" job in .github/workflows/ci.yml.
const GENERATORS = [
    { script: 'gen-repo-map.js', artifact: 'specs/_architecture_/repo-map.json' },
    { script: 'gen-symbols.js', artifact: 'specs/_architecture_/symbols.json' },
    { script: 'gen-specs-index.js', artifact: 'specs/INDEX.md' },
];

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
    // hook lives at <root>/tools/hooks/guard-generated-artifacts.js
    return path.resolve(__dirname, '..', '..');
}

/// True when the command is a real `git commit`. Excludes the read-only
/// inspection forms, which have no reason to be gated.
function isGitCommit(cmd) {
    if (!/\bgit\b[^\n]*\bcommit\b/.test(cmd)) return false;
    // `git log`, `git show <commit>`, `git rev-parse` etc. can mention "commit"
    // in a path or message without being a commit themselves.
    if (/\bgit\s+(log|show|diff|rev-parse|rev-list|status|cherry)\b/.test(cmd)) return false;
    return true;
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
    const stale = [];

    for (const { script, artifact } of GENERATORS) {
        const scriptPath = path.join(root, 'tools', script);
        if (!fs.existsSync(scriptPath)) continue; // generator removed -> nothing to check

        try {
            execFileSync(process.execPath, [scriptPath, '--check'], {
                cwd: root,
                stdio: 'pipe',
                timeout: 120000,
            });
        } catch (err) {
            // A non-zero exit means stale. Anything else (spawn failure, timeout)
            // is a hook problem, not a repo problem -- do not block on it.
            if (typeof err.status === 'number' && err.status !== 0) {
                stale.push({ script, artifact });
            }
        }
    }

    if (stale.length === 0) process.exit(0);

    const lines = stale.map((s) => `  ${s.artifact}  ->  node tools/${s.script}`);
    process.stderr.write(
        'BLOCKED: generated artifacts are stale, and CI\'s "Generated Artifacts In\n' +
        'Sync" job will fail on push. Regenerate and stage them with this commit:\n\n' +
        lines.join('\n') + '\n\n' +
        'symbols.json goes stale whenever a class or struct is added, renamed or\n' +
        'removed under dsp/ or plugins/.\n'
    );
    process.exit(2);
})();
