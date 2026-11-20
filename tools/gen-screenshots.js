#!/usr/bin/env node
// gen-screenshots.js — regenerate a plugin's site and manual screenshots (plugins/<plugin>/images/*.png).
//
// Builds the plugin's test executable, then runs its hidden `[site]` screenshot case, which builds the editor
// headlessly and renders each shot offscreen (the case holds the shot list; shared code in
// tests/test_helpers/editor_screenshot.h). Every plugin's case writes hero.png, og-image.png and card.png (the
// krateaudio.com images tools/gen-site.js uses) plus the images its MANUAL.md links as images/<file>. The PNGs are
// committed. Rerun after any editor change. Rendered on Windows (Direct2D) so the shots match what most users see;
// on Linux, install fonts-liberation first (the uidesc asks for Arial).
//
// Into images/ (the default) the run renders to a temporary directory first and then makes images/ hold exactly that
// set, so a shot dropped from a case's list does not linger. With --out the plugin's every [screenshots] case runs
// (Iterum's ten per-mode review shots included) into that directory, unchanged otherwise: the review workflows
// (.claude/workflows/iterum-*.js) use it.
//
// Usage:  node tools/gen-screenshots.js profundum              # build + render into images/
//         node tools/gen-screenshots.js profundum --no-build   # render with the existing build
//         node tools/gen-screenshots.js profundum --build-dir build/linux-release
//         node tools/gen-screenshots.js iterum --out specs/iterum-ui-redesign/shots/before   # review shots elsewhere
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const REPO = path.resolve(__dirname, '..');
const TIMEOUT_MS = 180000;   // a case renders its shots in seconds; a hang is a defect, never a wait

// Each plugin's test executable and the env var naming its shots directory.
const PLUGINS = {
  disrumpo: { target: 'disrumpo_tests', envDir: 'DISRUMPO_SHOTS_DIR' },
  gradus: { target: 'gradus_tests', envDir: 'GRADUS_SHOTS_DIR' },
  innexus: { target: 'innexus_tests', envDir: 'INNEXUS_SHOTS_DIR' },
  iterum: { target: 'plugin_tests', envDir: 'ITERUM_SHOTS_DIR' },
  membrum: { target: 'membrum_tests', envDir: 'MEMBRUM_SHOTS_DIR' },
  profundum: { target: 'profundum_tests', envDir: 'PROFUNDUM_SHOTS_DIR' },
  ruinae: { target: 'ruinae_tests', envDir: 'RUINAE_SHOTS_DIR' },
  seraphis: { target: 'seraphis_tests', envDir: 'SERAPHIS_SHOTS_DIR' },
  vorago: { target: 'vorago_tests', envDir: 'VORAGO_SHOTS_DIR' },
};

function fail(msg) {
  console.error(`gen-screenshots: ${msg}`);
  process.exit(1);
}

const args = process.argv.slice(2);
const plugin = args.find((a, i) => !a.startsWith('--') && !['--build-dir', '--out'].includes(args[i - 1]));
if (!plugin || !PLUGINS[plugin]) {
  fail(`usage: node tools/gen-screenshots.js <${Object.keys(PLUGINS).join('|')}> [--no-build] [--build-dir <dir>] [--out <dir>]`);
}
const { target, envDir } = PLUGINS[plugin];
const buildDirArg = args.indexOf('--build-dir');
const defaultBuild = process.platform === 'win32' ? 'build/windows-x64-release'
  : process.platform === 'darwin' ? 'build/macos-release' : 'build/linux-release';
const buildDir = path.resolve(REPO, buildDirArg >= 0 ? args[buildDirArg + 1] : defaultBuild);
const cmake = process.platform === 'win32' ? 'C:/Program Files/CMake/bin/cmake.exe' : 'cmake';

if (!args.includes('--no-build')) {
  console.log(`build ${target} in ${path.relative(REPO, buildDir)} ...`);
  // MSBuild capped at two projects: an uncapped build swamps the machine (one target, so -m:2 costs little).
  const native = process.platform === 'win32' ? ['--', '-m:2', '-nr:false', '-v:minimal'] : [];
  const build = spawnSync(cmake, ['--build', buildDir, '--config', 'Release', '--target', target, ...native],
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

const outArg = args.indexOf('--out');
const imagesDir = path.join(REPO, 'plugins', plugin, 'images');
const outDir = outArg >= 0 ? path.resolve(REPO, args[outArg + 1]) : fs.mkdtempSync(path.join(os.tmpdir(), `${plugin}-shots-`));
const filter = outArg >= 0 ? '[screenshots]' : '[site]';
console.log(`render ${filter} -> ${outArg >= 0 ? path.relative(REPO, outDir) : path.relative(REPO, imagesDir)}`);
const run = spawnSync(exe, [filter], {
  encoding: 'utf8',
  timeout: TIMEOUT_MS,
  env: { ...process.env, [envDir]: outDir },
  maxBuffer: 16 * 1024 * 1024,
});
if (run.error && run.error.code === 'ETIMEDOUT') fail(`render timed out after ${TIMEOUT_MS / 1000} s (hung)`);
const out = `${run.stdout || ''}${run.stderr || ''}`;
for (const line of out.split(/\r?\n/)) {
  if (/\.png:|shots written|FAILED|failed|passed|No test cases matched/.test(line)) console.log(`  ${line.trim()}`);
}
if (run.status !== 0 || /No test cases matched/.test(out)) {
  console.log(out);
  fail(`render failed (exit ${run.status})`);
}

if (outArg < 0) {
  const shots = fs.readdirSync(outDir).filter((f) => f.endsWith('.png'));
  if (!['hero.png', 'og-image.png', 'card.png'].every((f) => shots.includes(f))) {
    fail(`the [site] case wrote no hero.png / og-image.png / card.png (got ${shots.join(', ')})`);
  }
  fs.mkdirSync(imagesDir, { recursive: true });
  for (const f of fs.readdirSync(imagesDir).filter((f) => f.endsWith('.png') && !shots.includes(f))) {
    fs.rmSync(path.join(imagesDir, f));
    console.log(`  removed ${f} (no longer in the shot list)`);
  }
  for (const f of shots) fs.copyFileSync(path.join(outDir, f), path.join(imagesDir, f));
  fs.rmSync(outDir, { recursive: true, force: true });
  console.log(`${shots.length} images in ${path.relative(REPO, imagesDir)}`);
}
