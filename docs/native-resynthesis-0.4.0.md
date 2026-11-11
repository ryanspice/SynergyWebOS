# Native 0.4.0 — findings resynthesis

**Date:** 2026-11-11 · **Source handoff:** `synergy-tv-native-0.4.0.tar.gz`,
SHA-256 `bebfdb4572a71442056aee922193c0e20a678e58b49ffe9bbf9249909e977984`
(190 files; import commit follows this document).

This reconciles the 1.0.0 release plan (thread `40f3e519`), the parallel review
and dock review (thread `4de03926`), the unfinished MiniMax review (`7c27ad6d`)
and four independent source audits of the native rewrite. No native build, ARM
target, TV, uinput device or Synergy server was exercised in this pass (WSL is
denied here); "FIXED (source)" means the code path is present and readable, not
that it has run on the target.

## 1. Bottom line

The native rewrite **retires the web client's confirmed protocol/input defects
in source** and **closes the dock question**. It does not yet produce a
shippable 0.4.0: the remaining blockers are target evidence (ARM32 build, IPK,
TV launch), root/boot persistence, the controller-UID trust boundary, and
verification claims that are stronger than their evidence. CI for the legacy
web app was also red before this import and is fixed here.

## 2. Disposition summary

| Disposition | Meaning |
|---|---|
| **FIXED (source)** | The old defect's mechanism is gone or replaced; a device/review gate may still remain. |
| **FIXED (integration)** | A repo/test/docs defect corrected in this import pass. |
| **NEEDS DEVICE RE-VERIFY** | Source is plausible; only the TV/ARM/uinput run can settle it. |
| **NEEDS REIMPLEMENTATION** | The old behavior is absent, but the replacement is incomplete or not yet built. |
| **RETIRED** | Deliberately removed (dock, Apps page, web root-exec), not a gap. |

## 3. Findings matrix

### Protocol and lifecycle

| Prior finding | Native evidence | Disposition | Gate |
|---|---|---|---|
| P1 partial-frame timeout desynchronised the stream | `native/src/protocol.cpp:58-73` persistent decoder; `client.cpp:164-174` aborts the connection on a stale partial | **FIXED (source)** | Mid-frame stall test + reconnect soak; no test holds a partial past 5 s today. |
| P2 no idle deadline; silent half-open session | `client.cpp:171-174`, `config.cpp:67` (3–60 s, default 15 s) | **FIXED (source)** | No test against a partially-open kernel socket; liveness only. |
| P3 stop during reconnect, once-on-failure, duplicate daemon | `client.cpp:83-88,102-122`; `ipc.cpp:110-116` | **FIXED (source)** | Successful reconnect after a live drop is still untested (only connect-fail/stop). |
| P4 TLS/oversized/short frames must be refused | `protocol.cpp:64-67,372-373,412` | **FIXED (source)** | TLS is refused, not implemented (explicit consent gate); no server authentication. |
| P5 held keys/buttons released on leave/disconnect/stop | `protocol.cpp:347-369`; `client.cpp:153-162`; `core_tests.cpp:197-223` | **FIXED (source)** | Release-write failures are swallowed (`protocol.cpp:348-357`); real uinput release unproven. |
| N6 handshake has no total deadline | `client.cpp:201` resets on any accepted frame | **OPEN (low)** | Add a time-to-QINN bound; no input occurs before CINN. |
| N9 ignored commands accept any length | `protocol.cpp:416` (`DCLP/CCLP/CSEC`) | **OPEN (low)** | Enforce documented lengths or state the exemption. |

### Root, daemon and IPC

| Prior finding | Native evidence | Disposition | Gate |
|---|---|---|---|
| P6 app-driven HBC `exec` denied after reboot; persistence never held | No app root-exec path; root route is `daemon.cpp:30-61` + `provision-root.sh:53-61` | **NEEDS DEVICE RE-VERIFY** | Cold boot on the C3: pid, socket, status without manual steps. |
| P7 UID-checked socket: ordering, mode, stale socket, bounds, no generic RPC | `ipc.cpp:154-156`, `config.cpp:152-154`, `daemon.cpp:148-201` | **FIXED (source)** | Jail reachability and the actual app UID are unproven; socket is 0666 and authority is the whole UID. |
| N1 autostart equates a marker line with success | `daemon.cpp:30-37,53,158`; hook output discarded, no supervisor/PID | **NEEDS REIMPLEMENTATION** | Supervisor or an explicit "not supervised" status; last-start evidence. |
| N2 controller UID is permanent, unaudited, includes root boot hooks | `daemon.cpp:182-185`; `provision-root.sh:25-26` refuses replacement | **OPEN (high)** | Rotation/revocation drill, per-op audit (UID/pid/op), shared-UID acknowledgment recorded. |
| N4 no UI↔daemon schema negotiation | `daemon.cpp:151` emits `schema:1`; no reader | **NEEDS REIMPLEMENTATION** | Reject/annotate mismatched schema at startup. |
| N5 one-daemon guard only knows `/var/lib/webosbrew/junction.pid` | `daemon.cpp:19-29,46-48` | **OPEN (medium)** | Process/device-based detection before boot autostart. |
| N7 control plane can be stalled by one same-UID peer | `ipc.cpp:145-168`; one connection per 25 ms, 500 ms/1 s deadlines | **OPEN (low)** | Per-connection worker or shorter deadlines. |

### Input and UI

| Prior finding | Native evidence | Disposition | Gate |
|---|---|---|---|
| Modifier state discarded on entry | `protocol.cpp:231-251,429`; `core_tests.cpp:216-229` | **FIXED (source)** | Capture real CINN/DKDN/DKUP sequences with pre-held Shift/Ctrl/Alt and both sides. |
| Pointer position not synchronised; keepalive thread safety | `input.cpp:207` relative-only; `protocol.cpp:341-345` keepalive on the session thread; `protocol.cpp:442-459` DMMV deltas | **NEEDS DEVICE RE-VERIFY** | 20 entries after Magic Remote movement, 30 s idle, drag/edge; is the nudge visible? Absolute sync is still a declared non-goal. |
| Home: both Meta keys, 0–5000 ms, no repeat | `protocol.cpp:256-270,327-331`; `core_tests.cpp:231-290` | **FIXED (source)** | TV: both keys, threshold/release, Luna foreground. |
| Alt+Tab consumes Tab, Shift reverses, cancels Alt-Home | `protocol.cpp:272-319,332-340` | **FIXED (source)** | TV: 0/1/many apps, stale target, rapid direction changes. |
| N1 Alt+Tab cache is wall-clock (1.5 s), not per hold | `platform.cpp:209` | **NEEDS REIMPLEMENTATION** | Refresh per Alt hold; two quick holds must not reuse a stale list. |
| N2 COUT can silently drop a queued Home/cycle action | `client.cpp:197`; `platform.cpp:200` | **OPEN (medium)** | Inject COUT after queuing; honor or report the action. |
| Handoff must not edit the Windows server | `daemon.cpp:187-199` returns `applied_to_desktop:false` | **FIXED (source)** | Local save/load across restart; Windows owns the corner. |
| Dock/Apps page/launcher RPCs | Four panels (`ui.cpp:382-391`); no `apps/launch/home` IPC; `bundle_tests.py` asserts absence | **RETIRED** | Keep out of packaging; `dock/` deleted (D-011). |
| Native window/Diagnostics never shows 0.4.0 | `ui.cpp:502-503,632` shows GL strings/title only | **NEEDS REIMPLEMENTATION** | Show `SYNERGY_VERSION` in-window/Diagnostics; legacy footer fixed to 0.3.4 here. |

### Release, identity, docs and CI

| Prior finding | Evidence | Disposition | Action |
|---|---|---|---|
| Dock not a switcher; do not ship | `dock/` deleted (9 tracked deletions), D-011; no dock wiring in shipped source | **RETIRED** | Do not restore; archive review is `.ai/archive/dock-review-20261111.md`. |
| App-ID history falsified; no migration note | `docs/decisions.md` D-007 corrected; D-012 added (hurrspice→canopy coexistence, first-boot/rollback) | **FIXED (integration)** | Put the D-012 warning in the release notes before any TV install. |
| Legacy `/app` still has Apps page, catalog, `launchApp(id)` | `app/index.html`, `app/js/core.js` | **OPEN** | Decide: strip from migration source or state precisely that only native/dock wiring was removed. |
| CI red: guard case bug | `scripts/check-repo-guards.ps1:110` now `(?-i)` for root `Synergy/` | **FIXED (integration)** | Rerun guard; 4 `tools/synergy/` false positives should disappear. |
| CI red: IPK reader vs `@webos-tools/cli` 3.2.6 | `tests/lib/Ipk.ps1:65-66` now ignores a non-path ustar prefix | **FIXED (integration)** | Rebuild with 3.2.6 and run the IPK tests. |
| CI red: `.ai/` scratch trips forbidden-artifact/TV-compat scans | `scripts/build/lib/Checks.ps1:190` excludes `.ai` | **FIXED (integration)** | Keep `.ai` out of shipped scans; do not delete evidence. |
| Import regressed the package-ID assertion | `tests/integration/Pipeline.Tests.ps1:125` restored to `dev.canopy.synergywebos` | **FIXED (integration)** | Rebuild and rerun. |
| No native CI, IPK or release process | `.github/workflows/ci.yml` still legacy-only; no ARM/IPK | **NEEDS REIMPLEMENTATION** | Native host/CTest job + owner-run ARM/IPK gate + native release procedure. |
| Live docs promised the removed dock/Apps | `INTENT.md`, `TESTING.md`, `SECURITY.md`, `app/BRAND.md` rewritten/bannered | **FIXED (integration)** | Keep historical dock prose only in labelled legacy docs. |
| `THIRD_PARTY_NOTICES.md` described a Chromium app; licence holder mismatch | Native runtime section added (SDL2/FreeType/json-c/libpng/foundation); `LICENSE` holder aligned | **FIXED (integration)** | Review before publication. |
| Bundle tests fail on Windows cp1252; `.ai` scratch trips them | `native/tests/bundle_tests.py` reads UTF-8 and excludes `.ai`/`dist` | **FIXED (integration)** | Still set `PYTHONUTF8=1` in CI for consistency. |
| Gear-menu spike must stay read-only | `spikes/gear-settings/` collector only; no menu patch | **FIXED (scope)** | Run the three captures on the C3 before any adapter. |
| SHA manifest ignores 5 `.pyc`; evidence/binaries gitignored | `SHA256SUMS.txt` 189/194; `.gitignore` excludes `native/bin`, `native/verification`, `.ai` | **HANDOFF-ONLY** | The bundle manifest was moved out of the tree to `.ai/archive/native-0.4.0-SHA256SUMS.handoff.txt`; do not imply a clean clone reproduces the ignored evidence. |

### Verification claims

| Claim | Audit result | Disposition |
|---|---|---|
| Debug **and** Release builds of all three executables | Only Release built all three; the Debug evidence is ASan with `SYNERGY_BUILD_UI=OFF` | **OVERCLAIM** — regenerate or narrow the claim. |
| ASan/UBSan on the final source | Sanitizer logs predate the final daemon/UI/test edits; reported 65 assertions, not 68; no instrumented rebuild logged | **NEEDS REIMPLEMENTATION** — rebuild from final source with flags recorded. |
| 68 integration assertions | 68 only as root; 67 otherwise; 13 are `packet()` helper checks; sanitizer log says 65 | **OVERCLAIM** — record root/non-root and semantic counts. |
| 17 UI checks use real SDL input | Events are app-pushed `SDL_PushEvent`; "physical text" is a pushed `SDL_TEXTINPUT`; screenshots are not asserted | **PARTIAL** — say "synthetic events"; assert pixels/dimensions. |
| Resolutions 720/1080/4K | PNG headers match; JSON encodes no resolution; edit tests check draft text, not pointer pixels | **PARTIAL** — add real pointer/scaling assertions. |
| TV-only `-fsyntax-only` gate | One 56-byte PASS line; no command/compiler/flags | **NOT REPRODUCIBLE** — log the command or drop the claim. |
| No ARM/IPK/TV/uinput/root run | Admitted in `native/docs/VERIFICATION.md:40-44` | **ACCEPTED LIMIT** — these are the 0.4.0 gates, not footnotes. |

## 4. Release gates before 0.4.0 can ship

**P0 — cannot publish without these**

1. **ARM32 target build** against the verified SkiFree/Spice Home SDK; run
   `audit-stage.py`, build the IPK, inspect contents/modes/checksums.
2. **TV acceptance on the C3:** install, launch through the application manager,
   confirm the app UID and jail reachability of the control socket, provision
   root, cold-boot autostart, uinput input, dry-run then real input.
3. **Boot/root evidence:** prove the managed hook starts the daemon after a
   power cycle, or state explicitly that it is not supervised; make `status`
   report a last-start signal, not a marker line (N1).
4. **UID boundary:** record the actual controller UID, add per-op audit and a
   supported rotation/revocation path (N2); schema negotiation (N4).
5. **Native CI/release:** a host build + CTest job, an owner-run ARM/IPK step,
   and a native release procedure (not the 0.3.4 web pipeline).
6. **Verification honesty:** rebuild sanitizer evidence from the final source,
   fix the assertion counts, and say "synthetic SDL events" where that is what
   happened.

**P1 — before a public 0.4.0 announcement**

- Show `0.4.0` in-window/Diagnostics; fix the legacy footer (done here).
- Alt+Tab cache per Alt hold; COUT/queued-action race; silent release failures.
- Total handshake deadline; ignored-command length checks; control-plane stall.
- Legacy daemon detection beyond the one PID file.
- Identity/migration release note (D-012): two-app coexistence, stop the old
  client/autostart, rollback to `v0.3.4-web`.
- Decide the retained legacy Apps page: strip it or label it migration-only.

**P2 — polish**

- Dead/historical dock prose in labelled legacy docs; keep only banner-flagged.
- Handoff-only vs tracked preview binaries/verification evidence; regenerate the
  SHA manifest or remove it from the tree.
- Gear-settings spike: run the three read-only captures; no menu patch until the
  C3's panel implementation is known.

## 5. What this import fixed in the repo

- Deleted `dock/` and archived its review; recorded D-011 (dock retirement).
- Corrected D-007's false history and added D-012 (identity/migration/rollback).
- Fixed the imported `Pipeline.Tests.ps1` stale-ID assertion, the repo-guard
  case sensitivity, the IPK ustar-prefix parser, and the `.ai` artifact scan.
- Made `native/tests/bundle_tests.py` UTF-8 and `.ai`-aware.
- Updated `INTENT.md`, `TESTING.md`, `SECURITY.md`, `app/BRAND.md` to the native
  product; fixed the legacy web UI's 0.3.2 labels and the remaining live 0.3.2
  metadata fields; added the native runtime section to `THIRD_PARTY_NOTICES.md`;
  aligned the licence holder. Historical 0.3.2/hurrspice reports remain, but
  every one is banner-labelled legacy.
- Allowed nested licence/notice files and README badge SVGs through the repo
  guard (they were false positives), and moved the incomplete bundle checksum
  manifest out of the tree to `.ai/archive/`.

Rollback: branch `legacy-web-0.3.4` / tag `v0.3.4-web` at the pre-import HEAD.
`native/bin`, `native/verification`, `native/.ai` and the handoff checksum
manifest are handoff-only and not tracked; the source tree is the reproducible
artefact. Checks after these fixes: docs links PASS (49 files / 145 links), repo
guards PASS (0 violations), native bundle tests 8/8, legacy suite 69 pass / 0
fail / 1 skip (no POSIX `sh`).

## 6. Prior-thread reconciliation (re-read against commit `00207e1`)

Thread `40f3e519` is complete. Thread `7c27ad6d` is still running and has no
final report; after the import landed it began re-reviewing the moved tree, so
its partial findings are used but not treated as a finished review.

| Prior claim | Status against the native build |
|---|---|
| 40f3e519: stabilize the 0.3.4 web app for 1.0 and retain the dock | **Superseded.** The owner rewrote the product natively; the dock is retired (D-011). The web app is migration/rollback source only. |
| 40f3e519: partial-frame timeout is a P0 | **FIXED in native source** (`protocol.cpp:58-73`); the mid-frame-stall soak remains an acceptance test, not a code fix. |
| 40f3e519: app-button root access and reboot persistence | **Architecture replaced.** The app no longer asks HBC to execute root commands; a provisioned daemon + init.d hook replaces it. Cold boot is still unproven. |
| 40f3e519: cursor keepalive unverified | **Still device-gated.** The native nudge is optional, off by default and single-threaded; absolute cursor sync remains a non-goal. |
| 40f3e519: modifier entry / pointer sync P1 | **Modifier entry fixed in source**; pointer behavior remains relative-only and needs a TV decision. |
| 40f3e519: status polling can stall on overlap | **Old prototype.** The native control plane has a different, lower-severity serial-stall risk (N7), not the same defect. |
| 40f3e519: repo guard false positives, 71/3 test baseline | **Fixed.** Guard 0 violations; legacy suite 69/0/1; native bundle tests 8/8. |
| 40f3e519 acceptance budgets (cold boots, handoffs, reconnects, idle soak) | **Still valid** and adopted below, extended with UID/jail/ARM/IPK gates. |
| 7c27ad6d: CI is red (guard, IPK reader, `.ai` scan) | **Fixed locally** (guard `(?-i)`, ustar-prefix parser, `.ai` exclusion); remote CI has not been re-observed, and no native job exists yet. |
| 7c27ad6d: app-ID rename breaks in-place upgrade; D-007 is false | **Corrected.** D-007 now records hurrspice→canopy; D-012 requires a migration/rollback release note. |
| 7c27ad6d: About/footer says 0.3.2 | **Fixed** in the legacy UI; remaining 0.3.2/hurrspice text lives only in banner-labelled historical reports. |
| 7c27ad6d: dock IPK is stale/mismatched; the build does not package it | **Retired.** The dock cannot be shipped because it no longer exists in the tree. |
| 7c27ad6d: cursor keepalive thread safety | **Superseded.** The native keepalive runs on the session thread; the remaining issue is visibility/drift on hardware. |
| 7c27ad6d: `.ai` scratch breaks scans | **Fixed** in `Checks.ps1` and `bundle_tests.py`. |
| 7c27ad6d: 74-pass vs 71/3-pass conflict | **Reconciled by command/environment.** The maintained result is 69/0/1 (legacy) plus 8/8 native bundle tests; prior totals depended on TEMP and `.ai` fixtures. |
| 7c27ad6d: final review | **Not delivered.** The thread is still running; its partial findings above were incorporated and checked against the new tree. |

**Net:** none of the old web-runtime findings is still a release blocker for the
native build. The surviving blockers are the native target/root/evidence gates
in section 4, plus the native CI/release path.
