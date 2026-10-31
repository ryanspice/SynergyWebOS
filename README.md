# synergy-tv

**Working title.** `synergy-tv` is the placeholder name for a clean rebuild of a
TV-first Synergy experience for LG webOS televisions and a companion dock/app
switcher. The branded name, visual identity and final repository name are owned
by the brand decision in [`app/BRAND.md`](app/BRAND.md) (maintained by the app
owner; the lead finalises the repository name before publication).

> Status: **pre-release scaffolding.** This repository is being assembled from
> the previous prototype workspace. Not every directory described below is
> populated yet, and nothing here has been deployed to the live TV or PC.

## What it is meant to be

- **TV app** — a lean, 1080p, keyboard/mouse-first webOS app that shows the
  Synergy client connection, live log, root/uinput probe, autostart state,
  home-key target and handoff settings. See [`app/`](app/) and
  [`app/BRAND.md`](app/BRAND.md).
- **TV dock / app switcher** — an autohide taskbar for switching between open
  TV apps, independent of any third-party home screen. See [`dock/`](dock/).
- **Handoff configuration tooling** — a small, configurable hot corner that
  hands control to the TV, plus Synergy 3 server/config/API helpers. See
  [`tools/synergy/`](tools/synergy/).
- **Home-key handling** — the Windows key returns to the TV's own system home
  screen, not to another app. See [`scripts/home/`](scripts/home/).
- **Build and test pipeline** — reproducible webOS package build and the
  acceptance checks. See [`scripts/build/`](scripts/build/) and
  [`tests/`](tests/).

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

Directories that are not yet populated are documented here on purpose; they are
created by their owning workstream. Do not create placeholder files in another
owner's directory.

## Requirements

- Windows 10/11 with PowerShell 7.5+ for the tooling and scripts.
- Git 2.40+.
- For building the webOS package: Node.js 20+ and the
  [webOS CLI](https://www.npmjs.com/package/@webos-tools/cli) (`3.2.6` validated;
  pin whatever `scripts/build/` declares). See [`docs/building.md`](docs/building.md).
- A rooted LG webOS TV running an up-to-date Homebrew Channel to deploy and
  use the client. Root is required for the virtual input device; see
  [`docs/security-model.md`](docs/security-model.md).

## Quick start (development)

```powershell
git clone <your-fork-or-remote-url> synergy-tv
cd synergy-tv

# Install the optional local pre-commit guard once per clone
git config core.hooksPath .githooks

# Show what a build would do without creating artifacts
pwsh ./scripts/build/build.ps1 -DryRun
```

The build and test entry points live in `scripts/build/` and are owned by the
build workstream. Until they land, see [`docs/building.md`](docs/building.md)
for the intended interface.

## Repository hygiene

The repository deliberately holds **no** secrets, license keys, backups, device
credentials, proprietary Synergy binaries or customer/personal data. The rules
are described in [`docs/repo-policy.md`](docs/repo-policy.md) and enforced by
[`.gitignore`](.gitignore) plus the optional [`.githooks/`](.githooks/) guard.
Keep changes small and stage explicit paths; do not use `git add -A` in this
repository.

## Documentation

- [`docs/README.md`](docs/README.md) — documentation index.
- [`docs/architecture.md`](docs/architecture.md) — how the pieces fit together.
- [`docs/building.md`](docs/building.md) — build and packaging.
- [`docs/testing.md`](docs/testing.md) — how changes are verified.
- [`docs/security-model.md`](docs/security-model.md) — trust boundaries and
  threat model.
- [`docs/repo-policy.md`](docs/repo-policy.md) — ignore/lifecycle/cleanup rules.
- [`docs/github-setup.md`](docs/github-setup.md) — remaining GitHub steps.
- [`docs/lifecycle.md`](docs/lifecycle.md) — artifact lifecycle receipts.

## Contributing and security

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the contribution workflow and
[`SECURITY.md`](SECURITY.md) for how to report a vulnerability privately.

## License

[MIT](LICENSE). Third-party components (Synergy, LG webOS tooling, bundled
libraries) keep their own licenses; see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
Synergy is used, not redistributed.

## Trademarks

"Synergy" is a trademark of Symless Ltd. This is an independent, unofficial
project and is not affiliated with or endorsed by Symless Ltd or LG
Electronics. "LG" and "webOS" are trademarks of LG Electronics. See
[`docs/brand-and-licensing.md`](docs/brand-and-licensing.md).
