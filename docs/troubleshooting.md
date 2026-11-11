> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Troubleshooting

Safe diagnostics only. **Do not restart the PC, the TV, the Synergy core or
the running client to "fix" a problem** while someone is using them; collect
evidence first and decide with the maintainer.

## The TV app says "client not connected"

- Is the client process running and is a PID/state file present? A stale
  process can hold the connection without updating state; check the process
  start time against the last client start.
- Does the Synergy server log show the TV's screen name connecting? A
  "client not connected" while the server shows it connected points at a stale
  client process.
- Is the server config currently generated (GUI layout) rather than a custom
  config? A config refresh can rewrite the active config and drop screens that
  exist only in a manual file.

## The TV's screen name is rejected by the server

The server only accepts screens that exist in its active config. Either add the
TV as a normal screen in the Synergy GUI (preferred for the rebuild) or update
the custom config with the tooling in `tools/synergy/` **[pending]**. Match the
client's `--name` exactly, including case.

## Handoff is too eager or misses

- Check the handoff corner/edge size and the switch delay in the Synergy
  config. The rebuild target is a tiny top-left corner with a configurable
  delay; broad edges are the old behaviour.
- The pointer slowing near corners is usually edge detection with a long delay
  or a wide edge; see the tooling's findings.

## Windows key does nothing (or opens the wrong thing)

- The home patch **[pending: scripts/home/]** must map the key to the TV's
  **system home**. If a third-party launcher still opens, its input-hook
  binding for the key is still active.
- Check that repeat and release events are suppressed so Meta does not repeat.

## Package install fails

- Confirm the TV is in developer mode/enabled for the webOS CLI and the device
  is registered with `ares-setup-device`.
- Check the `appinfo.json` version/app ID and the package signing mode
  ([`code-signing.md`](code-signing.md)).

## Root probe fails

- Root access comes from the Homebrew Channel; confirm it is installed and the
  app's root service runs. Do not elevate by other means.
- uinput devices may be missing if the client was not started as root or the
  kernel modules are unavailable on that TV model.

## Collecting evidence without leaking data

Take logs locally, redact LAN addresses, machine names and usernames, then
attach only the redacted excerpt. Never attach Synergy configs that contain
license data.
