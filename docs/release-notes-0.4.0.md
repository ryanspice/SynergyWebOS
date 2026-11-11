# SynergyWebOS 0.4.0 — release notes (DRAFT)

**Status: DRAFT — not published. TV acceptance is not complete.** Do not post
this release or submit it to a store/apps repo until the gates in
`docs/native-tv-runbook.md` pass on the exact artifact.

**Date:** 2026-11-11 · **Source:** `main` after the native P1/schema batch ·
**Artifact:** `dev.canopy.synergywebos_0.4.0_arm.ipk`

## Highlights

- Native C++20 / SDL2 / raw GLES2 app replaces the web runtime on the TV:
  Connection, Input, Handoff and Diagnostics panels.
- Separate root input daemon with a UID-checked local control socket; the UI
  does not request root itself.
- Classic Synergy 1.4 protocol with persistent framing, idle/handshake
  deadlines, modifier reconciliation, Home/Alt holds, Alt+Tab and held-input
  cleanup on leave/disconnect/stop.
- Control-schema negotiation between UI and daemon; the UI warns on a mismatch.
- The dock, Apps page, companion launch/catalog writing and generic launcher
  RPCs are **removed** (D-011). Home and Alt+Tab are input shortcuts only.
- Legacy 0.3.4 web app remains in the tree as migration/rollback source and is
  **not** published as 0.4.0.

## Artifact

| Field | Value |
|---|---|
| Package | `dev.canopy.synergywebos` |
| Version | `0.4.0` |
| Architecture | `arm` (ARM32 EABI5) |
| Size | 2,793,826 bytes |
| SHA-256 | `4801e5971ea8827daa0a9d0dfbc149885eeee799c8fb4e8b45ac137f7a9bd501` |
| Contents | three ARM executables, appinfo, icons, notices, `scripts/provision-root.sh`, bundled SDL2/json-c/zlib |
| Modes | executables 755, data 644 |

Host/ARM evidence: `docs/native-build-evidence-0.4.0.md`. The IPK is currently
local/untracked; no release asset exists.

## Requirements and installation

- LG C3-class rooted TV with Homebrew Channel, webOS SDK 11.2.0 target runtime,
  and the verified root provisioning path.
- Windows PC with Synergy 3 on a trusted LAN; classic unencrypted TCP only.
- Stop and disable the legacy 0.3.4 client/autostart before the first native
  boot. Do not run two input daemons.
- Install through the TV application manager, then follow
  `docs/native-tv-runbook.md` for UID/socket, root provisioning, dry run and
  real-input checks.
- Roll back by reinstalling the retained 0.3.4 package and restoring its
  startup route; native state lives under `/var/lib/webosbrew/synergy-native`.

## Identity and migration (D-012)

The pre-rename 0.3.4 package was `dev.hurrspice.synergywebos`. Native 0.4.0 is
`dev.canopy.synergywebos`; on a TV that still has the earlier install it appears
as a **second app**. Stop the old client/hook first, record both entries, and do
not import legacy settings automatically. See `docs/decisions.md` D-012.

## Verification performed

- Host Release: 4/4 CTest; host ASan/UBSan (UI off): 4/4 CTest.
- Native UI under Xvfb/llvmpipe: 17/17 checks, real GLES2 framebuffer; Diagnostics
  displays `SynergyWebOS 0.4.0`.
- ARM32 cross-build via Starfish SDK 11.2.0 Clang; `audit-stage.py` PASS;
  `GLIBC_2.38` / `GLIBCXX_3.4.32`.
- Not performed: TV install/launch, app UID/jail socket, uinput, root
  provisioning, cold boot, hardware GLES and pointer/modifier behavior.

## Known limitations

- No TLS, clipboard, file transfer, cloud discovery or absolute cursor
  synchronization. Relative uinput only; the cursor nudge is experimental and
  off by default.
- Shared controller UID is a shared trust boundary; no app-level identity and no
  rotation UI yet.
- No dock, Apps page or system-wide overlay.
- The TV application-manager jail and uinput acceptance are unproven.
- The ARM IPK has not been installed on a TV; this draft must not be published as
  a working release.
