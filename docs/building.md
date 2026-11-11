> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Building and packaging

Status: **[pending]** — the build pipeline is owned by the build workstream
(`scripts/build/`, `tests/`). This document defines the agreed interface so the
repository docs, CI and releases stay consistent with it.

## Entry points (intended interface)

| Command | Purpose | Output |
| --- | --- | --- |
| `pwsh ./scripts/build/build.ps1` | Build + package the TV app | `dist/<name>_<version>_all.ipk` |
| `pwsh ./scripts/build/build.ps1 -DryRun` | Validate inputs, print steps, create nothing | — |
| `pwsh ./scripts/build/test.ps1` | Static checks + unit/integration tests | console report, non-zero on failure |
| `pwsh ./scripts/build/release.ps1` | Copy a verified package to a release folder | `dist/release/<name>_<version>_all.ipk` + `.sha256` |

If the build workstream chooses a Node entry point instead
(`node scripts/build/build.mjs`), keep the same behaviour and update this file
and `.github/workflows/ci.yml` in the same change.

## Toolchain

- `@webos-tools/cli` `3.2.6` (Apache-2.0), installed as a dev dependency or
  used through `npx`; requires Node.js 20+.
- Staging folder is assembled by the build script (`ares-package` output is an
  `.ipk`).
- The app is a static webOS app: `appinfo.json` plus static assets; there is no
  Node runtime on the TV.
- `app/appinfo.json` must declare the fields the TV loader needs (`id`,
  `version`, `vendor`, `main`, `icon`, `type`) and the icons
  (`icon`, `largeIcon`, `icon-256.png`, `icon-512.png`) must exist next to it,
  or `ares-package` and the install will fail.

## Output rules

- All build output goes to `dist/`, which is **ignored** by
  [`.gitignore`](../.gitignore).
- Packages are never committed. Deliberate releases go to `dist/release/`
  (ignored except for whitelists) and are uploaded as GitHub Release assets
  with a `.sha256` file.
- The build must be reproducible from a clean checkout: record Node, CLI and
  package versions in the release notes.

## CI

`.github/workflows/ci.yml` runs the repository hygiene checks on every push. It
runs build + tests only when `scripts/build/build.ps1` exists (or a root
`package.json` with a lockfile), so the repository stays green while the
pipeline is being assembled.

## Live deployment

Installing on the TV is the maintainer's step; see
[`setup.md`](setup.md#5-deploy-the-tv-app-pending-app--scriptsbuild) and
[`release.md`](release.md). CI never touches a device.
