# App dock review

**Date:** 2026-11-11 · **Scope:** `dock/` standalone app-switcher package and its
integration with `dev.canopy.synergywebos` · **Basis:** current source, built
artifacts and earlier device reports. No TV hardware was used in this pass;
device results are marked *reported*.

**Decision: do not ship the standalone dock in 1.0.0.** The intent is worth
keeping; this implementation is not a dock, duplicates the main app's Apps view,
and there is no current dock package to ship. Treat the Apps view as the 1.0
switcher, keep `dock/` as experimental source, and stop advertising or wiring it
as a shipped capability.

## 1. Intent

The brief asks for a separate dock/app switcher that lets a keyboard/mouse user
**switch between open apps**, autohides, and has no Orbis dependency. It
explicitly allows a true always-on overlay to be infeasible and asks for the best
working option with the limitation documented (`BRIEF.md:12`). The brief also
allows the alternative of "a dock component integrated into the new Synergy app"
(`BRIEF.md:12`, `BRIEF.md:100-102`).

The job to be done is: while watching or using an app, bring up a switcher and
jump to another open app without losing your place. A switcher that only exists
after you leave the content does not perform that job.

## 2. What is implemented

- A separate full-screen 1920×1080 card app, `dev.canopy.tvdock`
  (`dock/appinfo.json:2`, `:9`), containing a hero page and a fixed bottom bar
  (`dock/index.html:3`).
- The bar lists the TV catalog with `Foreground` / `Running` / `Open` labels,
  sorts running first, and launches on click (`dock/dock.js:66-80`).
- The catalog is injected as `catalog.js` by SynergyWebOS: `openDock()` reads the
  catalog as root, base64-writes it into the installed dock directory, then
  launches the dock (`app/js/core.js:724-768`, `:780-789`).
- With a preset present the dock makes **zero** TV service calls; without one it
  queries `listLaunchPoints` / `listApps` / `getForegroundAppInfo` /
  `dev/running` and degrades honestly (`dock/dock.js:25-31`, `:72`).
- The only launch-capable call is `launch {id}` (`dock/dock.js:34`, `:79`). There
  is no close, return or restore action.
- "Autohide" is a CSS slide of the bar inside the dock's own window
  (`dock/dock.js:63-65`; `dock/dock.css:1`). The README concedes it hides UI, not
  the window or input capture (`dock/README.md:20-27`).

## 3. What the user actually experiences

1. In SynergyWebOS, press **Launch dock**. The app reads the catalog as root and
   writes `catalog.js`. If that write fails, `openDock()` swallows the error and
   launches anyway (`app/js/core.js:782-785`).
2. The dock becomes foreground; the previous app disappears. The user sees a
   full-screen "Your apps, within reach" page with the app bar at the bottom, not
   a switch over the content they were watching.
3. Picking an app launches it; the dock page goes away. To switch again the user
   must return to SynergyWebOS and press **Launch dock** again, or use the TV
   launcher (which shows the last written catalog or an unavailable message).
4. Pressing **Escape** or **Hide** slides only the bar away. The full-screen hero
   page stays foreground; the previous app never returns. There is no close or
   return contract.
5. Bottom-edge reveal and the `D` key work only while the dock page itself owns
   input (`dock/dock.js:83-85`; `dock/README.md:14`). The dock cannot be summoned
   from inside YouTube or Twitch — the one situation the switcher exists for.

## 4. Release-blocking findings

### 4.1 It is not a switcher

There is no global hotkey, no system-level edge trigger and no overlay. All
controls are page-scoped (`dock/dock.js:84`; `dock/README.md:14`). The dock can
only be opened from SynergyWebOS or the TV launcher, so it cannot switch apps
while another app is on screen. It is a second, full-screen launcher grid that
must itself be launched.

### 4.2 Hide/Escape strands the user

`hide()` toggles only `#dock` (`dock/dock.js:64`); the `<main class="stage">` is
static markup (`dock/index.html:3`) and remains foreground. No close/quit call
exists anywhere (`dock/dock.js:26`, `:34`). Selecting an app is the only way to
leave the card, and that loses the dock. This is a UX defect, not just a missing
feature.

### 4.3 There is no current dock package to ship

- `scripts/build/build.ps1` packages the app only (`scripts/build/package-manifest.json`
  has `appDir: "app"` and `artifactName: "synergy-tv-app"`); its only dock
  reference is a static source scan (`scripts/build/build.ps1:180-183`). The
  pipeline produces no dock IPK.
- The only dock artifact in `dist/` is `synergy-tv-dock-0.3.0.ipk`, dated
  2026-10-31 (before the `dev.canopy` rename). Extracting it shows
  `Package: dev.hurrspice.tvdock`, version `0.3.0`, and an app directory
  `usr/palm/applications/dev.hurrspice.tvdock`.
- Current source declares `dev.canopy.tvdock` (`dock/appinfo.json:2`) and the app
  launches exactly that ID (`app/js/core.js:10`, `:787`). No
  `dev.canopy.tvdock` IPK exists in `dist/`.

Result: a 1.0 install has no valid dock to launch. The **Launch dock** button
dead-ends with "Dock unavailable… install the companion package"
(`app/js/app.js:870-879`). This is currently an accidental exclusion, not a
decision — the 1.0 contract still lists the dock (`PLAN.md` item 7), and the
release docs still advertise it (`README.md`). (The main app IPK *is* current:
`dist/synergy-tv-app-0.3.4.ipk` extracts as `dev.canopy.synergywebos` 0.3.4.)

### 4.4 Stale catalog is silent

`openDock()` catches a catalog-write failure and opens the dock anyway
(`app/js/core.js:782-785`). The payload carries `generatedAt` (`core.js:754`) but
the dock never reads or displays it (`dock/dock.js`), and **Refresh apps** only
re-reads the same preset (`dock/dock.js:83` → `:25`). On a launcher launch the
user gets data of unknown age labeled "Open apps first", not an age warning.
This is not hypothetical: the root `exec` bridge is *reported* denied after the
TV reboot until re-provisioning (`app/REPORT.md:159-162`), which is exactly the
path that writes `catalog.js`.

### 4.5 It duplicates the main app's Apps view

The Apps view already renders the full catalog with Foreground/Running badges,
search, keyboard navigation and working launches (`app/js/app.js:714-727`,
`:759-788`); Home has a six-app jump strip (`:790-821`). The dock's only deltas
are running-first sort, a bottom-bar skin and in-page autohide — while being
weaker (no search, Tab-only navigation, full-screen takeover, no close). The
client-side Alt+Tab already cycles running apps from any TV app (*reported*,
`app/REPORT.md:38-40`), so the closest thing to the brief's switcher already
exists and is not the dock.

### 4.6 Fragile root write into an installed app directory

The catalog path is hardcoded to the developer-mode install root
(`/media/developer/apps/usr/palm/applications/dev.canopy.tvdock`,
`app/js/core.js:746`), unlike the main app's three-root fallback for its helper
(`core.js:17-21`). Reinstalling or updating the dock restores the `null` default
(`dock/catalog.js`), and a failed write is swallowed rather than surfaced. This
couples app switching to a privileged write into another package's directory.

### 4.7 Thin, un-gated coverage

`dock/dock.test.cjs` covers only `normalize` and adapter fallbacks
(`dock/dock.test.cjs:4-26`); nothing exercises the preset path, hide/schedule,
Escape/D, `reloadPreset`, or launch failure. CI runs
`./tests/lib/Run-Tests.ps1` (`.github/workflows/ci.yml:108`), which only checks
that dock files are present (`tests/lib/Run-Tests.ps1:85-93`), not the dock's
node tests. So the dock's behavior is not gated in CI, and the packaging gap in
4.3 is not caught either.

## 5. Why we would want it

- It is the closest feasible option to the brief's dock without overlay
  privileges, and the limitation is documented honestly (`dock/README.md:20-28`).
- Clean privilege split: the root catalog read stays in SynergyWebOS; the dock's
  preset path never calls HBC exec.
- No fabricated running list: unavailable state is stated explicitly
  (`dock/dock.js:71-72`).
- Keyboard/mouse-accessible, dependency-free, 1080p static markup, and it was
  exercised on the TV (*reported*: 64 apps, open/foreground first,
  `app/REPORT.md:126-129`).

## 6. Why we would not

- **It fails the only use case that matters:** it cannot be summoned over
  content, so it cannot switch apps while you are watching something. A card
  that takes over the screen is a launcher, not a dock.
- **It duplicates the Apps view** and is weaker than it; the second package buys
  no switching capability.
- **It currently cannot be shipped:** no dock IPK is built, and the only existing
  one has the pre-rename ID, so **Launch dock** fails on a clean 1.0 install.
- **It adds release surface:** a second package, app ID, version pair, apps-repo
  entry and acceptance gate for a secondary feature, plus a silent stale-data
  path and a root write into another app's directory.
- **It adds foreground churn to a known-fragile input stack:** repeated
  Home/dock/app cycles previously left the compositor without a focused app
  (*reported*, `app/REPORT.md:163-166`).
- **The honest thing is a smaller product:** the brief explicitly allows the dock
  component to live in the Synergy app. `app/INTEGRATION.md:119-122` already
  anticipated replacing or appending within the existing `#dockSlot`.

## 7. Recommendation for 1.0.0

1. **Ship one package.** Make the main app's Apps view the 1.0 switcher; add
   running/foreground-first ordering there (a few lines in `renderApps`) so it
   gains the dock's only real delta.
2. **Exclude `dev.canopy.tvdock` from 1.0.** Do not advertise it in `README.md`,
   release notes or the 1.0 contract; amend the plan's dock item to "post-1.0
   experiment". Do not submit it to apps-repo.
3. **Fix the dead-end.** Relabel the Apps/Settings **Launch dock** card to
   "App switcher (experimental)" or hide it when no companion build is
   installed; don't offer a button that always fails on a fresh install.
4. **Keep `dock/` as experimental source** with its tests, documented as a
   standalone card — explicitly not an overlay, not a global switcher, and not
   part of the release artifact set.
5. **Revisit a true dock only with evidence:** a third-party global-key/overlay
   capability accepted on the target firmware.

If the maintainer overrides this and wants the card shipped anyway, the minimum
gates are:

- Build a `dev.canopy.tvdock` IPK from current source and assert ID parity with
  `app/js/core.js:10` in the pipeline (the main app's ID check should cover the
  dock too).
- Make the catalog failure visible (show `generatedAt` age or a "stale" banner);
  do not launch a stale catalog as if it were current.
- Define and test the Escape/Hide outcome: return to the previous app, or exit
  the card explicitly. No stranding.
- Verify on hardware: focus change on launch, `visibilitychange` on return,
  pointer-edge reveal, 1080p safe area, install-root catalog write survives a
  reinstall, and no input-wedge regression over repeated cycles.
- Publish a tested app/dock version pair and label it "App Switcher card — not an
  overlay" everywhere.

## 8. Verified vs. unverified

**Verified from source/artifacts:** the full launch path; the preset short-circuit;
hide/Escape toggling only the bar while the hero page persists; no close/return
call; `generatedAt` written but unused; the dock artifact carries
`dev.hurrspice.tvdock` 0.3.0 while source and the app use `dev.canopy.tvdock`; the
main app 0.3.4 IPK is current (`dev.canopy.synergywebos` 0.3.4); the build
packages only `app/`; the Apps view duplicates catalog/launch/badges; CI does not
run the dock tests.

**Unverified / needs device (or relies on earlier reports):** actual focus
switching and `visibilitychange` behavior; Escape/back delivery on webOS; the
reported 64-app run, the ACL denials and the input-wedge incident; whether an
IPK-installed dock lives at the hardcoded write path; whether any third-party
overlay/global-key dock is feasible on this firmware.
