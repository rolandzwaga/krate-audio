#!/usr/bin/env node
// gen-screenshots.js — regenerate a plugin's manual screenshots (plugins/<plugin>/images/*.png).
//
// Builds the plugin's test executable, then runs its hidden [screenshots] case, which builds the editor headlessly,
// loads factory presets and renders each shot offscreen at 2x (plugins/profundum/tests/unit/controller/
// editor_screenshot_test.cpp holds the shot list). The PNGs are committed; MANUAL.md links them as images/<file>.
// Rerun after any editor change. Rendered on Windows (Direct2D) so the shots match what most users see; on Linux,
// install fonts-liberation first (the uidesc asks for Arial).
//
// Usage:  node tools/gen-screenshots.js profundum              # build + render
//         node tools/gen-screenshots.js profundum --no-build   # render with the existing build
//         node tools/gen-screenshots.js profundum --build-dir build/linux-release
'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const REPO = path.resolve(__dirname, '..');
const TIMEOUT_MS = 120000;   // the case renders 16 shots in ~2 s; a hang is a defect, never a wait

// Plugins whose test executable carries a [screenshots] case, and the env var naming its output directory.
const PLUGINS = {
  profundum: { target: 'profundum_tests', envDir: 'PROFUNDUM_SHOTS_DIR' },
};

function fail(msg) {
  console.error(`gen-screenshots: ${msg}`);
  process.exit(1);
}

const args = process.argv.slice(2);
const plugin = args.find((a) => !a.startsWith('--'));
if (!plugin || !PLUGINS[plugin]) {
  fail(`usage: node tools/gen-screenshots.js <${Object.keys(PLUGINS).join('|')}> [--no-build] [--build-dir <dir>]`);
}
const { target, envDir } = PLUGINS[plugin];
const buildDirArg = args.indexOf('--build-dir');
const defaultBuild = process.platform === 'win32' ? 'build/windows-x64-release'
  : process.platform === 'darwin' ? 'build/macos-release' : 'build/linux-release';
const buildDir = path.resolve(REPO, buildDirArg >= 0 ? args[buildDirArg + 1] : defaultBuild);
const cmake = process.platform === 'win32' ? 'C:/Program Files/CMake/bin/cmake.exe' : 'cmake';

if (!args.includes('--no-build')) {
  console.log(`build ${target} in ${path.relative(REPO, buildDir)} ...`);
  const build = spawnSync(cmake, ['--build', buildDir, '--config', 'Release', '--target', target],
    { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  if (build.status !== 0) {
    process.stdout.write(build.stdout || '');
    process.stderr.write(build.stderr || '');
    fail(`build of ${target} failed (exit ${build.status})`);
  }
}

const exeName = process.platform === 'win32' ? `${target}.exe` : target;
const exe = [path.join(buildDir, 'bin', 'Release', exeName), path.join(buildDir, 'bin', exeName)]
  .find((p) => fs.existsSync(p));
if (!exe) fail(`no ${exeName} under ${buildDir}/bin`);

const outDir = path.join(REPO, 'plugins', plugin, 'images');
console.log(`render [screenshots] -> ${path.relative(REPO, outDir)}`);
const run = spawnSync(exe, ['[screenshots]'], {
  encoding: 'utf8',
  timeout: TIMEOUT_MS,
  env: { ...process.env, [envDir]: outDir },
  maxBuffer: 16 * 1024 * 1024,
});
if (run.error && run.error.code === 'ETIMEDOUT') fail(`render timed out after ${TIMEOUT_MS / 1000} s (hung)`);
const out = `${run.stdout || ''}${run.stderr || ''}`;
for (const line of out.split(/\r?\n/)) {
  if (/\.png:|shots written|FAILED|failed|passed/.test(line)) console.log(`  ${line.trim()}`);
}
if (run.status !== 0) {
  console.log(out);
  fail(`render failed (exit ${run.status})`);
}
