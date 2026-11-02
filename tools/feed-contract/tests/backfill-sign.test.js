'use strict';
require('./helpers/write-guard.js');

// tools/backfill-sign.js (spec FR-051 to FR-057a, SC-010 to SC-013, SC-025; plan §4.4, PD-4, PD-9, PD-10, PD-20,
// PD-23, PD-24, PD-26, PD-28, §7.1).
// Every effect the tool has on the world goes through deps; helpers/fake-gh.js builds a fake gh / git / pkgutil / lipo /
// tar / objdump over fixtures/phase2/backfill/state.json, writing only under each test's fs.mkdtemp dir. Nothing here
// reaches GitHub. The real spawns are SC-025's rsign sign through the tool's own productionDeps, gated by rsignGate,
// and FR-051's `git init` probe of the CLI's git directory in a temp dir.
// Linux facts on win32 use the fake's listDirectory (the tarball's own listing, PD-4); the POSIX legs extract with the
// real tar through helpers/external-tools.js and the tool's default lister.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const contract = require('../contract.js');
const listing = require('../listing.js');
const minisign = require('../../minisign.js');
const backfillSign = require('../../backfill-sign.js');
const fakeGh = require('./helpers/fake-gh.js');
const { rsignGate, rsignGenerate } = require('./helpers/external-tools.js');

const REPO = 'krate-fixture/backfill';
const BAD_DIR = path.join(__dirname, '..', 'fixtures', 'listings', 'bad');
// The nine synthetic plugins and their newest stable versions (state.json also lists an older stable, a newer
// prerelease and a higher-versioned draft for each).
const CURRENT = Object.freeze({
  Alphaplug: '1.2.0',
  Betaplug: '2.0.1',
  Deltaplug: '1.10.0',
  Epsilonplug: '3.4.5',
  Etaplug: '0.8.2',
  Gammaplug: '0.12.0',
  Iotaplug: '1.0.4',
  Thetaplug: '4.1.0',
  Zetaplug: '1.0.0',
});
const NAMES = Object.keys(CURRENT).sort();
const tagOf = (name) => `${name.toLowerCase()}/v${CURRENT[name]}`;
const dashed = (tag) => tag.replace('/', '-');
const exeOf = (name) => `${name}-${CURRENT[name]}-Windows-x64.exe`;
const pkgOf = (name) => `${name}-${CURRENT[name]}-macOS.pkg`;
const tarOf = (name) => `${name}-${CURRENT[name]}-Linux-x64.tar.gz`;
const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const readJson = (f) => JSON.parse(fs.readFileSync(f, 'utf8'));

function tempDir(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km2-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

function run(argv, deps) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = backfillSign.main(argv, io, deps);
  return { code, stdout, stderr };
}

/** A fresh fake world in its own temp dir, with the paths every step of the flow uses. */
function freshWorld(t, opts = {}) {
  const tmp = tempDir(t, 'backfill');
  const w = fakeGh.makeWorld({ tmp, ...opts });
  const p = {
    plan: path.join(tmp, 'plan.json'),
    dl: { 'inno-exe': path.join(tmp, 'dl-exe'), pkg: path.join(tmp, 'dl-pkg'), 'tar.gz': path.join(tmp, 'dl-tar') },
    work: { darwin: path.join(tmp, 'work-darwin'), linux: path.join(tmp, 'work-linux') },
    facts: path.join(tmp, 'facts'),
    applyWork: path.join(tmp, 'work-apply'),
    out: path.join(tmp, 'out'),
  };
  const logMark = () => w.logs.length;
  return {
    w,
    tmp,
    p,
    plan: (pluginsDir = fakeGh.PLUGINS_DIR) =>
      run(['plan', '--plugins-dir', pluginsDir, '--repo', REPO, '--out', p.plan], w.deps),
    download: (format) => {
      const mark = logMark();
      const r = run(['download', '--plan', p.plan, '--format', format, '--dir', p.dl[format], '--repo', REPO], w.deps);
      return { ...r, logs: w.logs.slice(mark) };
    },
    facts: (osName) => {
      const dir = osName === 'darwin' ? p.dl.pkg : p.dl['tar.gz'];
      return run(
        ['facts', '--plan', p.plan, '--os', osName, '--dir', dir, '--work', p.work[osName], '--out', p.facts],
        w.deps,
      );
    },
    /** apply with the signer armSigner made; `work` is a fresh dir per call (gh release download refuses to clobber). */
    apply: (signer, { dryRun = false, pluginsDir = fakeGh.PLUGINS_DIR, work = p.applyWork } = {}) => {
      const argv = ['apply', '--plan', p.plan, '--plugins-dir', pluginsDir, '--facts', p.facts, '--key', signer.keyFile];
      argv.push('--public-key', signer.publicKeyFile, '--repo', REPO, '--work', work, '--out-dir', p.out);
      if (dryRun) argv.push('--dry-run');
      const mark = logMark();
      const callMark = w.calls.length;
      const r = run(argv, w.deps);
      return { ...r, logs: w.logs.slice(mark), calls: w.calls.slice(callMark) };
    },
    report: (file) => {
      const doc = readJson(file);
      assert.ok(Array.isArray(doc), `${path.basename(file)} is a JSON array`);
      return doc;
    },
  };
}

/**
 * Arms the fake with a throwaway minisign.js signer and writes the --key file apply must delete (PD-20); from here on
 * the fake asserts at every gh release upload / view that the key file is gone. `publicKeyText` overrides the
 * --public-key contents (a mismatching key, the KAT key).
 */
function armSigner(f, { publicKeyText, name = 'krate' } = {}) {
  const signer = fakeGh.throwawaySigner();
  const keyFile = path.join(f.tmp, `${name}.key`);
  const publicKeyFile = path.join(f.tmp, `${name}.pub`);
  fs.writeFileSync(keyFile, 'placeholder: the throwaway signer keeps its key in memory\n');
  fs.writeFileSync(publicKeyFile, publicKeyText ?? signer.publicKeyText);
  f.w.setSigner(signer.sign);
  f.w.setKeyFile(keyFile);
  return { ...signer, keyFile, publicKeyFile };
}

const isUpload = (c) => c.cmd === 'gh' && c.argv[0] === 'release' && c.argv[1] === 'upload';
const isAuditView = (c) => c.cmd === 'gh' && c.argv[1] === 'view' && c.argv[c.argv.length - 1] === 'assets';
const uploadNames = (c) => c.argv.slice(5).map((a) => path.basename(a));
const PLATFORM_OF = Object.freeze([
  [exeOf, 'windows-x86_64'],
  [pkgOf, 'darwin-universal'],
  [tarOf, 'linux-x86_64'],
]);
const minisigsOf = (name) => PLATFORM_OF.map(([fn]) => `${fn(name)}.minisig`).sort();

/** The three .minisig texts in `dir` verify against the signer's key and bind to their own asset (C3). */
function assertSignaturesIn(f, dir, name, signer) {
  for (const [fn, platform] of PLATFORM_OF) {
    const asset = fn(name);
    const text = fs.readFileSync(path.join(dir, `${asset}.minisig`), 'utf8');
    const { trustedComment } = minisign.verifyBuffer(f.w.served.bytes(tagOf(name), asset), text, signer.publicKeyText);
    contract.checkBinding(trustedComment, { name, version: CURRENT[name], platform, asset });
  }
}

/** The out dir of one release: exactly SHA256SUMS, three .minisig and a C2-valid release.json of that release. */
function assertOutDir(f, name, signer) {
  const dir = path.join(f.p.out, dashed(tagOf(name)));
  assert.deepEqual(fs.readdirSync(dir).sort(), ['SHA256SUMS', ...minisigsOf(name), 'release.json'].sort(), name);
  assertSignaturesIn(f, dir, name, signer);
  const doc = readJson(path.join(dir, 'release.json'));
  contract.checkReleaseManifest(doc, { repo: REPO });
  assert.equal(doc.tag, tagOf(name));
  assert.equal(doc.pub_date, releaseOf(f.w.state, name).publishedAt, `${name}: pub_date is publishedAt (D9)`);
  const sums = PLATFORM_OF.map(([fn]) => fn(name))
    .sort()
    .map((asset) => `${sha256(f.w.served.bytes(tagOf(name), asset))}  ${asset}\n`)
    .join('');
  assert.equal(fs.readFileSync(path.join(dir, 'SHA256SUMS'), 'utf8'), sums, `${name} SHA256SUMS`);
  return doc;
}

/**
 * apply after the facts loops: the releases in `reported` ({Name: CODE}) are reported under their code with 0 uploads,
 * every other plugin of NAMES is signed with its two upload calls.
 */
function assertApplyReports(f, reported, { code = 3, pluginsDir } = {}) {
  const signer = armSigner(f);
  const r = f.apply(signer, pluginsDir ? { pluginsDir } : {});
  assert.equal(r.code, code, r.stderr);
  const uploads = r.calls.filter(isUpload);
  for (const name of NAMES) {
    const mine = uploads.filter((c) => c.argv[2] === tagOf(name));
    if (Object.prototype.hasOwnProperty.call(reported, name)) {
      assert.equal(mine.length, 0, `${name}: no upload for a reported release`);
      const line = r.logs.find((l) => l.startsWith(`${name}: `));
      assert.ok(line && line.startsWith(`${name}: reported ${reported[name]}: `), `${name}: ${line}`);
    } else {
      assert.equal(mine.length, 2, `${name}: two upload calls`);
      assert.ok(r.logs.includes(`${name}: signed`), `${name} signed: ${r.logs.join(' | ')}`);
    }
  }
  const signedCount = NAMES.filter((n) => !Object.prototype.hasOwnProperty.call(reported, n)).length;
  assert.equal(r.logs[r.logs.length - 1], `uploads: ${2 * signedCount}`);
  assert.equal(fs.existsSync(signer.keyFile), false, 'apply removed the --key file');
  return r;
}

const entryOf = (planDoc, name) => planDoc.releases.find((r) => r.name === name);
const byTag = (entries, tag) => entries.filter((e) => e.tag === tag);
const execCalls = (w, cmd) => w.calls.filter((c) => c.cmd === cmd);
const mentions = (call, text) => call.argv.some((a) => a.includes(text));

function assertExecArgvArrays(w) {
  for (const c of w.calls.filter((x) => ['pkgutil', 'lipo', 'tar', 'objdump'].includes(x.cmd))) {
    assert.ok(Array.isArray(c.argv) && c.argv.every((a) => typeof a === 'string'), `${c.cmd} argv is an array`);
  }
}

/** The release of `name`'s current tag in the fake state. */
const releaseOf = (state, name) => state.releases.find((r) => r.tagName === tagOf(name));

/** Runs plan, both downloads and both facts loops; asserts each returns 0. */
function flowToFacts(f) {
  const steps = [f.plan(), f.download('pkg'), f.download('tar.gz'), f.facts('darwin'), f.facts('linux')];
  steps.forEach((r, i) => assert.equal(r.code, 0, `step ${i}: ${r.stderr}`));
  return steps;
}

/** Facts loop of one OS with one release reported under `code`; the other eight get facts files. */
function assertFactsReported(f, osName, name, code) {
  const report = f.report(path.join(f.p.facts, `facts-report.${osName}.json`));
  const mine = byTag(report, tagOf(name));
  assert.equal(mine.length, 1, `one ${osName} report entry for ${name}: ${JSON.stringify(report)}`);
  assert.equal(mine[0].code, code, mine[0].message);
  assert.equal(typeof mine[0].message, 'string');
  assert.equal(report.length, 1, `only ${name} is reported: ${JSON.stringify(report)}`);
  assert.equal(fs.existsSync(path.join(f.p.facts, `${name}-${CURRENT[name]}.${osName}.json`)), false);
  for (const other of NAMES.filter((n) => n !== name)) {
    assert.ok(fs.existsSync(path.join(f.p.facts, `${other}-${CURRENT[other]}.${osName}.json`)), `${other} facts`);
  }
}

// FR-051: D12's `git show <tag>:plugins/<dir>/…` needs the checkout and its tags, and CI's --work is $RUNNER_TEMP/work,
// outside the checkout, so the CLI's git runs in --plugins-dir and every other tool in --work. `git init` is the probe
// the write guard admits: the repository it creates shows which directory git ran in.
test('backfill production signer drives rsign2', async (t) => {
  await t.test('git runs in --plugins-dir, not in --work', (t) => {
    const root = tempDir(t, 'backfill-gitcwd');
    const work = path.join(root, 'work');
    const pluginsDir = path.join(root, 'checkout', 'plugins');
    fs.mkdirSync(work, { recursive: true });
    fs.mkdirSync(pluginsDir, { recursive: true });
    const argv = ['apply', '--plugins-dir', pluginsDir, '--work', work];

    const r = backfillSign.cliDeps(argv).git(['init', '-q']);

    assert.equal(r.status, 0);
    assert.ok(fs.existsSync(path.join(pluginsDir, '.git')), 'git ran in --plugins-dir');
    assert.ok(!fs.existsSync(path.join(work, '.git')), 'git did not run in --work');
  });

  await t.test('the signer drives rsign2', (t) => {
    if (!rsignGate(t)) return;
    const dir = tempDir(t, 'backfill-rsign');
    const { pub, sec } = rsignGenerate(dir);
    const asset = 'Alphaplug-1.2.0-Linux-x64.tar.gz';
    const file = path.join(dir, asset);
    fs.writeFileSync(file, crypto.randomBytes(4096));
    const comment = contract.formatTrustedComment('Alphaplug', '1.2.0', 'linux-x86_64', asset);

    const text = backfillSign.productionDeps(dir).sign(file, comment, sec);

    assert.equal(text, fs.readFileSync(`${file}.minisig`, 'utf8'), 'the returned text is the .minisig on disk');
    const verified = minisign.verifyFile(file, text, fs.readFileSync(pub, 'utf8'));
    assert.equal(verified.trustedComment, comment);
    contract.checkBinding(verified.trustedComment, {
      name: 'Alphaplug',
      version: '1.2.0',
      platform: 'linux-x86_64',
      asset,
    });
  });
});

test('backfill signs nine current releases', (t) => {
  const f = freshWorld(t);
  const [, pkgRun, tarRun] = flowToFacts(f);
  for (const r of [pkgRun, tarRun]) assert.equal(r.logs[r.logs.length - 1], 'digest checked: 9 of 9');
  const planDoc = readJson(f.p.plan);
  const signer = armSigner(f);

  const r = f.apply(signer);

  assert.equal(r.code, 0, r.stderr);
  assert.equal(r.stderr, '');
  assert.equal(fs.existsSync(signer.keyFile), false, 'apply removed the --key file (PD-20)');
  assert.deepEqual(
    r.logs.filter((l) => l.startsWith('digest checked: ')),
    ['digest checked: 9 of 9', 'digest checked: 9 of 9', 'digest checked: 9 of 9'],
    'one digest line per format; every asset carries a GitHub digest',
  );
  assert.deepEqual(r.logs.slice(-10), [...NAMES.map((n) => `${n}: signed`), 'uploads: 18']);

  // Exactly two upload calls per release, at the newest stable tag: [SHA256SUMS, 3 x .minisig] then [release.json].
  const uploads = r.calls.filter(isUpload);
  assert.equal(uploads.length, 18);
  assert.deepEqual([...new Set(uploads.map((c) => c.argv[2]))].sort(), NAMES.map(tagOf).sort());
  for (const name of NAMES) {
    const mine = uploads.filter((c) => c.argv[2] === tagOf(name));
    assert.equal(mine.length, 2, name);
    assert.deepEqual(mine[0].argv.slice(0, 5), ['release', 'upload', tagOf(name), '--repo', REPO]);
    const first = uploadNames(mine[0]);
    assert.equal(first[0], 'SHA256SUMS', `${name}: SHA256SUMS first`);
    assert.deepEqual(first.slice(1).sort(), minisigsOf(name));
    assert.deepEqual(uploadNames(mine[1]), ['release.json'], `${name}: release.json alone and last (D8)`);
    const before = entryOf(planDoc, name).assets_before;
    for (const c of mine) {
      for (const file of c.argv.slice(5)) {
        assert.ok(path.isAbsolute(file), file);
        assert.equal(path.dirname(file), path.join(f.p.out, dashed(tagOf(name))));
        assert.equal(before.includes(path.basename(file)), false, `${name}: ${path.basename(file)} was not there`);
      }
    }
    // The tag's own version.json and setup.iss (D12).
    for (const rel of ['version.json', 'installers/windows/setup.iss']) {
      const want = ['show', `${tagOf(name)}:plugins/${name}/${rel}`];
      assert.ok(
        r.calls.some((c) => c.cmd === 'git' && JSON.stringify(c.argv) === JSON.stringify(want)),
        `git ${want.join(' ')}`,
      );
    }
  }
  assert.equal(f.w.calls.filter((c) => c.argv.some((a) => a.includes('--clobber'))).length, 0, 'no --clobber');

  // 27 uploaded signatures verify and bind; 9 release.json pass C2.
  let verified = 0;
  for (const name of NAMES) {
    const doc = assertOutDir(f, name, signer);
    verified += 3;
    if (name === 'Gammaplug') {
      assert.equal(doc.install.preset_subdir, 'Kits');
      assert.equal(doc.install.macos_pkg_id, 'com.krateaudio.gammakit');
    } else {
      assert.equal('preset_subdir' in doc.install, false, name);
      assert.equal(doc.install.macos_pkg_id, `com.krateaudio.${name.toLowerCase()}`);
    }
  }
  assert.equal(verified, 27);

  // The audit: one assets view per release after the last upload, and the fake state is assets_before + the uploads.
  const lastUpload = r.calls.lastIndexOf(uploads[uploads.length - 1]);
  const audits = r.calls.filter(isAuditView);
  assert.deepEqual(audits.map((c) => c.argv[2]).sort(), NAMES.map(tagOf).sort());
  for (const c of audits) assert.ok(r.calls.indexOf(c) > lastUpload, 'the audit runs after the uploads');
  for (const name of NAMES) {
    const now = releaseOf(f.w.state, name).assets.map((a) => a.name).sort();
    const added = ['SHA256SUMS', ...minisigsOf(name), 'release.json'];
    assert.deepEqual(now, [...entryOf(planDoc, name).assets_before, ...added].sort());
  }
});

test('backfill second run uploads nothing', (t) => {
  const f = freshWorld(t);
  flowToFacts(f);
  const first = f.apply(armSigner(f));
  assert.equal(first.code, 0, first.stderr);
  assert.equal(first.calls.filter(isUpload).length, 18);

  const replan = f.plan();
  assert.equal(replan.code, 0, replan.stderr);
  const planDoc = readJson(f.p.plan);
  for (const name of NAMES) assert.equal(entryOf(planDoc, name).status, 'skipped-signed', name);
  const signer = armSigner(f, { name: 'second' });
  const r = f.apply(signer, { work: path.join(f.tmp, 'work-apply-2') });

  assert.equal(r.code, 0, r.stderr);
  assert.deepEqual(r.logs.slice(-10), [...NAMES.map((n) => `${n}: skipped (already signed)`), 'uploads: 0']);
  assert.equal(r.calls.filter(isUpload).length, 0);
  assert.equal(r.calls.filter((c) => c.cmd === 'sign').length, 0);
  assert.equal(r.calls.filter((c) => c.cmd === 'gh' && c.argv[1] === 'download').length, 0);
  assert.equal(r.calls.filter(isAuditView).length, 9, 'the audit still runs');
  assert.equal(fs.existsSync(signer.keyFile), false);
});

test('backfill dry run uploads nothing', (t) => {
  const f = freshWorld(t);
  flowToFacts(f);
  const planDoc = readJson(f.p.plan);
  const signer = armSigner(f);

  const r = f.apply(signer, { dryRun: true });

  assert.equal(r.code, 0, r.stderr);
  assert.equal(f.w.calls.filter(isUpload).length, 0, 'no gh release upload argv');
  assert.deepEqual(r.logs.slice(-10), [...NAMES.map((n) => `${n}: signed`), 'uploads: 0']);
  assert.equal(fs.existsSync(signer.keyFile), false);
  for (const name of NAMES) assertOutDir(f, name, signer);
  assert.equal(r.calls.filter(isAuditView).length, 9);
  for (const name of NAMES) {
    const now = releaseOf(f.w.state, name).assets.map((a) => a.name);
    assert.deepEqual(now.sort(), entryOf(planDoc, name).assets_before, `${name}: audit = assets_before`);
  }
});

test('backfill reports defective releases by name', async (t) => {
  await t.test('(baseline) nine planned at the newest stable; reports empty; digest checked: 9 of 9', (t) => {
    const f = freshWorld(t);
    const [planRun, pkgRun, tarRun, darwinRun, linuxRun] = flowToFacts(f);
    assert.equal(planRun.stderr, '');
    for (const r of [darwinRun, linuxRun]) assert.equal(r.stderr, '');

    const planDoc = readJson(f.p.plan);
    assert.equal(planDoc.repo, REPO);
    assert.deepEqual(
      planDoc.releases.map((r) => r.name),
      NAMES,
      'one entry per plugin, sorted by name',
    );
    for (const name of NAMES) {
      const e = entryOf(planDoc, name);
      const rel = releaseOf(f.w.state, name);
      assert.equal(e.status, 'planned', name);
      assert.equal(e.dir, name);
      assert.equal(e.tag, tagOf(name), `${name}: newest stable, never the prerelease, the draft or the older stable`);
      assert.equal(e.version, CURRENT[name]);
      assert.equal(e.publishedAt, rel.publishedAt);
      const expected = [
        [exeOf(name), 'inno-exe'],
        [pkgOf(name), 'pkg'],
        [tarOf(name), 'tar.gz'],
      ].map(([asset, format]) => {
        const a = rel.assets.find((x) => x.name === asset);
        return { name: asset, format, size: a.size, digest: a.digest };
      });
      assert.deepEqual(e.assets, expected, name);
      for (const a of e.assets) {
        assert.match(a.digest, /^sha256:[0-9a-f]{64}$/);
      }
      assert.deepEqual(e.assets_before, [exeOf(name), pkgOf(name), tarOf(name)].sort());
    }

    for (const r of [pkgRun, tarRun]) assert.equal(r.logs[r.logs.length - 1], 'digest checked: 9 of 9');
    assert.deepEqual(f.report(path.join(f.p.dl.pkg, 'download-report.json')), []);
    assert.deepEqual(f.report(path.join(f.p.dl['tar.gz'], 'download-report.json')), []);
    assert.deepEqual(f.report(path.join(f.p.facts, 'facts-report.darwin.json')), []);
    assert.deepEqual(f.report(path.join(f.p.facts, 'facts-report.linux.json')), []);
    for (const name of NAMES) {
      const tag = tagOf(name);
      const pkg = f.w.served.bytes(tag, pkgOf(name));
      assert.deepEqual(fs.readFileSync(path.join(f.p.dl.pkg, dashed(tag), pkgOf(name))), pkg, `${name} pkg bytes`);
      const darwin = readJson(path.join(f.p.facts, `${name}-${CURRENT[name]}.darwin.json`));
      assert.deepEqual(
        { ...darwin, macho: darwin.macho.map((m) => m.path) },
        {
          asset: pkgOf(name),
          size: pkg.length,
          sha256: sha256(pkg),
          platform_key: 'darwin-universal',
          min_os: '11.0',
          archs: ['arm64', 'x86_64'],
          macho: [
            `${name}_au.pkg/Payload/${name}.component/Contents/MacOS/${name}`,
            `${name}_vst3.pkg/Payload/${name}.vst3/Contents/MacOS/${name}`,
          ],
        },
      );
      const tar = f.w.served.bytes(tag, tarOf(name));
      const lines = listing.listingFromTarGz(tar);
      assert.deepEqual(readJson(path.join(f.p.facts, `${name}-${CURRENT[name]}.linux.json`)), {
        asset: tarOf(name),
        size: tar.length,
        sha256: sha256(tar),
        min_os: '2.34',
        payload: { files: lines.length, sha256: listing.digest(lines) },
      });
    }
    const x = (osName, name) => path.join(f.p.work[osName], `${dashed(tagOf(name))}-x`);
    for (const name of NAMES) {
      const tarCalls = execCalls(f.w, 'tar').filter((c) => mentions(c, dashed(tagOf(name))));
      assert.deepEqual(
        tarCalls.map((c) => c.argv),
        [['-xzf', path.join(f.p.dl['tar.gz'], dashed(tagOf(name)), tarOf(name)), '-C', x('linux', name)]],
      );
      const so = path.join(x('linux', name), `${name}.vst3`, 'Contents', 'x86_64-linux', `${name}.so`);
      assert.ok(execCalls(f.w, 'objdump').some((c) => c.argv[0] === '-T' && c.argv[1] === so), `${name} objdump`);
      const pkgCalls = execCalls(f.w, 'pkgutil').filter((c) => mentions(c, dashed(tagOf(name))));
      assert.deepEqual(
        pkgCalls.map((c) => c.argv),
        [['--expand-full', path.join(f.p.dl.pkg, dashed(tagOf(name)), pkgOf(name)), x('darwin', name)]],
      );
    }
    assert.equal(execCalls(f.w, 'lipo').length, 18, 'one lipo -archs per Mach-O');
    assert.equal(f.w.calls.filter((c) => c.cmd === 'gh' && c.argv[1] === 'upload').length, 0);
    assertExecArgvArrays(f.w);
  });

  await t.test('a release missing its .pkg is reported E_BACKFILL_ASSET_MISSING naming the asset', (t) => {
    const f = freshWorld(t, {
      mutate: (state) => {
        const r = releaseOf(state, 'Betaplug');
        r.assets = r.assets.filter((a) => a.name !== pkgOf('Betaplug'));
      },
    });
    const [, pkgRun] = flowToFacts(f);
    const planDoc = readJson(f.p.plan);
    const e = entryOf(planDoc, 'Betaplug');
    assert.equal(e.status, 'reported');
    assert.equal(e.code, 'E_BACKFILL_ASSET_MISSING');
    assert.ok(e.message.includes(pkgOf('Betaplug')), e.message);
    assert.equal(e.tag, tagOf('Betaplug'));
    assert.deepEqual(e.assets_before, [exeOf('Betaplug'), tarOf('Betaplug')].sort());
    assert.equal(planDoc.releases.filter((r) => r.status === 'planned').length, 8);
    assert.equal(pkgRun.logs[pkgRun.logs.length - 1], 'digest checked: 8 of 8');
    const dl = f.w.calls.filter((c) => c.cmd === 'gh' && c.argv[1] === 'download');
    assert.equal(dl.filter((c) => c.argv[2] === tagOf('Betaplug')).length, 0, 'a reported release is not downloaded');
    const r = assertApplyReports(f, { Betaplug: 'E_BACKFILL_ASSET_MISSING' });
    assert.ok(r.logs.includes(`Betaplug: reported E_BACKFILL_ASSET_MISSING: ${e.message}`), r.logs.join('\n'));
  });

  await t.test('an immutable release is reported E_BACKFILL_IMMUTABLE', (t) => {
    const f = freshWorld(t, { mutate: (state) => (releaseOf(state, 'Deltaplug').isImmutable = true) });
    flowToFacts(f);
    const e = entryOf(readJson(f.p.plan), 'Deltaplug');
    assert.equal(e.status, 'reported');
    assert.equal(e.code, 'E_BACKFILL_IMMUTABLE');
    assert.ok(e.message.includes(tagOf('Deltaplug')), e.message);
    assertApplyReports(f, { Deltaplug: 'E_BACKFILL_IMMUTABLE' });
  });

  await t.test('SHA256SUMS without release.json is reported E_BACKFILL_PARTIAL', (t) => {
    const f = freshWorld(t, {
      mutate: (state) => releaseOf(state, 'Epsilonplug').assets.push({ name: 'SHA256SUMS', size: 300, digest: null }),
    });
    flowToFacts(f);
    const e = entryOf(readJson(f.p.plan), 'Epsilonplug');
    assert.equal(e.status, 'reported');
    assert.equal(e.code, 'E_BACKFILL_PARTIAL');
    assert.ok(e.assets_before.includes('SHA256SUMS'));
    assertApplyReports(f, { Epsilonplug: 'E_BACKFILL_PARTIAL' });
  });

  await t.test('a .minisig without release.json is reported E_BACKFILL_PARTIAL', (t) => {
    const f = freshWorld(t, {
      mutate: (state) =>
        releaseOf(state, 'Etaplug').assets.push({ name: `${tarOf('Etaplug')}.minisig`, size: 400, digest: null }),
    });
    assert.equal(f.plan().code, 0);
    assert.equal(entryOf(readJson(f.p.plan), 'Etaplug').code, 'E_BACKFILL_PARTIAL');
  });

  await t.test('a release already carrying release.json is skipped-signed', (t) => {
    const extra = ['SHA256SUMS', 'release.json', ...[exeOf, pkgOf, tarOf].map((fn) => `${fn('Zetaplug')}.minisig`)];
    const f = freshWorld(t, {
      mutate: (state) => {
        // release.json present, even with an asset missing: idempotence wins (plan §4.4 classification order).
        const r = releaseOf(state, 'Zetaplug');
        for (const name of extra) r.assets.push({ name, size: 100, digest: null });
      },
    });
    assert.equal(f.plan().code, 0);
    const e = entryOf(readJson(f.p.plan), 'Zetaplug');
    assert.equal(e.status, 'skipped-signed');
    assert.equal(e.tag, tagOf('Zetaplug'));
    assert.equal(e.code, undefined);
    assert.deepEqual(
      e.assets_before,
      [exeOf('Zetaplug'), pkgOf('Zetaplug'), tarOf('Zetaplug'), ...extra].sort((a, b) =>
        Buffer.compare(Buffer.from(a), Buffer.from(b)),
      ),
    );
  });

  await t.test('a size off by one is a download-report E_BACKFILL_SIZE entry, copied unchanged into facts', (t) => {
    const f = freshWorld(t, {
      mutate: (state, ctx) => {
        const a = releaseOf(state, 'Alphaplug').assets.find((x) => x.name === pkgOf('Alphaplug'));
        a.size = ctx.bytes(tagOf('Alphaplug'), a.name).length + 1;
      },
    });
    assert.equal(f.plan().code, 0);
    const pkgRun = f.download('pkg');
    assert.equal(pkgRun.code, 0, pkgRun.stderr);
    assert.equal(pkgRun.logs[pkgRun.logs.length - 1], 'digest checked: 8 of 9', 'a size failure checks no digest');
    const report = f.report(path.join(f.p.dl.pkg, 'download-report.json'));
    assert.equal(report.length, 1, JSON.stringify(report));
    assert.equal(report[0].tag, tagOf('Alphaplug'));
    assert.equal(report[0].code, 'E_BACKFILL_SIZE');
    assert.ok(report[0].message.includes(pkgOf('Alphaplug')), report[0].message);

    const darwinRun = f.facts('darwin');
    assert.equal(darwinRun.code, 0, darwinRun.stderr);
    assert.deepEqual(f.report(path.join(f.p.facts, 'facts-report.darwin.json')), report);
    assert.equal(fs.existsSync(path.join(f.p.facts, `Alphaplug-${CURRENT.Alphaplug}.darwin.json`)), false);
    assert.equal(execCalls(f.w, 'pkgutil').filter((c) => mentions(c, dashed(tagOf('Alphaplug')))).length, 0);
    assert.equal(execCalls(f.w, 'pkgutil').length, 8);

    assert.equal(f.download('tar.gz').code, 0);
    assert.equal(f.facts('linux').code, 0);
    const r = assertApplyReports(f, { Alphaplug: 'E_BACKFILL_SIZE' });
    assert.ok(r.logs.includes(`Alphaplug: reported E_BACKFILL_SIZE: ${report[0].message}`), r.logs.join('\n'));
    const applyDownloads = r.calls.filter((c) => c.cmd === 'gh' && c.argv[1] === 'download');
    assert.equal(applyDownloads.filter((c) => c.argv[2] === tagOf('Alphaplug')).length, 0);
    assert.equal(applyDownloads.length, 24, 'three assets for each of the eight other releases');
  });

  await t.test('a digest mismatch is a download-report E_BACKFILL_DIGEST entry', (t) => {
    const f = freshWorld(t, {
      mutate: (state) => {
        const a = releaseOf(state, 'Etaplug').assets.find((x) => x.name === tarOf('Etaplug'));
        a.digest = `sha256:${'0'.repeat(64)}`;
      },
    });
    assert.equal(f.plan().code, 0);
    const tarRun = f.download('tar.gz');
    assert.equal(tarRun.code, 0, tarRun.stderr);
    const report = f.report(path.join(f.p.dl['tar.gz'], 'download-report.json'));
    assert.deepEqual(
      report.map((e) => [e.tag, e.code]),
      [[tagOf('Etaplug'), 'E_BACKFILL_DIGEST']],
    );
    assert.ok(report[0].message.includes(tarOf('Etaplug')), report[0].message);

    assert.equal(f.download('pkg').code, 0);
    assert.equal(f.facts('darwin').code, 0);
    assert.equal(f.facts('linux').code, 0);
    assert.deepEqual(f.report(path.join(f.p.facts, 'facts-report.linux.json')), report);
    assertApplyReports(f, { Etaplug: 'E_BACKFILL_DIGEST' });
  });

  await t.test('an asset GitHub lists without a digest is refused with E_BACKFILL_DIGEST, never unpacked or signed', (t) => {
    const f = freshWorld(t, {
      mutate: (state) => {
        const a = releaseOf(state, 'Etaplug').assets.find((x) => x.name === tarOf('Etaplug'));
        a.digest = null;
      },
    });
    assert.equal(f.plan().code, 0);
    const tarRun = f.download('tar.gz');
    assert.equal(tarRun.code, 0, tarRun.stderr);
    assert.equal(tarRun.logs[tarRun.logs.length - 1], 'digest checked: 8 of 9');
    const report = f.report(path.join(f.p.dl['tar.gz'], 'download-report.json'));
    assert.deepEqual(
      report.map((e) => [e.tag, e.code]),
      [[tagOf('Etaplug'), 'E_BACKFILL_DIGEST']],
    );
    assert.ok(report[0].message.includes(tarOf('Etaplug')), report[0].message);
    assert.ok(report[0].message.includes('no digest'), report[0].message);

    assert.equal(f.download('pkg').code, 0);
    assert.equal(f.facts('darwin').code, 0);
    assert.equal(f.facts('linux').code, 0);
    assert.deepEqual(f.report(path.join(f.p.facts, 'facts-report.linux.json')), report, 'the tarball is never extracted');
    assertApplyReports(f, { Etaplug: 'E_BACKFILL_DIGEST' });
  });

  await t.test('a digest not of the form sha256:<64 hex> is E_BACKFILL_DIGEST', (t) => {
    const f = freshWorld(t, {
      mutate: (state) => {
        const a = releaseOf(state, 'Betaplug').assets.find((x) => x.name === pkgOf('Betaplug'));
        a.digest = 'md5:0123';
      },
    });
    assert.equal(f.plan().code, 0);
    assert.equal(f.download('pkg').code, 0);
    const report = f.report(path.join(f.p.dl.pkg, 'download-report.json'));
    assert.deepEqual(
      report.map((e) => [e.tag, e.code]),
      [[tagOf('Betaplug'), 'E_BACKFILL_DIGEST']],
    );
  });

  await t.test('a tarball without README.txt is an E_LAYOUT facts-report entry and no linux facts file', (t) => {
    const f = freshWorld(t, {
      mutate: (state, ctx) =>
        ctx.setServed(
          tagOf('Thetaplug'),
          tarOf('Thetaplug'),
          fakeGh.c4Tarball('Thetaplug', CURRENT.Thetaplug, { omit: ['README.txt'] }),
        ),
    });
    flowToFacts(f);
    assertFactsReported(f, 'linux', 'Thetaplug', 'E_LAYOUT');
    assert.deepEqual(f.report(path.join(f.p.facts, 'facts-report.darwin.json')), []);
    assertApplyReports(f, { Thetaplug: 'E_LAYOUT' });
  });

  await t.test('two Mach-Os with different architecture sets are E_FACTS_ARCH_MISMATCH', (t) => {
    const f = freshWorld(t, {
      hooks: {
        machos: (asset, defaults) =>
          asset === pkgOf('Gammaplug') ? [defaults[0], { ...defaults[1], ...fakeGh.arm64Only() }] : defaults,
      },
    });
    flowToFacts(f);
    assertFactsReported(f, 'darwin', 'Gammaplug', 'E_FACTS_ARCH_MISMATCH');
    assertApplyReports(f, { Gammaplug: 'E_FACTS_ARCH_MISMATCH' });
  });

  await t.test('a pkg whose expansion holds no Mach-O is E_FACTS_NO_MACHO', (t) => {
    const f = freshWorld(t, { hooks: { machos: (asset, defaults) => (asset === pkgOf('Alphaplug') ? [] : defaults) } });
    flowToFacts(f);
    assertFactsReported(f, 'darwin', 'Alphaplug', 'E_FACTS_NO_MACHO');
    assertApplyReports(f, { Alphaplug: 'E_FACTS_NO_MACHO' });
  });

  await t.test('lipo -archs disagreeing with the Node set is E_FACTS_LIPO_MISMATCH naming the file', (t) => {
    const f = freshWorld(t, {
      hooks: { lipo: (file, words) => (file.includes(dashed(tagOf('Deltaplug'))) ? 'arm64' : words) },
    });
    flowToFacts(f);
    assertFactsReported(f, 'darwin', 'Deltaplug', 'E_FACTS_LIPO_MISMATCH');
    const [entry] = f.report(path.join(f.p.facts, 'facts-report.darwin.json'));
    assert.ok(entry.message.includes('Contents/MacOS/Deltaplug'), entry.message);
    assertApplyReports(f, { Deltaplug: 'E_FACTS_LIPO_MISMATCH' });
  });

  await t.test('a facts dir missing one .darwin.json with no report entry is E_FACTS_MISSING', (t) => {
    const f = freshWorld(t);
    flowToFacts(f);
    const missing = `Betaplug-${CURRENT.Betaplug}.darwin.json`;
    fs.rmSync(path.join(f.p.facts, missing));
    const r = assertApplyReports(f, { Betaplug: 'E_FACTS_MISSING' });
    const line = r.logs.find((l) => l.startsWith('Betaplug: '));
    assert.ok(line.includes(missing), line);
  });

  await t.test('a tag whose version.json carries another version is E_VERSION_MISMATCH', (t) => {
    const f = freshWorld(t, {
      hooks: {
        gitShow: (tag, rel, text) =>
          tag === tagOf('Deltaplug') && rel.endsWith('/version.json') ? text.replace(CURRENT.Deltaplug, '9.9.9') : text,
      },
    });
    flowToFacts(f);
    const r = assertApplyReports(f, { Deltaplug: 'E_VERSION_MISMATCH' });
    assert.equal(r.calls.filter((c) => c.cmd === 'sign' && mentions(c, dashed(tagOf('Deltaplug')))).length, 0);
  });

  await t.test('a missing facts-report.darwin.json aborts apply with E_INPUT and 0 uploads', (t) => {
    const f = freshWorld(t);
    flowToFacts(f);
    fs.rmSync(path.join(f.p.facts, 'facts-report.darwin.json'));
    const r = f.apply(armSigner(f));
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('backfill-sign: E_INPUT: '), r.stderr);
    assert.ok(r.stderr.includes('facts-report.darwin.json'), r.stderr);
    assert.equal(r.calls.filter(isUpload).length, 0);
    assert.equal(r.calls.length, 0, 'nothing downloaded, signed or viewed');
  });

  await t.test('a signer whose key differs from --public-key aborts with E_SIGN_KEY_MISMATCH and 0 uploads', (t) => {
    const f = freshWorld(t);
    flowToFacts(f);
    const other = fakeGh.throwawaySigner();
    const signer = armSigner(f, { publicKeyText: other.publicKeyText });
    const r = f.apply(signer);
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('backfill-sign: E_SIGN_KEY_MISMATCH: '), r.stderr);
    assert.equal(f.w.calls.filter(isUpload).length, 0, '0 uploads in the whole run');
    assert.equal(r.calls.filter((c) => c.cmd === 'sign').length, 1, 'the first signature aborts the run at once');
    assert.equal(fs.existsSync(signer.keyFile), false, 'an abort inside pass 1 removes the key too (PD-20)');
  });

  await t.test('a post-run asset list gaining a name apply did not upload is E_BACKFILL_ASSETS_CHANGED', (t) => {
    const stray = 'Thetaplug-notes.txt';
    const f = freshWorld(t, {
      hooks: {
        // Only the audit asks for assets alone; plan's view (tagName, publishedAt, ...) is untouched.
        view: (tag, doc) =>
          tag === tagOf('Thetaplug') && !('tagName' in doc)
            ? { assets: [...doc.assets, { name: stray, size: 1, digest: null }] }
            : doc,
      },
    });
    flowToFacts(f);
    const r = f.apply(armSigner(f));
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('backfill-sign: E_BACKFILL_ASSETS_CHANGED: '), r.stderr);
    assert.ok(r.stderr.includes(tagOf('Thetaplug')) && r.stderr.includes(stray), r.stderr);
    assert.equal(r.logs[r.logs.length - 1], 'uploads: 18', 'the summary is still printed');
  });

  await t.test('a release list of 1000 rows aborts plan with E_BACKFILL_LIST_TRUNCATED and no plan file', (t) => {
    const filler = (i) => ({
      tagName: `fillerplug/v0.0.${i}`,
      isPrerelease: false,
      isDraft: false,
      publishedAt: '2026-03-01T00:00:00Z',
    });
    const f = freshWorld(t, {
      hooks: { releaseList: (rows) => [...rows, ...Array.from({ length: 1000 - rows.length }, (_, i) => filler(i))] },
    });
    const r = f.plan();
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('backfill-sign: E_BACKFILL_LIST_TRUNCATED: '), r.stderr);
    assert.equal(fs.existsSync(f.p.plan), false);
    assert.equal(f.w.calls.filter((c) => c.cmd === 'gh' && c.argv[1] === 'view').length, 0);
  });

  await t.test('(a) a plugin with only a prerelease and a draft is skipped-no-stable', (t) => {
    const f = freshWorld(t);
    const plugins = path.join(f.tmp, 'plugins');
    fs.cpSync(fakeGh.PLUGINS_DIR, plugins, { recursive: true });
    fs.cpSync(path.join(fakeGh.PLUGINS_EXTRA_DIR, 'Kappaplug'), path.join(plugins, 'Kappaplug'), { recursive: true });
    const r = f.plan(plugins);
    assert.equal(r.code, 0, r.stderr);
    const planDoc = readJson(f.p.plan);
    assert.deepEqual(
      planDoc.releases.map((e) => e.name),
      [...NAMES, 'Kappaplug'].sort(),
    );
    assert.deepEqual(entryOf(planDoc, 'Kappaplug'), { name: 'Kappaplug', dir: 'Kappaplug', status: 'skipped-no-stable' });
    assert.equal(planDoc.releases.filter((e) => e.status === 'planned').length, 9);
    const views = f.w.calls.filter((c) => c.cmd === 'gh' && c.argv[1] === 'view');
    assert.equal(views.filter((c) => c.argv[2].startsWith('kappaplug/')).length, 0);

    for (const step of [f.download('pkg'), f.download('tar.gz'), f.facts('darwin'), f.facts('linux')]) {
      assert.equal(step.code, 0, step.stderr);
    }
    const applied = assertApplyReports(f, {}, { code: 0, pluginsDir: plugins });
    assert.ok(applied.logs.includes('Kappaplug: skipped (no stable release)'), applied.logs.join('\n'));
    assert.equal(applied.calls.filter((c) => c.argv.some((a) => a.startsWith('kappaplug/'))).length, 0);
  });

  await t.test('(c) a failed release.json upload is reported E_INPUT; the audit is clean; plan then says PARTIAL', (t) => {
    const victim = tagOf('Epsilonplug');
    const f = freshWorld(t, {
      hooks: { upload: (tag, names) => (tag === victim && names.includes('release.json') ? { status: 1 } : undefined) },
    });
    flowToFacts(f);
    const planDoc = readJson(f.p.plan);
    const r = f.apply(armSigner(f));
    assert.equal(r.code, 3, r.stderr);
    const line = r.logs.find((l) => l.startsWith('Epsilonplug: '));
    assert.ok(line.startsWith(`Epsilonplug: reported E_INPUT: gh release upload ${victim}`), line);
    for (const name of NAMES.filter((n) => n !== 'Epsilonplug')) assert.ok(r.logs.includes(`${name}: signed`), name);
    assert.equal(r.logs[r.logs.length - 1], 'uploads: 17', 'eight releases x 2 calls + the victim first call');
    const mine = r.calls.filter((c) => isUpload(c) && c.argv[2] === victim);
    assert.deepEqual(
      mine.map((c) => uploadNames(c)[0]),
      ['SHA256SUMS', 'release.json'],
      'the first call made, the release.json call failed',
    );
    // The audit expected assets_before plus the four names of the first call, and found them.
    assert.deepEqual(
      releaseOf(f.w.state, 'Epsilonplug')
        .assets.map((a) => a.name)
        .sort(),
      [...entryOf(planDoc, 'Epsilonplug').assets_before, 'SHA256SUMS', ...minisigsOf('Epsilonplug')].sort(),
    );
    assert.equal(r.calls.filter(isAuditView).length, 9);

    assert.equal(f.plan().code, 0);
    assert.equal(entryOf(readJson(f.p.plan), 'Epsilonplug').code, 'E_BACKFILL_PARTIAL');
  });

  await t.test('(d) --public-key a copy of the KAT public key aborts apply with E_SIGNING_CONFIG', (t) => {
    const f = freshWorld(t);
    flowToFacts(f);
    const kat = fs.readFileSync(path.join(__dirname, '..', 'fixtures', 'kat', 'kat.pub'), 'utf8');
    const r = f.apply(armSigner(f, { publicKeyText: kat }));
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('backfill-sign: E_SIGNING_CONFIG: '), r.stderr);
    assert.equal(r.calls.filter(isUpload).length, 0);
    assert.equal(r.calls.filter((c) => c.cmd === 'sign').length, 0);
  });

  await t.test('(b) a stable tag <name>/v1.2 aborts plan with E_TAG_FORMAT and no plan file', (t) => {
    const f = freshWorld(t, {
      mutate: (state) =>
        state.releases.push({
          tagName: 'betaplug/v1.2',
          isPrerelease: false,
          isDraft: false,
          isImmutable: false,
          publishedAt: '2026-03-02T00:00:00Z',
          assets: [],
        }),
    });
    const r = f.plan();
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('backfill-sign: E_TAG_FORMAT: '), r.stderr);
    assert.equal(fs.existsSync(f.p.plan), false);
  });

  await t.test('(e) unsafe tarballs are reported before any tar or objdump runs on them', (t) => {
    const bad = { Epsilonplug: 'dotdot-path', Zetaplug: 'symlink-escape-root' };
    const f = freshWorld(t, {
      mutate: (state, ctx) => {
        for (const [name, fixture] of Object.entries(bad)) {
          ctx.setServed(tagOf(name), tarOf(name), fs.readFileSync(path.join(BAD_DIR, `${fixture}.tar.gz`)));
        }
      },
    });
    flowToFacts(f);
    const report = f.report(path.join(f.p.facts, 'facts-report.linux.json'));
    assert.equal(report.length, 2, JSON.stringify(report));
    for (const [name, fixture] of Object.entries(bad)) {
      const expected = fs.readFileSync(path.join(BAD_DIR, `${fixture}.expected-error`), 'utf8').trim();
      assert.deepEqual(
        byTag(report, tagOf(name)).map((e) => e.code),
        [expected],
        name,
      );
      assert.match(expected, /^E_LISTING_/);
      for (const cmd of ['tar', 'objdump']) {
        assert.equal(execCalls(f.w, cmd).filter((c) => mentions(c, dashed(tagOf(name)))).length, 0, `${name} ${cmd}`);
      }
      assert.equal(fs.existsSync(path.join(f.p.facts, `${name}-${CURRENT[name]}.linux.json`)), false);
    }
    assert.equal(execCalls(f.w, 'tar').length, 7);
    assert.equal(execCalls(f.w, 'objdump').length, 7);
    assertExecArgvArrays(f.w);
  });

  await t.test('facts without a download-report.json aborts with E_INPUT', (t) => {
    const f = freshWorld(t);
    assert.equal(f.plan().code, 0);
    const r = f.facts('darwin');
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('backfill-sign: E_INPUT: '), r.stderr);
    assert.equal(execCalls(f.w, 'pkgutil').length, 0);
  });

  await t.test('usage: no subcommand, an unknown flag, a bad --format exit 2 with the usage line', (t) => {
    const f = freshWorld(t);
    for (const argv of [
      [],
      ['plan', '--plugins-dir', fakeGh.PLUGINS_DIR, '--repo', REPO],
      ['download', '--plan', f.p.plan, '--format', 'zip', '--dir', f.tmp, '--repo', REPO],
      ['facts', '--plan', f.p.plan, '--os', 'windows', '--dir', f.tmp, '--work', f.tmp, '--out', f.tmp],
      ['plan', '--plugins-dir', fakeGh.PLUGINS_DIR, '--repo', REPO, '--out', f.p.plan, '--out', f.p.plan],
      ['apply', '--plan', f.p.plan, '--repo', REPO],
      ['apply', '--dry-run', 'yes', '--plan', f.p.plan],
    ]) {
      const r = run(argv, f.w.deps);
      assert.equal(r.code, 2, JSON.stringify(argv));
      assert.ok(r.stderr.split('\n').some((l) => l.startsWith('usage: node tools/backfill-sign.js')), r.stderr);
    }
    assert.equal(f.w.calls.length, 0);
  });
});
