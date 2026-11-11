# SynergyWebOS integration contract

This document is the handoff between the app scope (`app/**`) and the other
workstreams. The lead owns final merge, live apply, TV install and acceptance.
Nothing here has been applied to the live TV, client, Synergy config or server.

## 1. App surface

| Concern | Location | Contract |
| --- | --- | --- |
| UI | `index.html`, `css/app.css`, `js/app.js` | static 1080p; four sections; no framework runtime |
| TV services | `js/core.js` | `PalmServiceBridge` root exec + LS2 calls; one-shot timeouts |
| client control | `client/tvctl.sh` | commands and `snapshot` markers below |
| Synergy client | `client/client.py` | Synergy 1.x, uinput, Meta/HOME |
| probe | `client/probe.py` | read-only JSON + PASS/FAIL |
| handoff profile | `contracts/handoff-profile.schema.json` | `junction.handoff.v1` |
| brand | `BRAND.md` | name, palette, app-ID and migration |

The app calls the root helper (`luna://org.webosbrew.hbchannel.service/exec`)
only for `tvctl.sh`. It calls the application manager for catalog, foreground,
launch and the companion dock. It calls the connection manager for the TV
address. There is no other network call.

### Stable `tvctl.sh` surface

The app depends on these commands remaining available and accepting these
argument orders:

```text
snapshot
start HOST PORT NAME MODE WIDTH HEIGHT HOME_TARGET HOME_DELAY_MS
restart HOST PORT NAME MODE WIDTH HEIGHT HOME_TARGET HOME_DELAY_MS
stop
probe
autostart-enable HOST PORT NAME MODE WIDTH HEIGHT HOME_TARGET HOME_DELAY_MS
autostart-disable
profile-save JSON
server-status HOST API_PORT
```

`MODE` is `real` or `dry`; `HOME_TARGET` is `system` or `off`;
`HOME_DELAY_MS` is `0..5000`.

`snapshot` output is parsed, not displayed raw. Preserve these section markers
and the first-line status convention:

```text
===STATE===
running pid=1234
{"state":"connected","pid":1234,"server":"...","port":24800,"screen":"webos-tv"}
===AUTOSTART===
enabled
===CONFIG===
home_target=system
home_delay_ms=0
===LOG===
...
```

If a future control script changes the format, update `snapshot()` in
`app/js/core.js` and `app/client/README.md` together.

## 2. Terra 1 — Synergy handoff/delay

### What the app does

- Settings > "The 2 × 2 corner" stores a local profile:
  `junction.handoff.v1` with edge `top-left`, 2 × 2 px, enabled/suspend,
  `switch_delay_ms`, and the TV screen name.
- "Save profile to TV" writes
  `/var/lib/webosbrew/junction/handoff-profile.json` (0600) via
  `tvctl.sh profile-save`. It does **not** touch the Synergy server.
- "Read desktop" runs `tvctl.sh server-status HOST 24803`, which performs a
  read-only `GET http://HOST:24803/v1/settings` with curl or wget and displays
  only service version, `developers.switchDelay`, and
  `override.shouldOverride`. It does not use the write endpoints.
- The app never modifies the live custom config, the database, the core or the
  service.

### What Terra 1 / the lead must do

- Consume the exported profile and translate it to the reviewed custom-config
  text. A true 2D 2 × 2 region is not established by edge intervals alone; Terra
  1 rejects that claim. The candidate uses two 2-pixel edge intervals and still
  needs physical acceptance. The profile is honest about this in its `notes`.
- Apply delay/override through the reviewed desktop path, not from the TV. The
  server's observed `developers.switchDelay` and the custom config's
  `switchDelay` are separate; do not assume one changes the other.
- Keep the TV screen name stable (`webos-tv`) unless the desktop config is
  deliberately migrated. The app can set any valid name, but changing it without
  the server update breaks the link.
- If Terra 1 provides a desktop helper or an apply script, have it accept this
  exact JSON schema. The app's exported path is a stable staging location; the
  helper must validate the schema and require an explicit apply, with a backup.

The app intentionally has no "Apply to desktop" button. The current server
tooling has no proven safe write contract, and a false success from a TV button
would be worse than a copyable, reviewable profile. This is the accepted
limitation for this build.

## 3. Terra 2 — dock / app switcher

### Built-in surface

"Apps" is always available and uses the TV application manager directly:

- catalog: `listLaunchPoints`, then `listApps` fallback
- foreground: `getForegroundAppInfo`
- running: optional `dev/running`; only claimed when it returns an array
- open: `launch` with a validated app ID

No running state is fabricated when the TV denies the optional call. Icons use
the path the TV returns with a letter fallback. The grid is keyboard-navigable
(arrows) and filterable (`/`, `Esc`).

### Companion dock

- Proposed standalone ID: `dev.canopy.tvdock`.
- The Apps view reserves the `#dockSlot` / `data-dock-slot="junction"` card as
  the visual integration point. The current card launches the standalone app;
  if Terra 2/lead embeds a dock instead, replace or append within that slot and
  keep the built-in app grid and launch fallback.
- A single constant in `app/js/core.js` (`APP.dockAppId`) plus the Apps and
  Settings copy must change if Terra 2/lead chooses a different final ID.
- "Launch dock" requests the app through the normal launcher. Failure is shown
  as "Dock unavailable; the built-in app grid still works". A launch request is
  not proof that focus changed.
- Do **not** bind the system Home key to the dock. The Home key belongs to the
  TV's system home.
- A third-party card app cannot guarantee an always-on overlay above other TV
  apps. The app does not claim one and does not request floating-window
  privileges. If the lead later accepts a true overlay, it is a new integration
  with its own permission and recovery review, not a hidden mode of this app.
- The standalone dock can be packaged separately. If it is embedded into this
  app later, namespace its IDs/listeners and keep the same launch/catalog
  adapter; do not copy document-level handlers into this shell.

## 4. Terra 3 — Windows key to system HOME

The app UI stores `homeTarget` (`system`/`off`) and `homeDelayMs` (0–5000) and
passes them as `--home-target` and `--home-delay-ms` on the next client start.
`app/client/client.py` already contains the behaviour:

- Windows/Super/Meta IDs `0xEFE7/0xEFE8/0xEFEB/0xEFEC` consumed on
  down/repeat/up; no modifier injected, including in `off` mode;
- system target launches the webOS system Home app
  (`com.webos.app.home`) with `luna-send`;
- hold delay cancels on release/COUT/disconnect/stop;
- holding either Alt key for five seconds launches system Home and releases the
  injected Alt, without breaking shorter Alt shortcuts;
- dry-run logs instead of launching.

The root helper must resolve the packaged `client/tvctl.sh`, not a package-root
`tvctl.sh`. Keep `requiredACG` containing `lunabus.query`; the acceptance suite
checks both. The TV denies application-manager queries to unprivileged apps, so
SynergyWebOS reads the app catalog/running list through the root helper and feeds the
dock's `catalog.js`. The dock remains unprivileged.

This folds in the intent of `scripts/home/client-home.patch`; reconcile names
and comments if that patch is treated as the canonical source. The old patch
should not be applied blindly on top of the new client.

`scripts/home/cleanup.py` remains a separate lead-controlled cleanup and is not
invoked by the app. The app has no launcher-specific code, assets, bindings or
home fallback. It never launches a third-party home screen.

The lead must verify on the TV: both Windows keys, repeat, both keys together,
off mode, 0 ms and long holds, release cancellation, pointer exit, missing
`luna-send`, system HOME, no stray Meta, and no other app opening. A successful
service exit code is not visual proof.

## 5. Build, package and deploy

### Package

- Source root: `app/`
- Package ID: `dev.canopy.synergywebos` (upgrade-compatible; see `BRAND.md`)
- Version: `0.3.2`
- Entry: `index.html`
- Icons: `icon.png`, `icon-large.png`
- Permissions: `lunabus.query`, `network.query`, `application.launcher`
- No npm install, no bundler, no Node on TV.

Include the `client/` directory in the package; the UI resolves `tvctl.sh` at
`/media/developer/apps/usr/palm/applications/dev.canopy.synergywebos/` and
also checks the two fallback install roots.

Do not package local preview/cache files (`.preview*`, `.edge-profile*`) or
documentation-only PNGs. The build/QA owner owns the exact `ares-package`
command, output name, hash and release artifact.

### Pre-deploy gates

1. `app/appinfo.json` parses; ID and version are correct.
2. `node --check app/js/core.js` and `app/js/app.js` pass.
3. `python -m py_compile app/client/client.py app/client/probe.py` passes.
4. `sh -n app/client/tvctl.sh` passes on a POSIX/busybox shell.
5. Handoff profile parses against its schema (or the lead's validator).
6. No secrets, backups, proprietary bundles, device credentials or third-party
   launcher assets in the package.
7. The package contains no Node runtime and makes no external web request.
8. The lead has a current client/config backup and a rollback IPK.

### Deploy order (lead only)

1. Coordinate a quiet window; stop the live client first so the installed files
   are not replaced underneath a running process.
2. Install the new IPK over the same ID (version 0.3.2 > 0.1.8).
3. Keep the desktop screen name and current Synergy config unchanged for the
   first launch.
4. Open SynergyWebOS, run **Probe TV**, confirm root + writable uinput.
5. Start the link from the saved host/port/name; verify pointer/keyboard.
6. Verify Windows key behavior for both targets, then enable autostart only
   after the link is proven. Confirm the old autostart hook was quarantined or
   remove it through the app's Disable action.
7. Install/launch the companion dock separately if the lead accepts the
   standalone-card limitation; record that it is not an overlay.
8. Apply the 2 × 2 handoff profile on the desktop only after physical
   acceptance; do not infer a true 2 × 2 region from parser success.
9. Record checks, hashes, failures and remaining acceptance limits in the
   repository handoff.

### Rollback

Reinstall the previous IPK over the same ID. The old browser storage key is left
untouched, and the new runtime files are inert to the old build. Review the
renamed legacy autostart hook before restoring it. No repository data is
required for rollback.

## 6. Known limitations

- Classic unencrypted Synergy TCP; no TLS and no clipboard sync.
- Real input requires root + `/dev/uinput`; a locked-down TV will show a blocked
  probe and cannot inject.
- The Synergy GUI may not represent the TV as a normal movable computer; the
  current custom-config approach is the accepted approximation and is external
  to this app.
- A true 2D 2 × 2 activation region, per-corner delay and a true global overlay
  dock are not established. The UI labels the handoff "Experimental" and the
  dock "standalone".
- TV service names, permissions and icon paths can vary by firmware. The app
  degrades to an explicit status, not a fake connected state.
- The app is not yet installed or launched on the target TV. Runtime and
  physical acceptance remain lead gates.
