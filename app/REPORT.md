# SynergyWebOS app — current delivery report

**Owner:** app workstream (lead accepts)
**Scope:** `B:\Dev\webos\synergy-tv\app\**`
**Status:** built, packaged, installed and launched on the TV; core link, root
helper, Windows-key Home, Alt-hold Home and the dock catalog were exercised on
the target firmware.
**Brand:** SynergyWebOS `0.3.2`
**Package ID:** `dev.canopy.synergywebos` (kept for in-place upgrade)

## What landed

A static 1080p webOS app plus the adapted Synergy client. No third-party home
screen assets, bindings or launch path remain in the package.

| Area | Delivery |
| --- | --- |
| Brand | `app/BRAND.md` — SynergyWebOS, 2 × 2 corner mark, palette, app-ID rationale |
| Shell | `app/index.html`, `app/css/app.css` — 1920 × 1080, keyboard/mouse-first |
| Runtime | `app/js/core.js`, `app/js/app.js` — bridge, settings, polling, app catalog |
| Client | `app/client/client.py`, `client/tvctl.sh`, `client/probe.py` |
| Package | `app/appinfo.json`, icons, `webos-meta/` build receipt |
| Contracts | `app/contracts/handoff-profile.schema.json`, `app/INTEGRATION.md` |

### Features

- **Home:** live handoff board, input/root state, Windows-key state, autostart
  state and a six-app jump strip.
- **Link:** validated connection form, saved settings, start/stop, explicit
  restart-to-apply, root/uinput probe, live log, diagnostics, autostart.
- **Apps:** live catalog from SynergyWebOS's root helper, open/foreground apps first,
  filter (`/`), keyboard navigation, refresh, and a companion-dock launch card.
- **Settings:** Windows key target **System home / Off**, hold delay 0–5000 ms,
  handoff profile (enable/suspend/switch delay), read-only desktop check, reset.
- **Escape hatch:** holding either Alt key for five seconds opens the system Home
  screen and releases the injected Alt state. Normal Alt shortcuts still work
  before the hold threshold.
- **Alt+Tab:** jumps immediately to the next running TV app from the root
  `dev/running` list; no animation. Repeated Tab presses cycle, and Shift+Alt+Tab
  walks back. The cycle list refreshes for each new Alt hold.
- **Safety:** quoted root commands, no silent restarts, no third-party launch
  target, explicit preview/unavailable states.

## Root and helper contract

The packaged app declares `requiredACG: ["lunabus.query", "network.query",
"application.launcher"]`. On the TV this was verified end to end:

- `probe` reports `is_root: true`, `euid: 0`, writable `/dev/uinput`;
- the snapshot API succeeds through the Homebrew Channel exec service;
- the UI shows **Root helper · Ready**, not the old false “Helper unavailable”.

The false helper error was a real path bug: the app looked for `tvctl.sh` at the
package root, but the client script ships in `client/`. `core.js` now resolves
`$D/client/tvctl.sh` (and keeps the legacy `$D/tvctl.sh` fallback), with an
acceptance test. `requiredACG` is also checked by the test suite so a future
package cannot silently drop root access.

The TV Dock is intentionally **unprivileged**. This firmware denies
`listApps`, `listLaunchPoints`, `getForegroundAppInfo` and HBC `exec` to a
third-party category `/` app even with `lunabus.query`. SynergyWebOS therefore reads
the catalog and running list as root, writes `catalog.js` into the dock package,
and then launches the dock. The dock only uses the permitted `launch` method.
Do not “fix” this by weakening the TV's ACG policy.

## Handoff contracts

### Corner and delay

The live desktop Synergy config uses:

- `spicepc` `up(0,0.0520833333)` and `left(0,0.0925925926)` to `webos-tv`;
- `webos-tv` `right(70,100)` back to `spicepc(30,60)`;
- `switchDelay = 0` (and `developers.switchDelay = 0`).

Physical tests on the 3840 × 2160 PC screen confirmed:

| Input | Result |
| --- | --- |
| `x=100, y=0` top edge | no switch |
| `x=5, y=0` top edge | no switch |
| `x=0, y=100` left edge | no switch |
| `x=1, y=1` corner | switches to TV |
| TV lower-right/right edge | returns to PC |

The implementation is still an **edge-interval approximation**, not a hostile
proof of an interior 2 × 2 region; Synergy offers no positive 2D corner selector.
At the corner, the `left` link can win and enter the TV on its right edge rather
than its bottom. This is a documented topology limitation, not a packaging bug.
Per-corner delay is not supported; the delay is global.

### Windows key / Home

`client.py` consumes Windows/Super/Meta key IDs `0xEFE7/0xEFE8/0xEFEB/0xEFEC`
(down/repeat/up) and launches `com.webos.app.home` with `luna-send` on the
configured target. A real Windows-key press was verified to move the foreground
to `com.webos.app.home`; the client log recorded `System HOME launch requested`.

### Alt-hold Home

Alt key IDs `0xEFE9/0xEFEA` map to evdev 56/100. A five-second continuous hold
releases the injected Alt and launches system Home on a daemon thread; release
before five seconds cancels. A real 5.5-second hold produced:

```text
Alt held for 5s; opening system HOME
System HOME launch requested
```

and `getForegroundAppInfo` then returned `com.webos.app.home`. Pointer exit,
disconnect and stop cancel the pending hold. Normal Alt use is preserved until
the threshold.

## Dock catalog and open apps

SynergyWebOS's `getCatalog()` first asks the root helper for `listApps`,
`getForegroundAppInfo` and `dev/running`. On the TV this returned 64 apps with
SynergyWebOS foreground, YouTube and the dock running. `openDock()` writes a compact
`catalog.js` (absolute `file://` icon paths, running IDs, foreground) into the
dock package and launches `dev.canopy.tvdock`. The dock reloads `catalog.js`
on visibility, sorts open/foreground apps first, and marks each entry
`Foreground`, `Running` or `Open`.

Verified dock state:

```text
apps=64  mode=Open apps first · select an app
first: SynergyWebOS Foreground, YouTube AdFree Running, 365livesport Open, ...
```

The dock remains a standalone card: it replaces the foreground app and is not a
true always-on overlay or global hotkey. Open it from SynergyWebOS's **Launch dock**
button. The built-in Apps view is the fallback when the dock is not installed.

## Checks actually run

| Check | Result |
| --- | --- |
| Build pipeline `scripts/build/build.ps1` | PASS, artifact `dist/synergy-tv-app-0.3.2.ipk`, SHA-256 `44005d565ecfbc31422f491826e7ddfe922c75981b30a7945a01c780c5e4d6df` |
| Unit tests | 29/29 pass |
| Acceptance tests | 36 pass, 1 skip (no local POSIX `sh`; TV busybox syntax already passed) |
| Python syntax / Node syntax | pass |
| TV install | `ares-install` success over `dev.canopy.synergywebos` |
| Root probe | `is_root=true`, `uinput_access=true`, Python 3.12.6 |
| Helper snapshot | succeeds; UI shows **Ready** |
| Windows-key Home | foreground became `com.webos.app.home` |
| Alt-hold Home | log and foreground verified |
| Alt+Tab | verified live: `ALT-TAB -> youtube.leanback.v4`, foreground changed; no animation |
| 2 px edge tests | outside positions did not switch; corner did |
| Dock | 64 apps, open/foreground first, catalog reload verified |

## Residual risks and limits

- **Corner geometry is approximate.** See above; physical x/y at both edges is
  accepted, but a true 2D interior region is not supplied by Synergy.
- **Dock is not an overlay.** No always-on top-level dock or global hotkey is
  claimed. It must be launched, normally from SynergyWebOS.
- **Root/helper state is firmware-persistent only after re-provisioning.** The
  root helper (`lunabus.query` + HBC exec) was verified before the TV reboot. This
  TV's reboot restored an older `/var/lib/webosbrew` snapshot and HBC `exec` is
  currently denied; the client autostart hook was also reverted. Re-provision
  autostart/ACG after a clean power cycle if the helper shows unavailable.
- **Cursor/input wedging.** Repeated Home/dock/app cycles left the TV compositor
  with no focused app (`document.hasFocus() === false`) and no injected pointer
  events. A full TV reboot restored input (verified keyboard + mouse events).
  The live handoff now uses only the top 2px link, avoiding the right-edge entry
  that caused insta-return; a TV reboot is the recovery for a wedged input stack.
- **Dock is not an overlay.** No always-on top-level dock or global hotkey is
  claimed. It must be launched, normally from SynergyWebOS.
- **Alt-hold timing.** The timer counts continuous client-side Alt hold; Windows
  key-repeat or losing the TV screen cancels it.
- **Classic unencrypted Synergy TCP.** No TLS/clipboard; trusted LAN only.
- **Server status from the TV** still depends on TV→PC port 24803 reachability.
- **Reboot autostart** did not survive the observed TV runtime reset; the old
  `60-synergy-webos` hook returned and `60-junction` was absent. Re-enable
  autostart after the TV is re-provisioned.
- **Svelte exception.** Static ES2017 JS remains the documented choice for a
  zero-build TV package; no framework runtime is present.

## Current deployment sequence

1. Stop the live client.
2. Build with `scripts/build/build.ps1`.
3. Install `dist/synergy-tv-app-0.3.2.ipk` over the same ID.
4. Keep `webos-tv` and the existing Synergy screen entry.
5. Launch SynergyWebOS; confirm **Root helper · Ready** and start the link.
6. Verify Windows-key Home, five-second Alt-hold Home, pointer and keyboard.
7. Enable/confirm autostart; check the legacy hook is quarantined.
8. Install `dist/synergy-tv-dock-0.3.0.ipk`; launch from SynergyWebOS and verify the
   open/foreground ordering.
9. Leave the desktop Synergy delay at the reviewed value; do not infer a 2D
   corner from parser success.
