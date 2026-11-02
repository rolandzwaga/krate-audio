# Encrypted secret-key known-answer fixture (kat-secret)

FR-064, FR-060, plan PD-6. One encrypted minisign secret key made by rsign2 at a terminal, its public key, and one
signature made with it over a small committed file. `tools/minisign.js` (`decryptSecretKey`) and its tests use them as
the known answer for decrypting an rsign2 key with a password.

**Test-only: signs nothing that ships; the one committed secret-key file (spec FR-064).** The FR-024 scan in
`tests/c3-trusted-comment.test.js` (`KAT binds and no secret key is committed`) allows exactly this one path,
`fixtures/phase2/kat-secret/kat.key`, and flags any other secret-key file under `tools/feed-contract/`. Plan PD-23:
the signing tools in real mode refuse this `kat.pub` (and `fixtures/kat/kat.pub`) as a production key, by key id or by
32-byte key.

| File | What it is |
|---|---|
| `kat.key` | the rsign2 encrypted secret key; its header line is `untrusted comment: rsign encrypted secret key`, its second line decodes to 158 bytes starting `EdScB2` |
| `kat.pub` | its public key, key id `63861317F7D60239`, `RWQ5Atb3FxOGY19eL6lGVIKLgfU5J0qV58UHKSdQafpKhtsB/LOndMnN` |
| `message.txt` | the signed file: `krate kat-secret message` and a final LF (25 bytes) |
| `message.txt.minisig` | `rsign sign` output over `message.txt`, LF line endings |

The password is published on purpose: **`krate-kat-test-password`**.

These files are byte-exact (`.gitattributes`: `tools/feed-contract/fixtures/** -text`). Never regenerate them.

## How it was made

- Date: 2026-10-30.
- Who: the main loop, per the Q-A ruling (`specs/krate-manager-phase2/spec.md`, Clarifications, Session 2026-10-30
  (plan)). rsign2 reads a password only from a terminal, so each rsign command ran under `script -qec` in WSL Ubuntu
  24.04 with the password fed to the pseudo-terminal.
- Signer: `cargo install rsign2 --version 0.6.7 --locked` (Rust 1.99.1); `rsign --version` printed `rsign2 0.6.7`.
- Where: a fresh `mktemp -d` directory outside the repo.

The five steps (tasks.md Preconditions, Q-A), as run:

1. `rsign generate -p kat.pub -s kat.key` (password `krate-kat-test-password`, typed twice).
2. Write `message.txt` with the content `krate kat-secret message` and a final LF.
3. `rsign sign -s kat.key -t "krate:1:KatPlugin:1.2.3:linux-x86_64:message.txt" -x message.txt.minisig message.txt`
   (password again).
4. `rsign verify -p kat.pub -x message.txt.minisig message.txt`, which printed:

   ```text
   Signature and comment signature verified
   Trusted comment: krate:1:KatPlugin:1.2.3:linux-x86_64:message.txt
   ```

   The Windows rsign2 0.6.7 prints the same two lines over the copied files (exit 0).
5. Copy `kat.key`, `kat.pub`, `message.txt` and `message.txt.minisig` into this directory, all with LF line endings.

## Checking it

From a temp directory holding copies of the four files:

```bash
rsign verify -p kat.pub -x message.txt.minisig message.txt
```

The key id is not readable from `kat.key` directly: an encrypted minisign key XORs the whole keynum_sk (key id, secret
key, checksum) with the scrypt keystream, so the id is checked after decrypting with the password (tasks.md T009).
