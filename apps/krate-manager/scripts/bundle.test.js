// node:test coverage of bundle.js's path logic (spec FR-030 to FR-032, FR-092, FR-109; SC-008;
// plan §4.6, §4.8, PD-15, PD-20, PD-22). Pure functions only: no process is spawned, `main` never runs.
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  assetTable,
  extraConfigs,
  parseArgs,
  releaseTauriArgs,
  resolveAsset,
  sidecarName,
  tauriArgs,
  universalSidecars,
} from './bundle.js';

await test('bundle sidecar name per host tuple', () => {
  assert.equal(
    sidecarName('x86_64-pc-windows-msvc'),
    'krate-manager-helper-x86_64-pc-windows-msvc.exe',
  );
  assert.equal(sidecarName('aarch64-pc-windows-msvc'), 'krate-manager-helper-aarch64-pc-windows-msvc.exe');
  assert.equal(sidecarName('aarch64-apple-darwin'), 'krate-manager-helper-aarch64-apple-darwin');
  assert.equal(sidecarName('x86_64-apple-darwin'), 'krate-manager-helper-x86_64-apple-darwin');
  assert.equal(sidecarName('x86_64-unknown-linux-gnu'), 'krate-manager-helper-x86_64-unknown-linux-gnu');

  const withHelper = ['build', '--config', 'src-tauri/tauri.helper.conf.json'];
  assert.deepEqual(tauriArgs('win32'), withHelper);
  assert.deepEqual(tauriArgs('darwin'), withHelper);
  assert.deepEqual(tauriArgs('linux'), ['build']);
});

/**
 * Asserts that `parseArgs(argv, platform)` refuses with a usage error (exit code 2).
 * @param {string[]} argv
 * @param {string} platform
 */
function refuses(argv, platform) {
  assert.throws(
    () => parseArgs(argv, platform),
    (/** @type {unknown} */ e) => e instanceof Error && 'exitCode' in e && e.exitCode === 2,
    `${platform} ${JSON.stringify(argv)} must be a usage error`,
  );
}

await test('bundle release mode argv', () => {
  // parseArgs: accepted forms.
  for (const platform of ['win32', 'darwin', 'linux']) {
    assert.deepEqual(parseArgs([], platform), { release: false });
    assert.deepEqual(parseArgs(['--release'], platform), { release: true });
    assert.deepEqual(parseArgs(['--release', '--features', 'updater-e2e'], platform), {
      release: true,
      features: 'updater-e2e',
    });
  }
  assert.deepEqual(parseArgs(['--release', '--target', 'universal-apple-darwin'], 'darwin'), {
    release: true,
    target: 'universal-apple-darwin',
  });
  assert.deepEqual(parseArgs(['--release', '--bundles', 'appimage,deb'], 'linux'), {
    release: true,
    bundles: ['appimage', 'deb'],
  });
  assert.deepEqual(
    parseArgs(['--release', '--target', 'universal-apple-darwin', '--features', 'updater-e2e'], 'darwin'),
    { release: true, target: 'universal-apple-darwin', features: 'updater-e2e' },
  );

  // parseArgs: refusals (exit 2).
  refuses(['--bogus'], 'win32');
  refuses(['--release', '--bogus'], 'linux');
  refuses(['--release', '--release'], 'win32');
  refuses(['--release', '--target', 'universal-apple-darwin'], 'win32');
  refuses(['--release', '--target', 'universal-apple-darwin'], 'linux');
  refuses(['--release', '--target', 'x86_64-apple-darwin'], 'darwin');
  refuses(['--release', '--target'], 'darwin');
  refuses(['--release', '--bundles', 'appimage,deb'], 'darwin');
  refuses(['--release', '--bundles', 'appimage,deb'], 'win32');
  refuses(['--release', '--bundles', 'appimage,'], 'linux');
  refuses(['--release', '--features', 'other'], 'win32');
  refuses(['--release', '--features', 'updater-e2e,other'], 'linux');
  refuses(['--target', 'universal-apple-darwin'], 'darwin');
  refuses(['--bundles', 'appimage,deb'], 'linux');
  refuses(['--features', 'updater-e2e'], 'win32');

  // releaseTauriArgs per plan §4.8.
  const helper = ['--config', 'src-tauri/tauri.helper.conf.json'];
  const release = ['--config', 'src-tauri/tauri.release.conf.json'];
  assert.deepEqual(releaseTauriArgs('win32', { release: true }, []), ['build', ...helper, ...release]);
  assert.deepEqual(
    releaseTauriArgs('darwin', { release: true, target: 'universal-apple-darwin' }, []),
    ['build', ...helper, ...release, '--target', 'universal-apple-darwin'],
  );
  assert.deepEqual(releaseTauriArgs('linux', { release: true, bundles: ['appimage', 'deb'] }, []), [
    'build',
    ...release,
    '--bundles',
    'appimage,deb',
  ]);
  assert.deepEqual(
    releaseTauriArgs('win32', { release: true, features: 'updater-e2e' }, ['a.json', 'b.json']),
    ['build', ...helper, ...release, '--config', 'a.json', '--config', 'b.json', '--features', 'updater-e2e'],
  );
  assert.deepEqual(
    releaseTauriArgs(
      'darwin',
      { release: true, target: 'universal-apple-darwin', features: 'updater-e2e' },
      ['e2e.json'],
    ),
    [
      'build',
      ...helper,
      ...release,
      '--target',
      'universal-apple-darwin',
      '--config',
      'e2e.json',
      '--features',
      'updater-e2e',
    ],
  );
  assert.deepEqual(
    releaseTauriArgs('linux', { release: true, bundles: ['appimage', 'deb'], features: 'updater-e2e' }, ['e.json']),
    ['build', ...release, '--bundles', 'appimage,deb', '--config', 'e.json', '--features', 'updater-e2e'],
  );

  // extraConfigs: KRATE_BUNDLE_EXTRA_CONFIG split on path.delimiter, each an existing file.
  assert.deepEqual(extraConfigs({}), []);
  assert.deepEqual(extraConfigs({ KRATE_BUNDLE_EXTRA_CONFIG: '' }), []);
  const dir = mkdtempSync(path.join(os.tmpdir(), 'km-bundle-'));
  try {
    const a = path.join(dir, 'a.json');
    const b = path.join(dir, 'b.json');
    writeFileSync(a, '{}');
    writeFileSync(b, '{}');
    assert.deepEqual(extraConfigs({ KRATE_BUNDLE_EXTRA_CONFIG: a }), [a]);
    assert.deepEqual(extraConfigs({ KRATE_BUNDLE_EXTRA_CONFIG: [a, b].join(path.delimiter) }), [a, b]);
    const missing = path.join(dir, 'missing.json');
    assert.throws(() => extraConfigs({ KRATE_BUNDLE_EXTRA_CONFIG: missing }), /missing\.json/);
    const withMissing = [a, missing].join(path.delimiter);
    assert.throws(() => extraConfigs({ KRATE_BUNDLE_EXTRA_CONFIG: withMissing }), /missing\.json/);
    assert.throws(() => extraConfigs({ KRATE_BUNDLE_EXTRA_CONFIG: dir }), /is not a file/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }

  // universalSidecars: the three names of D13.
  assert.deepEqual(universalSidecars(), [
    'krate-manager-helper-aarch64-apple-darwin',
    'krate-manager-helper-x86_64-apple-darwin',
    'krate-manager-helper-universal-apple-darwin',
  ]);

  // tauriArgs (the no-argument path) is unchanged.
  assert.deepEqual(tauriArgs('win32'), ['build', '--config', 'src-tauri/tauri.helper.conf.json']);
  assert.deepEqual(tauriArgs('linux'), ['build']);
});

await test('bundle release asset table', () => {
  // PD-15 dirs and globs, D6 names (spec.md D6 as amended by SI-21: no `.deb.sig` row).
  assert.deepEqual(assetTable('win32', '0.2.0', { release: true }), [
    {
      dir: 'target/release/bundle/nsis',
      glob: '*_0.2.0_x64-setup.exe',
      name: 'Krate-Manager-0.2.0-Windows-x64-setup.exe',
    },
    {
      dir: 'target/release/bundle/nsis',
      glob: '*_0.2.0_x64-setup.exe.sig',
      name: 'Krate-Manager-0.2.0-Windows-x64-setup.exe.sig',
    },
  ]);
  const macos = 'target/universal-apple-darwin/release/bundle/macos';
  assert.deepEqual(assetTable('darwin', '0.2.0', { release: true, target: 'universal-apple-darwin' }), [
    { dir: macos, glob: 'Krate Manager.app.tar.gz', name: 'Krate-Manager-0.2.0-macOS-universal.app.tar.gz' },
    {
      dir: macos,
      glob: 'Krate Manager.app.tar.gz.sig',
      name: 'Krate-Manager-0.2.0-macOS-universal.app.tar.gz.sig',
    },
  ]);
  const linux = [
    {
      dir: 'target/release/bundle/appimage',
      glob: '*_0.2.0_amd64.AppImage',
      name: 'Krate-Manager-0.2.0-Linux-x86_64.AppImage',
    },
    {
      dir: 'target/release/bundle/appimage',
      glob: '*_0.2.0_amd64.AppImage.sig',
      name: 'Krate-Manager-0.2.0-Linux-x86_64.AppImage.sig',
    },
    { dir: 'target/release/bundle/deb', glob: '*_0.2.0_amd64.deb', name: 'Krate-Manager-0.2.0-Linux-amd64.deb' },
  ];
  assert.deepEqual(assetTable('linux', '0.2.0', { release: true, bundles: ['appimage', 'deb'] }), linux);
  assert.deepEqual(assetTable('linux', '0.2.0', { release: true }), linux);
  assert.deepEqual(assetTable('linux', '0.2.0', { release: true, bundles: ['deb'] }), linux.slice(2));
  assert.deepEqual(assetTable('linux', '0.2.0', { release: true, bundles: ['appimage'] }), linux.slice(0, 2));
  for (const platform of ['win32', 'darwin', 'linux']) {
    for (const row of assetTable(platform, '0.2.0', { release: true })) {
      assert.ok(!row.name.endsWith('.deb.sig'), `${platform}: no .deb.sig row (SI-21)`);
      assert.ok(!row.name.includes(' '), `${platform}: ${row.name} carries no space (D6)`);
    }
  }

  // resolveAsset: exactly one match per glob, else E_BUNDLE_ASSET naming the glob and the count (FR-032).
  const nsis = ['Krate Manager_0.2.0_x64-setup.exe', 'Krate Manager_0.2.0_x64-setup.exe.sig'];
  assert.equal(resolveAsset(nsis, '*_0.2.0_x64-setup.exe'), 'Krate Manager_0.2.0_x64-setup.exe');
  assert.equal(resolveAsset(nsis, '*_0.2.0_x64-setup.exe.sig'), 'Krate Manager_0.2.0_x64-setup.exe.sig');
  assert.throws(() => resolveAsset([], '*_0.2.0_x64-setup.exe'), {
    message: 'E_BUNDLE_ASSET *_0.2.0_x64-setup.exe matched 0',
  });
  assert.throws(() => resolveAsset(['Krate Manager_0.2.1_x64-setup.exe'], '*_0.2.0_x64-setup.exe'), {
    message: 'E_BUNDLE_ASSET *_0.2.0_x64-setup.exe matched 0',
  });
  assert.throws(() => resolveAsset([...nsis, 'Old_0.2.0_x64-setup.exe'], '*_0.2.0_x64-setup.exe'), {
    message: 'E_BUNDLE_ASSET *_0.2.0_x64-setup.exe matched 2',
  });
  // The version's dots are literal, not regex wildcards.
  assert.throws(() => resolveAsset(['Krate Manager_0x2x0_x64-setup.exe'], '*_0.2.0_x64-setup.exe'), {
    message: 'E_BUNDLE_ASSET *_0.2.0_x64-setup.exe matched 0',
  });

  const mac = ['Krate Manager.app', 'Krate Manager.app.tar.gz', 'Krate Manager.app.tar.gz.sig'];
  assert.equal(resolveAsset(mac, 'Krate Manager.app.tar.gz'), 'Krate Manager.app.tar.gz');
  assert.equal(resolveAsset(mac, 'Krate Manager.app.tar.gz.sig'), 'Krate Manager.app.tar.gz.sig');
  assert.throws(() => resolveAsset(['Krate Manager.app'], 'Krate Manager.app.tar.gz'), {
    message: 'E_BUNDLE_ASSET Krate Manager.app.tar.gz matched 0',
  });

  const appimage = ['Krate Manager_0.2.0_amd64.AppImage', 'Krate Manager_0.2.0_amd64.AppImage.sig'];
  assert.equal(resolveAsset(appimage, '*_0.2.0_amd64.AppImage'), 'Krate Manager_0.2.0_amd64.AppImage');
  assert.throws(() => resolveAsset([], '*_0.2.0_amd64.AppImage.sig'), {
    message: 'E_BUNDLE_ASSET *_0.2.0_amd64.AppImage.sig matched 0',
  });
  assert.throws(() => resolveAsset([...appimage, 'Other_0.2.0_amd64.AppImage'], '*_0.2.0_amd64.AppImage'), {
    message: 'E_BUNDLE_ASSET *_0.2.0_amd64.AppImage matched 2',
  });
  const deb = ['Krate Manager_0.2.0_amd64.deb', 'Krate Manager_0.2.0_amd64.deb.sig'];
  assert.equal(resolveAsset(deb, '*_0.2.0_amd64.deb'), 'Krate Manager_0.2.0_amd64.deb');
  assert.throws(() => resolveAsset([...deb, 'x_0.2.0_amd64.deb'], '*_0.2.0_amd64.deb'), {
    message: 'E_BUNDLE_ASSET *_0.2.0_amd64.deb matched 2',
  });
});
