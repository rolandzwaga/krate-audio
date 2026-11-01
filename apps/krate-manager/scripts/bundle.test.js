// node:test coverage of bundle.js's path logic (spec FR-092, FR-109; plan §4.6, PD-20).
import assert from 'node:assert/strict';
import test from 'node:test';

import { sidecarName, tauriArgs } from './bundle.js';

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
