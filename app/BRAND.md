> **Historical 0.3.4 web app.** This document describes the retired web UI and
> its Apps/switcher surface. The native 0.4.0 candidate is documented in the
> root `README.md` and `native/docs/`; the dock is removed (D-011).

# SynergyWebOS — brand and migration

**Status:** adopted for the `0.3.4` legacy web build by **Ryan Spice-Finnie**.
The package ID is `dev.canopy.synergywebos`; a TV that still has the earlier
`dev.hurrspice.synergywebos` install may list both (see `docs/decisions.md`
D-012).

## 1. Name

**SynergyWebOS** is the TV-first Synergy experience: the point where the
desktop keyboard and mouse meet the TV, and where the TV's apps come together in
one switcher.

- Display name: **SynergyWebOS**
- Credit: **by Ryan Spice-Finnie**
- Wordmark: `SYNERGYWEBOS` in wide uppercase tracking
- Tagline: **Your desk, on the TV.**
- Product line: *Desktop input for TV*
- Repository name: `synergy-tv`
- Vendor: **Ryan Spice-Finnie**

This is a personal, unofficial app. “Synergy” is a trademark of Symless Ltd.;
this project is not affiliated with or endorsed by Symless or LG. The package ID
can stay stable while the display name evolves.

## 2. Visual identity

### Mark — "the corner"

Four rounded cells form a 2 × 2 block. The top-left cell is signal amber, a
direct reference to the two-pixel handoff corner; the other three are panel
tones. The gap is the junction. The mark is deliberately legible at 48 px and
still readable as an 80 px app icon.

- Master: [`assets/mark.svg`](assets/mark.svg)
- Package icons: `icon.png` (80), `icon-large.png` (130), plus `icon-256.png`
  and `icon-512.png` for documentation/store use
- Clearspace: at least one cell gap on all sides
- Do not stretch, rotate, recolor the amber cell, or place the mark on a
  low-contrast light background

### Palette

| Token | Hex | Use |
| --- | --- | --- |
| Ink | `#070b10` | app background |
| Panel | `#0d141b` | cards, rail |
| Panel raised | `#111a23` | interactive surfaces |
| Line | `#1d2a35` | borders, dividers |
| Text | `#f2f7fa` | primary text |
| Text dim | `#d3e0e8` | secondary text |
| Muted | `#8fa3b3` | supporting copy |
| Signal amber | `#ffb35c` | primary action, focus, live handoff |
| Amber high | `#ffc987` | hover |
| Circuit teal | `#56d9c3` | connected / healthy |
| Warning | `#f4c66a` | experimental / paused |
| Danger | `#ff7a7a` | blocked / error |

The interface stays dark so the TV does not become a light source in a dark
room. Amber is reserved for action and the handoff; teal is reserved for
connection health. There is no decorative red or green.

### Type

No web fonts are bundled. The app uses the TV's own UI font and then a system
fallback:

```
"LG Smart UI Display", "LG Smart UI", "Segoe UI", system-ui, -apple-system, sans-serif
```

The technical log and numeric values use:

```
"Cascadia Mono", "SFMono-Regular", Consolas, "Liberation Mono", monospace
```

At 1920 × 1080 the scale is:

- page title 52–76 px, tight tracking
- section title 27–28 px
- body 16–21 px
- labels 13 px, uppercase 11–12 px for metadata
- log 13.5 px, 1.62 line height

### Layout

- 90 px top bar, 296 px navigation rail, 1520 px maximum content width
- 46–56 px outer padding, content kept inside a 48 px TV-safe margin
- four keyboard-first sections: Home, Link, Apps, Settings
- large hit targets (54 px minimum for primary controls)
- one visible focus ring: 3 px amber with offset
- no hover-only information; every state is also text

### Motion

One pulse on a live status dot, one slow dash movement on the link path, and a
180 ms toast entrance. Motion is decorative only. `prefers-reduced-motion`
disables it.

### Voice

Direct, calm, specific. Say what will happen before it happens. No exclamation
marks, no "magic", no invented success. Example: "Saved values apply the next
time the client starts." A failed action says what failed and what still works.

## 3. App identity

**Recommendation: keep the package ID `dev.canopy.synergywebos` for this build.**

Reasons:

1. **Same-ID upgrade.** webOS installs a higher version over the same app ID. A
   TV already on `dev.canopy.synergywebos` keeps one entry. A TV still on the
   earlier `dev.hurrspice.synergywebos` install gets a second app, not an
   in-place upgrade; follow D-012 for that migration.
2. **Stable paths.** The installed application directory, the startup hook and
   the desktop's screen name (`webos-tv`) keep working during the rebrand.
3. **Less re-authorization.** A new ID is a second app with its own permissions
   and data; it adds no product value for this build.
4. **Brand ≠ package key.** The display name, icon and UI can change without
   changing the package identity.

The package metadata in [`appinfo.json`](appinfo.json) is therefore:

- id: `dev.canopy.synergywebos`
- title: `SynergyWebOS`
- version: `0.3.4`
- vendor: `Ryan Spice-Finnie`

A new ID is justified only for a true fork, a different publisher, or an app
that must coexist with this one. In that case use a new reverse-DNS ID owned by
the publisher and document the two-client migration; do not reuse this package
ID for unrelated software.

## 4. Migration notes

### Package upgrade

1. Stop the current client through the lead's controlled procedure; do not
   start two clients against one screen.
2. Install the new IPK over the existing canopy package ID. The higher version
   replaces the UI and static files in place. A `dev.hurrspice.*` install is a
   second app; follow D-012 instead.
3. Keep the Synergy screen name `webos-tv` until the desktop configuration is
   deliberately changed. Renaming the screen without updating the desktop breaks
   the link.
4. Open SynergyWebOS and run **Probe TV**. Start a link only after root + uinput
   are reported ready.

### Settings

- The app reads its own key `junction.settings.v1`.
- On first run it migrates only the connection fields from the previous build's
  `synergy-webos-settings` browser key. Launcher-specific options from the old
  build are intentionally dropped because SynergyWebOS has no third-party launcher
  coupling.
- Home target, hold delay and handoff profile start at safe defaults:
  system HOME, 0 ms hold, 2 × 2 corner profile enabled, 250 ms switch delay.

### Runtime files

New names live under `/var/lib/webosbrew`:

| Purpose | New path |
| --- | --- |
| log | `junction.log` |
| PID | `junction.pid` |
| state | `junction-state.json` |
| saved client config | `junction.conf` |
| autostart hook | `init.d/60-junction` |
| handoff profile | `junction/handoff-profile.json` |

`app/client/tvctl.sh` still reads the previous `synergy-webos.conf` if the new
config is absent, so an in-place upgrade can recover the old host/port/name.
Enabling autostart quarantines the previous generated hook (renamed, not
deleted) before writing `60-junction`; disabling autostart removes both hooks.
This is a user action, not an install-time change.

### Rollback

Reinstall the previous IPK over the same ID. The old browser key is left in
place, and the new `junction.*` runtime files are inert to the old build. Review
the renamed `60-synergy-webos.disabled-*` hook before re-enabling the old
autostart. The repository contains no backups or device data.

## 5. Assets and licensing

All visual assets in `app/` are original to this project: `assets/mark.svg`,
the generated PNG icons and the CSS-only UI. No third-party logos, icon packs,
fonts or launcher assets are redistributed. "Synergy" is a trademark of Symless
Ltd.; SynergyWebOS is an independent, unofficial client. "LG" and "webOS" are
trademarks of LG Electronics. See the repository `LICENSE` and
`THIRD_PARTY_NOTICES.md` for the repository-level terms.
