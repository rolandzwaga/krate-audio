# C3 known-answer fixture (KAT)

FR-023, SC-013 (creation half). One real minisign signature over one real C4-layout archive, made by the tool the probes
proved against a `tauri signer` key (`specs/krate-manager/probes/results.md`, "Are `tauri signer` keys
minisign-compatible?"). The C3 tests (`c3-trusted-comment.test.js`, `KAT binds and no secret key is committed`) and the CI
step `rsign verify (KAT)` use it as the known answer.

| File | What it is |
|---|---|
| `KatPlugin-1.2.3-Linux-x64.tar.gz` | GNU-format gzip tar in the C4 root layout (FR-033); 452 bytes, sha256 `5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890` |
| `KatPlugin-1.2.3-Linux-x64.tar.gz.minisig` | `rsign sign` output, stored with LF line endings and a final LF (D7) |
| `kat.pub` | the throwaway key pair's public key, key id `440FA75168B852F7` |

These files are byte-exact (`.gitattributes`: `tools/feed-contract/fixtures/** -text`). Never regenerate them: the
generator refuses to run when the archive or the `.minisig` exists.

## How it was made

- Date: 2026-10-30.
- OS: Windows 11 Pro 10.0.26200 (Node v24.7.0), archive built in WSL2 (`Linux 6.18.54.1-microsoft-standard-WSL2`) with
  `tar (GNU tar) 1.35`.
- Signer: `rsign --version` printed `rsign2 0.6.7` (`~/.cargo/bin/rsign`).
- Generator: `node specs/krate-manager-phase1/evidence/make-kat.js` (run once from the repo root). It prints every
  command below with its output.

### 1. Archive (WSL, from a `mktemp -d` tree in WSL `/tmp`, removed on exit)

The script feeds this to `wsl -e bash -s` on stdin (LF):

```bash
set -euo pipefail
umask 022
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
cd "$T"
mkdir -p KatPlugin.vst3/Contents/Resources KatPlugin.vst3/Contents/x86_64-linux presets
printf '%s\n' '{"Name": "KatPlugin", "Version": "1.2.3", "Classes": [{"Name": "KatPlugin", "Version": "1.2.3"}]}' > KatPlugin.vst3/Contents/Resources/moduleinfo.json
printf '%s\n' 'KatPlugin known-answer fixture, not a plug-in.' > KatPlugin.vst3/Contents/x86_64-linux/KatPlugin.so
printf '%s\n' 'KatPlugin default preset (fixture).' > presets/Default.preset
printf '%s\n' 'KatPlugin 1.2.3: C3 known-answer fixture.' > README.txt
printf '%s\n' '## [1.2.3] - 2026-10-30' '' '- Known-answer fixture.' > CHANGELOG.txt
find . -type d -exec chmod 0755 {} +
find . -type f -exec chmod 0644 {} +
tar --owner=0 --group=0 --numeric-owner --mtime='2026-10-29 00:00:00Z' --sort=name --format=gnu -cf - -C "$T" CHANGELOG.txt KatPlugin.vst3 README.txt presets | gzip -n -9 > '/mnt/f/projects/iterum/tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz'
tar --version | head -1
tar -tvzf '/mnt/f/projects/iterum/tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz'
```

`tar -tvzf` output (times shown in the box's local zone, CET; the stored mtime is `2026-10-29 00:00:00Z`):

```
-rw-r--r-- 0/0              49 2026-10-29 01:00 CHANGELOG.txt
drwxr-xr-x 0/0               0 2026-10-29 01:00 KatPlugin.vst3/
drwxr-xr-x 0/0               0 2026-10-29 01:00 KatPlugin.vst3/Contents/
drwxr-xr-x 0/0               0 2026-10-29 01:00 KatPlugin.vst3/Contents/Resources/
-rw-r--r-- 0/0              98 2026-10-29 01:00 KatPlugin.vst3/Contents/Resources/moduleinfo.json
drwxr-xr-x 0/0               0 2026-10-29 01:00 KatPlugin.vst3/Contents/x86_64-linux/
-rw-r--r-- 0/0              47 2026-10-29 01:00 KatPlugin.vst3/Contents/x86_64-linux/KatPlugin.so
-rw-r--r-- 0/0              42 2026-10-29 01:00 README.txt
drwxr-xr-x 0/0               0 2026-10-29 01:00 presets/
-rw-r--r-- 0/0              36 2026-10-29 01:00 presets/Default.preset
```

Files `0644` (the `.so` included) and directories `0755`, as the real tarball measured in the spec ("Real data").

### 2-5. Throwaway key, signature, public key

```
key = fs.mkdtempSync(path.join(os.tmpdir(), 'kat-'))        -> C:\Users\scab\AppData\Local\Temp\kat-cakxOE (outside the repo)
rsign generate -p <key>\kat.pub -s <key>\kat.key -W
rsign sign -s <key>\kat.key -W -t krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz F:\projects\iterum\tools\feed-contract\fixtures\kat\KatPlugin-1.2.3-Linux-x64.tar.gz
fs.copyFileSync(<key>\kat.pub, tools/feed-contract/fixtures/kat/kat.pub)
fs.rmSync(<key>, {recursive: true, force: true})            (in a finally block)
```

(`spawnSync` passes the trusted comment as one argument; it contains no space, so no quoting appears in the printed
command.) `rsign generate` printed the public key `RWT3UrhoUacPRCZIFM5RB85NymQ43PiBujVT5slm1tJ7s+Ce9pLUczoo`;
`rsign sign` printed nothing and exited 0.

The temporary key directory `C:\Users\scab\AppData\Local\Temp\kat-cakxOE` was deleted after signing (existsSync false).

### 6. CRLF -> LF normalisation (D7)

The `.minisig` is rewritten with `toLfMinisig` from `tools/feed-contract/contract.js` (CRLF -> LF; no signed byte
changes, because the signature covers the file bytes and the trusted-comment text, not the line endings). The spec's
External facts record that `rsign sign` wrote CRLF on Windows when measured on 2026-10-29. On this run the generator
read the file rsign wrote and found **LF only** (`.minisig as written by rsign: LF`), so the normalisation was applied and
changed nothing. The committed file has 0 CR bytes, 4 LF bytes and ends in LF; `kat.pub` has 0 CR bytes and 2 LF bytes.

`parseMinisigText` on the committed file: algorithm `ED`, key id `f752b86851a70f44` (the public key's
`440FA75168B852F7`, little-endian), trusted comment
`krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz`.

### 7. Verify (on the normalised file)

```
$ rsign verify -p tools/feed-contract/fixtures/kat/kat.pub tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz
Signature and comment signature verified
Trusted comment: krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz
exit 0
```

Before the fixture existed, the same command printed `The system cannot find the path specified. (os error 3)` and
exited 1.

### Run record

The first run built the archive and then stopped before key generation, because the "key dir is outside the repo"
check misread `path.relative` across drives (it returns an absolute path for `C:` vs `F:`). Its empty key directory
`C:\Users\scab\AppData\Local\Temp\kat-yw0nl1` and its archive were removed by hand, the check was fixed, and the second
run made every file above. No key was generated in the first run.
