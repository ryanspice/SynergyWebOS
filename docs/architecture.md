> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Architecture

## Overview

```
┌──────────────────────── PC (Windows) ────────────────────────┐
│                                                               │
│  Synergy 3 server / core  ──────────► local REST API :24803   │
│   (proprietary Symless app, NOT in this repo)                 │
│        │ TCP :24800 (classic protocol, unencrypted)           │
│        │                                                      │
│  Handoff config tooling (tools/synergy/)  ──► server config   │
│   • hot-corner / delay / override settings                    │
│   • puts the TV into the server layout as a normal screen     │
└────────│──────────────────────────────────────────────────────┘
         │ LAN
┌────────▼──────────────────── TV (LG webOS, rooted) ───────────┐
│                                                               │
│  Synergy client process (client.py, run as root via tvctl)    │
│   • connects to the Synergy server                            │
│   • creates uinput virtual keyboard/pointer devices           │
│   • maps the Windows key → TV system HOME (scripts/home/)     │
│                                                               │
│  TV app (app/)                                                │
│   • status, live client log, root/uinput probe, autostart     │
│   • home target, handoff/hot-corner settings, dock slot       │
│                                                               │
│  Dock / app switcher (dock/)                                  │
│   • autohide taskbar, keyboard/mouse app switching            │
│   • standalone app or embedded dock component                 │
│                                                               │
│  Build pipeline (scripts/build/) → .ipk   Tests (tests/)      │
└───────────────────────────────────────────────────────────────┘
```

## Components

### Synergy server (PC) — external

The Synergy 3 application owns screen layout, links between screens, the
switch delay and its own config. It is proprietary and is **never** shipped
from this repository. The tooling in `tools/synergy/` only reads and proposes
config/API changes; the maintainer applies them.

### Handoff tooling (`tools/synergy/`)

Owned by the Synergy-config workstream. Goal: only a small (2 × 2 px) top-left
corner on the main PC screen hands control to the TV, with a configurable
switch delay/override, without slowing the pointer near corners. The tooling
validates a proposed config (for example against a temporary core on a
free port) and documents the exact apply/rollback steps.

### TV client (`app/client/`)

A Python client that connects to the Synergy server and injects keyboard and
pointer events through uinput. It runs as root under the TV's Homebrew root
service (`tvctl.sh`) and writes a small state/log file that the app displays.
The home-key behaviour is patched by `scripts/home/` so the Windows key
returns to the TV's **system home**, never to a third-party launcher.

### TV app (`app/`)

A lean, static webOS app for 1080p, keyboard/mouse first. It owns the UX:
connection form/status, live log, root/uinput probe, autostart toggles, home
target choice, handoff settings and (optionally) the dock slot. The brand and
app ID decision live in `app/BRAND.md`.

### Dock (`dock/`)

A separate taskbar/app-switcher that lists open apps and switches focus,
autohiding when idle. Whether it can overlay other apps depends on webOS
capabilities available to a third-party app; the workstream documents the
feasible architecture and the limitation, and provides an integration
contract for the app.

### Build and tests (`scripts/build/`, `tests/`)

Produce a reproducible `.ipk`, run static contract checks (no dependency on
third-party launchers, home target contract, config tooling validation in dry
run) and record hashes. Live-TV acceptance is performed by the maintainer, not
CI.

## Data flow: pressing the Windows key

1. The TV client receives the Windows-key event from the Synergy server.
2. The home patch suppresses the raw Meta event (and its repeat/release).
3. The client triggers the TV's system HOME action through the supported
   mechanism (framed network-input `HOME` button, with a launch fallback).
4. The TV shows its own home screen; a third-party launcher is never launched
   unless the user explicitly selects it elsewhere.

## Data flow: handoff PC → TV and back

1. The pointer moves into the configured handoff corner/edge on the PC.
2. The Synergy server switches the active screen after the configured
   `switchDelay`.
3. The TV client takes over pointer/keyboard through uinput.
4. Moving off the TV's configured edge returns control to the PC.

## Boundaries

- No component in this repository depends on a third-party home screen.
- No component in this repository bundles, installs or patches Synergy
  binaries or license data.
- The repository contains source, scripts and documentation only; builds
  produce artifacts under ignored `dist/` or deliberate `dist/release/`.
