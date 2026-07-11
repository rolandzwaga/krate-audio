#!/usr/bin/env node
// ==============================================================================
// guard-version-h.js  —  PreToolUse hook (Edit / Write / MultiEdit)
// ==============================================================================
// plugins/<plugin>/src/version.h is GENERATED from plugins/<plugin>/version.json.
// Hand-editing version.h is silently overwritten on the next configure and drifts
// the two out of sync. This hook blocks any Edit/Write targeting a version.h under
// a plugin's src/ and redirects the edit to version.json.
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks the tool call and
// surfaces the stderr message to the agent. Any parse failure exits 0 (fail-open —
// a hook must never wedge the session).
// ==============================================================================
'use strict';

function readStdin() {
    return new Promise((resolve) => {
        let data = '';
        process.stdin.setEncoding('utf8');
        process.stdin.on('data', (c) => (data += c));
        process.stdin.on('end', () => resolve(data));
        // If nothing is piped, don't hang.
        if (process.stdin.isTTY) resolve('');
    });
}

(async () => {
    let payload;
    try {
        payload = JSON.parse(await readStdin());
    } catch {
        process.exit(0); // fail-open
    }

    const input = payload && payload.tool_input ? payload.tool_input : {};
    const filePath = input.file_path || input.path || '';
    const normalized = String(filePath).replace(/\\/g, '/');

    // plugins/<anything>/src/version.h
    if (/\/plugins\/[^/]+\/src\/version\.h$/.test(normalized) ||
        /^plugins\/[^/]+\/src\/version\.h$/.test(normalized)) {
        const jsonPath = normalized.replace(/\/src\/version\.h$/, '/version.json');
        process.stderr.write(
            `BLOCKED: ${normalized} is generated from version.json — edits here are ` +
            `overwritten on the next CMake configure.\n` +
            `Edit ${jsonPath} instead (bump the version there), then rebuild to regenerate ` +
            `version.h. For a release, also add the matching "## [X.Y.Z]" section to the ` +
            `plugin's CHANGELOG.md.\n`
        );
        process.exit(2); // block
    }

    process.exit(0);
})();
