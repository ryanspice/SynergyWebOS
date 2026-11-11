> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Delegate notes (sanitised)

Working notes from the rebuild workstreams that are useful to keep in the
repository. Live configuration, device addresses, credentials and raw evidence
stay in the internal working areas, not here. Replace addresses with
placeholders.

## Synergy config workstream (`tools/synergy/`) — pending report

Goal: TV usable as a normal + normal-sized screen in the Synergy GUI, a 2 × 2 px
top-left handoff to the TV, configurable switch delay/override, no pointer
slowdown at corners.

Notes for integration:

- The live server config is managed by the Synergy app; test parser/behaviour
  changes against a temporary core on a **free port** (e.g. `24999`) with a
  temporary config and `--no-restart --no-tray`, then stop only that process.
- Never change the live DB/config/core or use the daemon IPC port during
  development.
- Deliverables: exact config text, API/CLI scripts, validation script, apply and
  rollback steps.

## Dock workstream (`dock/`) — pending report

Goal: autohide dock / app switcher for open apps, keyboard/mouse first, no
third-party launcher dependency.

Notes for integration:

- A true always-on overlay over other TV apps may not be possible for a
  third-party app; the workstream documents the best feasible architecture and
  the limitation, and provides an integration contract for the app's dock slot.
- Do not install or launch prototypes on the TV during development.

## Home-key workstream (`scripts/home/`) — pending report

Goal: Windows key returns to the TV's **system home**, not a third-party
launcher; suppress repeat/release; configurable override/delay; clean up the
old third-party launcher prototype.

Notes for integration:

- The client must not send the raw Meta key when the override is active.
- Provide an exact, reversible cleanup script for the old prototype and a
  verification checklist.

## App workstream (`app/`) — pending report

Goal: branded, lean, 1080p, keyboard/mouse-first TV app with status, live log,
root/uinput probe, autostart, home target and handoff/dock settings.

Notes for integration:

- Brand and app ID: `app/BRAND.md` is authoritative; update `docs/` and README
  title after the decision.
- Keep the TV runtime (built-in Chromium) as the target; static output only.

## Build/QA workstream (`scripts/build/`, `tests/`) — pending report

Goal: reproducible `.ipk` build and the acceptance checks (no third-party
launcher references, home target contract, config tooling dry run, static TV
compatibility).

Notes for integration:

- Keep the build interface in [`building.md`](building.md) in sync; CI activates
  automatically when `scripts/build/build.ps1` exists.

## Repo workstream (this one)

- Read-only inventory of the preserved old working areas:
  [`lifecycle.md`](lifecycle.md).
- Repository rules: [`repo-policy.md`](repo-policy.md).
- Remaining publication steps: [`github-setup.md`](github-setup.md).
