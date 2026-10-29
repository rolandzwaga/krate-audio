#!/usr/bin/env node
// macOS Gatekeeper / quarantine probe for the Krate plugin manager research
// (specs/krate-manager/reports/Krate Audio plugin manager design.md, open-questions table).
// Runs headless on a GitHub-hosted macOS runner. Prints one RESULT line per question.
//
//   node macos-gatekeeper-probe.js <workdir> [<tauri-app-path>]
//
// Q1  Does a file downloaded by a non-browser client (curl / Node https, standing in
//     for reqwest) carry com.apple.quarantine?
// Q2  Does `sudo installer -pkg` install a QUARANTINED unsigned pkg (browser-download
//     stand-in), and does the payload inherit the quarantine attribute?
// Q3  Does spctl accept or reject the unsigned pkg and the installed plug-in bundle?
// Q4  What signature does the shipped plug-in binary carry (none / linker-signed / ad-hoc)?
// Q5  Does a zip extracted by ditto keep or drop quarantine (manager-extract path)?
// Q6  (with a Tauri app) Does the bundler write LSFileQuarantineEnabled, does the ad-hoc
//     signature verify, does the app launch unquarantined, and is it blocked quarantined?

const { execSync, spawnSync } = require('child_process');
const fs = require('fs');
const https = require('https');
const path = require('path');

const [work, tauriApp] = process.argv.slice(2);
if (!work) { console.error('usage: macos-gatekeeper-probe.js <workdir> [<tauri-app>]'); process.exit(2); }
fs.mkdirSync(work, { recursive: true });

const PKG_URL = 'https://github.com/rolandzwaga/krate-audio/releases/download/vorago/v1.0.1/Vorago-1.0.1-macOS.pkg';
const BUNDLE = '/Library/Audio/Plug-Ins/VST3/Vorago.vst3';
const COMPONENT = '/Library/Audio/Plug-Ins/Components/Vorago.component';
// Same shape Safari writes: flags;hex-timestamp;agent;UUID
const QUARANTINE = `0083;${Math.floor(Date.now() / 1000).toString(16)};Safari;`;

// Annotations are the only output readable without log access, and the first
// run hung with every result still buffered, so each one is emitted at once.
const OS = process.env.PROBE_OS || 'macos';
const CMD_TIMEOUT_MS = 90 * 1000;
function sh(cmd) {
  const t0 = Date.now();
  const r = spawnSync('/bin/bash', ['-c', cmd], {
    encoding: 'utf8', timeout: CMD_TIMEOUT_MS, killSignal: 'SIGKILL', stdio: ['ignore', 'pipe', 'pipe'],
  });
  const secs = ((Date.now() - t0) / 1000).toFixed(1);
  const out = `${r.stdout || ''}${r.stderr || ''}`.trim();
  const timedOut = r.error && r.error.code === 'ETIMEDOUT';
  console.log(`$ ${cmd}\n${out}\n[exit ${r.status}${timedOut ? ' TIMEOUT' : ''} ${secs}s]`);
  if (timedOut) console.log(`::warning title=${OS} TIMEOUT::${secs}s: ${cmd.slice(0, 200)}`);
  return { code: timedOut ? -1 : r.status, out };
}
function result(q, text) {
  console.log(`RESULT ${q}: ${text}`);
  console.log(`::notice title=${OS} ${q}::${text.replace(/[\r\n]+/g, ' ').slice(0, 900)}`);
}
function hasQuarantine(p) { return sh(`xattr -p com.apple.quarantine "${p}" 2>/dev/null`).code === 0; }

function nodeDownload(url, out) {
  return new Promise((resolve, reject) => {
    const get = (u, hops) => https.get(u, { headers: { 'User-Agent': 'krate-probe' }, timeout: 60000 }, (res) => {
      if ([301, 302, 303, 307, 308].includes(res.statusCode) && hops < 10) {
        res.resume();
        return get(new URL(res.headers.location, u).toString(), hops + 1);
      }
      if (res.statusCode !== 200) return reject(new Error(`HTTP ${res.statusCode}`));
      const f = fs.createWriteStream(out);
      res.pipe(f);
      f.on('finish', () => f.close(resolve));
    }).on('timeout', function onTimeout() { this.destroy(new Error(`download timeout ${u}`)); })
      .on('error', reject);
    get(url, 0);
  });
}

(async () => {
  const env = sh('sw_vers -productVersion; sw_vers -buildVersion; uname -m; spctl --status').out.replace(/\n/g, ' ');

  // Q1
  const pkgNode = path.join(work, 'node', 'Vorago.pkg');
  const pkgCurl = path.join(work, 'curl', 'Vorago.pkg');
  fs.mkdirSync(path.dirname(pkgNode), { recursive: true });
  fs.mkdirSync(path.dirname(pkgCurl), { recursive: true });
  await nodeDownload(PKG_URL, pkgNode);
  sh(`curl -sSL -o "${pkgCurl}" "${PKG_URL}"`);
  result('Q1', `[${env}] node-download quarantined=${hasQuarantine(pkgNode)} curl-download quarantined=${hasQuarantine(pkgCurl)}`);

  // Q3a: pkg assessment
  const pkgQ = path.join(work, 'quarantined', 'Vorago.pkg');
  fs.mkdirSync(path.dirname(pkgQ), { recursive: true });
  fs.copyFileSync(pkgNode, pkgQ);
  sh(`xattr -w com.apple.quarantine "${QUARANTINE}" "${pkgQ}"`);
  const assessPkg = sh(`spctl --assess --type install -vv "${pkgQ}"`);
  result('Q3-pkg', `spctl install-assess exit=${assessPkg.code} (${assessPkg.out.split('\n')[0]})`);

  // Q2: install the quarantined pkg from the CLI
  const inst = sh(`sudo installer -pkg "${pkgQ}" -target / -verboseR`);
  const installed = fs.existsSync(BUNDLE);
  result('Q2', `installer exit=${inst.code} bundle-installed=${installed}` +
    (installed ? ` vst3-quarantined=${hasQuarantine(BUNDLE)} component-quarantined=${hasQuarantine(COMPONENT)}` : ''));
  sh(`xattr -lr "${BUNDLE}" | head -20`);
  sh('pkgutil --pkgs | grep -i krate; pkgutil --pkg-info com.krateaudio.vorago');

  // Q4: signature on the shipped binary
  if (installed) {
    const bin = sh(`ls "${BUNDLE}/Contents/MacOS/"`).out.split('\n').pop();
    const binPath = `${BUNDLE}/Contents/MacOS/${bin}`;
    sh(`lipo -info "${binPath}"`);
    const cs = sh(`codesign -dv --verbose=4 "${binPath}" 2>&1`);
    const kind = /linker-signed/.test(cs.out) ? 'linker-signed'
      : /Signature=adhoc/.test(cs.out) ? 'ad-hoc'
      : /not signed/.test(cs.out) ? 'unsigned' : 'other';
    result('Q4', `plug-in binary signature=${kind}`);
    sh(`codesign --verify --deep --strict -v "${BUNDLE}" 2>&1`);

    // Q3b: bundle assessment, unquarantined vs quarantined
    sh(`sudo xattr -dr com.apple.quarantine "${BUNDLE}"`);
    const a1 = sh(`spctl --assess --type execute -vv "${BUNDLE}" 2>&1`);
    sh(`sudo xattr -w com.apple.quarantine "${QUARANTINE}" "${BUNDLE}"`);
    const a2 = sh(`spctl --assess --type execute -vv "${BUNDLE}" 2>&1`);
    result('Q3-bundle', `unquarantined exit=${a1.code} quarantined exit=${a2.code}`);
    sh(`sudo xattr -dr com.apple.quarantine "${BUNDLE}"`);

    // Q5: ditto zip round trip, clean vs quarantined archive
    const zipClean = path.join(work, 'zip', 'Vorago.vst3.zip');
    fs.mkdirSync(path.dirname(zipClean), { recursive: true });
    sh(`ditto -c -k --keepParent "${BUNDLE}" "${zipClean}"`);
    const outClean = path.join(work, 'zip', 'clean');
    sh(`ditto -x -k "${zipClean}" "${outClean}"`);
    const zipQ = path.join(work, 'zip', 'Vorago-q.vst3.zip');
    fs.copyFileSync(zipClean, zipQ);
    sh(`xattr -w com.apple.quarantine "${QUARANTINE}" "${zipQ}"`);
    const outQ = path.join(work, 'zip', 'from-quarantined');
    sh(`ditto -x -k "${zipQ}" "${outQ}"`);
    result('Q5', `extract-of-clean-zip quarantined=${hasQuarantine(path.join(outClean, 'Vorago.vst3'))} ` +
      `extract-of-quarantined-zip quarantined=${hasQuarantine(path.join(outQ, 'Vorago.vst3'))}`);
  }

  // Q6: Tauri app
  if (tauriApp && fs.existsSync(tauriApp)) {
    const plist = sh(`plutil -p "${tauriApp}/Contents/Info.plist"`);
    result('Q6-plist', `LSFileQuarantineEnabled present=${/LSFileQuarantineEnabled/.test(plist.out)}`);
    const v = sh(`codesign --verify --deep --strict -v "${tauriApp}" 2>&1`);
    const d = sh(`codesign -dv "${tauriApp}" 2>&1`);
    result('Q6-sign', `verify exit=${v.code} adhoc=${/Signature=adhoc/.test(d.out)}`);

    const exe = sh(`/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "${tauriApp}/Contents/Info.plist"`).out.split('\n').pop();
    // Run 1 launched the clean copy first (macOS may cache that approval) and
    // `open` blocked for 90 s. Now: quarantined copy first, every file
    // quarantined the way a browser-downloaded zip extracts (Q5), `open`
    // backgrounded so the probe never waits on it.
    const appQ = path.join(work, 'launch-q', 'krate-probe.app');
    const appC = path.join(work, 'launch-c', 'krate-probe.app');
    sh(`mkdir -p "${path.dirname(appQ)}" "${path.dirname(appC)}"; ditto "${tauriApp}" "${appQ}"; ditto "${tauriApp}" "${appC}"`);
    sh(`xattr -dr com.apple.quarantine "${appC}"`);
    sh(`xattr -rw com.apple.quarantine "${QUARANTINE}" "${appQ}"`);
    const launch = (app) => {
      sh(`(open -n "${app}" >/dev/null 2>&1 &); sleep 12`);
      const running = sh(`pgrep -f "${app}/Contents/MacOS/${exe}"`).code === 0;
      sh(`pkill -f "${app}/Contents/MacOS/${exe}"; sleep 2`);
      return running;
    };
    const aQ = sh(`spctl --assess --type execute -vv "${appQ}" 2>&1`);
    const runQ = launch(appQ);
    const qAfter = sh(`xattr -p com.apple.quarantine "${appQ}"`).out;
    const aC = sh(`spctl --assess --type execute -vv "${appC}" 2>&1`);
    const runC = launch(appC);
    result('Q6-launch', `quarantined(first) running=${runQ} spctl=${aQ.code} xattr-after="${qAfter}" | unquarantined running=${runC} spctl=${aC.code}`);
    const pol = sh(`log show --last 3m --predicate 'process == "syspolicyd" OR subsystem BEGINSWITH "com.apple.syspolicy"' --style compact | grep -iE "krate|probe" | tail -5`);
    result('Q6-syspolicyd', pol.out.slice(-800) || '(no matching lines)');
  } else {
    result('Q6', 'skipped (no Tauri app path given)');
  }
})().catch((e) => { console.error(e); process.exit(1); });
