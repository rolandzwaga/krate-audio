#!/usr/bin/env node
// ==============================================================================
// guard-heredoc.js  —  PreToolUse hook (Bash)
// ==============================================================================
// Blocks every Bash command that contains a heredoc (<<WORD ... WORD). Heredocs
// through the Bash tool on this Windows box fail in ways the agent does not
// notice: on 2026-10-28 two workflow auditors wrote their reports with
// `cat > file <<'EOF'`, bash died with "unexpected EOF while looking for
// matching `''", both reported success, and two audits were lost; the same day a
// `node - <<'EOF'` patch turned an escaped "\n" into a real newline and broke a
// C++ string literal. A memory note did not stop it; this gate does.
//
// Instead: write the file with the Write tool. For a script, Write it to the
// scratchpad (or tools/) and run `node <file>`. For a commit message, Write it to
// a file and use `git commit -F <file>`.
//
// Only a real heredoc is matched: a `<<WORD` / `<<'WORD'` / `<<-WORD` operator
// AND a later line that is exactly WORD. `<<<` here-strings, `$((1<<3))`, and
// `grep 'std::cout << "x"'` pass.
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks. Fail-open on any
// parse problem so the hook can never wedge the session.
//   node tools/hooks/guard-heredoc.js --self-test
// ==============================================================================
'use strict';

function findHeredoc(cmd) {
    if (typeof cmd !== 'string' || !cmd.includes('\n')) return null;
    const lines = cmd.split(/\r?\n/);
    const op = /(^|[^<])<<(-?)\s*(['"]?)([A-Za-z_][A-Za-z0-9_]*)\3/g;
    for (let i = 0; i < lines.length; ++i) {
        op.lastIndex = 0;
        let m;
        while ((m = op.exec(lines[i])) !== null) {
            const dash = m[2] === '-';
            const word = m[4];
            for (let j = i + 1; j < lines.length; ++j) {
                const line = dash ? lines[j].replace(/^\t+/, '') : lines[j];
                if (line === word || line.trim() === word) return word;
            }
        }
    }
    return null;
}

const MESSAGE = (word) => `BLOCKED by tools/hooks/guard-heredoc.js: this command contains a heredoc (<<${word}).
Heredocs through the Bash tool here fail silently (quoting/escaping), and the failure has already lost work.
Do this instead:
- writing a file: use the Write tool with the file's full content;
- running a script: Write it to a file (scratchpad, or tools/ if it is kept), then run \`node <file>\`;
- a commit message: Write it to a file, then \`git commit -F <file>\`.
Then check the result (the file exists, the command's exit code is 0) before reporting success.`;

function selfTest() {
    const block = [
        "mkdir -p x && cat > /f/x/tape.md <<'EOF'\n# Tape audit\nit's long\nEOF",
        "node - <<'EOF'\nconst s = `a\\nb`;\nEOF",
        'git commit -m "$(cat <<\'EOF\'\nfeat: x\n\nbody\nEOF\n)"',
        'python3 <<EOF\nprint(1)\nEOF',
        'cat <<-END > f\n\tindented\n\tEND',
        'tee out.txt << "DONE"\nline\nDONE',
    ];
    const allow = [
        "grep -n 'std::cout << \"x\"' file.cpp",
        'echo $((1<<3))',
        "git log --format='%h %s' -5",
        'cat <<< "here-string"',
        "grep -n '<<EOF' tools/x.js\nls",
        'cd /f/projects/iterum\nnode tools/run-gate.js --filter x plugin_tests',
        'awk \'{ if (a << 2) print }\' f\necho done',
    ];
    let failed = 0;
    for (const c of block) if (!findHeredoc(c)) { failed++; console.log(`FAIL (should block): ${JSON.stringify(c)}`); }
    for (const c of allow) if (findHeredoc(c)) { failed++; console.log(`FAIL (should allow): ${JSON.stringify(c)}`); }
    console.log(failed ? `guard-heredoc self-test: ${failed} FAILED` : `guard-heredoc self-test: ${block.length + allow.length} cases OK`);
    process.exit(failed ? 1 : 0);
}

if (process.argv.includes('--self-test')) selfTest();

let data = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', (c) => (data += c));
process.stdin.on('end', () => {
    try {
        const env = JSON.parse(data || '{}');
        const word = findHeredoc(env.tool_input && env.tool_input.command);
        if (word) {
            process.stderr.write(MESSAGE(word) + '\n');
            process.exit(2);
        }
    } catch {
        // fail open
    }
    process.exit(0);
});
if (process.stdin.isTTY) process.exit(0);
