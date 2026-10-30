# Krate Audio feed contract (C1-C4)

The prose contract for `versions.json` schema_version 2 (C1), the per-release `release.json` (C2), the trusted
comments that bind every plugin-asset signature (C3) and the Linux archive layout with its canonical listing (C4), plus
the generator `tools/gen-feed.js` that writes the feed. The machine-readable halves sit next to this file:
`versions.v2.schema.json` (C1), `versions.v1.schema.json` (the v1 compatibility floor), `release.schema.json` (C2),
`contract.js` (validators, C3, install and URL rules, the code table as `CODES`) and `listing.js` (C4).

Requirement ids (`FR-0xx`, `SC-0xx`) refer to `specs/krate-manager-phase1/spec.md`; `PD-n` to
`specs/krate-manager-phase1/plan.md` §0; `D1`-`D9` to the spec's "Decisions taken in this spec"; `Q1`-`Q10` and
`ROSTER` to the roadmap's decisions log (`specs/Krate-manager-roadmap.md`). Where this file and a schema disagree, the
schema is the bug: `tools/feed-contract/tests/hygiene.test.js` (`CONTRACT.md documents every code and field`) checks
that every schema path, every error code and every exit code below is documented.

## 1. Scope and readers

The feed is one public file, `versions.json`, served from GitHub Pages
(`https://rolandzwaga.github.io/krate-audio/versions.json`, every plugin's update config). Every change to it is
additive: a key is never renamed, retyped or removed, and objects stay open (D1), so every reader written against an
older contract keeps working.

| Reader / writer | What it uses | Where |
|---|---|---|
| The in-plugin update checker shipped in every plugin | `plugins` (object), `plugins.<Name>` (object), `plugins.<Name>.version` (string, `SemVer::parse`), and `download_url` / `release_notes` only when they are strings; it never reads `schema_version` and ignores every other key; any exception gives an empty result | `plugins/shared/src/update/update_checker.cpp:222-262` |
| `tools/gen-feed.js` (Phase 1) | writes the feed (v2 mode), reproduces today's v1 output (legacy mode), projects v2 to v1 | section 7 |
| Phase 2 tools (`gen-release-manifest.js`, `minisign.js`, the backfill) | write `release.json` (C2), the C3 trusted comments and the C4 listing | sections 4-6 |
| Phase 3 (`feed.yml`) | runs gen-feed, deploys the feed; exit 3 deploys and turns the run red | section 7 |
| Phase 5 (the manager's Rust feed client) | parses C1 with these reader rules, reuses the error-code names where it rejects the same thing | sections 2, 3, 7 |
| Phases 6-10 (detection, download and verify, extraction, installs) | `platforms`, `install`, the C3 binding, the C4 listing rules and digest | sections 2, 5, 6 |
| Phase 11 (the manager's self-update) | `manager.latest_json_url` | section 2 |

## 2. C1: versions.json schema_version 2

Schema: `versions.v2.schema.json` (JSON Schema 2020-12, `$id` `https://krateaudio.com/schemas/versions.v2.schema.json`,
an identifier that is never fetched), validated with `ajv` 8.20.0 `Ajv2020` in strict mode (D2). Every object is open
(D1). `<Name>` is a plugin key, `<key>` a platform key. "Plugin parser" is the in-plugin update checker of section 1;
"manager" is the Krate Manager app, by the phase that first reads the field.

Shared value rules used below:
- **core**: `^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$` (FR-004), a subset of what
  `SemVer::parse` accepts: no whitespace, no leading zeros, at most 9 digits per part so every part fits `uint32_t`.
- **url**: `^https://[^\s]+$` and never containing `/releases/latest/` (FR-005).
- **reserved**: a Windows reserved device name (`CON`, `PRN`, `AUX`, `NUL`, `COM0`-`COM9`, `LPT0`-`LPT9`), any case,
  alone or followed by an extension (PD-14, schema `$defs/reservedStem`).

| Path | Type | Required | Rule | Writer (gen-feed, from) | Reader |
|---|---|---|---|---|---|
| `schema_version` | integer | required | `const 2` (FR-001) | constant | manager (Phase 5); the plugin parser never reads it |
| `plugins` | object | required | keys match `^[A-Z][A-Za-z0-9]{0,63}$` and are not reserved (FR-002, PD-14); no plugin names are enumerated (ROSTER) | built from `version.json` files | plugin parser; manager (Phase 5) |
| `plugins.<Name>` | object | one per plugin with a stable release | the plugin entry; `platforms` and `install` present together or absent together (FR-003); an entry without them is v1-only | key = `version.json` `name`; a plugin with no stable release is omitted (FR-042) | plugin parser (looks up its own name); manager (Phase 5) |
| `plugins.<Name>.version` | string | required | core (FR-004); never a prerelease suffix | version part of the stable tag (Q9 rule, section 7) | plugin parser (`SemVer::parse`, compared with its own version); manager (Phase 5) |
| `plugins.<Name>.download_url` | string | required | url (FR-005) | `version.json` `url` | plugin parser (the banner's link); the manager downloads only from `platforms.<key>.url` |
| `plugins.<Name>.release_notes` | string | required | `const ""` | constant | plugin parser (reads it when a string) |
| `plugins.<Name>.tag` | string | required | `^[a-z0-9]+/v<core>$` (FR-005, the `release.yml:76` form) | release list `tagName` of the stable release | manager (Phase 5) |
| `plugins.<Name>.pub_date` | string | required | `^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$` (FR-005) | release list `publishedAt` (D6) | manager (Phase 5) |
| `plugins.<Name>.channel` | string | required | `const "stable"` | the release list's `isPrerelease` (false) alone (FR-018) | manager (Phase 5) |
| `plugins.<Name>.release_notes_url` | string | required | url | `https://github.com/<repo>/releases/tag/<tag>` from `--repo` (D6) | manager (link to the release page) |
| `plugins.<Name>.notes_markdown` | string | optional | a string, no `maxLength`, never an array; exactly one CHANGELOG section, the entry's own version (FR-044) | `<plugin dir>/CHANGELOG.md`, cut per section 7; omitted when the file or section is missing or empty | manager (release notes view) |
| `plugins.<Name>.platforms` | object | optional; required with `install` | keys match `^(windows\|darwin\|linux)-[a-z0-9_]+$` (FR-006); the five defined keys are `windows-x86_64`, `darwin-aarch64`, `darwin-x86_64`, `darwin-universal`, `linux-x86_64`; other keys are allowed (a future architecture) | the stable release's `release.json` `assets`, keys sorted bytewise (FR-043) | manager (Phase 5 resolves the native key, section 3) |
| `plugins.<Name>.platforms.<key>` | object | one per platform key | requires `url`, `size`, `sha256`, `signature`, `format`; `format` fixed per prefix (`windows-*` `inno-exe`, `darwin-*` `pkg`, `linux-*` `tar.gz`) | one `release.json` asset minus `name` and minus `payload.list` | manager (Phases 5, 7) |
| `plugins.<Name>.platforms.<key>.url` | string | required | url; per-tag form `https://github.com/<repo>/releases/download/<tag>/<asset name>` (FR-017); its last path segment is the asset file name (D8) | asset `url` | manager (Phase 7 downloads it) |
| `plugins.<Name>.platforms.<key>.size` | integer | required | `1 ≤ size ≤ 2147483647` (the Releases 2 GiB per-file limit, FR-006) | asset `size` | manager (Phase 7 checks the downloaded size) |
| `plugins.<Name>.platforms.<key>.sha256` | string | required | `^[0-9a-f]{64}$` | asset `sha256` | manager (Phase 7 verifies before anything is extracted or executed) |
| `plugins.<Name>.platforms.<key>.signature` | string | required | the four-line minisign text, LF only, final LF (FR-007, D7); line 2 decodes to 74 bytes starting `ED`; the trusted comment parses under C3 and binds this asset (section 5) | asset `signature` | manager (Phase 7 verifies the signature and the C3 binding) |
| `plugins.<Name>.platforms.<key>.format` | string | required | `inno-exe` \| `pkg` \| `tar.gz`, fixed per key prefix (FR-006) | asset `format` | manager (Phases 9-10 pick the install driver) |
| `plugins.<Name>.platforms.<key>.min_os` | string | required for `darwin-*` and `linux-*`, optional for `windows-*` | `^[0-9]+(\.[0-9]+){1,3}$`: the macOS product version or the minimum glibc version (FR-006) | asset `min_os` | manager (compatibility with the running OS) |
| `plugins.<Name>.platforms.<key>.payload` | object | required on `tar.gz`, forbidden on `inno-exe` and `pkg` (FR-009) | `{files, sha256}` of the C4 canonical listing | asset `payload` minus `list` | manager (Phase 8 staged-layout check) |
| `plugins.<Name>.platforms.<key>.payload.files` | integer | required in `payload` | `≥ 1`; the number of canonical listing lines | asset `payload.files` | manager (Phase 8) |
| `plugins.<Name>.platforms.<key>.payload.sha256` | string | required in `payload` | `^[0-9a-f]{64}$`; the C4 digest of the canonical listing (section 6) | asset `payload.sha256` | manager (Phase 8 compares it with the listing of the extracted tree) |
| `plugins.<Name>.install` | object | optional; required with `platforms` | the install facts (FR-008); every value is one plain name, checked again in code (`E_INSTALL_VALUE`); unique across plugins (section 3) | the manifest's `install` verbatim, after FR-043 checks 1 and 2 (section 7) | manager (Phases 6, 9, 10) |
| `plugins.<Name>.install.vst3_bundle` | string | required in `install` | `^[A-Za-z0-9][A-Za-z0-9 _.-]*\.vst3$`, no `..`, stem not reserved; equals `<Name>.vst3` (FR-017) | manifest `install.vst3_bundle` | manager (Phase 6 detection, Phase 8 layout, Phases 9-10 install folder) |
| `plugins.<Name>.install.au_component` | string | required when a `darwin-*` platform is present | `^[A-Za-z0-9][A-Za-z0-9 _.-]*\.component$`, no `..`, stem not reserved; equals `<Name>.component` (FR-017) | manifest `install.au_component` | manager (Phases 6 and 10, macOS) |
| `plugins.<Name>.install.preset_subdir` | string | optional | `^[A-Za-z0-9]([A-Za-z0-9 _-]*[A-Za-z0-9_-])?$`: one segment, never ending in a space, not reserved (PD-14) | manifest; equals `version.json` `preset_subdir` (FR-043 check 1) | manager (preset folder) |
| `plugins.<Name>.install.windows_uninstall_key` | string | required when `windows-x86_64` is present | `^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}_is1$`; equals the `setup.iss` AppId plus `_is1` (FR-043 check 2) | manifest | manager (Phase 10, Windows uninstall) |
| `plugins.<Name>.install.macos_pkg_id` | string | required when a `darwin-*` platform is present | `^com\.krateaudio\.[a-z0-9]+$`; equals `version.json` `identifier`, else `com.krateaudio.<name lower-cased>` (FR-043 check 1, `release.yml:321`) | manifest | manager (Phase 10, macOS receipt removal) |
| `plugins.<Name>.prerelease` | object | optional | the sibling (FR-010): the entry's shape minus the v1 keys | the prerelease with the highest SemVer precedence strictly above the stable version (Q9 rule) | manager v1 ignores it (Q9); the plugin parser never reads it |
| `plugins.<Name>.prerelease.tag` | string | required in the sibling | `^[a-z0-9]+/v<core>(-<prerelease>)?$`, prerelease identifiers per SemVer 2.0.0 §9; a plain core is normal (FR-018) | release list `tagName` | a later beta channel |
| `plugins.<Name>.prerelease.pub_date` | string | required in the sibling | as `plugins.<Name>.pub_date` | release list `publishedAt` | a later beta channel |
| `plugins.<Name>.prerelease.channel` | string | required in the sibling | `const "beta"` | the release list's `isPrerelease` (true) alone | a later beta channel |
| `plugins.<Name>.prerelease.release_notes_url` | string | required in the sibling | url | `https://github.com/<repo>/releases/tag/<tag>` | a later beta channel |
| `plugins.<Name>.prerelease.notes_markdown` | string | optional | as `plugins.<Name>.notes_markdown`, the sibling's own version's section | `CHANGELOG.md` | a later beta channel |
| `plugins.<Name>.prerelease.platforms` | object | optional; required with the sibling's `install` | as `plugins.<Name>.platforms` | the prerelease's `release.json` `assets` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>` | object | one per platform key | as `plugins.<Name>.platforms.<key>` | one asset | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.url` | string | required | as `plugins.<Name>.platforms.<key>.url` | asset `url` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.size` | integer | required | as `plugins.<Name>.platforms.<key>.size` | asset `size` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.sha256` | string | required | as `plugins.<Name>.platforms.<key>.sha256` | asset `sha256` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.signature` | string | required | as `plugins.<Name>.platforms.<key>.signature` | asset `signature` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.format` | string | required | as `plugins.<Name>.platforms.<key>.format` | asset `format` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.min_os` | string | as `plugins.<Name>.platforms.<key>.min_os` | as `plugins.<Name>.platforms.<key>.min_os` | asset `min_os` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.payload` | object | required on `tar.gz`, forbidden otherwise | as `plugins.<Name>.platforms.<key>.payload` | asset `payload` minus `list` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.payload.files` | integer | required in `payload` | as `plugins.<Name>.platforms.<key>.payload.files` | asset `payload.files` | a later beta channel |
| `plugins.<Name>.prerelease.platforms.<key>.payload.sha256` | string | required in `payload` | as `plugins.<Name>.platforms.<key>.payload.sha256` | asset `payload.sha256` | a later beta channel |
| `plugins.<Name>.prerelease.install` | object | optional; required with the sibling's `platforms` | as `plugins.<Name>.install`; may equal the entry's own `install` (one plugin, section 3) | the prerelease manifest's `install` | a later beta channel |
| `plugins.<Name>.prerelease.install.vst3_bundle` | string | required in `install` | as `plugins.<Name>.install.vst3_bundle` | manifest | a later beta channel |
| `plugins.<Name>.prerelease.install.au_component` | string | required when a `darwin-*` platform is present | as `plugins.<Name>.install.au_component` | manifest | a later beta channel |
| `plugins.<Name>.prerelease.install.preset_subdir` | string | optional | as `plugins.<Name>.install.preset_subdir` | manifest | a later beta channel |
| `plugins.<Name>.prerelease.install.windows_uninstall_key` | string | required when `windows-x86_64` is present | as `plugins.<Name>.install.windows_uninstall_key` | manifest | a later beta channel |
| `plugins.<Name>.prerelease.install.macos_pkg_id` | string | required when a `darwin-*` platform is present | as `plugins.<Name>.install.macos_pkg_id` | manifest | a later beta channel |
| `plugins.<Name>.prerelease.version` | (none) | forbidden | schema `false` (PD-1): no v1 key in the sibling, so no v1 reader can mistake it | never written | none |
| `plugins.<Name>.prerelease.download_url` | (none) | forbidden | schema `false` (PD-1) | never written | none |
| `plugins.<Name>.prerelease.release_notes` | (none) | forbidden | schema `false` (PD-1) | never written | none |
| `manager` | object | optional | requires `latest_json_url`; other keys left open for Phase 11 (FR-011) | written only when `--manager-latest-url` is given | manager (Phase 11) |
| `manager.latest_json_url` | string | required in `manager` | url whose path ends in `/manager/latest.json` (FR-011) | `--manager-latest-url` | manager (Phase 11 self-update) |

Code rules that JSON Schema cannot express, run by `contract.js` `validateFeed(doc, {schema: 'v2'})` after Ajv: each
`signature` through `parseMinisigText` (`E_SIGNATURE_TEXT`, a CR anywhere included) and its trusted comment through
`parseTrustedComment` (`E_TC_*`), `checkInstallValues` on each `install` (`E_INSTALL_VALUE`), and install uniqueness
(`E_INSTALL_DUPLICATE`, section 3). Ajv findings come back as `{code: 'E_SCHEMA', instancePath, keyword, message}`.

**v1 compatibility floor (FR-012).** `versions.v1.schema.json` encodes exactly what the plugin parser needs: `plugins`
an object of objects, each with a string `version`, and `download_url` / `release_notes` strings when present. Every
v2-valid document is also v1-valid.

## 3. Reader rules

Every reader of C1 (Phase 5's Rust client first) applies these rules. They are part of the contract, not advice.

1. **Ignore unknown keys** at every level (D1). A reader never fails on a key it does not know; a writer adds keys,
   never renames, retypes or removes them.
2. **Platform resolution (Phase 5).** Take the native key (`windows-x86_64`, `darwin-aarch64`, `darwin-x86_64` or
   `linux-x86_64`); on macOS, when the native key is absent, take `darwin-universal`. A platform key outside the five
   defined keys is ignored (it may be a future architecture).
3. **Manager v1 ignores `prerelease`** (Q9). The stable entry is the only one it offers. The plugin parser never reads
   the sibling either, and the sibling carries no v1 key (PD-1).
4. **Asset file name = the last path segment of `platforms.<key>.url`** (D8). The feed carries no separate name; the
   C3 binding (section 5) compares the trusted comment's asset name with it.
5. **Install uniqueness (FR-014, PD-13).** No two plugin entries share a value of `install.vst3_bundle`,
   `install.au_component`, `install.windows_uninstall_key` or `install.macos_pkg_id`. `vst3_bundle`, `au_component` and
   `macos_pkg_id` are compared after **ASCII case folding** (`Alpha.vst3` and `ALPHA.vst3` name one folder on NTFS and
   on default APFS); `windows_uninstall_key` is compared exactly (its pattern is upper-case hex only). An entry's own
   `install` and its `prerelease` sibling's `install` belong to one plugin and are not compared with each other; a
   v1-only entry has no `install` and takes no part. A violation is `E_INSTALL_DUPLICATE`, naming the field and both
   plugin keys (`contract.js` `findInstallDuplicates`, run by `validateFeed`). Phase 5 inherits the rule: a feed that
   breaks it must not let either plugin be installed or removed through the manager, because neither can be trusted to
   name its own files or uninstaller.
6. **Not available through the manager (ROSTER, FR-003).** A v1-only entry (no `platforms` / `install`), or an entry
   with no platform under one of the five defined keys (`contract.js` `definedPlatformKeys` returns `[]`), is listed
   with its version and shown as "not available through the manager".
7. **No Windows reserved names (PD-14).** No plugin key, `vst3_bundle` / `au_component` stem or `preset_subdir` is a
   Windows reserved device name in any case, with or without an extension; `preset_subdir` never ends in a space.
8. **Never a path unchecked.** Every `install` value is one name: no `/`, `\`, `:`, no `..` anywhere (FR-008). A reader
   re-checks the patterns before it joins a value to a filesystem root.

## 4. C2: release.json

Schema: `release.schema.json` (2020-12). C2 is written by Phase 2 (`gen-release-manifest.js`, the backfill) as one
release asset per release and read only by Krate's own tools (`gen-feed.js`), so every object is **closed**
(`additionalProperties: false`, D1).

**Shape (FR-015, FR-016).** Required: `tag` (`<name lower-cased>/v<version>`), `name` (a plugin key, the C1
`pluginName` rule), `version` (the bounded core with an optional SemVer 2.0.0 prerelease suffix, no build metadata),
`pub_date`, `assets` and `install`.
- `assets` has exactly three keys: `windows-x86_64`, `linux-x86_64` and one `darwin-*` key (`darwin-aarch64`,
  `darwin-x86_64` or `darwin-universal`). A release missing one of its three installer assets gets no `release.json`.
- Each asset requires `name`, `url`, `size`, `sha256`, `signature` and `format`, under the C1 `platform` rules, with the
  format bound per key prefix exactly as in C1 (`patternProperties` on `assets`, PD-10).
- The `tar.gz` asset carries `payload {files, sha256, list}`, `list` being the C4 canonical listing as an array of lines
  without terminators (`minItems: 1`); the `.exe` and `.pkg` assets carry no `payload`.
- `install` holds the five C1 install names and requires four of them: `vst3_bundle`, `au_component`,
  `windows_uninstall_key` and `macos_pkg_id` (`preset_subdir` optional). Every manifest has a `windows-x86_64` and a
  `darwin-*` asset, so this is FR-008's two conditional requirements applied, not a new rule (PD-10).

**The `min_os` departure (FR-016).** `min_os` is required on the `darwin-*` and `linux-x86_64` assets and optional on
`windows-x86_64`. This is a deliberate departure from the roadmap's C2 field list (`specs/Krate-manager-roadmap.md`
Phase 1, C2 bullet), which marks only `payload` optional: no Phase 2 step records a Windows minimum (Phase 2 records the
macOS product version and the glibc version), so requiring it would force `gen-release-manifest.js` and the backfill to
invent a value.

**There is no `channel` key (FR-018, Clarifications Q1).** The release list's `isPrerelease` flag is the only channel
source: gen-feed writes C1 `channel: "stable"` on the entry and `"beta"` on the sibling from that flag, and neither
`checkReleaseManifest` nor gen-feed reads a channel from `release.json`. A manifest that carries one fails the closed
object (`E_MANIFEST_SCHEMA`). A SemVer suffix stays optional (a plain tag such as `alpha/v1.2.0` flagged as a
prerelease is the normal case). Promoting a prerelease is unticking the GitHub prerelease flag: the uploaded `release.json` stays
valid byte for byte and the next feed run emits it as the stable entry. This also departs from the roadmap's C2 field
list, which named `channel`.

**Cross-field rules (FR-017).** JSON Schema cannot express these; `contract.js` `checkReleaseManifest(doc, {repo})`
enforces them, in this order (the first failure wins and names the plugin, file and field):
1. shape, then `checkInstallValues` (`E_INSTALL_VALUE`, before Ajv so the specific code wins), `checkUrl` on each asset
   (`E_URL_NOT_HTTPS` / `E_URL_LATEST`), `parseMinisigText` on each signature (`E_SIGNATURE_TEXT`);
2. the C2 schema (`E_MANIFEST_SCHEMA`);
3. `tag === name.toLowerCase() + "/v" + version`; each asset `name` is `<name>-<version>-Windows-x64.exe`,
   `<name>-<version>-macOS.pkg` or `<name>-<version>-Linux-x64.tar.gz` for its key (`release.yml:524-526`);
   `install.vst3_bundle === name + ".vst3"` and `install.au_component === name + ".component"` (`E_MANIFEST_SCHEMA`);
4. each `url === "https://github.com/" + repo + "/releases/download/" + tag + "/" + asset.name`, with the slash in the
   tag literal, not `%2F` (`E_URL_NOT_PER_TAG`);
5. each signature's trusted comment equals `formatTrustedComment(name, version, key, asset.name)` (`E_TC_*`,
   `E_TC_BINDING`, section 5);
6. on the `tar.gz` asset: `list` is its own canonical form (`E_LISTING_NOT_CANONICAL`, and the C4 path rules with their
   own codes), `payload.files === list.length` (`E_LISTING_COUNT`), `payload.sha256 === digest(list)`
   (`E_LISTING_DIGEST`), and `list` passes the C4 layout check with `install.vst3_bundle` (`E_LAYOUT`).

**C2 covers C1 for copied data (PD-10).** gen-feed copies `assets` (minus `name` and `payload.list`) and `install` into
C1, so every C1 rule on those values has a C2 counterpart above. gen-feed also checks each assembled entry on its own
against C1 (`checkAssembledEntry`) and degrades the plugin with `E_MANIFEST_SCHEMA` on a failure, so a manifest can never
reach the `E_OUTPUT_SCHEMA` abort.

## 5. C3: trusted comments and the known-answer fixture

**Grammar (FR-020).** Exactly one line, no CR and no LF, six `:`-separated fields:

```
krate:1:<Name>:<version>:<platform-key>:<asset-file-name>
```

- field 0 is `krate` and field 1 is `1` (the grammar version), else `E_TC_FIELDS` (also for a field count other than six
  and for a CR or LF);
- `<Name>` follows the C1 plugin-key rule (FR-002, PD-14), else `E_TC_NAME`;
- `<version>` is the bounded core with an optional SemVer 2.0.0 prerelease suffix and no build metadata (which keeps `+`
  out), else `E_TC_VERSION`;
- `<platform-key>` is one of the **five** defined keys (stricter than the feed's open keys: a signature is only ever
  made for a defined key), else `E_TC_PLATFORM`;
- `<asset-file-name>` matches `^[A-Za-z0-9][A-Za-z0-9._-]*$` and is neither `.` nor `..` (so no `/`, `\` or `:`), else
  `E_TC_ASSET`. An empty field fails its own rule.

`contract.js` exports `parseTrustedComment(str)` and `formatTrustedComment(name, version, platform, asset)`, which
validates its inputs; parsing its output returns them unchanged.

**Binding rule (FR-022).** minisign signs the trusted comment with the global signature
(`ed25519(<signature> || <trusted_comment>)`, https://jedisct1.github.io/minisign/ "Signature format", minisign 0.12),
so the comment cannot be changed without the key. A verifier first checks the signature cryptographically (Phase 2's
`tools/minisign.js`, Phase 7's Rust), then `checkBinding(trustedComment, {name, version, platform, asset})`: every field
must equal the metadata of the entry it is used for, the asset name taken from the feed URL's last segment (D8). The
first differing field is named in `E_TC_BINDING`. A valid signature for asset A presented with asset B's metadata fails,
so one asset's signature can never stand in for another's.

**`.minisig` text form (D7).** A C1/C2 `signature` value is the full four-line `.minisig` text:

```
untrusted comment: <anything>
<base64: 'ED' (2 bytes) || key id (8 bytes) || ed25519 signature (64 bytes)>
trusted comment: <C3 comment>
<base64: global signature (64 bytes)>
```

with **LF** line endings and a final LF. The trusted-comment prefix is exactly `trusted comment: ` (17 bytes) and the
comment is the untrimmed rest of the line. `parseMinisigText` accepts LF or CRLF in a `.minisig` file, requires four
non-empty lines, canonical base64 (a round trip must reproduce the text), a 74-byte blob starting `0x45 0x44` (`ED`,
the hashed format; legacy `Ed` is rejected) and a 64-byte global signature (`E_SIGNATURE_TEXT` otherwise). Writers
normalise to LF with `toLfMinisig`, which changes no signed byte. A CR anywhere in a C1/C2 value is `E_SIGNATURE_TEXT`.
Base64-wrapped signatures (the Tauri `.sig` style) are not used for plugin assets; the manager's own updates keep
Tauri's format (Phase 11).

**The known-answer fixture (FR-023).** `fixtures/kat/` holds the only real signature in this tree:
- `KatPlugin-1.2.3-Linux-x64.tar.gz`, a GNU-format gzip tar in the C4 layout (so it is also a listing fixture);
- `KatPlugin-1.2.3-Linux-x64.tar.gz.minisig`, trusted comment
  `krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz`;
- `kat.pub`, the public key (key id `440FA75168B852F7`).

How it was made (`fixtures/kat/README.md` records every command and its output): on 2026-10-30, on Windows 11 with Node
v24.7.0, the archive was built in WSL2 with GNU tar 1.35 (fixed owner, mtime and order, `gzip -n -9`); a throwaway key
pair was generated with `rsign generate -W` (rsign2 0.6.7) in a fresh `fs.mkdtemp` directory outside the repo; the
archive was signed with `rsign sign -W -t "<C3 comment>"`; the public key was copied into the fixture directory and the
key directory was deleted. The `.minisig` went through `toLfMinisig` (D7). `rsign verify` on the committed files printed
`Signature and comment signature verified` and exited 0. No secret-key file exists anywhere under `tools/feed-contract/`
(FR-024, enforced by a test), and the fixture is never regenerated.

## 6. C4: Linux archive layout and canonical listing

**Windows and macOS have no archive layout (FR-034).** The manager downloads the existing `.exe` / `.pkg` asset and
runs it verbatim; the installer places the files. Their feed entries therefore carry no `payload`. Only the Linux
`tar.gz` is extracted by the manager, so only it has a layout and a listing.

**Root layout (FR-033).** No wrapping folder (`release.yml:402-403`: `cd staging; tar -czvf … *`). The root holds exactly
`<vst3_bundle>` (directory), `presets` (directory), `README.txt` (file) and `CHANGELOG.txt` (file), as
`release.yml:384-395` stages them. The bundle holds `Contents/Resources/moduleinfo.json` (file, the detection source)
and `Contents/x86_64-linux/<Name>.so` (file, `<Name>` = the bundle name minus `.vst3`). A missing item, an item of the
wrong type, or any other root item is `E_LAYOUT` naming the item (`listing.js` `checkLinuxLayout`).

**Canonical listing grammar (FR-030).** One line per entry:

```
<type> <mode> <path>
l <mode> <path> -> <target>
```

- `<type>` is `f` (file), `d` (directory) or `l` (symlink);
- `<mode>` is four octal digits, normalised (D3): a file is `0755` when any exec bit (`mode & 0o111`) is set, else
  `0644`; every directory is `0755`; every symlink is `0777`. Raw modes vary with the umask and Windows has none; the
  exec bit is the only mode fact Phase 8 keeps;
- `<path>` is the relative POSIX path without a leading `./` and without a trailing `/`; the archive root entry (`.` or
  `./`) is dropped; every ancestor directory implied by a path but absent as an entry is added as `d 0755`, so an
  archive's listing and the listing of its extracted tree agree;
- lines are sorted by the UTF-8 bytes of `<path>` (`Buffer.compare`); each line, the last included, ends in LF.

**Digest.** `payload.sha256` is the lower-case hex SHA-256 of the listing text (every line plus its LF, UTF-8);
`payload.files` is the number of lines. The KAT archive's listing, as `listing.js` computes it:

```
f 0644 CHANGELOG.txt
d 0755 KatPlugin.vst3
d 0755 KatPlugin.vst3/Contents
d 0755 KatPlugin.vst3/Contents/Resources
f 0644 KatPlugin.vst3/Contents/Resources/moduleinfo.json
d 0755 KatPlugin.vst3/Contents/x86_64-linux
f 0644 KatPlugin.vst3/Contents/x86_64-linux/KatPlugin.so
f 0644 README.txt
d 0755 presets
f 0644 presets/Default.preset
```

10 lines, digest `db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6`.

**Sources.** `listing.js` builds the listing from in-memory entries, from a `.tar.gz` (`listingFromTarGz`: gzip via
`node:zlib`, ustar headers including the POSIX `prefix` field, GNU long-name / long-link records `L` / `K`, pax `x`
records `path` / `linkpath`, pax `g` records ignored; every header checksum verified) or from an extracted POSIX tree
(`listingFromDirectory`: `lstat`, never follows a link, never resolves a real path; on Windows it throws
`E_LISTING_NO_POSIX_MODES`, D4). All three share the rules below.

**Path and symlink rules, in check order (FR-031, PD-12).** The first failure wins.
1. Per entry, path: valid UTF-8 (`E_LISTING_PATH`); not empty, no NUL / CR / LF, no ` -> ` substring (`E_LISTING_PATH`);
   no leading `/` (`E_LISTING_ABSOLUTE`); no `^[A-Za-z]:` drive prefix (`E_LISTING_DRIVE`); no `\`
   (`E_LISTING_BACKSLASH`); after one leading `./` (and a single trailing `/` on a directory) is dropped, no empty, `.`
   or `..` segment (`E_LISTING_DOTDOT`, entry paths only); only file, directory and symlink entries: hard links,
   character and block devices, FIFOs, contiguous files, GNU `D` / `S` / `V` / `M` and every other type are
   `E_LISTING_TYPE`.
2. Per symlink: a symlink with a one-segment path (at the root) is `E_LISTING_ROOT_SYMLINK`. The target obeys the path
   character rules (`E_LISTING_PATH`, `E_LISTING_ABSOLUTE`, `E_LISTING_DRIVE`, `E_LISTING_BACKSLASH`). **Form rule:** its
   `/`-segments are zero or more leading `..`, then one or more segments none of which is empty, `.` or `..`, so
   `a/../b`, `./a`, `a//b`, `..`, `../..` and a trailing `/` fail (`E_LISTING_SYMLINK_FORM`). **Containment:** resolved
   lexically from the link's parent directory, the target must land strictly inside the link's own top-level entry (the
   first segment of its path): `<Name>.vst3/Contents/x -> ../../README.txt` stays in the archive but leaves the bundle,
   and is `E_LISTING_SYMLINK_ESCAPE`, as is a target above the root.
3. Across entries: (a) no entry (`f`, `d` or `l`) has a symlink's path as a proper ancestor, because it would be written
   through the link (`E_LISTING_THROUGH_SYMLINK`, checked before implied ancestors are added); (b) no path appears
   twice, and no path is a file in one entry and an implied directory in another (`E_LISTING_DUPLICATE`); (c) neither a
   link's resolved target nor any proper prefix of it is the path of a symlink, itself included: no link to a link,
   through a link, to itself, and no loop (`E_LISTING_SYMLINK_CHAIN`).

Why the three PD-12 rules suffice: with no entry under a link, every ancestor of a link is a real directory, so the
leading `..` run resolves the same lexically and on disk; the remaining segments descend, and since no prefix of the
result is a link, the descent never passes through one. Lexical and physical resolution agree for every accepted
listing.

**Archive errors.** A bad header checksum is `E_LISTING_CHECKSUM`; a stream that ends inside a header, inside file data
or before the end-of-archive block is `E_LISTING_TRUNCATED`; a gzip error, a non-octal or base-256 numeric field or a
malformed pax record is `E_LISTING_CORRUPT`.

**Phase 8 obligations.** The listing rules make an escape impossible on paper; the extractor must also make it
impossible on disk:
1. Phase 8 applies the same rules, in the same order, to the archive it actually extracts (after the artifact is
   verified, Phase 7), not only to the listing in a manifest, and rebuilds the canonical listing from the extracted
   tree and compares `payload.files` and `payload.sha256` with the feed entry and the root with the layout above.
2. After extraction it checks physically that every extracted entry stays under the staging root:
   `fs.realpath` in Node, `std::fs::canonicalize` in Rust, of every entry must lie inside the canonicalized staging
   directory. A lexical check alone is not enough.
3. It preserves in-bundle relative symlinks and exec bits, applies no extended attributes, and never adds quarantine.

## 7. gen-feed: modes, rules, codes and exit codes

`tools/gen-feed.js` is a pure generator from files to `versions.json`: no network, no `git` call; inputs are files
(FR-041). Its core (`buildFeed`, `buildLegacyFeed`, `projectV1`) is exported, and the CLI is an exported
`main(argv, {stdout, stderr})` returning the exit code. The tool contains no plugin name (ROSTER): every name comes from
a `version.json`.

**Modes.**

```
node tools/gen-feed.js --plugins-dir <dir> --releases <file> --manifests <dir> --repo <owner/name>
                       [--manager-latest-url <url>] --out <file>
node tools/gen-feed.js --legacy --plugins-dir <dir> --tags <file> --out <file>
node tools/gen-feed.js --project-v1 <feed>
```

- **v2 mode** (FR-041 to FR-049): `--plugins-dir` holds one directory per plugin with `version.json`, optional
  `CHANGELOG.md` and `installers/windows/setup.iss`; `--releases` is the JSON array that
  `gh release list --json tagName,isPrerelease,isDraft,publishedAt` emits; `--manifests` holds zero or more
  `release.json` files (`*.json`). Plugins are emitted in bytewise order of their directory names; entry keys in the
  fixed order `version, download_url, release_notes, tag, pub_date, channel, release_notes_url, notes_markdown,
  platforms, install, prerelease`; `platforms` keys bytewise. The document is self-checked against C1, the code rules of
  section 2 and v1 before it is written (`E_OUTPUT_SCHEMA`), as `JSON.stringify(doc, null, 2) + "\n"`, UTF-8, LF, to a
  temp file in the `--out` directory that is then renamed into place, so a failed run never leaves a partial or replaced
  file.
- **Legacy mode** (FR-040): reproduces today's generator (`release.yml:645-685`) byte for byte from bare tag names
  (one per line, as `git tag -l` prints them; an empty line other than the last, whitespace, `^` or a `refs/` prefix is
  `E_INPUT` naming the line). Per plugin the tag is the first `<name lower-cased>/v*` tag in git's `-v:refname` order
  (git `versioncmp` with no `versionsort.suffix`, so a suffixed tag sorts above its release, as today); a plugin without
  a tag is skipped; the entry is `{version, download_url: url, release_notes: ""}` and the document
  `{schema_version: 1, plugins}`.
- **v1 projection** (FR-047): prints `{schema_version: 1, plugins: {<k>: {version, download_url, release_notes}}}` of a
  v2 feed, in plugin order, formatted as legacy mode: what a v1 reader sees. Phase 3's dry run compares with it.

**The Q9 rule (FR-042), applied here and nowhere else.** Per plugin, drafts are ignored; of the releases whose tag is
`<name lower-cased>/v<version>`, the **stable** entry is the non-prerelease with the highest SemVer 2.0.0 precedence,
and its version is the v1 `version`. A prerelease never changes it. A non-prerelease whose version carries a
prerelease suffix is `E_TAG_FORMAT` (the v1 key would break `SemVer::parse`), as is a version outside the bounded core.
The `prerelease` sibling is the prerelease with the highest precedence strictly greater than the stable version; there
is none otherwise. A plugin with no stable release is omitted; tags that match no plugin are ignored.

**Install facts (FR-043).** A manifest whose tag is the selected stable or sibling tag must pass C2 and section 4's
cross-field rules, then: **check 1**, only when `version.json` `version` equals the manifest's `version` (otherwise it
is skipped silently): `install.preset_subdir` equals `version.json` `preset_subdir` (both absent or both equal) and
`install.macos_pkg_id` equals `version.json` `identifier`, or `com.krateaudio.<name lower-cased>` when that is absent;
**check 2**, always: the first `AppId={{<GUID>}` line of `installers/windows/setup.iss`, with Inno's doubled brace read
as one, gives `{<GUID>}`, and `install.windows_uninstall_key` must equal `{<GUID>}_is1`. A missing file, no `AppId=` line
or a different value fails (`E_MANIFEST_MISMATCH`). Without a manifest the entry is v1-only.

**The CHANGELOG cut (FR-044, PD-4).** `notes_markdown` is the lines of `<plugin dir>/CHANGELOG.md` after the line
starting with the literal string `## [<version>]` up to the next line starting `## [`, CRLF normalised to LF, leading
and trailing blank lines trimmed; the key is omitted when the file or section is missing or the section is empty. The
header is matched as a literal string, not as `release.yml:462-463`'s awk regex (whose `.` matches any character); for
every real header (`## [1.0.4] - 2026-10-29`) both cut the same lines. The sibling uses its own version's section.

**Degrade, never break the feed (FR-049, Clarifications Q3 and 2026-10-30 Q-A).** A degrade-class failure found for one
plugin drops `platforms` and `install` from that plugin's entry and from its sibling, keeps every other key exactly as
it would be without a manifest, prints one `gen-feed: WARN <CODE>: <message>` line, writes the feed and exits 3. An
unparseable `release.json` (or one whose `tag` is not a string) is ignored with a WARN naming the file, because no
plugin can be named from it. Two manifest files with the same `tag` are both dropped; when that tag is a selected tag
its plugin degrades with `E_MANIFEST_DUPLICATE`. `E_INSTALL_DUPLICATE` degrades every plugin that shares the value. An
abort-class failure anywhere wins: the first stderr line is `gen-feed: <CODE>: <message>`, nothing is written and a
pre-existing `--out` file stays untouched.

**Code table.** The class column is `contract.js` `CODES` (the same table as data). Classes: `usage` (exit 2), `abort`
(exit 1, nothing written), `degrade` (the plugin degrades, exit 3), `abort-or-degrade` (abort when the URL is a
`version.json` `url`, the v1 `download_url`; degrade when it is in a `release.json`), `none` (not a gen-feed outcome).
Phase 5's Rust client reuses these names where it rejects the same thing.

| Code | Class | Raised by | Meaning |
|---|---|---|---|
| `E_USAGE` | usage | gen-feed CLI | bad or missing flags, a repeated flag, conflicting modes (stderr: the usage line) |
| `E_INPUT` | abort | gen-feed | a missing, unreadable or unparseable input file, a malformed `releases.json` row, a bad `--tags` line, a bad `--repo`, a bad `--manager-latest-url`, a duplicate `tagName` in `releases.json`, a non-draft `publishedAt` outside the `pub_date` pattern, a failed `--out` write |
| `E_PLUGIN_NAME` | abort | gen-feed | a `version.json` `name` that is not a string, not `^[A-Z][A-Za-z0-9]{0,63}$`, or a Windows reserved device name in any case (PD-14) |
| `E_V1_TYPE` | abort | gen-feed | an input that would put a non-string in a v1 key (a non-string `version.json` `url`) |
| `E_DUPLICATE_PLUGIN` | abort | gen-feed | two plugin directories whose `name`s are equal after `toLowerCase()` (PD-13: `Alpha` and `ALPHA` name one folder on NTFS and default APFS and select the same `alpha/v*` tags) |
| `E_TAG_FORMAT` | abort | gen-feed, `contract.js` | a non-draft release whose tag is `<p>/v…` but whose version fails the bounded core (stable) or core plus suffix (prerelease); a suffixed version on a non-prerelease |
| `E_URL_NOT_HTTPS` | abort-or-degrade | `contract.js` `checkUrl` | a URL that does not match `^https://[^\s]+$` |
| `E_URL_LATEST` | abort-or-degrade | `contract.js` `checkUrl` | a URL that contains `/releases/latest/` |
| `E_URL_NOT_PER_TAG` | degrade | `checkReleaseManifest` | an asset URL other than `https://github.com/<repo>/releases/download/<tag>/<asset name>` |
| `E_OUTPUT_SCHEMA` | abort | gen-feed self-check | the generated document fails C1, v1 or the `validateFeed` code rules (a generator bug) |
| `E_MANIFEST_SCHEMA` | degrade | `checkReleaseManifest`, `checkAssembledEntry`, gen-feed | a C2 schema failure, a `channel` key, an unparseable manifest file (PD-8), the tag / name / version / asset-name / `vst3_bundle` / `au_component` naming rules of FR-017; an assembled entry that fails C1 on its own (PD-10) |
| `E_MANIFEST_DUPLICATE` | degrade | gen-feed | two manifest files with the same `tag` (both ignored; the plugin degrades when the tag is selected) (PD-8, Clarifications 2026-10-30 Q-A) |
| `E_MANIFEST_MISMATCH` | degrade | gen-feed | FR-043 check 1 (`preset_subdir`, `macos_pkg_id`) or check 2 (`windows_uninstall_key` against the `setup.iss` AppId, a missing file, no `AppId=` line) |
| `E_SIGNATURE_TEXT` | degrade | `parseMinisigText` | not four lines, a missing prefix, bad or non-canonical base64, a blob other than 74 bytes, an algorithm other than `ED`, a global signature other than 64 bytes, a CR in a C1/C2 value |
| `E_TC_FIELDS` | degrade | `parseTrustedComment` | not exactly six `:` fields, field 0 not `krate`, field 1 not `1`, a CR or LF in the comment |
| `E_TC_NAME` | degrade | `parseTrustedComment` | the `<Name>` field fails FR-020 (empty included) |
| `E_TC_VERSION` | degrade | `parseTrustedComment` | the `<version>` field fails FR-020 (empty included) |
| `E_TC_PLATFORM` | degrade | `parseTrustedComment` | the `<platform-key>` field is not one of the five defined keys |
| `E_TC_ASSET` | degrade | `parseTrustedComment` | the `<asset-file-name>` field fails FR-020 (empty, `.` and `..` included) |
| `E_TC_BINDING` | degrade | `checkBinding` | a trusted-comment field differs from the expected metadata (names the first differing field) |
| `E_INSTALL_VALUE` | degrade | `checkInstallValues` | an `install` value with `/`, `\`, `:`, `..`, a reserved device stem or a trailing space (PD-14), or outside its FR-008 pattern |
| `E_INSTALL_DUPLICATE` | degrade | `validateFeed`, gen-feed | FR-014: two plugins share an install value; `vst3_bundle`, `au_component`, `macos_pkg_id` compared after ASCII case folding, `windows_uninstall_key` exactly (PD-13); every plugin sharing the value degrades |
| `E_LAYOUT` | degrade | `checkLinuxLayout` | an FR-033 root or bundle item missing, of the wrong type, or extra (names the item) |
| `E_LISTING_PATH` | degrade | `listing.js` | invalid UTF-8, NUL / CR / LF, empty: in an entry path or a link target; a ` -> ` substring in an entry path |
| `E_LISTING_ABSOLUTE` | degrade | `listing.js` | a leading `/` in an entry path or a link target |
| `E_LISTING_DRIVE` | degrade | `listing.js` | a `^[A-Za-z]:` drive prefix in an entry path or a link target |
| `E_LISTING_BACKSLASH` | degrade | `listing.js` | a `\` in an entry path or a link target |
| `E_LISTING_DOTDOT` | degrade | `listing.js` | an empty, `.` or `..` segment in an entry path (after one leading `./` is dropped); never raised for a link target |
| `E_LISTING_DUPLICATE` | degrade | `listing.js` | the same path twice, or a file that is also an implied directory |
| `E_LISTING_ROOT_SYMLINK` | degrade | `listing.js` | a symlink whose path has one segment |
| `E_LISTING_SYMLINK_FORM` | degrade | `listing.js` | a link target that is not "zero or more leading `..`, then one or more segments none of which is empty, `.` or `..`" (PD-12) |
| `E_LISTING_SYMLINK_ESCAPE` | degrade | `listing.js` | the lexically resolved target is not strictly inside the link's own top-level entry (FR-031) |
| `E_LISTING_SYMLINK_CHAIN` | degrade | `listing.js` | the resolved target, or a proper prefix of it, is the path of a symlink (a link to a link, through a link, to itself, a loop) (PD-12) |
| `E_LISTING_THROUGH_SYMLINK` | degrade | `listing.js` | an entry whose path has a symlink's path as a proper prefix (it would be written through the link) (PD-12) |
| `E_LISTING_TYPE` | degrade | `listing.js` | a hard link `1`, a device `3` / `4`, a FIFO `6`, contiguous `7`, GNU `D` / `S` / `V` / `M`, any other typeflag or file type |
| `E_LISTING_CHECKSUM` | degrade | `listingFromTarGz` | a bad tar header checksum |
| `E_LISTING_TRUNCATED` | degrade | `listingFromTarGz` | the stream ends inside a header, inside file data, or before the end-of-archive block |
| `E_LISTING_CORRUPT` | degrade | `listingFromTarGz` | a gzip error, a non-octal or base-256 numeric field, a malformed pax record |
| `E_LISTING_NOT_CANONICAL` | degrade | `checkReleaseManifest` | a `payload.list` that is not its own canonical form |
| `E_LISTING_COUNT` | degrade | `checkReleaseManifest` | `payload.files` differs from `list.length` |
| `E_LISTING_DIGEST` | degrade | `checkReleaseManifest` | `payload.sha256` differs from `digest(list)` |
| `E_LISTING_NO_POSIX_MODES` | none | `listingFromDirectory` | called on Windows, which has no POSIX modes (D4) |
| `E_SCHEMA` | none | `validateFeed`, `validateRelease` | an Ajv finding (carries `instancePath` and `keyword`) |

**Exit codes of `gen-feed.js`.**

- `0`: clean; the feed was written and no plugin degraded.
- `1`: abort; nothing was written and a pre-existing `--out` file is untouched.
- `2`: usage error; the usage line on stderr, nothing written.
- `3`: the feed was written with at least one degraded plugin (one WARN line per degraded plugin, or per ignored
  unparseable manifest file), so `feed.yml` (Phase 3) can deploy the safe feed and still turn the run red.

`--legacy` and `--project-v1` return only `0`, `1` or `2`.

## 8. Fixture signatures

Fixture signatures are **structurally valid only**. Every `signature` value in the JSON fixtures (`fixtures/feeds/`,
`fixtures/release/`, the gen-feed case manifests) is built by `fixtures/make-fixtures.js` as

```
untrusted comment: krate fixture signature (not cryptographic)
<base64 of 'ED' + 8 zero bytes (key id) + 64 zero bytes>
trusted comment: <the C3 comment that binds the asset>
<base64 of 64 zero bytes>
```

with LF endings: the blobs are zero-filled. They pass `parseMinisigText` and the C3 binding check, so they exercise
every structural rule, but no later phase may expect them to verify cryptographically. The KAT (`fixtures/kat/`,
section 5) is the only cryptographic fixture: Phases 2 and 7 verify it with their minisign implementations.
