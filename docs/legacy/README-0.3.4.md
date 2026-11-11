> **Historical original README.** Its dock, paths and deployment claims refer to the supplied 0.3.4 snapshot, not this native candidate. No dock is included.

# Synergy for WebOS

[![Version: 0.3.4](https://img.shields.io/badge/version-0.3.4-b88a3b)](../../docs/release.md) [![Platform: LG webOS TV](https://img.shields.io/badge/platform-LG_webOS_TV-a50034)](../../docs/building.md) [![Target: web (all)](https://img.shields.io/badge/target-all_web-555555)](../../TESTING.md) [![TV evidence: LG C3](https://img.shields.io/badge/TV_evidence-LG_C3-6b8e23)](../../app/REPORT.md) [![License: MIT](https://img.shields.io/badge/license-MIT-green)](../../LICENSE) [![App ID: dev.canopy.synergywebos](https://img.shields.io/badge/app_id-dev.canopy.synergywebos-lightgrey)](../../app/BRAND.md)

**SynergyWebOS** (*"Your desk, on the TV."*) is a TV-first Synergy experience for LG
webOS televisions: a TV app plus a companion dock/app switcher that brings your
desktop keyboard and mouse to the TV through a small, configurable handoff
corner.

- Brand, visual identity, app ID and migration notes:
  [`app/BRAND.md`](../../app/BRAND.md).
- The repository name `synergy-tv` is retained; the package ID remains the
  existing one for upgrade compatibility.

**Status:** working local build — SynergyWebOS `0.3.4` is packaged, installed and exercised on the owner's LG C3 (Chromium 87 floor, tested on Chromium 132 / webOS 26). Physical acceptance recorded in [`app/REPORT.md`](../../app/REPORT.md) and [`docs/testing.md`](../../docs/testing.md). No remote/push is configured. See [`docs/bridging-map.md`](../../docs/bridging-map.md) for a comparison with a native C++20 port.

## What it is meant to be

- **TV app** — a lean, 1080p, keyboard/mouse-first webOS app that shows the
  Synergy client connection, live log, root/uinput probe, autostart state,
  home-key target and handoff settings. See [`app/`](../../app/) and
  [`app/BRAND.md`](../../app/BRAND.md).
- **TV dock / app switcher** — an autohide taskbar for switching between open
  TV apps. SynergyWebOS supplies the live catalog (open apps first); the standalone
  dock is intentionally unprivileged. See `dock/`.
- **Handoff configuration tooling** — a small, configurable hot corner that
  hands control to the TV, plus Synergy 3 server/config/API helpers. See
  [`tools/synergy/`](../../tools/synergy/).
- **Home-key and escape handling** — the Windows key returns to the TV's own
  system home screen; holding either Alt key for five seconds does the same.
  See [`scripts/home/`](../../scripts/home/).
- **Build and test pipeline** — reproducible webOS package build and the
  acceptance checks. See [`scripts/build/`](../../scripts/build/) and
  [`tests/`](../../tests/).

## Layout

| Path | Purpose | Owner |
| --- | --- | --- |
| `app/` | Branded TV app (UI, client scripts, `appinfo.json`, `BRAND.md`) | app owner |
| `dock/` | Standalone dock/app-switcher prototype and integration contract | dock owner |
| `tools/synergy/` | Synergy 3 server/UI/handoff config tooling | config owner |
| `scripts/home/` | Windows-key → system home patch and cleanup scripts | home owner |
| `scripts/build/` | Build/package pipeline | build owner |
| `tests/` | Automated checks and acceptance scripts | build owner |
| `docs/` | Repository, lifecycle, handoff and release notes | repo owner |
| `.github/` | CI workflows, issue/PR templates | repo owner |
| `BRIEF.md` | Internal working brief for the rebuild | lead |

All workstream directories are populated. `BRIEF.md` is an internal working
brief and is intentionally untracked; it is not part of the public package.

## Requirements

- Windows 10/11 with PowerShell 7.5+ for the tooling and scripts.
- Git 2.40+.
- For building the webOS package: Node.js 20+ and the
  [webOS CLI](https://www.npmjs.com/package/@webos-tools/cli) (`3.2.6` validated;
  pin whatever `scripts/build/` declares). See [`docs/building.md`](../../docs/building.md).
- A rooted LG webOS TV running an up-to-date Homebrew Channel to deploy and
  use the client. Root is required for the virtual input device; see
  [`docs/security-model.md`](../../docs/security-model.md).

## Quick start (development)

```powershell
git clone <your-fork-or-remote-url> synergy-tv
cd synergy-tv

# Install the optional local pre-commit guard once per clone
git config core.hooksPath .githooks

# Show what a build would do without creating artifacts
pwsh ./scripts/build/build.ps1 -DryRun
```

The build and test entry points are `scripts/build/build.ps1` and
`tests/lib/Run-Tests.ps1`; see [`docs/building.md`](../../docs/building.md) for the
deploy and acceptance sequence.

## Repository hygiene

The repository deliberately holds **no** secrets, license keys, backups, device
credentials, proprietary Synergy binaries or customer/personal data. The rules
are described in [`docs/repo-policy.md`](../../docs/repo-policy.md) and enforced by
[`.gitignore`](../../.gitignore) plus the optional [`.githooks/`](../../.githooks/) guard.
Keep changes small and stage explicit paths; do not use `git add -A` in this
repository.

## Documentation

- [`docs/README.md`](../../docs/README.md) — documentation index.
- [`docs/architecture.md`](../../docs/architecture.md) — how the pieces fit together.
- [`docs/building.md`](../../docs/building.md) — build and packaging.
- [`docs/testing.md`](../../docs/testing.md) — how changes are verified.
- [`docs/security-model.md`](../../docs/security-model.md) — trust boundaries and
  threat model.
- [`docs/repo-policy.md`](../../docs/repo-policy.md) — ignore/lifecycle/cleanup rules.
- [`docs/github-setup.md`](../../docs/github-setup.md) — remaining GitHub steps.
- [`docs/lifecycle.md`](../../docs/lifecycle.md) — artifact lifecycle receipts.

## Contributing and security

See [`CONTRIBUTING.md`](../../CONTRIBUTING.md) for the contribution workflow and
[`SECURITY.md`](../../SECURITY.md) for how to report a vulnerability privately.

## License

[MIT](../../LICENSE). Third-party components (Synergy, LG webOS tooling, bundled
libraries) keep their own licenses; see [`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md).
Synergy is used, not redistributed.

## Trademarks

"Synergy" is a trademark of Symless Ltd. This is an independent, unofficial
project and is not affiliated with or endorsed by Symless Ltd or LG
Electronics. "LG" and "webOS" are trademarks of LG Electronics. See
[`docs/brand-and-licensing.md`](../../docs/brand-and-licensing.md).
