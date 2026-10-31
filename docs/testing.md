# Testing and verification

This page describes how changes are verified in this repository. Component
workstreams may add more detail in their own folder; the rules here always
apply.

## Principles

- Verify by the failures a change could introduce, not by line count.
- Prefer deterministic checks (parse, lint, type check, build, unit tests)
  before manual review or device testing.
- Reuse valid prior evidence and attribute it. Do not rerun unrelated suites.
- **No test does not mean no verification.** A docs change needs a readback,
  link check and diff review; a client change needs a recorded live test.
- Report status as **PASS / FAIL / BLOCKED / NOT_RUN**, with the command and
  inputs. Keep the first failure. A timeout or skipped check is never a pass.
- Live TV/PC acceptance is the maintainer's step. Delegates and contributors
  provide scripts and evidence, not live changes.

## Repository-level checks (run locally and in CI)

| Check | Command | Covers |
| --- | --- | --- |
| Repo hygiene | `pwsh ./scripts/check-repo-guards.ps1 -Staged` (CI: no flag = tracked files) | No secrets, keys, backups, proprietary bundles, dependency/build junk staged or tracked. |
| Docs links | `pwsh ./scripts/check-docs-links.ps1` | Relative Markdown links resolve. While `app/`, `dock/`, `tools/`, `scripts/` and `tests/` are still uncommitted, CI passes `-AllowMissing 'app,dock,tools,scripts,tests'`, which only warns for those prefixes; remove it after integration. |
| Workflow syntax | `npx --yes yaml-lint .github/workflows/ci.yml .github/dependabot.yml` (optional) | YAML parses and is well formed. |

The optional pre-commit hook (`.githooks/pre-commit`) runs the repo hygiene
check automatically when `git config core.hooksPath .githooks` is set.

## Build-level checks **[pending: scripts/build/]**

- Package validation (`ares-package`), plus a static contract check that the
  app/client contains no reference to a third-party launcher.
- Home-target contract: the client's home action maps to the TV system HOME and
  suppresses repeat/release events.
- Config tooling: dry-run/validation against a temporary Synergy core on a
  free port; no live server changes.
- Dock: static checks plus a local browser check at 1920 × 1080.

## Manual TV/PC acceptance (maintainer)

These cannot run in CI. Minimum acceptance for a release:

1. PC → TV handoff triggers only from the configured small corner, after the
   configured delay, and does not slow the pointer at other corners.
2. TV → PC return works with a physical mouse.
3. Windows key shows the TV's system home; a third-party launcher is not
   launched; no stray Meta input.
4. Dock shows open apps, switches focus and autohides.
5. The TV app reports status/log/probe accurately at 1080p with keyboard and
   mouse.
6. The Synergy core/service and the live client were not restarted during
   verification unless the release notes say so.

Record the result (date, versions, TV model, webOS version, commit) in the
release notes. Do not commit device evidence.

## CI scope and limits

CI runs on GitHub-hosted runners. It has no TV and no Synergy server, so it
proves the repository is clean, docs links resolve and the build/tests the
pipeline defines pass. It does **not** prove the app works on a real TV — that
is the manual acceptance above.
