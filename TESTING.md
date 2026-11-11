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
- Native host: CMake + Ninja + CTest per `native/docs/BUILD.md`, including the
  core, integration and UI suites; ASan/UBSan in a separate build directory.
- Source bundle: `python native/tests/bundle_tests.py` (set `PYTHONUTF8=1` on
  Windows); gear-spike fixtures under `spikes/gear-settings/`.
- Legacy 0.3.4 web app (migration source only): `pwsh ./scripts/build/build.ps1`
  and `pwsh ./tests/lib/Run-Tests.ps1`. Do not label its IPK a native build.
- Target: ARM32 cross-build, `native/scripts/audit-stage.py`, then
  `ares-package`; unexecuted until the verified SDK is supplied.
- TV acceptance: install, UID/jail socket reachability, uinput, dry run, real
  input, Home/Alt+Tab, sleep/wake and rollback; record as manual evidence.

## Manual TV/PC acceptance
See `docs/testing.md` for the full manual acceptance checklist. These cannot run in CI. Record result (date, versions, TV model, webOS version, commit) in release notes. Do not commit device evidence.