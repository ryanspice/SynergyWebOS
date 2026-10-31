# Security policy

## Scope

This repository contains a TV app, a dock/app-switcher prototype and
configuration tooling for a **rooted LG webOS TV** talking to a **Synergy 3**
server on a trusted LAN. Relevant threats include:

- Leaked device credentials: the TV is reached over adb/SSH to a rooted device,
  and the PC holds per-device access material (for example webOS CLI keys
  under `%USERPROFILE%\.webos\` and any SSH key used for the device), all of
  which must stay out of the repo.
- Persisted root execution: the client runs as root to create uinput devices
  and writes an autostart entry under `/var/lib/webosbrew/`.
- Unencrypted network input: the classic Synergy protocol used by the TV
  client is not encrypted; anyone on the LAN who can reach the server could
  inject input.
- Handoff/input hijack: a misconfigured or broad handoff edge makes the TV
  take pointer control unexpectedly.
- Secrets in the repository or build artifacts.

Read [`docs/security-model.md`](docs/security-model.md) for the full model and
the recommended deployments.

## Supported versions

There is no released version yet. Once releases begin, only the latest release
line receives security fixes; older tags are unsupported.

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Use GitHub's private vulnerability reporting on the repository
(**Security → Advisories → Report a vulnerability**). If that channel is not
enabled yet, contact the maintainer through the GitHub profile linked from the
README and agree on a private channel first.

Please include:

- Affected component (app, dock, client scripts, tooling) and version/commit.
- Steps to reproduce, or a minimal test case.
- Impact and required attacker position (LAN, local PC, TV root, ...).
- Any suggested fix.

Do **not** include working credentials, license keys, your own SSH/device keys,
or personal data in a report.

## What to expect

- Acknowledgement within a few days.
- A private discussion and a coordinated fix.
- Credit in the advisory unless you prefer to remain anonymous.

## For contributors

- Never commit secrets, device keys, webOS CLI key directories, `.env` files,
  signing certificates, Synergy bundles or license data. The optional
  pre-commit guard in [`.githooks/`](.githooks/) blocks common cases.
- Redact host IPs, machine names, usernames and logs before posting them
  publicly.
- If you believe a secret was committed, report it privately **immediately**
  and do not rewrite history in a public fork.

## Out of scope

- Vulnerabilities in Synergy 3, LG webOS or the Homebrew Channel themselves;
  report those to their respective vendors.
- Attacks that require the attacker to already have root on the TV or
  administrator rights on the PC.
- Issues that only exist in an unsupported local configuration.

## Safety of the live system

Contributions must not install, launch, reboot or reconfigure the TV, or
restart the Synergy core/service. Provide patches or scripts; the maintainer
applies them. See [`CONTRIBUTING.md`](CONTRIBUTING.md).
