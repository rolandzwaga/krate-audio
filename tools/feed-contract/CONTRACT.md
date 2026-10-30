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
| `E_INPUT` | abort | gen-feed, `minisign.js`, `release-facts.js`, `gen-release-manifest.js`, `backfill-sign.js` | a missing, unreadable or unparseable input file, a malformed `releases.json` row, a bad `--tags` line, a bad `--repo`, a bad `--manager-latest-url`, a duplicate `tagName` in `releases.json`, a non-draft `publishedAt` outside the `pub_date` pattern, a failed `--out` write |
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
| `E_SIGNATURE_TEXT` | degrade | `parseMinisigText`, `minisign.js` verify | not four lines, a missing prefix, bad or non-canonical base64, a blob other than 74 bytes, an algorithm other than `ED`, a global signature other than 64 bytes, a CR in a C1/C2 value |
| `E_TC_FIELDS` | degrade | `parseTrustedComment` | not exactly six `:` fields, field 0 not `krate`, field 1 not `1`, a CR or LF in the comment |
| `E_TC_NAME` | degrade | `parseTrustedComment` | the `<Name>` field fails FR-020 (empty included) |
| `E_TC_VERSION` | degrade | `parseTrustedComment` | the `<version>` field fails FR-020 (empty included) |
| `E_TC_PLATFORM` | degrade | `parseTrustedComment` | the `<platform-key>` field is not one of the five defined keys |
| `E_TC_ASSET` | degrade | `parseTrustedComment` | the `<asset-file-name>` field fails FR-020 (empty, `.` and `..` included) |
| `E_TC_BINDING` | degrade | `checkBinding`, `gen-release-manifest.js` | a trusted-comment field differs from the expected metadata (names the first differing field) |
| `E_INSTALL_VALUE` | degrade | `checkInstallValues` | an `install` value with `/`, `\`, `:`, `..`, a reserved device stem or a trailing space (PD-14), or outside its FR-008 pattern |
| `E_INSTALL_DUPLICATE` | degrade | `validateFeed`, gen-feed | FR-014: two plugins share an install value; `vst3_bundle`, `au_component`, `macos_pkg_id` compared after ASCII case folding, `windows_uninstall_key` exactly (PD-13); every plugin sharing the value degrades |
| `E_LAYOUT` | degrade | `checkLinuxLayout`, `release-facts.js`, `backfill-sign.js` `facts` | an FR-033 root or bundle item missing, of the wrong type, or extra (names the item) |
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
| `E_LISTING_DIGEST` | degrade | `checkReleaseManifest`, `gen-release-manifest.js` | `payload.sha256` differs from `digest(list)` |
| `E_LISTING_NO_POSIX_MODES` | none | `listingFromDirectory` | called on Windows, which has no POSIX modes (D4) |
| `E_SCHEMA` | none | `validateFeed`, `validateRelease` | an Ajv finding (carries `instancePath` and `keyword`) |
| `E_MINISIG_PUBKEY` | none | `minisign.js` `parsePublicKey` | public-key text: first line not `untrusted comment: `, second line not canonical base64, not 42 bytes, algorithm not `Ed` |
| `E_MINISIG_SECRET_KEY` | none | `minisign.js` `decryptSecretKey` | secret-key text malformed: not 158 bytes decoded, unknown `sig_alg` / `kdf_alg` / `chk_alg`, scrypt cost above the limits (PD-2), public half ≠ the seed's public key (PD-3) |
| `E_MINISIG_PASSWORD` | none | `minisign.js` `decryptSecretKey` | checksum mismatch after decryption: wrong password or damaged key |
| `E_MINISIG_KEY_ID` | none | `minisign.js` `verifyBuffer` / `verifyFile` | the signature's key id differs from the public key's |
| `E_MINISIG_SIGNATURE` | none | `minisign.js` | the Ed25519 signature over Blake2b-512 of the data does not verify |
| `E_MINISIG_GLOBAL` | none | `minisign.js` | the global signature over signature ‖ trusted comment does not verify |
| `E_FACTS_NO_MACHO` | none | `release-facts.js` | the expanded pkg holds no Mach-O |
| `E_FACTS_ARCH` | none | `release-facts.js` | a thin 32-bit or big-endian 64-bit Mach-O, an unknown `cputype`, an arm64 subtype other than ALL/V8, a 32-bit or unknown fat slice |
| `E_FACTS_ARCH_MISMATCH` | none | `release-facts.js` | two Mach-Os in one pkg report different architecture sets |
| `E_FACTS_NO_MINOS` | none | `release-facts.js` | a slice has neither `LC_BUILD_VERSION` (macOS) nor `LC_VERSION_MIN_MACOSX` |
| `E_FACTS_MACHO_CORRUPT` | none | `release-facts.js` | a header, slice or load command runs past the buffer; `nfat_arch` 0; `cmdsize` < 8 |
| `E_FACTS_NO_GLIBC` | none | `release-facts.js` | `objdump -T` output names no `GLIBC_<n>.<n>[.<n>]` |
| `E_FACTS_LISTING_MISMATCH` | none | `release-facts.js` | the extracted tree's listing differs from the tarball's (names the first differing line) |
| `E_FACTS_LIPO_MISMATCH` | none | `backfill-sign.js` `facts` | `lipo -archs` disagrees with the Node architecture set (names the file) |
| `E_FACTS_STALE` | none | `gen-release-manifest.js` | a facts file's `asset`, `size` or `sha256` differs from the asset in `--assets` |
| `E_FACTS_MISSING` | none | `backfill-sign.js` `apply` | a planned release has no darwin or linux facts file and no facts-report entry |
| `E_ASSET_MISSING` | none | `gen-release-manifest.js` | one of the three assets or its `.minisig` is absent from `--assets` |
| `E_VERSION_MISMATCH` | none | `backfill-sign.js` `apply` | the tag's `version.json` `version` differs from the tag's version |
| `E_SIGNING_CONFIG` | none | `gen-release-manifest.js` `signingConfig`; `backfill-sign.js` `apply` (PD-23) | a real run without a non-empty key secret, a non-empty password secret and a parsable committed public key; or a public key equal to a test key (`fixtures/kat/kat.pub`, `fixtures/phase2/kat-secret/kat.pub`), or those files unreadable in real mode |
| `E_BACKFILL_LIST_TRUNCATED` | none | `backfill-sign.js` `plan` | `gh release list --limit 1000` returned 1000 rows |
| `E_BACKFILL_ASSET_MISSING` | none | `backfill-sign.js` `plan` | a current release lacks one of its three asset names |
| `E_BACKFILL_IMMUTABLE` | none | `backfill-sign.js` `plan` | the release is immutable (`isImmutable: true`) |
| `E_BACKFILL_PARTIAL` | none | `backfill-sign.js` `plan` | `SHA256SUMS` or a `.minisig` exists without `release.json` |
| `E_BACKFILL_SIZE` | none | `backfill-sign.js` `download` | a downloaded asset's size differs from the release listing |
| `E_BACKFILL_DIGEST` | none | `backfill-sign.js` `download` | a downloaded asset's SHA-256 differs from GitHub's `digest` |
| `E_BACKFILL_ASSETS_CHANGED` | none | `backfill-sign.js` `apply` (audit) | a release's asset list after the run differs from `assets_before` (+ uploads on a live run) |
| `E_SIGN_KEY_MISMATCH` | none | `backfill-sign.js` `apply` | a signature the run produced does not verify against `--public-key` |
| `E_FEED_LIST_TRUNCATED` | none | `feed-publish.js collect` | `gh release list --limit 1000` returned 1000 rows; releases may be missing |
| `E_FEED_DOWNLOAD` | none | `feed-publish.js collect` | a downloaded `release.json` / `latest.json` differs from its release asset record in size, or in SHA-256 where GitHub lists a digest |
| `E_FEED_SCHEMA` | none | `feed-publish.js compare` | the generated feed fails C1 v2 or v1 (`validateFeed`); the message names the first `instancePath` |
| `E_FEED_NONDETERMINISTIC` | none | `feed-publish.js generate` | a second gen-feed run on the same inputs gave different bytes, exit code or stderr (PD-9) |
| `E_STEP_FAILED` | none | `feed-publish.js outcome` | a step's tool exited other than 0 or 3 |
| `E_V1_DRIFT` | none | `feed-publish.js compare` | a v1 key of a plugin differs from the live feed and no FR-040 case explains it |
| `E_MANAGER_LATEST` | degrade | `feed-publish.js collect` | the manager release's `latest.json` fails the FR-025 check; nothing is copied, the plugin feed still deploys |

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

## 9. Phase 2 tools

Phase 2 (`specs/krate-manager-phase2/spec.md`, plan §4) adds four tools that make and check a release's manager files
(`SHA256SUMS`, one `.minisig` per asset, `release.json`): `tools/minisign.js`, `tools/release-facts.js`,
`tools/gen-release-manifest.js` and `tools/backfill-sign.js`. Each is a flat file with the `tools/gen-feed.js` form:
first line `#!/usr/bin/env node`, a header comment with the usage and exit codes, `main(argv, io)` returning the exit
code, `module.exports` last; tests call `main` in-process. Every expected failure is a `ContractError` whose code has
a row in the section 7 table (the 27 Phase 2 codes carry class `none` there: they are not gen-feed outcomes, and these
tools exit by the table in 9.2, not by a class). A failure prints one stderr line `<tool>: <CODE>: <message>`, where
`<tool>` is the file name without `.js`. No message, stdout or stderr line ever carries a secret value, a password, a
key byte or a base64 key line.

### 9.1 Command lines

The usage text each tool prints on stderr, with an `<tool>: E_USAGE: <why>` line, for a missing, unknown or repeated
flag (exit 2):

```
usage: node tools/minisign.js verify -p <public key file> -x <.minisig file> <file>
       node tools/minisign.js decrypt-key --out <file>   (key and password from the environment)

usage: node tools/release-facts.js darwin --pkg <file> --expanded <dir> --out <json>
       node tools/release-facts.js linux --tarball <file> --extracted <dir> --objdump <txt> --out <json>
       node tools/release-facts.js linux-precheck --tarball <file>

usage: node tools/gen-release-manifest.js --version-json <file> --setup-iss <file> --assets <dir>
         --darwin-facts <json> --linux-facts <json> --repo <owner/name> --pub-date <YYYY-MM-DDTHH:MM:SSZ>
         --public-key <file> --out <release.json> --sums-out <SHA256SUMS>
       node tools/gen-release-manifest.js --trusted-comments --version-json <file> --assets <dir>
         --darwin-facts <json>
       node tools/gen-release-manifest.js --signing-config --dry-run <true|false> --public-key <file>

usage: node tools/backfill-sign.js plan --plugins-dir <dir> --repo <owner/name> --out <plan.json>
       node tools/backfill-sign.js download --plan <plan.json> --format <inno-exe|pkg|tar.gz> --dir <dir>
            --repo <owner/name>
       node tools/backfill-sign.js facts --plan <plan.json> --os <darwin|linux> --dir <download dir>
            --work <dir> --out <facts dir>
       node tools/backfill-sign.js apply --plan <plan.json> --plugins-dir <dir> --facts <facts dir> --key <file>
            --public-key <file> --repo <owner/name> --work <dir> --out-dir <dir> [--dry-run]
```

What each mode writes and prints on success:

- `minisign.js verify`: checks in the FR-005 order (text shape `E_SIGNATURE_TEXT`, public key `E_MINISIG_PUBKEY`, key
  id `E_MINISIG_KEY_ID`, signature `E_MINISIG_SIGNATURE`, global signature `E_MINISIG_GLOBAL`) and prints
  `verified: <trusted comment>`. It never checks the C3 binding (section 5); callers run `checkBinding`.
- `minisign.js decrypt-key`: reads the key text from `KRATE_SIGNING_KEY` and its password from
  `KRATE_SIGNING_KEY_PASSWORD` (environment only; absent or empty is `E_INPUT` naming the variable). `--out` holds no
  `..` segment, its parent directory exists, and the parent's realpath joined with the basename is the realpath of
  `$RUNNER_TEMP` (the OS temp directory when unset) or lies under it, case-folded on Windows, with no entry there yet
  (plan PD-19). The unencrypted key (9.3) is written with mode `0600` through a `wx` temp file and a rename, then it
  prints `decrypted: key id <KEYID>`. Nothing is left behind on a failure.
- `release-facts.js darwin`: writes the darwin facts (9.4) and prints `<archs joined by a space>\t<path>` per Mach-O
  for the workflow's `lipo -archs` cross-check. `linux`: writes the linux facts (9.4), prints nothing. `linux-precheck`
  (plan PD-26): the tarball's listing (section 6 rules), exactly one root-level `<Name>.vst3` directory, the C4 layout
  and `<Name>.vst3/Contents/x86_64-linux/<Name>.so` as a regular-file entry, before anything extracts the archive;
  prints nothing. No mode runs a child process.
- `gen-release-manifest.js` (manifest mode): checks in the FR-026 order, then writes `SHA256SUMS` (9.6) and
  `release.json` (section 4) as one unit: both temp files first, both targets absent or regular files, the old
  `SHA256SUMS` moved aside, the new one renamed in, `release.json` renamed in last; any failure restores the old
  `SHA256SUMS` (plan PD-25). Prints nothing. `--trusted-comments` prints `<asset>\t<C3 comment>` per platform key,
  keys bytewise, and reads only `--version-json` and `--darwin-facts`. `--signing-config` prints `signing=throwaway`
  or `signing=on` (9.7).
- `backfill-sign.js`: every effect (`gh`, `git`, `pkgutil`, `lipo`, `tar`, `objdump`, `rsign`, the log) goes through
  an injected `deps` object; the production log is stdout. `plan` writes `plan.json` (9.5) and logs one line per
  plugin. `download` writes one format of every planned release into `<dir>/<tag with "/" as "-">/` and
  `<dir>/download-report.json`, then logs `digest checked: <n> of <m>`. `facts` writes
  `<out>/<Name>-<version>.<os>.json` (9.4) and `<out>/facts-report.<os>.json`. `apply` signs and verifies in pass 1, writes `SHA256SUMS`, the three
  `.minisig` and `release.json` to `<out-dir>/<tag with "/" as "-">/`, deletes the `--key` file, uploads in pass 2
  (not with `--dry-run`; `SHA256SUMS` and the three `.minisig` in one call, then `release.json` alone, never
  `--clobber`), audits every viewed release's asset list in pass 3, and logs one line per plugin
  (`<Name>: signed | skipped (already signed) | skipped (no stable release) | reported <CODE>: <message>`), then
  `uploads: <n>`.

### 9.2 Exit codes

| Tool | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| `minisign.js verify` | verified | rejected (`E_MINISIG_*`, `E_SIGNATURE_TEXT`) or unreadable input (`E_INPUT`) | usage | — |
| `minisign.js decrypt-key` | key written | `E_INPUT`, `E_MINISIG_PASSWORD`, `E_MINISIG_SECRET_KEY`; nothing left behind | usage | — |
| `release-facts.js darwin\|linux` | facts written | any `E_FACTS_*`, `E_LAYOUT`, `E_LISTING_*`, `E_INPUT`; `--out` untouched | usage | — |
| `release-facts.js linux-precheck` | listing and layout pass | `E_LAYOUT`, `E_LISTING_*`, `E_INPUT` | usage | — |
| `gen-release-manifest.js` (all modes) | written / printed | abort, nothing written or changed (FR-026, plan PD-25), `E_SIGNING_CONFIG` included | usage | — |
| `backfill-sign.js plan` | loop finished (per-release failures recorded in `plan.json`) | abort, no plan file written (`E_BACKFILL_LIST_TRUNCATED`, `E_TAG_FORMAT`, a `gh release list` failure, an unreadable `--plugins-dir`) | usage | — |
| `backfill-sign.js download\|facts` | loop finished (per-release failures recorded in the report) | abort (an unreadable `--plan` or download report, an uncreatable directory) | usage | — |
| `backfill-sign.js apply` | nothing reported | abort (`E_SIGNING_CONFIG`, `E_SIGN_KEY_MISMATCH`, `E_BACKFILL_ASSETS_CHANGED`, `E_INPUT` for a missing facts report, an unreadable `--plan` or `--public-key`, a `--key` file that cannot be removed, or a `gh` failure outside one release) | usage | at least one release reported (an upload failure included), the rest signed |

### 9.3 minisign byte layouts

Sources: the format page https://jedisct1.github.io/minisign/ (minisign 0.12) and the source of the `minisign` crate
0.9.1, the version rsign2 0.6.7 depends on (`rsign2-0.6.7/Cargo.toml:55-56`); paths below are under
`minisign-0.9.1/src/`. Offsets are into the base64-decoded line 2. All three files are text, LF or CRLF accepted on
input; the tools write LF with a final LF (`contract.js` `toLfMinisig`).

**Public key** (`tools/feed-contract/keys/krate-signing.pub`, `fixtures/kat/kat.pub`): two lines. Line 1 is
`untrusted comment: minisign public key: <KEYID>`, where `<KEYID>` is the key id read as a little-endian u64 and
printed `{:016X}` (`public_key.rs:165-172`), that is the eight stored bytes reversed in upper-case hex
(`fixtures/kat/kat.pub` line 1 ends `440FA75168B852F7`). Line 2 decodes to 42 bytes (`public_key.rs:89-101`,
`:123-127`):

| Bytes | Field | Source |
|---|---|---|
| 0-1 | signature algorithm `Ed` | `constants.rs:37` (`SIGALG`) |
| 2-9 | key id (8 bytes, random at generation) | `constants.rs:25` (`KEYNUM_BYTES`) |
| 10-41 | Ed25519 public key | `constants.rs:33` (`PUBLICKEY_BYTES`) |

`minisign.js` imports bytes 10-41 behind the RFC 8410 SubjectPublicKeyInfo header `302a300506032b6570032100`. Any
other line count, prefix, length or algorithm is `E_MINISIG_PUBKEY`.

**Signature** (`<asset>.minisig`): four lines (`signature_box.rs:133-160`):

```
untrusted comment: <free text; minisign.js writes "signature from krate minisign.js">
<base64 of the 74-byte signature blob below>
trusted comment: <the C3 comment of section 5>
<base64 of the 64-byte global signature>
```

| Line 2 bytes | Field | Source |
|---|---|---|
| 0-1 | algorithm `ED` (prehashed; legacy `Ed` is `E_SIGNATURE_TEXT`) | `constants.rs:36` (`SIGALG_PREHASHED`), `lib.rs:95` |
| 2-9 | key id, equal to the public key's or `E_MINISIG_KEY_ID` | `signature.rs:9-25` |
| 10-73 | Ed25519 signature over Blake2b-512 of the file | `lib.rs:41-47` (`prehash`, `PREHASH_BYTES` 64), `lib.rs:85`, `:100` |

Line 4 is the Ed25519 signature over the 64 signature bytes (line 2 bytes 10-73) followed by the UTF-8 trusted comment
without its `trusted comment: ` prefix (`lib.rs:103-108`). `rsign verify` prints `Signature and comment signature
verified` and `Trusted comment: <comment>` on stderr (`lib.rs:202-204`).

**Secret key** (rsign2, minisign and `tauri signer generate` keys): two lines; line 1 is an untrusted comment and
line 2 decodes to 158 bytes (`secret_key.rs:79-87`, read in this order by `secret_key.rs:170-203`). A
`TAURI_SIGNING_PRIVATE_KEY` secret holds that two-line text base64-encoded once more as one line;
`decryptSecretKey` accepts either form.

| Bytes | Field | Source |
|---|---|---|
| 0-1 | signature algorithm `Ed` | `constants.rs:37` |
| 2-3 | KDF algorithm: `Sc` (scrypt, encrypted) or `00 00` (unencrypted) | `constants.rs:22-23` |
| 4-5 | checksum algorithm `B2` | `constants.rs:19` |
| 6-37 | scrypt salt (32 bytes) | `secret_key.rs:83`, `constants.rs:24` |
| 38-45 | scrypt opslimit, u64 little-endian | `secret_key.rs:84` |
| 46-53 | scrypt memlimit, u64 little-endian | `secret_key.rs:85` |
| 54-61 | key id (8 bytes) | `constants.rs:25` |
| 62-125 | Ed25519 secret key: seed (32) then public key (32) | `constants.rs:34`, `crypto/ed25519.rs:57-70` |
| 126-157 | checksum: BLAKE2b with a 32-byte digest over bytes 0-1, 54-61 and 62-125 | `secret_key.rs:96-104`, `constants.rs:20` |

When bytes 2-3 are `Sc`, bytes 54-157 are XORed, in that order, with a 104-byte scrypt keystream of the UTF-8
password and the salt (`secret_key.rs:106-128`). The scrypt parameters come from minisign's `raw_scrypt_params(memlimit,
opslimit)` (`helpers.rs:45-74`), ported exactly; the default key (`MEMLIMIT` 33554432, `OPSLIMIT` 1048576,
`constants.rs:26-27`) gives N = 2^15, r = 8, p = 1, run with `maxmem` 2 x 128 x N x r. A memlimit above 1 GiB
(`MEMLIMIT_MAX`, `constants.rs:28`), log2(N) above 20 (`N_LOG2_MAX`, `constants.rs:29`) or a cost 128 x N x r x p
above 2 GiB (plan PD-2) is `E_MINISIG_SECRET_KEY`. The 32-byte checksum is not a truncated BLAKE2b-512 (the digest
length is in the BLAKE2b parameter block, RFC 7693 §2.5), so `minisign.js` carries its own RFC 7693 BLAKE2b for it.
A checksum mismatch after decryption is `E_MINISIG_PASSWORD` (rsign's "Wrong password for that key",
`secret_key.rs:288`); on an unencrypted key it is `E_MINISIG_SECRET_KEY`. Bytes 94-125 must equal the public key
derived from the seed (bytes 62-93), else `E_MINISIG_SECRET_KEY` (plan PD-3). `minisign.js` imports the seed behind
the RFC 8410 PKCS#8 header `302e020100300506032b657004220420`.

`decrypt-key` writes the unencrypted layout that `generate_unencrypted_keypair` writes (`keypair.rs:29-55`): bytes 2-3
`00 00`, bytes 6-53 zero, bytes 54-125 in clear, the checksum over the clear bytes, and the line-1 comment text
`krate decrypted secret key (runner temp only)`. `rsign sign -W` reads such a key without a password, because it tries
the key as unencrypted first (`rsign2-0.6.7/src/bin/rsign/main.rs:103-113`).

### 9.4 Facts files

Both are `JSON.stringify(doc, null, 2) + "\n"`, keys in the order shown, written through a temp file and a rename.
`gen-release-manifest.js` refuses a facts file whose `asset`, `size` or `sha256` differs from the asset it describes
(`E_FACTS_STALE`), and a linux `payload` that differs from the tarball's own listing (`E_LISTING_DIGEST`).

Darwin (`release-facts.js darwin --out`; `backfill-sign.js facts --os darwin` writes `<Name>-<version>.darwin.json`):

```
{
  "asset": "<Name>-<version>-macOS.pkg",
  "size": <bytes of the pkg>,
  "sha256": "<64 lower-case hex of the pkg>",
  "platform_key": "darwin-universal" | "darwin-aarch64" | "darwin-x86_64",
  "min_os": "<X.Y, or X.Y.Z when the patch is not 0>",
  "archs": ["arm64", "x86_64"],
  "macho": [
    { "path": "<path relative to the expanded pkg, / separators>", "archs": ["arm64", "x86_64"], "min_os": "<X.Y>" }
  ]
}
```

`archs` is the one architecture set every Mach-O reports (`E_FACTS_ARCH_MISMATCH` otherwise), sorted bytewise;
`platform_key` follows from it (`{arm64, x86_64}`, `{arm64}`, `{x86_64}`; any other set `E_FACTS_ARCH`); `min_os` is the
highest `minos` over every Mach-O and slice; `macho` lists every regular-file Mach-O, sorted bytewise by `path`;
symbolic links are skipped, never followed.

Linux (`release-facts.js linux --out`; `backfill-sign.js facts --os linux` writes `<Name>-<version>.linux.json`):

```
{
  "asset": "<Name>-<version>-Linux-x64.tar.gz",
  "size": <bytes of the tarball>,
  "sha256": "<64 lower-case hex of the tarball>",
  "min_os": "<the highest GLIBC_<n>.<n>[.<n>] in objdump -T of the .so, e.g. 2.34>",
  "payload": { "files": <number of canonical listing lines>, "sha256": "<digest(listing), section 6>" }
}
```

The listing is the tarball's (`listingFromTarGz`), and the extracted tree's listing must equal it line for line
(`E_FACTS_LISTING_MISMATCH`, naming the first differing line).

The two `backfill-sign.js` reports, `<dir>/download-report.json` and `<out>/facts-report.<os>.json`, are JSON arrays
of `{"tag": "<tag>", "code": "<CODE>", "message": "<message>"}`, always written (`[]` when empty); a facts report starts
with the download report's entries. `apply` treats a missing report as `E_INPUT` (that job never ran).

### 9.5 plan.json

Written by `backfill-sign.js plan`; releases sorted bytewise by `name`, one per `version.json` under `--plugins-dir`:

```
{
  "repo": "<owner>/<name>",
  "releases": [
    { "name": "<Name>", "dir": "<plugins/ subdirectory>", "tag": "<name lower-cased>/v<X.Y.Z>",
      "assets_before": ["<every asset name on the release at plan time, sorted bytewise>"],
      "status": "planned", "version": "<X.Y.Z>", "publishedAt": "<YYYY-MM-DDTHH:MM:SSZ>",
      "assets": [
        { "name": "<Name>-<X.Y.Z>-Windows-x64.exe", "format": "inno-exe", "size": <bytes>, "digest": "sha256:<hex>" | null },
        { "name": "<Name>-<X.Y.Z>-macOS.pkg", "format": "pkg", "size": <bytes>, "digest": "sha256:<hex>" | null },
        { "name": "<Name>-<X.Y.Z>-Linux-x64.tar.gz", "format": "tar.gz", "size": <bytes>, "digest": "sha256:<hex>" | null }
      ] },
    { "name": "<Name>", "dir": "<dir>", "status": "skipped-signed", "tag": "<tag>", "assets_before": ["…"] },
    { "name": "<Name>", "dir": "<dir>", "status": "skipped-no-stable" },
    { "name": "<Name>", "dir": "<dir>", "status": "reported", "code": "<CODE>", "message": "<message>", "tag": "<tag>",
      "assets_before": ["…"] }
  ]
}
```

The release per plugin is the one `gen-feed.js` `selectReleases` picks (section 7, the Q9 rule). Classification order:
`release.json` present → `skipped-signed`; an asset name missing → `E_BACKFILL_ASSET_MISSING`; `isImmutable` →
`E_BACKFILL_IMMUTABLE`; `SHA256SUMS` or a `.minisig` present → `E_BACKFILL_PARTIAL`; otherwise `planned`. A
`reported` entry whose `gh release view` failed has no `assets_before`. `digest` is GitHub's per-asset digest or `null`
(a best-effort check; `size` is the guaranteed one). Readers rebuild every asset name from `name` and `version` and
require `tag` to be `<name lower-cased>/v<version>`; a listed name is compared, never used as a path.

### 9.6 SHA256SUMS

Three lines, one per installer asset, `<64 lower-case hex>  <asset name>` (two spaces, the GNU `sha256sum` text
form), sorted bytewise by asset name, LF, final LF (spec D7), so `sha256sum -c SHA256SUMS` and
`shasum -a 256 -c SHA256SUMS` work in a download folder. It is not signed: C3 has no platform key for it, and every
asset it lists carries its own bound signature.

### 9.7 Signing configuration

`gen-release-manifest.js --signing-config` is the one decision of a release run (spec D6, FR-029; plan PD-23). It sees
the two secrets only as present or absent: the workflows map `secrets.TAURI_SIGNING_PRIVATE_KEY` to
`KRATE_SIGNING_KEY` and `secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD` to `KRATE_SIGNING_KEY_PASSWORD` in the jobs of
the `signing` environment. `--public-key` is `tools/feed-contract/keys/krate-signing.pub`.

| Run | Condition | Result |
|---|---|---|
| dry run (`--dry-run true`) | any; no key file is read | `signing=throwaway`: the job signs with a throwaway key made under `$RUNNER_TEMP` |
| real run | both variables non-empty, the public key file exists and parses (`parsePublicKey`), and it is not a test key | `signing=on`: `decrypt-key` writes the key, `rsign sign -W` signs, every signature is verified against `--public-key` before upload |
| real run | a variable absent or empty, the public key absent, unreadable or unparsable (alone or together) | `E_SIGNING_CONFIG`, exit 1, naming each missing part (secret, password, public key, in that order) and `tools/feed-contract/keys/README.md` |
| real run | the public key's key id or 32-byte Ed25519 key equals that of `fixtures/kat/kat.pub` or `fixtures/phase2/kat-secret/kat.pub`, or either file is unreadable or unparsable (fail closed) | `E_SIGNING_CONFIG`, exit 1, `public key is a test key (<fixture path>)` |

There is no unsigned real release: a real run that cannot sign fails before `Create Release`. `backfill-sign.js apply`
applies the same test-key refusal to its `--public-key` before any download or upload.

## 10. Phase 3 tools

Phase 3 (`specs/krate-manager-phase3/spec.md`, plan §4) builds `versions.json` (schema_version 2) from the GitHub
releases and the `release.json` each carries, checks it, and deploys it to GitHub Pages. It adds one tool,
`tools/feed-publish.js`, and one workflow, `.github/workflows/feed.yml`; `release.yml` calls `feed.yml` as its job
`feed` in place of the inline "Generate Update Checker Endpoint" step it had before. Every decision of the workflow is
the tool's: the YAML only runs it and passes exit codes along (10.4).

The tool has the section 9 form (first line `#!/usr/bin/env node`, a header comment with the usage and exit codes,
`module.exports` last), with one difference: `main(argv, io, deps)` requires a `deps` object and throws a `TypeError`
before reading `argv` without one. `deps` carries the only effects a test replaces: `gh` (already wrapped in the retry
of 10.3) and `genFeedMain` (the exported `main` of `tools/gen-feed.js`, run in-process). `productionDeps`, which only
the entry point builds, is the only code that spawns anything: `gh`, with a fixed argument array and no shell. Files
are written only at paths given as flags, through a temp file and a rename. Every expected failure is a `ContractError`
whose code has a row in the section 7 table (the seven Phase 3 codes carry class `none` there, except
`E_MANAGER_LATEST`, class `degrade`), printed once on stderr as `feed-publish: <CODE>: <message>`; a degrade prints
`feed-publish: WARN <CODE>: <message>`. The tool names no plugin: every name comes from a `version.json` under
`--plugins-dir`.

Every line the tool writes, on stdout or stderr, goes through one writer that prints the line's `JSON.stringify` form
instead of the line when it holds a code unit U+0000-U+001F or U+007F or starts with `::`; in every line it builds, a
value taken from a release, a report or the live feed is printed raw only when it passes its own check
(`contract.isPluginName` for a name, `parseSemVer` for a version, `checkUrl` for a URL) and as its `JSON.stringify` form
otherwise. So no data can form a workflow command: the annotations of 10.6 are the only lines that start with `::`.

### 10.1 Command lines

The usage text, printed on stderr after a `feed-publish: E_USAGE: <why>` line (exit 2):

```
usage: node tools/feed-publish.js collect --plugins-dir <dir> --repo <owner/name> --out-dir <dir> [--annotate]
       node tools/feed-publish.js generate --in-dir <dir> --plugins-dir <dir> --repo <owner/name> --site-dir <dir>
            [--annotate]
       node tools/feed-publish.js outcome --tool <collect|generate|compare> --exit <n> --github-output <file>
       node tools/feed-publish.js compare --live <file> --feed <file> --releases <file> --dry-run <true|false>
            [--report <file>] [--summary <file>] [--annotate]
       node tools/feed-publish.js verdict --dry-run <true|false> --ref <ref> --build <result> --deploy <result>
            --feed-degraded <true|false|""> --manager-degraded <true|false|""> --drift-warned <true|false|"">
            [--evidence <dir>] [--annotate]
```

A known subcommand comes first; every required flag of it is present once, every optional flag at most once;
`--annotate` is a switch; every value is non-empty and does not start with `--`, except that the three verdict flags
accept `""` (the job output was never written because `build` failed early; it counts as not degraded); `--repo`
matches `^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$`; `--exit` is a non-negative integer without leading zeros; `--dry-run` is
`true` or `false`; `<result>` is one of `success`, `failure`, `cancelled`, `skipped` (the values of
`needs.<job_id>.result`); `--ref` is the run's `github.ref`.

What each subcommand writes:

- `collect`: `<out-dir>/releases.json` (the `gh release list` rows, 2-space JSON), `<out-dir>/manifests/<tag with "/"
  as "-">.json` per downloaded `release.json` (`manifests/` always exists after a successful run, empty or not),
  `<out-dir>/manager/latest.json` only when it is copied, and `<out-dir>/collect-report.json` (10.3).
- `generate`: creates `<site-dir>` (gen-feed does no `mkdir`) and refuses (`E_INPUT`, nothing touched, gen-feed never
  run) a `<site-dir>/manager` that already exists. It runs gen-feed with `--plugins-dir <dir> --releases
  <in-dir>/releases.json --manifests <in-dir>/manifests --repo <owner/name> [--manager-latest-url
  https://krateaudio.com/manager/latest.json] --out <site-dir>/versions.json`, the `--manager-latest-url` pair exactly
  when the report's `manager.status` is `copied`; writes gen-feed's stderr to `<in-dir>/gen-feed.log` and echoes it;
  on gen-feed exit 0 or 3 runs gen-feed a second time into `<in-dir>/versions.again.json` and requires byte-identical
  files, the same exit code and the same stderr (`E_FEED_NONDETERMINISTIC` otherwise); then, when `copied`, creates
  `<site-dir>/manager/` and copies `<in-dir>/manager/latest.json` into it byte for byte. On any exit other than 0 or 3
  no `<site-dir>/manager/` made by the run is left.
- `outcome`: appends one `<flag>=true` or `<flag>=false` line to `--github-output` (10.4).
- `compare`: the result lines (10.5) on stdout; `--report` writes exactly the result lines (atomic, LF, whatever the
  exit code; empty on `E_INPUT` / `E_FEED_SCHEMA`); `--summary` appends them in a fenced `text` block whose fence is
  `max(3, longest backtick run in the lines + 1)` backticks, so no value can close it.
- `verdict`: its reason lines on stdout (10.4).

**The feed workflow.** `feed.yml` has two triggers: `workflow_dispatch` with a boolean `dry_run` that defaults to
`true`, and `workflow_call` with a boolean `dry_run` that every caller must state (`release.yml` passes its own
`dry_run`). It has no `push`, `pull_request`, `release` or `schedule` trigger, top-level `permissions: contents: read`,
no top-level `concurrency`, no `secrets.` reference and no `environment:`. A dry run builds and checks only.

| Job | `needs` | Runs when | `permissions` | `concurrency` | Does |
|---|---|---|---|---|---|
| `build` | — | every run | `contents: read` | group `krate-feed`, `cancel-in-progress: false`, `queue: max` | `collect` → fetch the live feed (`curl --fail --silent --show-error --location --retry 3 https://krateaudio.com/versions.json`, the only network call outside `gh`) → `generate` → `compare`, each tool step followed by `outcome`; uploads `_site/` as artifact `feed-site` (only when every earlier step passed) and `$RUNNER_TEMP/feed-in/` as `feed-evidence` (always) |
| `deploy` | `build` | `!inputs.dry_run && github.ref == 'refs/heads/main'` (no status function, so it also needs a green `build`) | `contents: write` | the same block as `build` | downloads `feed-site` and pushes it with `peaceiris/actions-gh-pages@v4`, `keep_files: true`, `cname: krateaudio.com`; runs no repository code |
| `verdict` | `build`, `deploy` | `always()` | `contents: read` | none | downloads `feed-evidence` (allowed to fail) and runs `verdict` with both job results, the three `build` outputs and `--evidence` |

So deploy happens only on a real run from `refs/heads/main`; a real run from another ref builds and uploads, skips
`deploy`, and its `verdict` is red (`real run from <ref>`). Every `run:` step declares `shell: bash` and starts with
`set -euo pipefail`; a tool step is `rc=0; node tools/feed-publish.js <subcommand> … || rc=$?` followed by `node
tools/feed-publish.js outcome --tool <subcommand> --exit "$rc" --github-output "$GITHUB_OUTPUT"`, so `outcome` alone
turns the exit code into "continue" or "fail".

The `krate-feed` group sits on `build` and on `deploy`, not at the top level: GitHub documents job-level concurrency in
a called workflow, and does not state that a called workflow's top-level `concurrency` is honoured (plan PD-1). With
the group on both jobs, builds run one at a time in the order GitHub enqueues the runs, and each run's `deploy` is
enqueued the moment its own `build` ends, before the next `build` can end, so deploys run in build order and a later
deploy never carries an older release list. `queue: max` lets up to 100 jobs wait, so no feed run is cancelled.
`release.yml`'s `feed` job declares no `concurrency`, so no group is shared between caller and callee. A `docs.yml` or
`deploy-pages` push to `gh-pages` at the same moment is in a different group: a rejected push fails that run, and a
fresh dispatch recovers (10.7).

### 10.2 Exit codes

| Subcommand | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| `collect` | complete | abort: nothing of `releases.json`, `manifests/`, `manager/`, `collect-report.json` left in `--out-dir`; a pre-filled `--out-dir` refused untouched | usage | complete, manager degraded (`E_MANAGER_LATEST`) |
| `generate` | gen-feed 0, deterministic | gen-feed 1, `E_FEED_NONDETERMINISTIC`, a manager-copy or report failure, a `<site-dir>/manager` that already exists (`E_INPUT`, refused untouched); no `<site-dir>/manager/` created by this run is left | usage (its own, or gen-feed's 2) | gen-feed 3, deterministic |
| `outcome` | continue (flag appended) | fail (`E_STEP_FAILED`, nothing appended) | usage (incl. a non-integer `--exit`) | — |
| `compare` | no drift | a `block` drift; any drift with `--dry-run true`; `E_INPUT` (incl. a control character in the live file); `E_FEED_SCHEMA` | usage (incl. a missing or non-boolean `--dry-run`) | `--dry-run false` and every drift `warn` |
| `verdict` | green | red | usage | — |

### 10.3 collect

`collect` makes its requests in this order and in no other:

1. **Before any `gh` call.** The plugin names are the `name` of each `<plugins-dir>/*/version.json`: each must pass
   `contract.isPluginName` (`E_PLUGIN_NAME`), no two may be equal after `toLowerCase()` (`E_DUPLICATE_PLUGIN`), and none
   may lower-case to `manager` (`E_PLUGIN_NAME`); they are sorted bytewise. `--out-dir` is created when missing and
   refused (`E_INPUT`, nothing in it touched) when it already holds `releases.json`, `manifests`, `manager` or
   `collect-report.json`.
2. **The list.** `gh release list --repo <repo> --limit 1000 --json tagName,isPrerelease,isDraft,publishedAt`. The rows
   must be an array of objects with a non-empty string `tagName` and boolean `isPrerelease` and `isDraft` (`E_INPUT`;
   `publishedAt` is checked by gen-feed); 1000 rows is `E_FEED_LIST_TRUNCATED` (releases may be missing).
3. **The selection.** `gen-feed.js` `selectReleases` (section 7, the Q9 rule) for every plugin before any view, so an
   `E_TAG_FORMAT` aborts first. A plugin without a stable release is `no-stable` and costs no request.
4. **Per plugin, per selected release** (the stable release, then its prerelease sibling when there is one): `gh
   release view <tag> --repo <repo> --json tagName,assets`. The view must return the tag asked for and assets that
   each have a string `name`, a non-negative safe-integer `size` and a `digest` that is absent, `null` or
   `sha256:<64 hex>` (case-insensitive), else `E_INPUT`. When an asset is named `release.json`: a `null` or absent
   digest is refused with `E_FEED_DOWNLOAD` ("has no digest on GitHub") before any download (the Phase 2 CC-verify
   rule, Phase 3 Clarifications P-2); otherwise `gh release download <tag> --repo <repo> --pattern release.json
   --output <out-dir>/manifests/<tag with "/" as "-">.json`, then the written file's size and SHA-256 must equal the
   asset record (`E_FEED_DOWNLOAD`); the release is `downloaded`. Without such an asset nothing is downloaded and the
   release is `no-manifest`.
5. **The manager release.** From the same rows, the non-draft, non-prerelease rows whose `tagName` matches
   `^manager/v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$`, highest by `compareSemVer`. None:
   `manager: none`, no request. Else one view (the same check); no `latest.json` asset: `none`. Else `gh release
   download <tag> --repo <repo> --pattern latest.json --output <out-dir>/manager/latest.json` with the step 4 digest,
   size and SHA-256 checks (a mismatch aborts), then the `latest.json` check below.
6. **The two files the next step reads**, written atomically after the last check: `releases.json`, then
   `collect-report.json`.

For nine plugins without siblings or a manager release that is 1 list, 9 views and at most 9 downloads: at most 19
requests. `collect` never passes `--clobber`, never calls `gh api` and never names a host. An abort removes each of the
four names above that the run created (after the step 1 refusal, any of them that exists is the run's), so no
`releases.json`, `manifests/`, `manager/`, `collect-report.json` or temp file is left.

**The gh retry** (spec FR-023a). The one `gh` spawn of `productionDeps` is wrapped in the exported `withGhRetry(gh,
{sleep, log})`, for `release list`, `release view` and `release download` alike: at most **3 attempts**, a fixed
backoff of **5 s** before the second and **15 s** before the third. Each failed attempt is logged on stderr as
`feed-publish: WARN gh attempt <n>/3 failed: <label> (exit <code>); retrying in <s> s`, where `<label>` is `list` for
the list and `<subcommand> <tag>` for a view or a download (e.g. `view alphaplug/v1.2.0`). Only a `gh` that fails to
run (reported as exit `-1`) or exits non-zero is retried; before a download is retried, the partial `--output` file the
failed attempt left is deleted (`gh` without `--clobber` refuses an existing file). A result decided on a call that
succeeded is **never** retried: a shape mismatch (`E_INPUT`), a view for another tag, a size or digest mismatch
(`E_FEED_DOWNLOAD`), 1000 list rows (`E_FEED_LIST_TRUNCATED`) and `E_TAG_FORMAT`. A `gh` that still fails after its
last attempt aborts with `E_INPUT`, naming the subcommand, the tag and the attempt count. Every attempt counts in
`gh_calls`.

**`collect-report.json`** (2-space JSON, final LF, written atomically):

```json
{
  "plugins": [
    { "name": "<Name>", "stable": "<tag>", "sibling": "<tag> | null",
      "manifests": [ { "tag": "<tag>", "status": "downloaded" } ] },
    { "name": "<Name>", "status": "no-stable" }
  ],
  "manager": { "status": "none" },
  "gh_calls": 10
}
```

Plugins are bytewise by name; `manifests` lists the stable release, then the sibling, each `downloaded` or
`no-manifest`. `manager` is `{"status": "none"}`, `{"status": "copied", "tag": "<tag>"}` or `{"status": "degraded",
"tag": "<tag>", "message": "<message>"}`. `manager.status` is the only channel by which `generate` learns whether to
pass `--manager-latest-url`; no stdout line is parsed by the workflow. stdout is one line per plugin, `collect: <Name>
stable <tag> (<status>)[, sibling <tag> (<status>)]` or `collect: <Name> no-stable`, then `manager: none`, `manager:
copied <tag>` or `manager: degraded <tag>: <message>`.

**The `latest.json` check** (spec FR-025): the file is JSON; an object; its `version` a string equal to the tag's
version after one leading `v` is stripped (for tools that write one); `platforms` a non-empty object; each platform
entry an object with a string `signature` and a string `url` that passes `contract.checkUrl` (https, no whitespace, no
`/releases/latest/`; an `E_URL_*` failure is reported as `E_MANAGER_LATEST` with the original code in the message). No
other key is read: Tauri owns the format (https://v2.tauri.app/plugin/updater/). The first failure is a degrade: the
file and `manager/` are removed, the report says `degraded` with the message, stderr gets `feed-publish: WARN
E_MANAGER_LATEST: <message>`, and `collect` exits 3 after writing everything else. The plugin feed still deploys
without a `manager` block, the run turns red, and the previously published `manager/latest.json` stays on Pages
(`keep_files: true`).

### 10.4 outcome and verdict

`outcome` is the only place an exit code becomes a workflow decision. It appends (never rewrites) one line to
`--github-output` in the `name=value` form:

| `--tool` | Flag | `--exit 0` | `--exit 3` | Any other `--exit` |
|---|---|---|---|---|
| `collect` | `manager_degraded` | appends `manager_degraded=false`, exit 0 | appends `manager_degraded=true`, exit 0 | appends nothing, `feed-publish: E_STEP_FAILED: collect exited <n>`, exit 1 |
| `generate` | `feed_degraded` | appends `feed_degraded=false`, exit 0 | appends `feed_degraded=true`, exit 0 | appends nothing, `feed-publish: E_STEP_FAILED: generate exited <n>`, exit 1 |
| `compare` | `drift_warned` | appends `drift_warned=false`, exit 0 | appends `drift_warned=true`, exit 0 | appends nothing, `feed-publish: E_STEP_FAILED: compare exited <n>`, exit 1 |

`verdict` prints one `verdict: red <reason>` line per reason, in this order, and exits 1; with no reason it prints
`verdict: green` and exits 0:

| # | Condition | Reason text |
|---|---|---|
| 1 | `--build` is not `success` | `build <result>` |
| 2 | `--dry-run false` and `--ref` is not `refs/heads/main` | `real run from <ref>` |
| 3 | `--build` is `success` and not #2: on a real run `--deploy` is not `success`; on a dry run `--deploy` is neither `success` nor `skipped` | `deploy <result>` |
| 4 | `--feed-degraded true` | `feed degraded:`, then each `gen-feed: WARN` line of `<evidence>/gen-feed.log` on its own line |
| 5 | `--manager-degraded true` | `manager latest.json degraded: <manager.message of collect-report.json>` |
| 6 | `--drift-warned true` | `v1 drift deployed: <the drift … warn lines of compare.txt, joined by " / ">` |
| 7 | `--build` is `success` and an evidence file is missing under `--evidence` | `evidence missing <file>`, once per missing file of `releases.json`, `collect-report.json`, `gen-feed.log`, `live-versions.json`, `compare.txt` |

A failed build skips `deploy` (`needs: build`), so the deploy reason is not added on top of a build reason, and #2
replaces it for the `deploy` a non-`main` ref skips. Evidence files are read only when a reason needs them; an
unreadable one prints `(unreadable)` in place of its lines. Every quoted evidence line and the `manager.message` pass
the writer rule of this section's introduction before they are printed or joined.

### 10.5 The comparison rule

`compare` reads the three files (`E_INPUT` when one is unreadable or not JSON), validates `--feed` against C1 v2 and
then v1 with `validateFeed` (`E_FEED_SCHEMA: <schema> <instancePath>: <message>`), checks the `--releases` rows by the
10.3 list rule (`E_INPUT`), projects both feeds with `gen-feed.js` `projectV1` (`E_INPUT` when the live file has no
`plugins` object), and refuses (`E_INPUT: live plugins.<JSON.stringify(key)> holds a control character`, before any
result line exists) a live file in which a `plugins` key, or a string `version`, `download_url` or `release_notes`,
holds a code unit U+0000-U+001F or U+007F. The live file is network data: whatever can write `gh-pages` or answer for
the domain decides it.

Definitions, for plugin name `n`, `lower` = `n.toLowerCase()`, `L` the live projection and `N` the new one:
`stableRow(v)` is a row with `isDraft` and `isPrerelease` false and `tagName` equal to `<lower>/v<v>`; `preRow(v)` is the
same with `isPrerelease` true; `gt(a, b)` holds when both parse as SemVer and `compareSemVer(a, b) > 0` (a value that
is not SemVer is never compared, so a non-SemVer live `version` lands in `version changed without a release`); `sel` is
`selectReleases(n, rows)`. The names are the bytewise-sorted union of the `L` and `N` keys; per name the first matching
case wins:

| # | Case | Line |
|---|---|---|
| 1 | in both; `version`, `download_url`, `release_notes` all equal | `same <n> <v>` |
| 2 | in both; `gt(N.version, L.version)`; `stableRow(N.version)`; `release_notes` equal | `newer <n> <L.v> -> <N.v> (<tag> <publishedAt>)`, with ` download_url <old> -> <new>` appended when the URL changed too |
| 3 | in both; `preRow(L.version)` and no `stableRow(L.version)`; `sel` picks `N.version` as the stable release; `download_url` and `release_notes` equal | `q9-corrected <n> <L.v> -> <N.v> (prerelease <lower>/v<L.v>)` |
| 4 | in `L` only; `preRow(L.version)`; `sel` is `null` | `q9-corrected <n> <L.v> -> absent (prerelease <lower>/v<L.v>)` |
| 5 | in `N` only; `stableRow(N.version)` | `added <n> <N.v> (<tag> <publishedAt>)` |
| 6 | anything else | `drift <n> <kind>[; <kind>…] <severity>` |

`q9-corrected` covers the transition from the legacy live file, which took the highest tag with prereleases included,
to the v2 feed, whose v1 `version` is the newest stable release. A `drift` line lists every kind that applies, in the
fixed order below, joined by `; `; its severity is `block` when any of its kinds is `block`, else `warn`:

| Kind | Severity | Applies when |
|---|---|---|
| `release_notes not empty` | block | in both and `L.release_notes` or `N.release_notes` is not `""` |
| `version lowered` | block | in both and `gt(L.version, N.version)` |
| `removed` | block | in `L` only (case 4 did not hold) |
| `version changed without a release` | warn | in both, the versions differ, and neither `version lowered` nor case 2's release condition holds |
| `download_url changed without a newer release` | warn | in both, the URLs differ, and not `gt(N.version, L.version)` with a stable row |
| `added without a release` | warn | in `N` only and no `stableRow(N.version)` |

The last result line is `compare: <n> same, <n> newer, <n> added, <n> q9-corrected, <n> drift (<n> block, <n> warn)`,
counting plugins. Exit 0 with no drift; 1 with a `block` drift, or with any drift when `--dry-run true`; 3 when
`--dry-run false` and every drift is `warn`. stderr carries one `feed-publish: E_V1_DRIFT: <line>` per drift that fails
the run (every drift on a dry run, the `block` ones on a real run) and one `feed-publish: WARN E_V1_DRIFT: <line>` per
`warn` drift of a real run. So a `block` drift fails `build` before `feed-site` exists and nothing deploys; a real run
with `warn` drift only deploys and its `verdict` is red (`v1 drift deployed`); a dry run fails on any drift.

**Two blocking kinds cannot heal themselves** (plan SI-7; Phase 3 Clarifications P-3, ruled: keep them `block`). C1
fixes `release_notes` to `""` (section 2) and the new feed is validated before the rule runs, so `release_notes not
empty` can only describe the live file; `removed` describes a plugin that is no longer under `plugins/`. No `feed.yml`
run can change either, so once one appears every run fails before deploy. The recovery is by hand: a commit on the
`gh-pages` branch that fixes `versions.json` (removes the non-empty `release_notes` value or the removed plugin's
entry), then a dry-run dispatch of `feed.yml` (the plugin compares `same`, or no longer appears), then a real run. The
same recovery applies to a live file that the control-character check refuses. The case is latent: no workflow other
than `feed.yml` writes `versions.json`.

### 10.6 Annotation titles

With `--annotate`, the tool prints GitHub workflow commands (`::<level> title=<title>::<message>`) on stdout, so every
decision is readable through the check-run annotations API without the run's logs. Each message is one line: the tool
replaces every CR or LF in it with a space and relies on no escaping. The titles are fixed:

| Title | Printed by | Level and message |
|---|---|---|
| `feed-collect` | `collect` | `notice`: `<n> downloaded, <n> no-manifest, <n> no-stable, manager <status>` |
| `feed-generate` | `generate` | `notice`: `exit <n>, deterministic` |
| `feed-degrade` | `generate` | `warning`: one per gen-feed `gen-feed: WARN` line |
| `feed-validate` | `compare` | `notice`: `v2 ok, v1 ok`; or `error`: the `E_FEED_SCHEMA` message |
| `feed-compare` | `compare` | `notice`: the summary line |
| `feed-drift` | `compare` | `error`: one per drift line that fails the run; `warning`: one per `warn` drift line of a real run |
| `feed-verdict` | `verdict` | `notice`: `green`; or `error`: one per reason (a multi-line reason on one line) |

### 10.7 User steps

No trigger publishes the feed on its own beyond `release.yml`'s `feed` job; these steps are the user's (spec FR-061,
Clarifications Q4):

1. **First dispatch.** `workflow_dispatch` only runs the default branch's copy of a workflow, so the first dispatch is
   possible after `feed.yml` is merged. Dispatch `feed.yml` from `main` with `dry_run: true` (the default) and read
   `verdict`'s annotations and the `feed-evidence` artifact.
2. **After the green dry run,** dispatch `feed.yml` from `main` with `dry_run: false`, then check one plugin's update
   banner against the live feed: the banner's version and download link are unchanged, or the newer release is shown.
3. **After the live backfill** (Phase 2 `backfill-sign.yml`, job `apply-live`) has attached `release.json` to the
   current releases, dispatch `feed.yml` with `dry_run: true`, review the result, then dispatch it with
   `dry_run: false`. Nothing does this automatically: `backfill-sign.yml` does not call `feed.yml`.

**A failed run is recovered by a fresh dispatch, not by "Re-run jobs".** A re-run job would upload `feed-site` or
`feed-evidence` again under a name the run may still hold (`actions/upload-artifact` does not overwrite by default),
which was not verified (plan §9). A fresh `feed.yml` dispatch is idempotent: it rebuilds the feed from the releases,
and every `gh-pages` writer deploys with `keep_files: true`. A failed `release.yml` real run's feed is recovered the
same way.
