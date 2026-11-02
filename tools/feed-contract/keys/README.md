# Release signing key (keys/)

FR-061, FR-062, plan §4.7, PD-21, PD-22, PD-23 (`specs/krate-manager-phase2/`). Every release asset and every
`release.json` is signed in CI with one minisign key. Its secret half lives only in two GitHub environment secrets; its
public half is the one file committed here, `tools/feed-contract/keys/krate-signing.pub`. That file is added by the user
together with the secrets, never by an agent, and no private key is ever generated into or committed to this repository
(FR-060).

## 1. What `krate-signing.pub` is

A minisign public key in its two-line text form, the file that `rsign verify -p <file>` and
`node tools/minisign.js verify -p <file>` read:

- line 1 starts `untrusted comment:` (for example `untrusted comment: minisign public key: <KEY ID>`);
- line 2 is the canonical base64 of 42 bytes: `Ed`, the 8-byte key id, the 32-byte Ed25519 public key.

The `.pub` file that `tauri signer generate` writes is the base64 of exactly this text (Tauri key files are base64 of
standard minisign files, measured with tauri-cli 2.12.4 in `specs/krate-manager/probes/results.md`). So the committed
file is that `.pub` base64-decoded, not the `.pub` itself.

The release job (`.github/workflows/release.yml`, step `Signing configuration and key`) and the backfill
(`.github/workflows/backfill-sign.yml`) run `node tools/gen-release-manifest.js --signing-config` against this path; a
real run refuses with `E_SIGNING_CONFIG` when the file is absent, does not parse, or is a test key (below), and every
signature CI makes is verified against it before anything is uploaded.

## 2. User steps

1. Generate the key **outside this repository** (for example in a directory under your home folder), with a non-empty
   password (Tauri 2 CLI, `-w/--write-keys`, `-p/--password`, https://v2.tauri.app/reference/cli/):

   ```sh
   tauri signer generate -w ~/krate-signing/krate.key -p '<a non-empty password>'
   ```

   This writes the encrypted private key file and its `.pub` next to it. The password must be non-empty: the release
   job decrypts the key with it (`node tools/minisign.js decrypt-key`), because rsign2 reads a password only from a
   terminal.
2. In this repository's GitHub settings, create the GitHub Environment `signing` with the deployment-branch policy
   "Selected branches and tags" = `main` (no other branch or tag).
3. In the `signing` environment, add two environment secrets (not repository secrets):
   - `TAURI_SIGNING_PRIVATE_KEY`: the content of the private key file `tauri signer generate` wrote (the one-line
     base64 text, as is);
   - `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`: the password from step 1.

   Only jobs that reference the environment receive these secrets. The `release` job of `release.yml` and the
   `apply-dry` / `apply-live` jobs of `backfill-sign.yml` carry `environment: signing`, so every one of those runs, dry
   or real, starts only from `main` (PD-22). Any later job that uses these secrets must reference the same environment:
   Phase 11 (the manager's own release) reads the same two secrets in one place only, the `sign` job of
   `release-manager.yml`: `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` decrypts the key with `node tools/minisign.js
   decrypt-key`, and `rsign` signs manager updates with it, as `release.yml` does; no manager build job receives them.
4. Base64-decode the `.pub` into `tools/feed-contract/keys/krate-signing.pub` and commit it:

   ```sh
   base64 -d ~/krate-signing/krate.key.pub > tools/feed-contract/keys/krate-signing.pub
   ```

   Check the result is two lines, the first starting `untrusted comment:`.

Never use a fixture key as `krate-signing.pub`: not `fixtures/kat/kat.pub` and not
`fixtures/phase2/kat-secret/kat.pub` (or a copy or relabelling of either). They are test keys (the `kat-secret` one has
its secret key and password committed on purpose), so a real run refuses them by key id and by key bytes with
`E_SIGNING_CONFIG` (PD-23).

## 3. Merge instructions

Do these in this order (FR-062):

1. Create the GitHub Environment `signing` (deployment branches: `main` only) and in it the environment secrets
   `TAURI_SIGNING_PRIVATE_KEY` and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` (section 2, steps 1-3).
2. Commit `tools/feed-contract/keys/krate-signing.pub` on the phase branch (section 2, step 4).
3. Only then merge to `main`. From the merge on, every real `release.yml` run fails with `E_SIGNING_CONFIG` until
   steps 1 and 2 are done, and a real run also fails on any facts failure (FR-047a); there is no unsigned release.
4. After the merge, dispatch SC-018 (`release.yml` with `dry_run: true`) and SC-019 (`backfill-sign.yml` with its
   default `dry_run: true`), both from `main`: the `signing` environment admits no other ref.

A real run refuses a version whose tag or GitHub release already exists (`Tag must not exist`, PD-21). If a run fails
after `Create Release` has made the tag or the release, delete both (the release and the tag) before dispatching that
version again. An already published version is never re-released: bump the plugin's `version.json` instead.
