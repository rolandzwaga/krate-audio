# C4 listing fixtures

Archives for the C4 contract (Linux tarball layout and canonical listing; spec FR-030 to FR-033, FR-058; plan §4.3,
§4.7, PD-12, PD-18). Read by `tests/c4-listing-tar.test.js` (`C4 listings from tar headers`, SC-008, every OS) and
`tests/c4-listing-dir.test.js` (`C4 listings from extracted trees`, SC-009, macOS and Linux). Phase 2 reuses
`listing.js` to compute `payload.list` / `payload.sha256` from a real tarball; Phases 5 and 8 inherit the rules.

Every fixture uses the bundle `Alpha.vst3`, so FR-033's `Alpha.vst3/Contents/x86_64-linux/Alpha.so` applies. The bad
archives are only ever parsed, never extracted. The files are byte-exact (`.gitattributes`:
`tools/feed-contract/fixtures/** -text`).

| Directory | Per fixture | Made by |
|---|---|---|
| `good/` | `<name>.tar.gz`, `<name>.listing` (`toText` of `listingFromTarGz`), `<name>.sha256` (`digest` + LF) | GNU tar in WSL, `specs/krate-manager-phase1/evidence/make-good-archives.js` |
| `bad/` | `<name>.tar.gz`, `<name>.expected-error` (the code + LF) | `make-bad.js` (20 malformed), `make-good-archives.js` (the 2 layout archives) |

Made 2026-10-30 on the Windows dev box (Node v24.7.0), GNU tar 1.35 in WSL (`wsl -e tar --version` →
`tar (GNU tar) 1.35`).

## Good set and the layout archives (GNU tar in WSL)

```
node specs/krate-manager-phase1/evidence/make-good-archives.js
```

For each archive the script builds the tree in `T=$(mktemp -d /tmp/krate-listing-XXXXXX)` inside WSL (never under
`/mnt/f`, which would strip modes and links), with `umask 022`, every directory `chmod 0755` and every file
`chmod 0644` (then the stated exceptions), and runs

```
tar --owner=0 --group=0 --numeric-owner --mtime='2026-10-29 00:00:00Z' --sort=name --format=gnu -cf - -C "$T" <items> \
  | gzip -n -9 > /mnt/f/projects/iterum/tools/feed-contract/fixtures/listings/<good|bad>/<name>.tar.gz
```

then `rm -rf "$T"` (a `trap ... EXIT`). Each script is fed to `wsl -e bash -s` on stdin instead of
`wsl -e bash -c '<script>'`, so its quotes and spaces never pass through Windows command-line quoting; bash runs the
same text. After building, the script writes `.listing` / `.sha256` with `listing.js` and prints `tar -tvzf` of the
archive. The base tree of every archive is `Alpha.vst3/Contents/Resources/moduleinfo.json`,
`Alpha.vst3/Contents/x86_64-linux/Alpha.so`, `presets/Init.vstpreset`, `README.txt`, `CHANGELOG.txt`; `<items>` is
`Alpha.vst3 CHANGELOG.txt README.txt presets` unless stated.

| Fixture | Dir | Tree beyond the base / flags | What it exercises | Lines | `listing.js` vs `tar -tvzf` |
|---|---|---|---|---|---|
| `real-shaped` | good | `Resources/editor.uidesc`; `presets/Warm Pads/Slow Swell.vstpreset`, `.../Night Air.vstpreset` | FR-033 layout, preset names with spaces, files 0644 / dirs 0755 as the real tarball | 14 | agree: 14 entries, same paths, modes 0644 / 0755 |
| `exec-and-symlink` | good | `Alpha.so` `chmod 0755`; `x86_64-linux/libalpha.so -> Alpha.so` | exec bit (`f 0755`), in-bundle relative symlink (`l 0777`) | 11 | agree: `-rwxr-xr-x` → `f 0755`, `lrwxrwxrwx ... -> Alpha.so` → `l 0777 ... -> Alpha.so` |
| `long-name` | good | `presets/Long Names/A preset whose file name is deliberately long enough to need a GNU long-name record.vstpreset` (112 bytes) | GNU `L` record | 12 | agree: 12 entries |
| `long-link` | good | `Resources/a-deliberately-long-directory-name-for-the-gnu-long-link-fixture/a-deliberately-long-file-name-for-the-long-link-target.json`; `x86_64-linux/long-link.json -> ../Resources/<that dir>/<that file>` (target 137 bytes) | GNU `K` record (and `L` for the long file path) | 13 | agree: 13 entries, same link target |
| `pax-path` | good | `--format=pax`; a 4-level chain of 60-byte segments under `Resources/pax/`, holding `link.json -> target-directory-x…/target-file-x….json` (link path 287 bytes, target 121 bytes) and that target file | pax `x` with `path` and `linkpath` in one record set | 18 | agree: 18 entries, same link target |
| `pax-global` | good | `--format=pax --pax-option=comment=krate-fixture` | pax `g` (first header, name `/tmp/GlobalHead.1`), ignored | 10 | agree: 10 entries |
| `implicit-dirs` | good | `--no-recursion -T "$T.list"` with the five file paths only (`find . -type f -printf '%P\n' \| sort`) | implied ancestors added as `d 0755` | 10 | agree: tar lists the 5 files; `listing.js` adds the 5 implied directories as `d 0755` |
| `dot-slash` | good | items `.` | `./`-prefixed entries, root `./` entry (mode 0700 from `mktemp`) dropped | 10 | agree: 11 tar entries minus the root `./`, prefixes stripped |
| `ustar-prefix` | good | `--format=ustar`; `Resources/ustar-prefix-directory-x…/split-by-the-ustar-prefix-field.json` (file path 157 bytes; prefix part 120, name part 36) | ustar `prefix` + `/` + `name` join under magic `ustar\0` version `00` | 12 | agree: 12 entries |
| `layout-extra-root-file` | bad | `NOTES.txt` at the root; items + `NOTES.txt` | `E_LAYOUT` naming `NOTES.txt` | 11 | agree: 11 entries |
| `layout-missing-presets` | bad | `rm -rf presets`; items `Alpha.vst3 CHANGELOG.txt README.txt` | `E_LAYOUT` naming `presets` | 8 | agree: 8 entries |

"Agree" was checked by eye on 2026-10-30, entry by entry: every `tar -tvzf` line is one listing line with the path's
leading `./` and trailing `/` dropped and the mode normalised per D3 (`drwxr-xr-x` → `d 0755`, `-rw-r--r--` →
`f 0644`, `-rwxr-xr-x` → `f 0755`, `lrwxrwxrwx` → `l 0777`), sorted by UTF-8 bytes; the listing has no line tar does
not show, except the implied directories of `implicit-dirs`.

The archive bytes are not reproducible from a rebuild (the pax archives carry `atime` / `ctime` records of the build
moment); the listings are. Rebuilding replaces the committed archives, so it is never part of a test.

## Malformed set (`make-bad.js`)

```
node tools/feed-contract/fixtures/listings/make-bad.js --out tools/feed-contract/fixtures/listings/bad
```

Hand-built 512-byte POSIX ustar headers (each with its own checksum, mtime 2026-10-29 00:00:00Z, uid / gid 0), gzipped
with `node:zlib` level 9 (OS byte set to `0xff` so the output does not depend on the platform). Each archive is the base
C4 tree above plus exactly one defect, so the first FR-031 check that fails is the one it exists for.

| Fixture | Defect | `.expected-error` | GNU tar 1.35 `tar -tvzf` |
|---|---|---|---|
| `absolute-path` | `f /etc/evil` | `E_LISTING_ABSOLUTE` | lists it (exit 0) |
| `dotdot-path` | `f ../evil` | `E_LISTING_DOTDOT` | lists it |
| `drive-path` | `f C:/x` | `E_LISTING_DRIVE` | lists it |
| `backslash-path` | `f Alpha.vst3\evil` | `E_LISTING_BACKSLASH` | lists it |
| `symlink-escape-root` | `l Alpha.vst3/Contents/x -> ../../../etc` | `E_LISTING_SYMLINK_ESCAPE` | lists it |
| `symlink-escape-bundle` | `l Alpha.vst3/Contents/x -> ../../README.txt` (inside the root, outside the bundle) | `E_LISTING_SYMLINK_ESCAPE` | lists it |
| `root-symlink` | `l readme-link -> README.txt` | `E_LISTING_ROOT_SYMLINK` | lists it |
| `hard-link` | typeflag `1`, `Alpha.vst3/Contents/hard` → `README.txt` | `E_LISTING_TYPE` | lists it |
| `char-device` | typeflag `3`, `Alpha.vst3/Contents/dev` | `E_LISTING_TYPE` | lists it |
| `duplicate-entry` | `f README.txt` twice | `E_LISTING_DUPLICATE` | lists it |
| `file-dir-clash` | `f presets/Init.vstpreset/inner.vstpreset` (a file that is also an implied directory) | `E_LISTING_DUPLICATE` | lists it |
| `truncated` | the tar stream cut 100 bytes into the fourth header, inside a complete gzip | `E_LISTING_TRUNCATED` | lists 3 entries, exit 0 |
| `no-end-block` | valid headers and data, no all-zero end-of-archive block | `E_LISTING_TRUNCATED` | lists it, exit 0 |
| `bad-checksum` | the `README.txt` header stores its checksum plus one | `E_LISTING_CHECKSUM` | exit 2 |
| `pax-malformed` | an `x` header whose record is `1x path=a\n` (non-decimal length) | `E_LISTING_CORRUPT` | exit 2 |
| `base256-size` | a file header whose `size` field is GNU base-256 (high bit set, value 5) | `E_LISTING_CORRUPT` | lists it |
| `symlink-dotdot-chain` | `l Alpha.vst3/b/c/s -> ../../d`, `l Alpha.vst3/b/c/t -> s/../../../x` (PD-12 review chain) | `E_LISTING_SYMLINK_FORM` (on `t`) | lists it |
| `symlink-chain` | `l Alpha.vst3/Contents/s -> Resources`, `l Alpha.vst3/Contents/t -> s/moduleinfo.json` | `E_LISTING_SYMLINK_CHAIN` | lists it |
| `symlink-self` | `l Alpha.vst3/Contents/l -> l` | `E_LISTING_SYMLINK_CHAIN` | lists it |
| `through-symlink` | `l Alpha.vst3/Contents/t -> Resources`, then `f Alpha.vst3/Contents/t/evil` | `E_LISTING_THROUGH_SYMLINK` | lists it |

The GNU tar column (run 2026-10-30 inside WSL in this directory, `tar -tvzf <file>`) shows the headers are well-formed
ustar that a real tar reads; the contract is deliberately stricter than GNU tar, which accepts a cut stream, a missing
end block and a base-256 size.

The FR-031 rules that need no archive (invalid UTF-8, NUL / CR / LF, ` -> `, target absolute / drive / backslash, empty
and `.` segments, a FIFO) are rows of the in-memory table in `c4-listing-tar.test.js` (`FR-031 rules on in-memory
entries`). The KAT archive (`../kat/`) joins the SC-008 set in task T013.
