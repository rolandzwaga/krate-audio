'use strict';
require('./helpers/write-guard.js');

// `legacy tag order matches git versionsort` (spec SC-004; FR-040; plan §4.5.3, §7.1).
// gen-feed's legacy mode picks each plugin's tag with gitVersionCompare, an independent implementation of the
// documented strverscmp order. This test holds it against the real git: one temp repo holds every tag of the corpus
// (fixtures/git-versionsort.json), and for each set `git tag -l "<p>/v*" --sort=-v:refname` must put first the tag
// whose version buildLegacyFeed publishes. No versionsort.suffix is configured anywhere: the global and system config
// files are two empty files in the temp dir. Every spawn runs with cwd = the temp repo (the write guard enforces it).

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const childProcess = require('node:child_process');

const { readJson } = require('./helpers/fixtures.js');
const { buildLegacyFeed, gitVersionCompare } = require('../../gen-feed.js');

const versionOf = (tag) => tag.replace(/.*\/v/, ''); // the greedy regex of release.yml:672

test('legacy tag order matches git versionsort', async (t) => {
  const corpus = readJson('git-versionsort.json');
  const allTags = [...corpus.sets.flatMap((s) => s.tags), ...corpus.unrelated];

  await t.test('the corpus covers what SC-004 names', () => {
    assert.ok(corpus.sets.length >= 20, `${corpus.sets.length} sets, need >= 20`);
    const has = (re) => allTags.some((tag) => re.test(tag));
    assert.ok(has(/\/v\d+\.\d{2,}\./), 'a multi-digit part');
    assert.ok(has(/-rc1$/), '-rc1');
    assert.ok(has(/-beta\.1$/), '-beta.1');
    assert.ok(has(/-beta\.10$/), '-beta.10');
    assert.ok(has(/\.01(\.|$)/), 'a leading zero (01)');
    assert.ok(has(/\.001(\.|$)/), 'two leading zeros (001)');
    assert.ok(corpus.unrelated.length > 0, 'unrelated tags');
    const prefixes = corpus.sets.map((s) => s.prefix);
    assert.ok(prefixes.includes('alpha') && prefixes.includes('alphabet'), 'a prefix that is a prefix of another');
    assert.equal(new Set(allTags).size, allTags.length, 'every tag is unique');
    for (const s of corpus.sets) {
      assert.equal(s.name.toLowerCase(), s.prefix, `${s.id}: the plugin name lower-cases to the prefix`);
      assert.ok(s.tags.every((tag) => tag.startsWith(`${s.prefix}/v`)), `${s.id}: every tag is <prefix>/v...`);
    }
  });

  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km1-versionsort-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const repo = path.join(tmp, 'repo');
  fs.mkdirSync(repo);
  const globalConfig = path.join(tmp, 'gitconfig-global');
  const systemConfig = path.join(tmp, 'gitconfig-system');
  fs.writeFileSync(globalConfig, '');
  fs.writeFileSync(systemConfig, '');
  // No inherited GIT_* variable may point git at another repository or configuration.
  const env = Object.fromEntries(Object.entries(process.env).filter(([k]) => !/^GIT_/i.test(k)));
  Object.assign(env, { GIT_CONFIG_GLOBAL: globalConfig, GIT_CONFIG_SYSTEM: systemConfig, GIT_CONFIG_NOSYSTEM: '1' });

  const git = (args, input) => {
    const r = childProcess.spawnSync('git', args, { cwd: repo, env, input, encoding: 'utf8' });
    assert.equal(r.error, undefined, `git ${args.join(' ')}: ${r.error}`);
    assert.equal(r.status, 0, `git ${args.join(' ')} exited ${r.status}: ${r.stderr}`);
    return r.stdout;
  };

  git(['init', '-q']);
  git(['-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '--allow-empty', '-q', '-m', 'x']);
  git(['update-ref', '--stdin'], allTags.map((tag) => `create refs/tags/${tag} HEAD\n`).join(''));
  const tagsText = allTags.join('\n') + '\n';

  for (const set of corpus.sets) {
    await t.test(`${set.id}: ${set.prefix}/v*`, () => {
      const gitOrder = git(['tag', '-l', `${set.prefix}/v*`, '--sort=-v:refname'])
        .split(/\r?\n/)
        .filter((l) => l !== '');
      const ours = allTags.filter((tag) => tag.startsWith(`${set.prefix}/v`)).sort((a, b) => gitVersionCompare(b, a));
      const orders = `\n  git:  ${gitOrder.join(' ')}\n  ours: ${ours.join(' ')}`;

      const plugin = { dir: set.prefix, versionJson: { name: set.name, url: 'https://example.invalid/' } };
      const feed = JSON.parse(buildLegacyFeed({ plugins: [plugin], tagsText }));
      const picked = Object.hasOwn(feed.plugins, set.name) ? feed.plugins[set.name].version : undefined;
      const expected = gitOrder.length > 0 ? versionOf(gitOrder[0]) : undefined;
      assert.equal(picked, expected, `legacy pick differs from git's first tag${orders}`);
      assert.deepEqual(ours, gitOrder, `gitVersionCompare order differs from git${orders}`);
    });
  }
});
