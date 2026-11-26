// Run: node --test tools/hooks/tests/hooks.test.js
//
// The hooks with several agents at work: every hook script is wired, the commit hooks also see commits made
// through the PowerShell tool, the changelog gate reads a message file, the RT-safety reminder reaches each agent.
'use strict';

const { test, before, after } = require('node:test');
const assert = require('node:assert');
const { execFileSync, spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const HOOKS = path.resolve(__dirname, '..');
const REPO = path.resolve(HOOKS, '..', '..');
const settings = JSON.parse(fs.readFileSync(path.join(REPO, '.claude', 'settings.json'), 'utf8'));
const wired = Object.values(settings.hooks).flat().flatMap((entry) =>
    entry.hooks.map((h) => ({ matcher: entry.matcher, script: (/tools\/hooks\/([\w-]+\.js)/.exec(h.command) || [])[1] })));
const matchersOf = (script) => wired.filter((w) => w.script === script).map((w) => w.matcher);

const runHook = (script, payload) =>
    spawnSync(process.execPath, [path.join(HOOKS, script)], { input: JSON.stringify(payload), encoding: 'utf8' });

test('every hook script in tools/hooks is wired in .claude/settings.json', () => {
    const scripts = fs.readdirSync(HOOKS).filter((n) => /^(guard|remind)-[\w-]+\.js$/.test(n));
    assert.ok(scripts.length >= 9, `found only ${scripts.length} hook scripts`);
    for (const s of scripts) assert.notDeepStrictEqual(matchersOf(s), [], `${s} is not wired: it never runs`);
});

test('a hook that guards a shell command also sees the PowerShell tool', () => {
    // A commit, a WSL shutdown or a whole-suite run can be issued through either shell tool.
    for (const s of ['guard-changelog.js', 'guard-ci-gates.js', 'guard-portability.js', 'guard-wsl-shutdown.js', 'guard-tidy-scope.js', 'guard-test-scope.js']) {
        const tools = matchersOf(s).join('|').split('|');
        assert.ok(tools.includes('Bash'), `${s} does not match Bash`);
        assert.ok(tools.includes('PowerShell'), `${s} does not match PowerShell: that tool bypasses it`);
    }
});

let tmp, main, lane;
const git = (cwd, ...args) => execFileSync('git', args, { cwd, stdio: 'pipe' }).toString().trim();

before(() => {
    tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hooks-test-'));
    main = path.join(tmp, 'main');
    lane = path.join(tmp, 'lane1');
    fs.mkdirSync(path.join(main, 'plugins', 'foo'), { recursive: true });
    fs.writeFileSync(path.join(main, 'plugins', 'foo', 'CHANGELOG.md'), '# Changelog\n\n## [1.0.0]\n');
    git(main, 'init', '-q');
    git(main, 'config', 'user.email', 'test@example.com');
    git(main, 'config', 'user.name', 'test');
    git(main, 'add', '-A');
    git(main, '-c', 'commit.gpgsign=false', 'commit', '-q', '-m', 'init');
    git(main, 'worktree', 'add', '-q', '--detach', lane);
    fs.writeFileSync(path.join(lane, 'plugins', 'foo', 'CHANGELOG.md'), '# Changelog\n\n## [9.9.9]\n\n## [1.0.0]\n');
    // The same file name in both checkouts, with different content: a relative -F must be read where the commit runs.
    fs.writeFileSync(path.join(main, 'msg.txt'), 'chore(foo): release 9.9.9\n\nbody\n');
    fs.writeFileSync(path.join(lane, 'msg.txt'), 'chore(foo): release 9.9.9\n\nbody\n');
    fs.writeFileSync(path.join(tmp, 'plain.txt'), 'fix(foo): something\n');
});

after(() => {
    fs.rmSync(tmp, { recursive: true, force: true });
});

test('guard-changelog reads the message of git commit -F <file>', () => {
    const run = (cwd, command) => runHook('guard-changelog.js', { cwd, tool_input: { command } }).status;
    // Agents commit with -F (heredocs are blocked), so a gate that reads only -m never fires for them.
    assert.strictEqual(run(main, `git commit -F "${path.join(main, 'msg.txt')}"`), 2, 'release without a CHANGELOG entry');
    assert.strictEqual(run(main, 'git commit -F msg.txt'), 2, 'relative path, read in the cwd');
    assert.strictEqual(run(main, `git commit --file="${path.join(main, 'msg.txt')}"`), 2);
    assert.strictEqual(run(main, `git commit --file msg.txt`), 2);
    assert.strictEqual(run(main, `git -C "${lane}" commit -F msg.txt`), 0, 'the lane has the entry; msg.txt is read in the lane');
    assert.strictEqual(run(main, `cd "${lane}" && git commit -F msg.txt`), 0);
    assert.strictEqual(run(main, `git commit -F "${path.join(tmp, 'plain.txt')}"`), 0, 'not a release');
    assert.strictEqual(run(main, 'git commit -F missing.txt'), 0, 'an unreadable file never blocks');
    assert.strictEqual(run(main, 'git commit -F -'), 0, 'stdin cannot be read');
});

test('remind-rt-safety reminds each agent once', () => {
    const session = `hooks-test-${process.pid}-${Date.now()}`;
    const edit = (agent) => {
        const payload = { session_id: session, tool_input: { file_path: 'plugins/innexus/src/processor/processor.cpp' } };
        if (agent) payload.agent_id = agent;
        return runHook('remind-rt-safety.js', payload).stdout.includes('Real-time-safety reminder');
    };
    try {
        assert.strictEqual(edit('lane1-impl'), true, 'first edit by agent 1');
        assert.strictEqual(edit('lane2-impl'), true, 'first edit by agent 2, same session');
        assert.strictEqual(edit('lane1-impl'), false, 'second edit by agent 1');
        assert.strictEqual(edit(undefined), true, 'the main conversation is its own reader');
        assert.strictEqual(edit(undefined), false);
    } finally {
        for (const n of fs.readdirSync(os.tmpdir())) if (n.startsWith(`krate-rtsafety-${session}`)) fs.rmSync(path.join(os.tmpdir(), n), { force: true });
    }
});
