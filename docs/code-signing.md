# Code signing and key storage

## Rules

- **No signing key, certificate, keystore or password is ever committed.**
  They are not in this repository and are not given to CI.
- The repo carries only *how* to sign: this page, the build script interface
  and the release checklist.
- Keys live in a private, non-synced location on the maintainer's PC (for
  example an encrypted container or the OS credential store), never in a
  synced folder and never in the old working area if that area is shared.

## Release package signing

A webOS `.ipk` is normally either a development package or signed with a
private key and certificate pair for installation on a TV. The choice matters:

- Installable on the user's own TV: the package must be signed appropriately
  for that TV (developer mode package or signed with the vendor-issued
  certificate, depending on setup).
- Redistribution: any Store/Homebrew distribution has its own signing and
  review requirements.

Before the first public release the lead records:

1. Which signing mode is used (development vs signed release package).
2. Where the key material is stored (description only, never the key).
3. How a release asset's signature/hash is verified by a user
   (`sha256sum` of the `.ipk`).

## CI

CI builds unsigned artifacts only for verification. Signed artifacts are
produced locally by the maintainer or in a later, explicitly configured release
workflow with its own secret storage — not part of repository CI.

## If a key is exposed

Treat the key as compromised: revoke/reissue it, remove any release assets made
with it, and follow [`SECURITY.md`](../SECURITY.md). Do not commit history
rewrites in a public fork; notify the maintainer privately.
