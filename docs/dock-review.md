# App dock review

## Intent
The dock is a **separate, standalone app switcher** for the TV: an autohide bottom
bar that lists open/running apps first and lets a keyboard/mouse user switch
between them. It is deliberately **not** an overlay over other apps and has **no
Orbis Home dependency**. SynergyWebOS supplies the live catalog because the TV denies
third-party `listApps`/`getForegroundAppInfo` calls; the dock itself stays
unprivileged and only calls the permitted `launch` method.

## What it currently does
- Packaged as a separate app `dev.canopy.tvdock` (`dock/`), with its own
  `appinfo.json`, `index.html`, `dock.js`, `dock.css`, `catalog.js`.
- SynergyWebOS's **Launch dock** button (`app/js/app.js` ~870) calls
  `getCatalog()` (root helper reads the app/running/foreground catalog), then
  `prepareDockCatalog()` base64-encodes it and writes `catalog.js` into the dock
  package directory via HBC `exec`, then `launchApp('dev.canopy.tvdock')`.
- The dock reloads `catalog.js` on visibility, sorts open/foreground apps first,
  marks each entry Foreground/Running/Open, and supports bottom-edge reveal,
  `D` key, Tab/Enter, Escape/Hide, 3.5 s autohide, and Refresh.
- If opened without a fresh catalog, it shows the last catalog or an explicit
  unavailable message (no fabricated list).
- No close action is exposed (no portable authorized close contract).

## Why we would want it
- Gives a keyboard/mouse user a way to switch between open TV apps without
  Orbis Home, satisfying a core user goal (BRIEF §1.2).
- Keeps the privileged catalog read in SynergyWebOS and the dock unprivileged,
  respecting the TV's ACG policy (app/REPORT.md).
- Autohide + keyboard-first matches the TV-safe interaction model.
- Reuses the existing root helper and catalog code; no new service dependency.

## Why we might not want it (or its limits)
- **It is not a true overlay.** It replaces the foreground app; it does not stay
  visible over another app. A genuine always-on dock and global hotkey remain
  unverified (dock/README.md overlay research).
- **Catalog freshness depends on SynergyWebOS.** The dock cannot refresh its own
  catalog (no root); it shows stale data unless launched via SynergyWebOS.
- **Two-app install burden.** Users must install both `dev.canopy.synergywebos`
  and `dev.canopy.tvdock`; the dock is inert without the main app.
- **Running-state availability is firmware-dependent.** `dev/running` may be
  denied or absent; the UI degrades to "unavailable" rather than a real list.
- **No close/quit contract.** Launching an app replaces the dock; returning
  requires the user to reopen the dock.
- **Maintenance surface.** Two packages, two app IDs, two build artifacts, and a
  fragile `catalog.js` write path (root exec + base64) that can fail silently.

## Recommendation
Keep the dock as a **secondary, documented capability** for 1.0.0 (it already
works and is tested), but do not advertise it as an overlay or a complete
running-app list. Consider, for 1.x, folding a simpler in-app "open apps" list
into SynergyWebOS itself (the Apps view already exists) and treating the
standalone dock as optional. Re-evaluate a true overlay only with firmware-
specific evidence. Record the decision in `docs/decisions.md`.