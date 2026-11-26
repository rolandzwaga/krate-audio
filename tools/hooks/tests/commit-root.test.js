// Run: node --test tools/hooks/tests/commit-root.test.js
//
// The commit hooks must judge the tree that is COMMITTING. A commit made in a git worktree
// (a fix lane) used to be judged against the checkout the hook file lives in.
'use strict';

const { test, before, after } = require('node:test');
const assert = require('node:assert');
const { execFileSync, spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const HOOKS = path.resolve(__dirname, '..');
const { commitRoot, hookRoot } = require(path.join(HOOKS, 'commit-root.js'));

let tmp, main, lane, laneSub;
const git = (cwd, ...args) => execFileSync('git', args, { cwd, stdio: 'pipe' }).toString().trim();
const real = (p) => fs.realpathSync(p);
const same = (a, b) => assert.strictEqual(real(a), real(b));

before(() => {
    tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'commit-root-'));
    main = path.join(tmp, 'main');
    lane = path.join(tmp, 'lane 1'); // a space: the path must survive quoting
    fs.mkdirSync(path.join(main, 'plugins', 'foo'), { recursive: true });
    fs.mkdirSync(path.join(main, 'tools'), { recursive: true });
    fs.writeFileSync(path.join(main, 'plugins', 'foo', 'CHANGELOG.md'), '# Changelog\n\n## [1.0.0]\n');
    fs.writeFileSync(path.join(main, 'tools', 'lint-odr.js'), 'process.exit(0)\n');
    git(main, 'init', '-q');
    git(main, 'config', 'user.email', 'test@example.com');
    git(main, 'config', 'user.name', 'test');
    git(main, 'add', '-A');
    git(main, '-c', 'commit.gpgsign=false', 'commit', '-q', '-m', 'init');
    git(main, 'worktree', 'add', '-q', '--detach', lane);
    // The lane differs from the main checkout in exactly the two things the hooks read.
    fs.writeFileSync(path.join(lane, 'plugins', 'foo', 'CHANGELOG.md'), '# Changelog\n\n## [9.9.9]\n\n## [1.0.0]\n');
    fs.writeFileSync(path.join(lane, 'tools', 'lint-odr.js'), 'console.log("ODR: duplicate Foo"); process.exit(1)\n');
    git(lane, 'add', '-A');
    laneSub = path.join(lane, 'plugins', 'foo');
});

after(() => {
    fs.rmSync(tmp, { recursive: true, force: true });
});

const payload = (cwd, command) => ({ cwd, tool_input: { command } });
const root = (cwd, command) => commitRoot(payload(cwd, command), command);

test('the tool call cwd decides when the command names no directory', () => {
    same(root(lane, 'git commit -m "x"'), lane);
    same(root(main, 'git commit -m "x"'), main);
});

test('a cwd below the top level resolves to the top level', () => {
    same(root(laneSub, 'git commit -m "x"'), lane);
});

test('git -C <dir> wins over the cwd', () => {
    same(root(main, `git -C "${lane}" commit -F msg.txt`), lane);
    same(root(main, `git -C '${lane}' -c commit.gpgsign=false commit -F msg.txt`), lane);
    same(root(lane, `git -C ${main} commit -m x`), main);
});

test('a relative git -C is resolved against the cwd', () => {
    same(root(tmp, 'git -C main commit -m x'), main);
});

test('cd <dir> && git commit uses that directory', () => {
    same(root(main, `cd "${lane}" && git commit -m x`), lane);
    same(root(main, `cd "${lane}"; git add -- a.txt; git commit -m x`), lane);
});

test('git -C applies after an earlier cd', () => {
    same(root(lane, `cd "${tmp}" && git -C main commit -m x`), main);
});

test('cd inside a subshell, a brace group, on its own line, or as pushd', () => {
    same(root(main, `(cd "${lane}" && git commit -m x)`), lane);
    same(root(main, `( cd "${lane}"; git commit -m x )`), lane);
    same(root(main, `{ cd "${lane}"; git commit -m x; }`), lane);
    same(root(main, `cd "${lane}"\ngit add -- a.txt\ngit commit -F msg.txt`), lane);
    same(root(main, `pushd "${lane}" && git commit -m x`), lane);
});

test('PowerShell forms of cd', () => {
    same(root(main, `Set-Location "${lane}"; git commit -m x`), lane);
    same(root(main, `Set-Location -Path '${lane}'; git commit -m x`), lane);
    same(root(main, `Push-Location "${lane}"; git commit -m x`), lane);
    same(root(main, `sl "${lane}"; git commit -m x`), lane);
});

test('a cd inside a command substitution does not move the commit', () => {
    same(root(lane, `MSG=$(cd "${main}" && cat msg.txt); git commit -m "$MSG"`), lane);
});

test('a cd named in the commit message does not move the commit', () => {
    same(root(main, `git -C "${lane}" commit -m "see cd ${main} && git commit"`), lane);
});

test('--work-tree and GIT_WORK_TREE name the committing tree', () => {
    const gitDir = path.join(main, '.git', 'worktrees', path.basename(lane));
    same(root(main, `git --git-dir="${gitDir}" --work-tree="${lane}" commit -m x`), lane);
    same(root(main, `git --work-tree "${lane}" commit -m x`), lane);
    same(root(main, `GIT_WORK_TREE="${lane}" git commit -m x`), lane);
    same(root(main, `cd "${tmp}" && GIT_DIR=x GIT_WORK_TREE='${lane}' git commit -m x`), lane);
});

test('every git commit shape is recognised as a commit', () => {
    const { commitDir } = require(path.join(HOOKS, 'commit-root.js'));
    for (const cmd of ['git commit -m x', 'git -C a commit', 'git -c user.name=x commit', 'git --work-tree a commit', 'git --no-pager commit -F m'])
        assert.notStrictEqual(commitDir(cmd, main), null, cmd);
    for (const cmd of ['git add .claude/workflows/pre-commit-audit.js', 'git log --grep commit', 'echo commit', 'git cherry-pick -n abc'])
        assert.strictEqual(commitDir(cmd, main), null, cmd);
});

test('falls back to the hook checkout when nothing resolves', () => {
    same(root(undefined, 'git commit -m x'), hookRoot());
    same(root(path.join(tmp, 'missing'), 'git commit -m x'), hookRoot());
    same(root(tmp, 'git commit -m x'), hookRoot()); // tmp itself is not a git checkout
});

const runHook = (script, cwd, command) =>
    spawnSync(process.execPath, [path.join(HOOKS, script)], { input: JSON.stringify(payload(cwd, command)), encoding: 'utf8' });

test('guard-changelog reads the CHANGELOG of the committing tree', () => {
    const release = 'git commit -m "chore(foo): release 9.9.9"';
    assert.strictEqual(runHook('guard-changelog.js', lane, release).status, 0, 'the lane has the 9.9.9 entry');
    assert.strictEqual(runHook('guard-changelog.js', main, release).status, 2, 'the main checkout does not');
    assert.strictEqual(runHook('guard-changelog.js', main, `git -C "${lane}" commit -m "chore(foo): release 9.9.9"`).status, 0);
});

test('guard-ci-gates runs the gates of the committing tree', () => {
    const blocked = runHook('guard-ci-gates.js', lane, 'git commit -m "fix: x"');
    assert.strictEqual(blocked.status, 2, 'the lane lint fails');
    assert.match(blocked.stderr, /ODR: duplicate Foo/);
    assert.strictEqual(runHook('guard-ci-gates.js', main, 'git commit -m "fix: x"').status, 0, 'the main checkout lint passes');
    assert.strictEqual(runHook('guard-ci-gates.js', main, `git -C "${lane}" commit -m "fix: x"`).status, 2);
});
