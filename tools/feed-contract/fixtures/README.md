# Feed-contract fixtures

Test data for the Krate feed contract (`tools/feed-contract/CONTRACT.md`, spec `specs/krate-manager-phase1/spec.md`).
Later phases read these files in place, so their bytes are part of the contract: `.gitattributes` marks
`tools/feed-contract/fixtures/** -text` (FR-063), and no line-ending conversion ever touches them.

## How the JSON fixtures are made

The JSON fixture sets are built by `make-fixtures.js` from the tables inside it (plan PD-7, §4.7). Never edit a built
file by hand: change the table, rebuild, and commit both.

```
node tools/feed-contract/fixtures/make-fixtures.js --out tools/feed-contract/fixtures   # rebuild every set
node tools/feed-contract/fixtures/make-fixtures.js --check                             # exit 0: committed == built
```

`--check` builds into a fresh temp directory and compares byte for byte; it exits 1 naming the first differing,
missing or stray file. The builder uses only `node:` modules, `../contract.js` and `../listing.js` (the `release`
set builds real C4 payload listings with it, a small extension of plan §4.7's "only `node:` and `../contract.js`"),
and no shipped plugin name (fixture plugins are `Alpha`, `Bravo`, ...).

Signature texts in the fixtures come from `fixtureSignature(comment)`: the four-line minisign shape with a zero-filled
`ED` blob (zero key id, zero signature), a zero global signature and the untrusted comment
`krate fixture signature (not cryptographic)`. They parse under `parseMinisigText` and verify under no key. The only real
signature is the known-answer fixture in `kat/`.

## `trusted-comments.json` (C3 grammar, FR-020 to FR-022, FR-057)

Built by `make-fixtures.js` (set `trusted-comments`). Shape:

- `valid`: comments that parse and round-trip through `formatTrustedComment` — one stable comment per defined platform
  key (`windows-x86_64`, `darwin-aarch64`, `darwin-x86_64`, `darwin-universal`, `linux-x86_64`) and one with the
  prerelease version `1.2.0-beta.1`, each with its FR-017 asset name (`assetNameFor`).
- `invalid`: `{comment, code, why}` rows, one per FR-057 category (five and seven fields, prefix `krate:2`, platform
  `windows-aarch64`, versions `1.2` / `01.2.3` / `1.2.3+b1` / `1.2.4294967296`, asset names with `/`, `\`, `:` and `..`,
  empty fields, a reserved device name, CR and LF). An asset name with `:` is `E_TC_FIELDS`, not `E_TC_ASSET`: the
  colon splits it, so the comment has seven fields.
- `binding`: one `comment` (asset A, `Alpha-1.2.0-Linux-x64.tar.gz`, `linux-x86_64`), the metadata it `matches`, and
  `mismatches` whose `field` is the first differing field `checkBinding` must name (`E_TC_BINDING`), including asset B's
  metadata (`Alpha-1.2.0-Windows-x64.exe`, `windows-x86_64` → `platform`).

Read by: Phase 1 `tests/c3-trusted-comment.test.js` (SC-007); Phase 2 (`tools/minisign.js` and
`gen-release-manifest.js` write C3 comments into every asset signature); Phase 7 (the Rust `core::verify` parses the
trusted comment under C3 and checks its binding to the feed entry).

## `feeds/` (C1 `versions.json` schema_version 2, FR-001 to FR-014, FR-055)

Built by `make-fixtures.js` (set `feeds`, which owns the whole `feeds/` directory, so a file the builder did not make is
a `--check` stray). Layout: `valid/<id>.json`, `invalid/<id>.json` and `cases.json`.

`cases.json` is an array of rows `{rule, file, schema, expect, plugin?}` (plan §3.1, PD-11):

- `rule`: the id of the rule the row exists for, `FR-0xx/<id>` (the ids of plan §4.7 "C1 invalid rows" and the valid
  ids listed under it). `tests/c1-feed.test.js` holds the same list as `C1_RULES` and fails when a rule has no error
  row or a row names an id outside the list.
- `file`: relative to `feeds/`.
- `schema`: `"v2"` (C1, `versions.v2.schema.json` plus the code rules) or `"v1"` (the compatibility floor,
  `versions.v1.schema.json`).
- `expect`: `"accept"` (valid; a `v2` row must also be valid under `v1`, FR-012); `"accept-without-platforms"` (as
  accept, and the entry named by `plugin` has no platform under any of the five defined keys, for itself and its
  `prerelease` sibling, so a manager lists it as "not available through the manager"); or `{"error": X}`, where `X` is
  a code (`E_INSTALL_DUPLICATE`) or an Ajv pair `"<instancePath> <keyword>"` (`"/plugins/Alpha/version pattern"`). The
  row passes when `X` is **among** `validateFeed`'s findings (Ajv with `allErrors` also reports enclosing `if` and
  `propertyNames` wrappers). A finding at the document root has the empty instancePath, so its pair starts with a
  space (`" required"`). `malformed-json` is not JSON at all; the reader's finding for it is `E_INPUT`.

Every invalid document is the full-v2 base with exactly one change, named by its id. The base (`valid/full-v2.json`):

- `Alpha` 1.2.0: `windows-x86_64`, `darwin-aarch64`, `linux-x86_64`, a short `notes_markdown`.
- `Bravo` 2.0.1: `windows-x86_64`, `darwin-x86_64`, `linux-x86_64`, `install.preset_subdir: "Kits"` (a preset folder
  below the plugin folder), and a `prerelease` sibling `bravo/v2.1.0-beta.1` with no platforms.
- `Charlie` 0.9.0: `windows-x86_64` (with the optional `min_os` `10.0`), `darwin-universal`, `linux-x86_64`.
- top-level `manager.latest_json_url` `https://rolandzwaga.github.io/krate-audio/manager/latest.json`.

Synthetic values (so later phases can build matching stub assets):

- asset names from `assetNameFor`, URLs from `perTagUrl("rolandzwaga/krate-audio", tag, asset)`;
- `size` and `sha256` are the byte length and real SHA-256 of the UTF-8 text `krate fixture asset <asset name>\n`;
- `payload` (Linux only) is `files: 3` and the SHA-256 of `krate fixture payload <asset name>\n` (C1 does not tie a
  payload to an archive; the C2 fixtures carry real C4 listings);
- `signature` is `fixtureSignature(formatTrustedComment(name, version, key, asset))`;
- `min_os` is `11.0` on `darwin-*` and `2.31` (glibc) on `linux-*`;
- `install`: `<Name>.vst3`, `<Name>.component` and `com.krateaudio.<name>` when a `darwin-*` key is present, and
  `{0A1F0000-0000-4000-8000-00000000000n}_is1` (n = 1, 2, 3) when `windows-x86_64` is present.

Valid documents: `v1-only` (today's live shape, `schema_version: 1`, a `v1` row), `full-v2`, `mixed` (base plus the
v1-only entry `Delta`), `prerelease-suffixed` and `prerelease-plain` (an `Alpha` sibling `alpha/v1.3.0-beta.1` /
`alpha/v1.3.0` with its own platforms and Alpha's `install`), `notes-14000` (an `Alpha` `notes_markdown` over 14,000
bytes, FR-044), `tenth-plugin` (ten plugins, `Delta` to `Juliet` Linux-only), `unknown-platform-key` (`Delta` whose only
platform is `windows-aarch64`, `accept-without-platforms`), `unknown-extra-keys` (unknown keys at the top level and in
`manager`, an entry, a platform, a payload, an `install` block and a sibling, D1), and `sibling-same-install` (Bravo's
sibling with platforms and an `install` identical to Bravo's: one plugin, not a duplicate, FR-014).

Code rules beyond the schema, as `validateFeed` applies them: every platform `signature` must parse as minisign text
(`E_SIGNATURE_TEXT`, a CR anywhere included); under the five defined keys its trusted comment must parse under C3
(`E_TC_*`); a key the contract does not define is ignored by readers (D1), so its comment is not judged by C3's
five-key platform field. Every `install` passes `checkInstallValues` (`E_INSTALL_VALUE`), and no two plugins share
`vst3_bundle`, `au_component`, `macos_pkg_id` (ASCII case-folded) or `windows_uninstall_key` (exact):
`E_INSTALL_DUPLICATE`, whose message names the field and both plugin keys (`dup-vst3_bundle-casefold` adds `ALPHA`
with `ALPHA.vst3` next to `Alpha.vst3`, PD-13).

Read by: Phase 1 `tests/c1-feed.test.js` (`C1 feed fixtures`, SC-005; `feed rejects shared install facts`, SC-026);
Phase 3 (its one-v2 / eight-v1 feed validates against the same C1 schema, roadmap Phase 3 measurable outcomes); Phase 5
(the Rust feed client reads this set in place and must reach each row's verdict: accept, accept-without-platforms, or
the named rejection, roadmap Phase 5 measurable outcomes).

## `release/` (C2 `release.json`, FR-015 to FR-018, FR-056)

Built by `make-fixtures.js` (set `release`, which owns the whole `release/` directory). Layout: `valid/<id>.json`,
`invalid/<id>.json` and `cases.json`.

`cases.json` is an array of rows `{rule, file, expect}` (plan §3.1, PD-11):

- `rule`: `FR-0xx/<id>`. The error ids are exactly those of plan §4.7 "C2 invalid rows" (FR-015, FR-016 with PD-10's
  `format-*` and `install-missing-*` rows, FR-017); `tests/c2-release.test.js` holds them as `C2_RULES` and fails when
  one has no error row, when a row names an id outside `C2_RULES` and the FR-056 valid ids, or when a row's code
  differs from the plan table.
- `file`: relative to `release/`.
- `expect`: `"accept"` (both `validateRelease` and `checkReleaseManifest(doc, {repo: "rolandzwaga/krate-audio"})`
  pass), or `{"error": CODE, "ajv": PAIR}`: `checkReleaseManifest` throws a `ContractError` whose `code` is `CODE`
  (the first failure in the plan §4.2 order; a schema failure is `E_MANIFEST_SCHEMA`), and `PAIR` is the schema
  finding the row exists for (`"<instancePath> <keyword>"`, among `validateRelease`'s findings), or `null` when the
  manifest is schema-valid and only an FR-017 code rule catches it. `install-dotdot`, `url-latest` and `url-http` fail
  the schema too, but their value checks run first, so the specific code wins (FR-046).

Every invalid manifest is `valid/darwin-aarch64.json` with exactly one change, named by its id. `list-not-canonical`
reverses `payload.list` and recomputes `files` and `sha256`, so only canonicality fails; `list-bad-path` (an added
`presets/../escape.txt` line) and `layout` (the `CHANGELOG.txt` line removed) recompute them the same way.
`fourth-asset` adds a `windows-aarch64` asset; `two-darwin-keys` adds a `darwin-x86_64` asset; `binding` gives the
Windows asset the Linux asset's signature text, so its trusted comment names another platform and asset.

Valid manifests (FR-056; none carries a `channel` key, FR-018, SC-022a):

| File | Plugin, version | macOS key | Notes |
|---|---|---|---|
| `darwin-aarch64` | `Alpha` 1.2.0 | `darwin-aarch64` | the base of every invalid row |
| `darwin-x86_64` | `Bravo` 2.0.1 | `darwin-x86_64` | `install.preset_subdir: "Kits"` |
| `darwin-universal` | `Charlie` 0.9.0 | `darwin-universal` | |
| `windows-without-min_os` | `Alpha` 1.2.0 | `darwin-aarch64` | the `windows-x86_64` asset has no `min_os` (optional there, FR-016) |
| `prerelease-suffixed` | `Alpha` 1.3.0-beta.1 | `darwin-aarch64` | tag `alpha/v1.3.0-beta.1` |
| `plain-version` | `Alpha` 1.3.0 | `darwin-aarch64` | a plain three-part version: as a manifest has no channel, it serves a stable release and a plain-version prerelease alike (FR-018) |

Synthetic values (the same rules as `feeds/`, so a stub asset built for one set matches the other):

- asset `name` from `assetNameFor`, `url` from `perTagUrl("rolandzwaga/krate-audio", tag, name)`;
- `size` and `sha256` are the byte length and real SHA-256 of the UTF-8 text `krate fixture asset <asset name>\n`;
- `signature` is `fixtureSignature(formatTrustedComment(name, version, key, asset name))`;
- `min_os` is `11.0` on `darwin-*`, `2.31` (glibc) on `linux-x86_64` and `10.0` on `windows-x86_64` (absent in
  `windows-without-min_os`);
- `install` holds all four required names: `<Name>.vst3`, `<Name>.component`, `{0A1F0000-0000-4000-8000-00000000000n}_is1`
  (n = 1, 2, 3 for Alpha, Bravo, Charlie) and `com.krateaudio.<name>`;
- the Linux `payload` is a real C4 listing: `listingFromEntries` (`../listing.js`) over the stated tree
  `<Name>.vst3/Contents/Resources/moduleinfo.json` (0644), `<Name>.vst3/Contents/x86_64-linux/<Name>.so` (0755),
  `presets/Default.preset`, `README.txt` and `CHANGELOG.txt` (0644), which passes `checkLinuxLayout`; `files` is the
  line count and `sha256` is `digest(list)`.

Read by: Phase 1 `tests/c2-release.test.js` (`C2 release.json fixtures`, SC-006, SC-022a), and `gen-feed.js`'s
projection check (C2 ⊇ C1, T015); Phase 2 (`gen-release-manifest.js` and the backfill job write manifests that must
pass `checkReleaseManifest` like the valid rows here); Phase 3 (the feed workflow assembles feed entries from
`release.json` files of this shape).

## `kat/` (C3 known-answer fixture and a C4 listing, FR-023, FR-024, SC-013, SC-008)

Not built by `make-fixtures.js`. The archive, its `.minisig` and `kat.pub` were made once by
`specs/krate-manager-phase1/evidence/make-kat.js` with `rsign2 0.6.7` and a throwaway key in a temp directory outside
the repo, deleted after signing; `kat/README.md` records every command, its output and the `rsign verify` run. Never
regenerate them.

| File | What it is |
|---|---|
| `KatPlugin-1.2.3-Linux-x64.tar.gz` | GNU-format gzip tar in the C4 root layout: `KatPlugin.vst3/` (`Contents/Resources/moduleinfo.json`, `Contents/x86_64-linux/KatPlugin.so`), `presets/Default.preset`, `README.txt`, `CHANGELOG.txt` |
| `KatPlugin-1.2.3-Linux-x64.tar.gz.minisig` | the real signature, LF only (D7); trusted comment `krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz` |
| `kat.pub` | the throwaway key's public key (key id `440FA75168B852F7`) |
| `KatPlugin-1.2.3-Linux-x64.tar.gz.listing` | the archive's C4 listing, `toText(listingFromTarGz(archive))` from `../listing.js`; 10 lines |
| `KatPlugin-1.2.3-Linux-x64.tar.gz.sha256` | `digest(lines)` and a final LF: `db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6` |

The two sidecars were written with `listing.js` from the committed archive (`toText` for the listing, `digest` plus LF
for the `.sha256`), the same format as `listings/good/*.listing` and `*.sha256`. They change only if `listing.js`'s
canonical form changes, and that is a contract change.

Read by: Phase 1 `tests/c4-listing-tar.test.js` (subtest `KAT archive`: listing and digest equal the sidecars, and
`checkLinuxLayout(lines, {vst3Bundle: 'KatPlugin.vst3'})` passes) and `tests/c3-trusted-comment.test.js` (test
`KAT binds and no secret key is committed`: `parseMinisigText` gives `ED`, the trusted comment passes `checkBinding`
for `KatPlugin` 1.2.3 `linux-x86_64`, the `.minisig` holds no CR, and the FR-024 scan of `tools/feed-contract/**` finds
no secret-key file, flags neither the KAT `.minisig` nor any `feeds/` or `release/` `signature` value, and flags each
of three synthetic key-shaped files by exactly its rule); the CI step `rsign verify (KAT)`; Phase 2
(`tools/minisign.js` is checked against this fixture, roadmap lines 362 and 391); Phase 7 (the fixture verifies through
the Rust `core::verify`, roadmap line 599).

## `git-versionsort.json` (git's tag order, FR-040, SC-004)

Built by `make-fixtures.js` (set `git-versionsort`, the table `VERSIONSORT_SETS`). Shape:

- `sets`: 24 rows `{id, prefix, name, covers, tags}`. `tags` are full tag names `<prefix>/v<version>`; `name` is the
  prefix with a capital first letter, so `name.toLowerCase() === prefix` (the legacy rule selects `<name lowercased>/v*`).
  The sets cover multi-digit parts, `-rc1` / `-rc10`, `-beta.1` / `-beta.10` (and `-beta.09`), leading zeros (`01`,
  `001`, `00`), zero-only runs, zero-led runs, non-digit suffixes, a part above 2^32, `alpha/v*` beside `alphabet/v*`,
  one tag only, and a set with no tag of its own (`nomatch`, which legacy mode must skip).
- `unrelated`: tags that belong to no set: plain `v*` tags, `manager/v0.1.0`, and near misses of the set prefixes
  (`alpha-v9.9.9`, `alpha/x2.0.0`, `alphav/v5.0.0`, `minor/1.99.0`, `nomatchx/v1.0.0`, ...).

No row states an expected answer: the real `git` is the oracle. `tests/git-versionsort.test.js` creates every tag of
the file in one temp repository (empty global and system config, so no `versionsort.suffix`), runs
`git tag -l "<prefix>/v*" --sort=-v:refname` per set, and requires that `buildLegacyFeed` (given all tags of the file)
publishes the first tag's version and that `gitVersionCompare` orders the set exactly as git does. Measured
2026-10-30 against git 2.49.0.windows.1: all 24 sets agree; a bytewise comparator gets 14 sets wrong and
`localeCompare` with `numeric: true` gets 5 wrong (the leading-zero, zero-run, fraction and suffix sets), so the corpus
separates git's order from both look-alikes.

Read by: Phase 1 `tests/git-versionsort.test.js` (SC-004) on Windows and on the three `node-test (…)` legs of
`feed-tools.yml`.

## `gen-feed/snapshot-2026-10-29/expected.legacy.json` (legacy golden, SC-001, SC-012)

Written once by `node tools/gen-feed.js --legacy --plugins-dir <snapshot>/plugins --tags <snapshot>/tags.txt --out
<snapshot>/expected.legacy.json` (exit 0) and compared with `cmp`: byte-identical to `live-versions.json`, the feed
GitHub Pages served on 2026-10-29. It is not built by `make-fixtures.js`. `tests/gen-feed-legacy.test.js` asserts the
golden equals `live-versions.json` and that `buildLegacyFeed` and `main --legacy` reproduce it; T015's determinism test
compares every case's legacy output with its golden.

## `gen-feed/<case>/` (v2 mode, groups `clean` and `prerelease`, FR-041 to FR-045, FR-059, SC-002, SC-003, SC-011, SC-012)

Inputs are built by `make-fixtures.js` (set `gen-feed`, table `GF_CASES`), which owns each case directory except its
goldens (`expected.*`, committed but never built: a stray check skips them). Layout per case: `case.json` (plan §4.7:
`group`, `modes`, `repo` `rolandzwaga/krate-audio`, `managerLatestUrl`?, `exit`, `legacyExpect`?),
`plugins/<name lower-cased>/version.json` (the repo's shape: `version`, `name`, `description`, `publisher`, `url`
`https://krateaudio.com/<name>/`, optional `identifier` / `preset_subdir`), optional `CHANGELOG.md` (`## [<version>] -
<date>` headers, newest first), `installers/windows/setup.iss` (every plugin; `AppId={{0A1F0000-0000-4000-8000-00000000000n}`
with Inno's doubled brace, n = the plugin's fixture number), `releases.json` (rows in `gh release list --json` key
order, `publishedAt` `2026-10-<row>T12:00:00Z`, a draft `0001-01-01T00:00:00Z` that gen-feed never reads), `tags.txt`
(the `releases.json` tag names, drafts included, FR-059) and `manifests/<name>-<version>.json` (C2 manifests made by
the `release` set's `releaseFor`, so their assets, signatures and C4 payloads follow the `release/` rules above).
`pre-plain` and `pre-plain-promoted` hold the same manifest bytes (SC-022c).

| Case | Group | What it holds |
|---|---|---|
| `clean-basic` | clean | Alpha 1.9.0 / 1.10.0 / 2.0.0 with a manifest for 2.0.0; Bravo 1.9.0 / 1.10.0 without one; Charlie with no release (omitted); non-plugin tags `v0.8.0`, `manager/v0.1.0` |
| `clean-identifier` | clean | Alpha `identifier: com.krateaudio.alphasynth`, manifest `macos_pkg_id` equal to it |
| `clean-changelog` | clean | Alpha's CHANGELOG has its 1.2.0 section; Bravo's has only `0.9.0` (no `notes_markdown`) |
| `clean-long-section` | clean | Alpha's 1.2.0 section is over 14,000 bytes, between a 1.3.0 and a 1.1.0 section (SC-027) |
| `clean-preset-subdir` | clean | `preset_subdir: "Kits"` in Alpha's `version.json` and manifest |
| `clean-version-ahead` | clean | `version.json` 1.3.0 (identifier `com.krateaudio.alphanext`, `preset_subdir` `Presets`) ahead of release 1.2.0, whose manifest says `Kits` and `com.krateaudio.alpha`: check 1 skipped (SC-024b) |
| `tenth-plugin` | clean | ten plugins (Alpha to Juliet, 1.0.0 to 1.9.0); Alpha and Juliet (`darwin-universal`) with manifests |
| `manager-block` | clean | `managerLatestUrl` `https://rolandzwaga.github.io/krate-audio/manager/latest.json` |
| `pre-plain-promoted` | clean | stable 1.1.0 and 1.2.0, the `pre-plain` manifest for 1.2.0 |
| `pre-newer` | prerelease | stable 1.1.0, prerelease 1.2.0-beta.1 with a manifest whose `preset_subdir` `Kits` differs from `version.json` (check 1 skipped, the versions differ); CHANGELOG sections for both |
| `pre-same-core` | prerelease | prerelease 1.1.0-beta.1 beside stable 1.1.0 |
| `pre-lower-core` | prerelease | prerelease 1.0.0-beta.1 beside stable 1.1.0 |
| `pre-only` | prerelease | Alpha has prereleases 1.0.0-beta.1 / -beta.2 only; Bravo stable 1.0.0 |
| `draft-above` | prerelease | stable 1.1.0, a non-prerelease draft 1.2.0 |
| `pre-plain` | prerelease | stable 1.1.0, plain-version prerelease 1.2.0 with a manifest |

`legacyExpect` (`{plugin, version}`) is the SC-003 table's legacy column; `tests/gen-feed-v2.test.js` also holds the
table and asserts both. The snapshot case (`snapshot-2026-10-29`, group `clean`, no `manifests/`: the test passes an
empty temp directory) gained `expected.v2.json`.

**Goldens.** `expected.v2.json` and `expected.legacy.json` of every case above (and the snapshot's `expected.v2.json`)
were written once by `main` (v2 mode with the case's flags, `--legacy` with `tags.txt`; every run exit 0) and reviewed
field by field against FR-043. They add byte stability (SC-012); correctness is asserted structurally by the tests
(SC-002 projection equals legacy, the SC-003 table, SC-011). Review record:

- reviewed 2026-10-30, by the T015 implementer agent (krate-manager-phase build), checked in every case: top-level keys
  `schema_version, plugins` (`manager` last, only in `manager-block`); entry keys in the order `version, download_url,
  release_notes, tag, pub_date, channel, release_notes_url, notes_markdown?, platforms?, install?, prerelease?`;
  `release_notes` `""`; `channel` `"stable"` on every entry and `"beta"` on every sibling; `release_notes_url` =
  `https://github.com/rolandzwaga/krate-audio/releases/tag/<tag>`; `pub_date` = the release row's `publishedAt`;
  `platforms` keys bytewise, each asset without `name`, each payload without `list`; `install` equal to the manifest's;
  every legacy golden equal to the v1 projection where SC-002 / SC-003 say so.
- case `clean-basic`: Alpha 2.0.0 (the highest of 1.9.0 / 1.10.0 / 2.0.0) with three platforms and four install names;
  Bravo 1.10.0 v1-only; no Charlie; non-plugin tags absent.
- case `clean-identifier`: Alpha `install.macos_pkg_id` `com.krateaudio.alphasynth`; Bravo v1-only.
- case `clean-changelog`: Alpha `notes_markdown` is exactly its 1.2.0 section body (65 bytes); Bravo has no `notes_markdown`.
- case `clean-long-section`: Alpha `notes_markdown` 14,148 bytes, the 1.2.0 body only (no `## [` line, no 1.3.0 / 1.1.0 text).
- case `clean-preset-subdir`: Alpha `install.preset_subdir` `Kits`.
- case `clean-version-ahead`: Alpha `version` 1.2.0 (the release, not `version.json`'s 1.3.0); `install` the
  manifest's verbatim (`Kits`, `com.krateaudio.alpha`); exit 0, no WARN.
- case `tenth-plugin`: ten entries in directory order; Alpha and Juliet with platforms (Juliet `darwin-universal`).
- case `manager-block`: `manager.latest_json_url` after `plugins`.
- case `pre-plain-promoted`: Alpha `version` 1.2.0, platforms from the shared manifest, no sibling.
- case `pre-newer`: Alpha 1.1.0 v1-only with the 1.1.0 section; sibling `alpha/v1.2.0-beta.1` `beta` with its own
  section, platforms and `install` with `preset_subdir` `Kits` (check 1 skipped); legacy `1.2.0-beta.1`.
- case `pre-same-core`: Alpha 1.1.0, no sibling; legacy `1.1.0-beta.1`.
- case `pre-lower-core`: Alpha 1.1.0, no sibling; legacy `1.1.0`, byte-equal to the projection.
- case `pre-only`: no Alpha; Bravo 1.0.0; legacy Alpha `1.0.0-beta.2`.
- case `draft-above`: Alpha 1.1.0, no sibling; legacy `1.2.0` (the draft's tag).
- case `pre-plain`: Alpha 1.1.0; sibling `alpha/v1.2.0` `beta` with platforms; legacy `1.2.0`.
- case `snapshot-2026-10-29`: nine v1-only entries (no manifests), each `version` equal to `live-versions.json`, `tag`
  `<dir>/v<version>`, `pub_date` the release's `publishedAt` (Membrum `membrum/v0.12.2`, `2026-10-26T22:50:47Z`, checked
  against `releases.json`).

Read by: Phase 1 `tests/gen-feed-v2.test.js` (SC-002, SC-003, SC-011, SC-012), `tests/gen-feed-errors.test.js` (T016:
SC-022 to SC-025, SC-027 read `pre-plain`, `pre-plain-promoted`, `clean-version-ahead`, `pre-newer`,
`clean-long-section`, `clean-basic`); Phase 3 (`feed.yml` runs gen-feed in this mode on the real inputs).

## `gen-feed/<case>/` (groups `reject` and `degrade`, FR-046, FR-049, FR-059, SC-010, SC-022 to SC-025)

Inputs are built by `make-fixtures.js` (set `gen-feed`, tables `GF_REJECT_CASES` and `GF_DEGRADE_CASES`, merged into
`GF_CASES`), in the same layout as the clean and prerelease cases, with these departures, each made by a table override:
a plugin directory may be named apart from its plugin (`dir`: `alpha1` / `alpha2`, and `reserved` for the `Nul` plugin,
because Windows cannot create a folder named `nul`); a `version.json` `url` may be any value (`url: 42`); a plugin may
have a custom `setup.iss` or none (`setupIss`); `releases.json` may be a literal row list (`releasesJson`); `tags.txt` may
be literal text (`tagsText`); and a `manifests/` file may be literal text (`broken.json`).

`case.json` adds, per plan §4.7: `modes` (`["v2"]` for the v2 rejects, `["legacy"]` for `reject-input-ls-remote`,
`["v2", "legacy"]` for every degrade case), `exit` (1 or 3), `error` (reject: the code of the first stderr line) and
`warn` (degrade: one `{code, plugin, file?}` row per WARN line, in stderr order; `plugin: null` with `file` is the
unattributed WARN of an unparseable manifest, PD-8; `file`, when given, is also named by the line).
`tests/gen-feed-errors.test.js` holds the same code tables (`REJECTS`, `DEGRADES`) and asserts both.

Goldens: a reject case has `expected.error`, the exact first stderr line of `main` plus LF (every reject message names
relative paths only, so the line is the same on every OS); a degrade case has `expected.v2.json`, the `--out` bytes of
`main` (exit 3). Neither group has `expected.legacy.json`: legacy mode validates nothing and is not what these cases
are about (`gen-feed degrades a bad manifest` still runs `--legacy` on every degrade case and requires exit 0).

Reject cases (exit 1, nothing written; each `version.json` `url` is `https://krateaudio.com/<name>/` unless stated):

| Case | What it holds | `error` |
|---|---|---|
| `reject-v1-type` | Alpha `url: 42` | `E_V1_TYPE` |
| `reject-url-latest` | Alpha `url` a `/releases/latest/download/` link | `E_URL_LATEST` |
| `reject-url-http` | Alpha `url` `http://krateaudio.com/alpha/` | `E_URL_NOT_HTTPS` |
| `reject-tag-suffix` | release `alpha/v1.2.0-rc1` with `isPrerelease: false` (SC-022d) | `E_TAG_FORMAT` |
| `reject-tag-ten-digits` | release `alpha/v4294967296.0.0` | `E_TAG_FORMAT` |
| `reject-plugin-name` | `name: "alpha"` | `E_PLUGIN_NAME` |
| `reject-reserved-name` | `name: "Nul"` in directory `reserved` (PD-14) | `E_PLUGIN_NAME` |
| `reject-duplicate-plugin` | directories `alpha1` and `alpha2`, both `name: "Alpha"` | `E_DUPLICATE_PLUGIN` |
| `duplicate-plugin-case` | `alpha1` `Alpha`, `alpha2` `ALPHA` (PD-13) | `E_DUPLICATE_PLUGIN` |
| `reject-input-ls-remote` | modes `["legacy"]`; `tags.txt` is raw `git ls-remote` output: `<sha>\trefs/tags/alpha/v1.0.0` and its `^{}` line | `E_INPUT` |
| `reject-input-releases` | a `releases.json` row whose `isPrerelease` is the string `"false"` (valid JSON, so the line holds no parser message) | `E_INPUT` |
| `reject-abort-plus-degrade` | Alpha `url: 42` beside Bravo whose manifest would degrade (`E_TC_BINDING`): abort wins (FR-049) | `E_V1_TYPE` |

Degrade cases (exit 3, the feed written). Unless stated: Alpha `version.json` 1.2.0 and release `alpha/v1.2.0` with
manifest `alpha-1.2.0.json` carrying the one change, beside a healthy Bravo 1.0.0 with its own valid manifest; the WARN
names plugin Alpha.

| Case | The change | WARN |
|---|---|---|
| `degrade-preset-subdir` | `version.json` `preset_subdir: "Kits"`, the manifest has none (check 1, versions equal) | `E_MANIFEST_MISMATCH` (`install.preset_subdir`) |
| `degrade-pkg-id` | manifest `macos_pkg_id` `com.krateaudio.alphaother` (check 1) | `E_MANIFEST_MISMATCH` (`install.macos_pkg_id`) |
| `degrade-uninstall-key` | manifest key `{…-000000000009}_is1`, `setup.iss` AppId `…-000000000001` (check 2) | `E_MANIFEST_MISMATCH`, names `alpha/installers/windows/setup.iss` |
| `degrade-no-appid` | `setup.iss` with no `AppId=` line | the same |
| `degrade-no-setup-iss` | no `setup.iss` | the same |
| `degrade-install-slash` / `-backslash` / `-dotdot` | `vst3_bundle` `sub/Alpha.vst3` / `sub\Alpha.vst3` / `Alpha..vst3` | `E_INSTALL_VALUE` |
| `degrade-signature-text` | Windows signature blob 73 bytes | `E_SIGNATURE_TEXT` |
| `degrade-tc-fields` / `-name` / `-version` / `-platform` / `-asset` | Windows signature whose trusted comment is `krate:1:Alpha:1.2.0` / names `alpha` / version `1.2` / platform `windows-aarch64` / asset `..` | `E_TC_FIELDS` / `E_TC_NAME` / `E_TC_VERSION` / `E_TC_PLATFORM` / `E_TC_ASSET` |
| `degrade-tc-binding` | the Windows asset carries the Linux asset's signature | `E_TC_BINDING` |
| `degrade-listing-dotdot` / `-not-canonical` / `-count` / `-digest` | payload list plus `presets/../escape.txt` / reversed / `files` + 1 / wrong `sha256` | `E_LISTING_DOTDOT` / `E_LISTING_NOT_CANONICAL` / `E_LISTING_COUNT` / `E_LISTING_DIGEST` |
| `degrade-listing-path` / `-absolute` / `-drive` / `-backslash` / `-duplicate` | payload list (re-digested) plus `f 0644 presets/escape -> txt` / `f 0644 /escape.txt` / `f 0644 C:/escape.txt` / `f 0644 presets\escape.txt` / a second `f 0644 README.txt` | `E_LISTING_PATH` / `E_LISTING_ABSOLUTE` / `E_LISTING_DRIVE` / `E_LISTING_BACKSLASH` / `E_LISTING_DUPLICATE` |
| `degrade-listing-root-symlink` / `-symlink-form` / `-symlink-escape` | payload list (re-digested) plus `l 0777 escape -> README.txt` / `l 0777 presets/link -> ./Default.preset` / `l 0777 presets/link -> ../README.txt` | `E_LISTING_ROOT_SYMLINK` / `E_LISTING_SYMLINK_FORM` / `E_LISTING_SYMLINK_ESCAPE` |
| `degrade-listing-symlink-chain` | payload list (re-digested) plus `l 0777 presets/link-a -> link-b` and `l 0777 presets/link-b -> Default.preset` | `E_LISTING_SYMLINK_CHAIN` |
| `degrade-listing-through-symlink` | payload list (re-digested) plus `l 0777 presets/link -> Default.preset` and `f 0644 presets/link/escape.txt` | `E_LISTING_THROUGH_SYMLINK` |
| `degrade-layout` | the `CHANGELOG.txt` line removed (list re-digested) | `E_LAYOUT` |
| `degrade-url-http` / `-latest` / `-not-per-tag` | Windows asset URL `http://…` / `/releases/latest/download/…` / on tag `alpha/v1.1.0` | `E_URL_NOT_HTTPS` / `E_URL_LATEST` / `E_URL_NOT_PER_TAG` |
| `degrade-sibling-manifest` | Alpha 1.1.0 with a valid manifest; sibling `alpha/v1.2.0-beta.1` whose manifest fails the binding: entry and sibling both lose `platforms` and `install` | `E_TC_BINDING` |
| `degrade-c2-missing-pkg-id` | `version.json` 1.3.0 (check 1 skipped), manifest without `install.macos_pkg_id` (PD-10) | `E_MANIFEST_SCHEMA` |
| `degrade-c2-wrong-format` | Windows asset `format: "pkg"` (PD-10) | `E_MANIFEST_SCHEMA` |
| `degrade-dup-uninstall-key` | Bravo's `setup.iss` and manifest copy Alpha's AppId; healthy Charlie (Q-C) | `E_INSTALL_DUPLICATE` Alpha, then Bravo |
| `degrade-dup-pkg-id` | Alpha and Bravo `identifier: com.krateaudio.shared`, manifests to match; healthy Charlie (Q-C) | `E_INSTALL_DUPLICATE` Alpha, then Bravo |
| `degrade-copied-bundle-name` | Bravo's manifest carries `Alpha.vst3` / `Alpha.component`; Alpha healthy (Q-C) | `E_MANIFEST_SCHEMA` Bravo |
| `degrade-manifest-unparseable` | `manifests/broken.json` is `{"tag": ` (truncated); Alpha alone, healthy (PD-8, Q-A) | `E_MANIFEST_SCHEMA`, `plugin: null`, names `manifests/broken.json` |
| `degrade-manifest-duplicate-tag` | `alpha-1.2.0.json` and `alpha-1.2.0-copy.json` both `tag: alpha/v1.2.0` (PD-8, Q-A) | `E_MANIFEST_DUPLICATE`, names both files |

The degrade set covers every degrade-class code gen-feed can raise (FR-059). `E_LISTING_TYPE`, `E_LISTING_CHECKSUM`,
`E_LISTING_TRUNCATED` and `E_LISTING_CORRUPT` are not among them: `parseListingLine` accepts only `f` / `d` / `l` lines
and the other three come from `listingFromTarGz`, which gen-feed never calls.

Review record (goldens):

- 2026-10-30, compliance remediation (FR-059): the ten `degrade-listing-*` path / symlink cases above were added; each
  was run once through `main` (exit 3, one `WARN` with its code naming plugin Alpha and `assets.linux-x86_64.payload.list`)
  and its `expected.v2.json` is byte-identical to the reviewed `degrade-listing-dotdot` golden (Alpha v1-only, Bravo
  healthy), so 41 degrade cases now exit 3.

- reviewed 2026-10-30, by the T016 implementer agent (krate-manager-phase build). Every case was run once through
  `main` and its exit code checked against `case.json` (12 rejects exit 1, 31 degrades exit 3, the WARN lines as in the
  table above); the goldens were then written from those runs.
- every reject `expected.error`: starts `gen-feed: <error>: `, names the plugin directory's `version.json` (relative),
  the plugin, the field or the tag / `--tags` line / `--releases` row, and no absolute path; `reject-abort-plus-degrade`
  prints no WARN line.
- every degrade `expected.v2.json`, checked field by field: top-level keys `schema_version, plugins`; entry keys in the
  FR-043 order; `release_notes` `""`, `channel` `"stable"`, `release_notes_url` on the entry's tag, `tag` =
  `<name lowercased>/v<version>`; each degraded plugin (Alpha, or Alpha and Bravo in the two `dup` cases, or Bravo in
  `degrade-copied-bundle-name`) is v1-only, with neither `platforms` nor `install`, its `version` the release's (1.2.0
  in `degrade-c2-missing-pkg-id`, not `version.json`'s 1.3.0); in `degrade-sibling-manifest` the sibling
  `alpha/v1.2.0-beta.1` (`channel` `"beta"`) has neither either; every healthy plugin (Bravo, Charlie in the `dup`
  cases, Alpha in `degrade-copied-bundle-name` and `degrade-manifest-unparseable`) has three platforms (keys bytewise,
  no `name`, payload without `list`) and its manifest's four install names.

Read by: Phase 1 `tests/gen-feed-errors.test.js` (`gen-feed rejects`, SC-010; `channel comes from the release list
only`, SC-022d; `gen-feed degrades a bad manifest`, SC-023; `install cross-check is gated on version`, SC-024a;
`windows_uninstall_key equals the setup.iss AppId`, SC-025c) and `tests/gen-feed-v2.test.js` (`release_notes is always
empty`, SC-011, over every degrade case); Phase 3 (`feed.yml` deploys nothing on exit 1, and on exit 3 deploys the
written feed and still turns the run red, FR-049).
