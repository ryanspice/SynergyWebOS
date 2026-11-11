> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Release process

Status: **[pending]** until the build pipeline exists. This is the intended
contract.

## Versioning

- Semantic versioning for the app: `MAJOR.MINOR.PATCH`.
- `app/appinfo.json` `version` is the source of truth for the package version;
  the release script reads it.
- Git tags: `v<version>` (e.g. `v0.2.0`).

## Steps (maintainer)

1. Ensure `main` is green in CI and the live acceptance in
   [`testing.md`](testing.md) (Manual TV/PC acceptance) passed on a real TV/PC.
   Record the TV model, webOS version, Synergy version and commit.
2. Build a clean package from a clean checkout:
   `pwsh ./scripts/build/build.ps1` → `dist/<name>_<version>_all.ipk`.
3. Produce `dist/release/<name>_<version>_all.ipk` and the `.sha256` via the
   release script (pending).
4. Create the GitHub Release for tag `v<version>` with:
   - release notes (changes, known limitations, required TV/PC setup);
   - the `.ipk` and `.sha256` as assets (never committed to `git`);
   - signing information if packages are signed.
5. Install on the TV and re-run the acceptance checklist; note the result.
6. If a release is bad, remove the release asset and mark the tag on the
   release notes — do not silently replace an artifact with different bytes.

## What not to do

- Do not commit packages, `dist/` or `node_modules/`.
- Do not attach Synergy binaries, license files or device keys.
- Do not push the TV app to the LG store without a separate decision
  (store distribution, privacy policy and store signing are separate topics).

## Rollback

The previous prototype package remains installed until the new one is
verified; keep the old package and a Synergy config rollback note so the user
can revert without losing the working server link.
