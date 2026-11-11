> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Security model

## Trust boundaries

| Boundary | Trust | Notes |
| --- | --- | --- |
| PC ↔ TV (LAN) | Trusted LAN only | The client speaks the unencrypted classic Synergy protocol. Anyone who can reach the server on the LAN can attempt to connect. |
| TV app ↔ TV root service | High | The client runs as root to create uinput devices. Only the app's own bundled scripts should be runnable through it. |
| PC ↔ Synergy REST API | Local only | The Synergy REST API must stay bound to localhost. Do not proxy it. |
| Developer PC ↔ TV (adb/DevTools) | High | Developer access enables arbitrary commands; keep it off in normal use. |

## Threats and mitigations

| Threat | Mitigation |
| --- | --- |
| Credentials (SSH/adb keys, webOS device keys, tokens) committed | Repository ignore rules, pre-commit/CI guards, explicit staging, no `.env` files. Keys stay outside the repo (see [`code-signing.md`](code-signing.md)). |
| Proprietary Synergy binaries or license data redistributed | Not allowed by design; ignore rules block `Synergy/`, `synergy-core*`, `*.asar`, `license*` and Synergy config snapshots. |
| Malicious input injection over the LAN | Keep the TV and PC on a trusted network; do not expose port 24800. A future encrypted-protocol mode would need a separate decision. |
| Broad handoff edge causing unexpected input takeover | Small (2 × 2 px) corner, configurable delay/override, documented rollback. |
| Root command execution from the TV app | The app only exposes fixed, bundled scripts; the root service is started by the Homebrew init script. Keep the app's package signing keys private. |
| Device access left enabled | Disable developer mode/DevTools after setup unless actively developing. |
| Supply-chain risk from dependencies | Dependency-light app; locked dependencies; Dependabot updates; no vendored binaries. |

## Deployment guidance

- Use a dedicated trusted Wi-Fi/VLAN for the TV and PC.
- Give the TV a reserved address, and the PC a fixed/reserved address.
- Do not port-forward Synergy, the webOS CLI, sshd, or the DevTools port.
- Keep the TV's Homebrew Channel and webOS updates under review; root
  services run with full device privileges.
- Sign release packages (see [`release.md`](release.md)) and verify the hash
  before installing.

## What this project does not protect against

- An attacker with root on the TV or administrator rights on the PC.
- Vulnerabilities inside Synergy 3, LG webOS or the Homebrew Channel; report
  those upstream.
- Network attackers on a network you do not control.

## Reporting

See [`SECURITY.md`](../SECURITY.md).
