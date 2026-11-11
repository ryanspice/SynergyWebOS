> **Legacy 0.3.4 reference.** The native 0.4.0 candidate is documented in the root README and `native/docs/`. Dock references below describe a retired component; no dock is included. Historical TV results do not validate the native rebuild.

# Flash-off 2 — build / QA / package report

Date: 2026-10-31 (final build update). Scope: `scripts/build/**` and `tests/**`;
the lead separately installed and exercised the artifact on the TV. The build
pipeline itself still never installs, launches or edits the TV.

Read before acting: `README.md`, `docs/building.md`, `docs/testing.md`,
`C:\Users\spice\.dsh\AGENTS.md`, `B:\Dev\AGENTS.md`.

## 1. Release artifact

| | |
| --- | --- |
| Artifact | `dist/synergy-tv-app-0.3.2.ipk` (gitignored `dist/`) |
| Size | 70,184 bytes (13 staged payload files) |
| **SHA-256** | **`44005d565ecfbc31422f491826e7ddfe922c75981b30a7945a01c780c5e4d6df`** |
| Sidecar | `dist/synergy-tv-app-0.3.2.ipk.sha256` |
| Receipt | `dist/build-info.json` (status `PASS`, same hash, tool versions) |
| Package ID / version | `dev.hurrspice.synergywebos` / `0.3.2` (upgrade over installed 0.1.8) |
| Companion dock | `dist/synergy-tv-dock-0.3.0.ipk` (`dev.hurrspice.tvdock`) |
| Payload | `index.html`, `appinfo.json`, `js/app.js`, `js/core.js`, `css/app.css`, `assets/mark.svg`, `contracts/handoff-profile.schema.json`, `client/{client.py,probe.py,tvctl.sh}`, `icon.png`, `icon-large.png`, `icon-256.png`, `webos-meta/synergy-tv-build.json` |

Verify: `Get-FileHash dist/synergy-tv-app-0.3.2.ipk -Algorithm SHA256` must equal the
sidecar. `ares-package -i` reports `Package: dev.hurrspice.synergywebos`, `Version: 0.3.2`,
`Architecture: all`.

Earlier builds of the same source produced `2f062a4a…` (98,242 B) and `93de40ea…`
(98,242 B, `icon-512.png` removed after the first); the final `e6412b8b…` (67,348 B) is a
rebuild of the identical source and shows that IPKs are **not bit-reproducible across
runs** — `ares-package` stores timestamps. Each build's hash is recorded in its own
receipt; the sidecar next to the artifact is authoritative for the file being installed.
Verify the sidecar against the artifact before every deploy (the lead should do this
after any rebuild).

## 2. Commands and actual results (direct observation, this session)

| Command | Result |
| --- | --- |
| `pwsh -NoProfile -File scripts/build/build.ps1` | **PASS, exit 0** — 0 errors, 1 warning; IPK + sha256 + receipt produced |
| `pwsh -NoProfile -File scripts/build/build.ps1 -CheckOnly` | **PASS** (no artifact) |
| `pwsh -NoProfile -File tests/lib/Run-Tests.ps1` | **PASS — 69 passed, 0 failed, 1 skipped, 0 blocked (70 tests, 12.9 s)** |
| `tests/lib/Run-Tests.ps1 -Tag unit` | 29/29 pass |
| `tests/lib/Run-Tests.ps1 -Tag acceptance` | 33 pass, 1 skip |
| `tests/lib/Run-Tests.ps1 -Tag integration` | 7/7 pass |

Environment: Windows 10.0.26300, PowerShell 7.6.9, Node v26.7.0, Python 3.12.1,
ares-cli 2.4.0 (`ares-package`).

### Skipped (NOT a pass)

- **`sh -n app/client/tvctl.sh`** — SKIPPED. Git-for-Windows `sh`/`dash` and WSL fail to
  start in this sandbox (`couldn't create signal pipe, Win32 error 5`,
  `Wsl/Service/CreateInstance/E_ACCESSDENIED`); shellcheck/busybox are not installed. The
  script compiles under the real TV shell only once on the device. Compensating static
  checks that **did** run: LF-only/no CR bytes, shebang, no bashisms (`[[`, `function()`,
  here-strings, `source`), no legacy launcher ID, `home_target`/`system` present. Run
  `sh -n` on a normal host or accept it as a lead device gate.

### Warnings (non-blocking)

- `tv.css-has` in `app/css/app.css`: `.radio-card:has(input:checked)`. **Benign for the
  dev TV** (webOS 26 = Chromium 132 supports `:has()`), but it silently fails on webOS 24
  (Chromium 108) and older, which the manifest floor claims (`>=22 / Chromium 87`). The
  same rule also has a `.is-selected` fallback class, so no visual break is expected — but
  if the floor claim is real, keep the `.is-selected` toggle authoritative (owner:
  Flash-high). Otherwise lower the claimed floor.
- Advisory to Flash-off 1: `.github/workflows/ci.yml` does not invoke
  `scripts/build/build.ps1` or `tests/lib/Run-Tests.ps1`; its Test step looks for
  `scripts/build/test.ps1` or `npm test`, neither of which exists, so **CI currently
  builds and tests nothing**. Recommended two steps:
  `./scripts/build/build.ps1 -CheckOnly` then `./tests/lib/Run-Tests.ps1`. `build.ps1`
  `-Tag`/CI needs ares-cli for the packaging step; `-CheckOnly` avoids that requirement.

## 3. What was built

- `scripts/build/build.ps1` — one entry point: static checks, clean staging, `ares-package`,
  SHA-256 + sidecar, `dist/build-info.json`. Flags: `-CheckOnly/-SkipPackage`,
  `-StagingDir`, `-AresCliDir`, `-AllowMissingApp`, `-Json`.
- `scripts/build/lib/Common.ps1` — environment/tooling detection, native runner with
  timeout, child-PowerShell runner, receipts.
- `scripts/build/lib/Checks.ps1` — all deterministic checks (appinfo, HTML incl. SVG via
  XML parser, CSS, JS syntax, TV static compatibility, layout heuristics, legacy-coupling
  scan, forbidden artifacts, home contract, corner tooling, 2 px geometry math).
- `scripts/build/package-manifest.json` — excludes, Chromium floor/targets, build-owned
  override (`webos-meta/synergy-tv-build.json`). `app/appinfo.json` override is refused
  before any staging side effect.
- `tests/lib/Run-Tests.ps1` (runner; unit/acceptance/integration tags; JSON/report output)
  and `tests/lib/{Assert,Ipk}.ps1` (fixtures; dependency-free `ar`+`tar.gz` IPK reader).
- `tests/unit/Checks.Tests.ps1`, `tests/acceptance/{App,Components,Repo,Shell}.Tests.ps1`,
  `tests/integration/Pipeline.Tests.ps1`; `scripts/build/README.md` (usage, dependency table).

## 4. Acceptance coverage against the brief (section 7)

| Requirement | Evidence | Status |
| --- | --- | --- |
| No Orbis in app/home/dock/docs/config | `repo: … no legacy Orbis coupling` (app, dock, tools, docs, root files), `shipped app/dock have no legacy launcher launch targets`, staged-tree scan | PASS for shipped product |
| Windows key → system HOME, no stray Meta, configurable delay | `app: client home contract`, `Meta keys … never injected`, `app: Meta keys are consumed`, `scripts/home/client-home.patch` contract, cleanup plan/apply/rollback parse | static PASS; device gate (§6) |
| Dock separate, autohide, keyboard/mouse, no Orbis | `dock: present with … catalog/launch`, `it is separate from the app`, overlay-limitation documented, keyboard/pointer documented; id parity with `app/js/core.js dockAppId` | static PASS; device gate |
| 2 px top-left hot corner config; delay/override; no broad edge | `corner: … switchDelay and override`, `candidate uses 2px percentage intervals, not the broad 15% edge`, unit geometry check (0.052 % × 0.093 % on 3840×2160), validator isolated to port 24999 | tooling PASS; 2D square not established (§6) |
| Rebrand, 1080p TV-safe, static, installable | appinfo/viewport/focus/reduced-motion/offline checks, `tv-static-compat` at Chromium 87, `:has()` warning, IPK built and inspected | PASS static; visual gate |
| Clean repo, no secrets/backups/proprietary | `repo: no secrets…`; staging filter + staged-tree scan | PASS (working tree) |
| Reproducible IPK + hash | `pipeline fixture: real build.ps1 packages … with sha256`, `IPK contains …`, `release IPK payload …`, `no Orbis … inside the real packaged artifact` | PASS |

## 5. Findings for other owners (not edited here)

5.1 **`scripts/home/cleanup.py` and `scripts/home/REPORT.md` contain the legacy package
name** (`com.canopy.orbishome`) and a `luna-send … launch` string. This is a deliberate,
reviewed exception: the cleanup must name its target and records a manual undo command.
The check scope is recorded in `scripts/build/lib/Checks.ps1` (`scripts/home/` excluded
from the token scan); the shipped `app/`+`dock/` are checked with a stricter rule that has
no such exception. **Lead:** keep `scripts/home/**` ship/cleanup-only and never package it
(staging only copies `app/`). The launch string must stay unused as a home action — the
`client-home.patch` check verifies the home path itself never references it.

5.2 **`icon-512.png`** (and `icon-256.png`) were packaged. `app/BRAND.md` says they are
for documentation/store use and no runtime file references them. `icon-512.png` is now
excluded via `package-manifest.json` (reviewed, 22 KB saved); `icon-256.png` is kept
(harmless, small). No app file was edited.

5.3 **`app/` contains local dev debris** (`.edge-profile*`, `.preview`, `.mcp-test.mjs`,
`.make-preview.mjs`, `.metrics.mjs`, `.smoke.mjs`, `client/__pycache__/`). None is
packaged (staging skips dot entries and `__pycache__`; verified). Owner: Flash-high —
remove before the final commit so `.gitignore`/repo guards do not have to carry them.

5.4 **CI wiring gap** (Flash-off 1) — see §2 warning.

5.5 **`tvctl.sh` cannot be POSIX-checked on this host** — see §2 skip; the app's own
`INTEGRATION.md` lists `sh -n` as a pre-deploy gate, so run it on a host where a POSIX
shell works (or on the TV with `sh -n` before installing; read-only).

## 6. TV deployment risks (lead acceptance — not run here)

1. **Client start / input injection:** needs root + writable `/dev/uinput`; `probe` must
   PASS on the TV. `tvctl.sh` autostart earlier quarantines the old hook — verify the old
   `60-synergy-webos*` hook is renamed and the new one works after reboot (do not reboot
   under this brief; verify on the next natural restart).
2. **Windows key:** socket write to `/tmp/netinput.pointer.sock` is not visual proof.
   Test left+right Meta, repeat, both together, `off` mode, 0 ms and long holds, release
   cancellation, missing socket, and that the system HOME appears with no stray Meta and no
   other app opening. The app client consumes 125 and 126 and never injects Meta (static
   check), but only the device proves it.
3. **`requiredACG`:** declared `lunabus.query`, `network.query`, `application.launcher`.
   webOS 26 enforces when present and the TV is webOS 26 (Chromium 132); a missing group
   would fail those Luna calls at runtime rather than at install. Verify app grid, dock
   launch and connection status after install.
4. **`:has()` fallback** (§2) — if the TV is on webOS ≤24 the radio-card highlight relies
   on the `.is-selected` class. TV is webOS 26, so low risk.
5. **Dock is a standalone card, not an overlay** — it replaces the foreground app. Do not
   expect it over YouTube/Twitch; the app's "Launch dock" only requests the launch.
6. **Hot corner is not a proven 2 × 2 square** — the candidate uses two 2 px edge
   intervals (0.0520833333 % / 0.0925925926 % of 3840×2160). Physical x/y 0–2 tests and
   the GUI-computer question remain open (Terra 1 report).
7. **Rollback:** reinstall the previous IPK (same ID). Keep the current live app/client
   untouched until the lead's controlled window.

## 7. Remaining gates owned elsewhere

- Physical/visual acceptance on the TV (lead): install, focus/pointer, autohide, corner,
  Windows key, system HOME.
- `sh -n` on `tvctl.sh` on a host with a working POSIX shell (or the TV).
- CI wiring to the build/test entry points (Flash-off 1).
- Removing `app/` dev debris and `.github` update before the final commit (Flash-high /
  Flash-off 1). The lead should re-run `build.ps1` + `Run-Tests.ps1` after any app
  change; both are read-only against `app/`.
