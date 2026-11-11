# Verification audit — native 0.4.0 handoff

**Date:** 2026-11-11 · **Scope:** the claims in `native/docs/VERIFICATION.md` and
the handoff `VERIFICATION (4).md`, checked against the delivered tests and
evidence logs. This is an audit note, not a replacement for the authoring run.

At audit time no native build was available in this environment. A later local
`WebOSBuild` rerun (host Release + ASan/UBSan, ARM32 cross-build, IPK) is recorded
in [docs/native-build-evidence-0.4.0.md](../../docs/native-build-evidence-0.4.0.md).
The source-bundle and Python checks below were re-run on Windows; the original
handoff CTest/sanitizer claims were read from its logs.

## Local rerun (2026-11-11)

| Check | Result |
|---|---|
| Host Release CTest | 4/4 pass |
| Host ASan/UBSan CTest (UI off) | 4/4 pass |
| ARM32 cross-build | ELF32 ARM for all three executables; stage audit PASS |
| ARM IPK | `dev.canopy.synergywebos_0.4.0_arm.ipk`, SHA-256 `74310a39…822bf` |

These are host/ARM-build results, not TV evidence.

## What was re-run

| Check | Result |
|---|---|
| `python native/tests/bundle_tests.py` (default Windows locale) | 8 tests, 2 `UnicodeDecodeError` (cp1252) — the source reads are now explicitly UTF-8, so this is fixed in-tree |
| `PYTHONUTF8=1 python native/tests/bundle_tests.py` | 8/8 pass |
| Gear-spike fixtures | 11 tests; 10 pass, `test_shell_syntax` fails without `sh` on Windows |
| `py_compile` on the Python tests/collector | exit 0 |
| Bundle manifest | 189/189 listed hashes match; archive SHA-256 matches the handoff |
| PNG dimensions | 720p/1080p/4K captures have the claimed sizes |

## Claim corrections

| Claim | Audit finding | Required wording / action |
|---|---|---|
| "Debug **and** Release host builds of all three executables" | Only the Release build evidences all three. The Debug evidence is an ASan build with `SYNERGY_BUILD_UI=OFF`. | Say "Release: all three; Debug: core/process/IPC under sanitizers (UI disabled)". |
| "68 assertions" (daemon + CLI) | The count is 68 only as root; the non-root trace is 67. Thirteen are `packet()` helper checks, and the sanitizer log says 65. | Record root/non-root counts and the semantic scenario count; do not present 68 as universal. |
| "ASan + UBSan … pass" | The logged sanitizer build predates the final daemon/UI/test edits; no final instrumented rebuild or commands are recorded. | Rebuild from the exact release commit, log flags/commands, and re-run all four CTest tests. |
| "17 UI checks using actual SDL events" | Events are app-pushed `SDL_PushEvent`; "physical text input" is a pushed `SDL_TEXTINPUT`; screenshots are real captures but the tests do not assert their content. | Say "synthetic/app-pushed SDL events"; assert screenshot dimensions/content or label the images as visual evidence only. |
| "Resolutions … pointer mapping" | PNG headers match; the JSON data does not encode resolution, and the edit tests check draft text, not pointer pixels. | Add a real pointer/scaling assertion before claiming mapping. |
| TV-only `-fsyntax-only` gate | Only a 56-byte PASS line exists; no command, compiler, flags or source list. | Log the command or drop the claim. |
| Eleven gear tests / shell syntax | 10/11 on Windows without `sh`; no shell-syntax log. | Run on Linux/CI or mark skipped, not passed. |

## What is still not established

No ARM32 target build, installable IPK, TV launch, uinput injection, root
provisioning, app-jail socket access, hardware GLES behavior, sleep/wake or boot
persistence was performed for this candidate. Host screenshots and llvmpipe
rendering are not TV evidence.

## Required before publication

1. Native CI green on the release commit (Release + CTest + ASan/UBSan).
2. Regenerated sanitizer evidence from that exact commit, with commands/flags.
3. Corrected counts and "synthetic events" wording in the verification notes.
4. TV session 1/2 evidence per `docs/native-tv-runbook.md`.

Handoff-only artifacts (`native/bin`, `native/verification`, `native/.ai`, the
bundle checksum manifest) are not tracked; a clean clone cannot reproduce them.
