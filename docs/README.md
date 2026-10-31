# Documentation index

Repository-level documentation owned by the repo workstream. Component
documentation lives with its component (`app/`, `dock/`, `tools/synergy/`,
`scripts/`).

| Document | Contents |
| --- | --- |
| [`architecture.md`](architecture.md) | How the app, dock, Synergy server, TV home path and build fit together. |
| [`setup.md`](setup.md) | PC and TV prerequisites and first-run configuration. |
| [`building.md`](building.md) | Build and package pipeline, output locations. |
| [`testing.md`](testing.md) | Verification strategy, statuses and what CI covers. |
| [`security-model.md`](security-model.md) | Trust boundaries, threats, mitigations, deployment guidance. |
| [`repo-policy.md`](repo-policy.md) | What may enter the repository; ignore/cleanup rules. |
| [`lifecycle.md`](lifecycle.md) | Artifact lifecycle inventory and preservation receipts for this rebuild. |
| [`handoff.md`](handoff.md) | Workstream handoff state and open dependencies. |
| [`decisions.md`](decisions.md) | Dated decisions and recorded exceptions (license, toolchain, brand). |
| [`github-setup.md`](github-setup.md) | Remaining GitHub steps (remote, description, branch protection, secrets scanning). |
| [`release.md`](release.md) | Release checklist and artifact handling. |
| [`brand-and-licensing.md`](brand-and-licensing.md) | Trademark, licensing and name-change considerations. |
| [`code-signing.md`](code-signing.md) | Where signing keys live (never in the repo) and how releases are signed. |
| [`troubleshooting.md`](troubleshooting.md) | Common failures and safe diagnostics. |
| [`delegate-notes.md`](delegate-notes.md) | Working notes from the rebuild delegates, sanitised for the repository. |

Conventions for these docs:

- Relative links only within the repository, so they work locally and on
  GitHub. `scripts/check-docs-links.ps1` verifies them.
- No secrets, license keys, host IPs, machine names, usernames, SSH keys,
  backups or personal captures. Use placeholders such as `<PC-LAN-IP>` and
  `<TV-LAN-IP>`.
- Device-specific evidence stays outside the repo; cite it by description.
