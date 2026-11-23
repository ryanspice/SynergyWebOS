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
  `ares-package`, using the verified SDK and a fresh stage. Dated 0.4.1
  ARM/package checks are recorded in `native/docs/INPUT-0.4.1.md`; those do not
  prove today's changed source, full target ABI/runtime closure or physical input.
- TV acceptance: install, UID/jail socket reachability, uinput, dry run, real
  input, Home/Alt+Tab, sleep/wake and rollback; record as manual evidence.

## Manual TV/PC acceptance
Use `native/docs/INPUT-0.4.1.md`, `native/docs/TV-EXECUTION.md` and
`docs/native-release.md` for current native acceptance and rollback boundaries.
`docs/native-tv-runbook.md` retains the initial 0.4.0 procedure;
`docs/testing.md` is legacy 0.3.4 guidance, not a dock requirement for native work.

Pairing, app and private-daemon upgrade, physical media/controllers/buttons,
focus-leave/disconnect/Stop release, PC-side exclusivity, runtime/ABI loading,
sleep/wake/boot and app-plus-private-runtime rollback need exact-artifact evidence.
These cannot run in CI. Record date, versions, TV model/webOS version, source
commit and artifact hashes; keep private device evidence project-local, not
committed. Installing the UI or passing loopback checks does not close these gates.
## Gate-to-evidence map (current candidate set)

The maintained native TV version is 0.4.1 (package/CMake). The desktop input
companion is versioned separately as 0.4.2; its output is not a native build,
and companion-only changes do not bump the native version.

| Gate | Command / CI job | Evidence | Status |
|---|---|---|---|
| Repo hygiene, docs links | `pwsh scripts/check-repo-guards.ps1`, `pwsh scripts/check-docs-links.ps1` | CI "Repo hygiene + docs links" | CI verified |
| Native strict Release | CMake + Ninja + CTest, `-DSYNERGY_STRICT=ON` | CI "Native 0.4.1 (Linux host)", 15/15 tests | CI verified |
| Native ASan/UBSan | CMake + Ninja + CTest, `-DSYNERGY_SANITIZE=ON` | CI "Native 0.4.1 (Linux host)", 14/14 tests | CI verified |
| i18n catalog validity and locale round-trip | `native/tests/core_tests.cpp` within native CTest | CI "Native 0.4.1 (Linux host)" | CI verified |
| Bridge unit tests | `ctest --test-dir desktop/input-bridge/build/ci` | CI "Desktop input bridge (Linux fixtures)", 63 host checks | CI verified |
| Bridge synthetic mTLS loopback | `python3 desktop/input-bridge/tests/proxy_loopback.py` | same CI job, 95 checks | CI verified |
| Native + bridge end-to-end | `python3 desktop/input-bridge/tests/native_proxy_e2e.py` | CI "Native 0.4.1 (Linux host)", 14 checks | CI verified |
| MSVC x64 companion build | VS 2022 + vcpkg OpenSSL 3, `ctest -C Release` | CI "Desktop input bridge (MSVC x64)" | CI verified |
| Legacy 0.3.4 web app | `scripts/build/build.ps1`, `tests/lib/Run-Tests.ps1` | CI "Legacy web build + test" | CI verified (migration source only) |
| Target ARM32 / IPK | cross-build, `audit-stage.py`, `ares-package` | dated 0.4.1 record in `native/docs/INPUT-0.4.1.md` | DATED - not re-run for current source |
| TV physical acceptance | manual runbook | `native/docs/TV-EXECUTION.md`, `native/docs/INPUT-0.4.1.md` | PENDING owner |
| Windows XInput hardware | companion with a real controller | none | PENDING owner |

The native catalog in `native/src/i18n.cpp` is the single source of truth for
translated UI strings; `catalog_valid()` and locale round-trip tests run in
native CTest. The legacy `app/` catalog checker is orphaned with that migration
source; its fate is an owner decision, not a CI gate.

Review-identified open follow-ups are recorded in `native/docs/VERIFICATION.md`
(TLS unit tests, UI sanitizers, sustained mixed traffic, reconnect/LS2/platform
cancellation) and `docs/ROADMAP.md`. None weakens strict warnings or the
fail-closed ingress budgets.
