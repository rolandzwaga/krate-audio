#!/usr/bin/env node
// ==============================================================================
// guard-changelog.js  —  PreToolUse hook (Bash)
// ==============================================================================
// Release discipline: a commit whose message says "release X.Y.Z" for a plugin
// MUST have the matching "## [X.Y.Z]" section already present in that plugin's
// CHANGELOG.md. This hook inspects `git commit` commands and blocks the commit
// when the CHANGELOG entry is missing, converting a "remember to update the
// changelog" rule into a mechanical gate.
//
// Recognized commit-message shapes (case-insensitive on "release"):
//   <type>(<plugin>): release X.Y.Z
//   <plugin>: release X.Y.Z
//   release X.Y.Z            (scope-less -> check every plugin CHANGELOG; block if
//                             none contains the version)
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks. Fail-open on any
// parse/IO problem so the hook can never wedge the session.
// ==============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

function readStdin() {
    return new Promise((resolve) => {
        let data = '';
        process.stdin.setEncoding('utf8');
        process.stdin.on('data', (c) => (data += c));
        process.stdin.on('end', () => resolve(data));
        if (process.stdin.isTTY) resolve('');
    });
}

// Extract the commit message text from a `git commit -m "..."` (or -m '...', or a
// heredoc/@'...'@). Returns '' if this isn't a git commit or no message found.
function extractCommitMessage(cmd) {
    if (!/\bgit\b[^\n]*\bcommit\b/.test(cmd)) return '';
    // -m "..." or -m '...'
    const dq = cmd.match(/-m\s+"([\s\S]*?)"/);
    if (dq) return dq[1];
    const sq = cmd.match(/-m\s+'([\s\S]*?)'/);
    if (sq) return sq[1];
    // heredoc / here-string bodies (best effort)
    const heredoc = cmd.match(/<<-?'?\w+'?\n([\s\S]*?)\n\w+/);
    if (heredoc) return heredoc[1];
    const psHereString = cmd.match(/@'\r?\n([\s\S]*?)\r?\n'@/);
    if (psHereString) return psHereString[1];
    return '';
}

function repoRoot() {
    // hook lives at <root>/tools/hooks/guard-changelog.js
    return path.resolve(__dirname, '..', '..');
}

function changelogHas(pluginDir, version) {
    const clPath = path.join(repoRoot(), 'plugins', pluginDir, 'CHANGELOG.md');
    let text;
    try {
        text = fs.readFileSync(clPath, 'utf8');
    } catch {
        return null; // no changelog -> can't verify (treated as "unknown")
    }
    // Accept "## [X.Y.Z]" or "## X.Y.Z"
    const re = new RegExp('^##\\s*\\[?' + version.replace(/\./g, '\\.') + '\\]?', 'm');
    return re.test(text);
}

function listPluginDirs() {
    try {
        return fs
            .readdirSync(path.join(repoRoot(), 'plugins'), { withFileTypes: true })
            .filter((d) => d.isDirectory())
            .map((d) => d.name);
    } catch {
        return [];
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
    if (!cmd) process.exit(0);

    const msg = extractCommitMessage(cmd);
    if (!msg) process.exit(0);

    // Find "release X.Y.Z" (X.Y.Z, optionally with a -suffix)
    const rel = msg.match(/release\s+v?(\d+\.\d+\.\d+(?:-[\w.]+)?)/i);
    if (!rel) process.exit(0);
    const version = rel[1];

    // Determine plugin scope from "type(scope):" or "scope:" prefix.
    let scope = '';
    const scoped = msg.match(/^\w+\(([a-z0-9-]+)\)\s*:/i) || msg.match(/^([a-z0-9-]+)\s*:/i);
    if (scoped) scope = scoped[1].toLowerCase();

    const plugins = listPluginDirs();

    if (scope && plugins.includes(scope)) {
        const has = changelogHas(scope, version);
        if (has === false) {
            process.stderr.write(
                `BLOCKED: commit says "release ${version}" for ${scope}, but ` +
                `plugins/${scope}/CHANGELOG.md has no "## [${version}]" section.\n` +
                `Add the CHANGELOG entry (same commit) before releasing.\n`
            );
            process.exit(2);
        }
        process.exit(0); // present, or changelog missing (unknown) -> don't block
    }

    // Scope-less "release X.Y.Z": block only if NO plugin changelog has the version
    // (avoids false positives while still catching a forgotten entry).
    const anyHas = plugins.some((p) => changelogHas(p, version) === true);
    if (!anyHas) {
        process.stderr.write(
            `BLOCKED: commit says "release ${version}" but no plugin CHANGELOG.md ` +
            `contains a "## [${version}]" section.\n` +
            `Add the entry to the releasing plugin's CHANGELOG.md before committing.\n`
        );
        process.exit(2);
    }
    process.exit(0);
})();
