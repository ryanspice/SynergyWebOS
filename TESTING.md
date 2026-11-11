# Testing

## Principles
- Verify by the failures a change could introduce, not by line count.
- Prefer deterministic checks (parse, lint, type check, build, unit tests) before manual review or device testing.
- Reuse valid prior evidence and attribute it. Do not rerun unrelated suites.
- No test does not mean no verification. A docs change needs a readback, link check and diff review; a client change needs a recorded live test.
- Report status as PASS / FAIL / BLOCKED / NOT_RUN, with the command and inputs. Keep the first failure. A timeout or skipped check is never a pass.

## Repository-level checks
- Repo hygiene: `pwsh ./scripts/check-repo-guards.ps1` (CI: no flag = tracked files).
- Docs links: `pwsh ./scripts/check-docs-links.ps1`.
- Workflow syntax: `npx --yes yaml-lint .github/workflows/ci.yml .github/dependabot.yml` (optional).

## Build-level checks
- Package validation (`ares-package`), plus static contract checks for the client path, root ACG declaration and absence of a third-party launcher.
- Home-target contract: Windows/Super/Meta keys map to system Home; repeats/releases consumed.
- Alt-hold contract: five-second continuous Alt launches Home, releases injected modifier, cancels on release/pointer exit/disconnect/stop.
- Config tooling: dry-run/validation against a temporary Synergy core on a free port; no live server changes.
- Dock: catalog injection contract, open-app sorting, local browser check at 1920x1080.

## Manual TV/PC acceptance
See `docs/testing.md` for the full manual acceptance checklist. These cannot run in CI. Record result (date, versions, TV model, webOS version, commit) in release notes. Do not commit device evidence.