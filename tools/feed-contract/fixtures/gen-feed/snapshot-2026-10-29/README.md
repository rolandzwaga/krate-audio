# Snapshot fixture `snapshot-2026-10-29`

Frozen real inputs for the gen-feed tests (spec `specs/krate-manager-phase1/spec.md` FR-071, SC-001, SC-002, SC-014,
SC-025). Tests read only these copies and never the network. Never edit a file here by hand and never re-capture over
it: the directory name and SC-001 pin this data.

Captured 2026-10-30T07:28:30.830Z by `specs/krate-manager-phase1/evidence/capture-snapshot.js` (task T008), with the
plugin blobs taken from commit `84c6aab1a95ceaca29c2da408758d72f1c500488`. The data matches the spec's "Real data read this session (2026-10-29)".

## Commands

| File | Command |
|---|---|
| `plugins/<dir>/version.json` | `git show HEAD:plugins/<dir>/version.json` (stored blob bytes, LF) |
| `plugins/<dir>/installers/windows/setup.iss` | `git show HEAD:plugins/<dir>/installers/windows/setup.iss` (stored blob bytes) |
| `tags.txt` | `git ls-remote --tags origin`, transformed by the Node code below |
| `releases.json` | `gh release list --limit 200 --json tagName,isPrerelease,isDraft,publishedAt` (stdout as emitted) |
| `live-versions.json` | `curl -sL https://rolandzwaga.github.io/krate-audio/versions.json` (raw bytes) |
| `case.json` | written by the script: the gen-feed case shape (plan §4.7), group `clean` |

The Node code that turned the `git ls-remote --tags origin` stdout into `tags.txt` (strip each line's
`<sha>\trefs/tags/` prefix, drop every `^{}` peel line, de-duplicate, keep input order, one bare tag per line, LF, final LF):

```js
const tagsFromLsRemote = (text) => [...new Set(text.split(/\r?\n/).filter((l) => /^[0-9a-f]+\trefs\/tags\//.test(l) && !l.endsWith('^{}')).map((l) => l.replace(/^[0-9a-f]+\trefs\/tags\//, '')))].join('\n') + '\n';
```

## Counts

| Item | Count |
|---|---|
| tags (`tags.txt`) | 88 |
| releases (`releases.json`, same tag names as `tags.txt`) | 88 |
| prereleases | 0 |
| drafts | 0 |
| plugins in `live-versions.json` (schema_version 1, keys `version`, `download_url`, `release_notes` only) | 9 |
| plugin directories copied (`plugins/*/version.json` at HEAD; `plugins/shared` has none) | 9 |

Plugin directories: `disrumpo`, `gradus`, `innexus`, `iterum`, `membrum`, `profundum`, `ruinae`, `seraphis`, `vorago`.
