# SynergyWebOS roadmap — synthesized 2026-11-23

This supersedes the two planning reviews of 2026-11-22 as the working roadmap.
It is rebased on the work actually completed since: the v0.4.2 desktop input
bridge stabilization (PR #10), closure of six focused review threads, strict
GCC 15 and MSVC x64 CI gates, and the 4 MiB frame-limit alignment between proxy
and native receiver. Planning only — no deployment or publication authority.

Source plans: `.ai/outbox/forward-plan-20261122/PLAN.md` and the two archived
T3 roadmap review threads. Status claims are host/CI evidence unless explicitly
labelled target-verified.

## Current state (what is actually true now)

| Area | Status | Evidence |
|---|---|---|
| 0.4.0 pointer baseline | **Done, target-verified** | `native/docs/TV-EXECUTION.md` (Twitch cursor, full-screen range, smooth leave/re-entry) |
| 0.4.1 media/keyboard/keypad candidate | Host-verified, target pending | `native/docs/INPUT-0.4.1.md`; host/loopback and package gates |
| Desktop bridge v0.4.2 stabilization | **Done, host + CI verified** | PR #10: bounded FIFO controller edges, safe in-flight writes, 4 MiB frames, fair classic/controller interleaving, reason-coded logs; GCC 15 + MSVC x64 CI |
| Proxy/native frame compatibility | **Done** | Both limits 4 MiB; ignored bodies stream; native 1.5 MiB parity test |
| CI gates | **Done** | 6 jobs: repo guards, build detection, legacy web, native Release + ASan/UBSan, Linux bridge, MSVC x64 |
| Reviews | **Integrated and settled** | Six focused threads; dispositions recorded in `native/docs/VERIFICATION.md` |
| Physical controller/TV/Dragon acceptance | **Pending** | No TV, XInput device, or real Dragon server was contacted |
| Native i18n `en`/`fr-CA` | In flight, target pending | `native/docs/I18N.md`; synthesis queue item `native-i18n-fr-ca-2026-11-22` |
| Handoff profile migration | Open | `.thoughts/HANDOFF.md` (new local schema; old Windows helper not migrated) |
| Dock, Apps page, launcher RPCs | **Deliberately removed** | D-011; do not re-propose |

## Phase 0 — land the stabilization (owner decision)

1. Merge PR #10 once the final CI run is green and the owner accepts the
   documented BLOCKED/PENDING items. No further code scope in this PR.
2. Preserve the evidence: PR description, CI run links, and the six thread
   dispositions in `native/docs/VERIFICATION.md`.

**Owner:** merge decision. **Agent:** nothing further on PR #10 without a new
approved scope.

## Phase 1 — cheap truth gaps (agent-actionable, host only)

1. Fix stale UI guidance in `native/src/ui.cpp` ("No TLS or clipboard",
   "relative pointer") to reflect the configured bridge and the verified
   0.4.0 absolute-pointer path.
2. Wire the i18n catalog check into the test runner and CI; one catalog source
   of truth; finish native `fr-CA` parity.
3. Rescue or formally retire the orphaned legacy-app i18n commit (`ba06e44`,
   reflog-only at review time) and the old handoff helper. **Owner** decides
   the fate of legacy `app/`.
4. Version/evidence consistency across bridge 0.4.2, native 0.4.1 and the
   release notes; map every `TESTING.md` gate to a recorded artifact.

## Phase 2 — close review-identified test gaps (agent-actionable, host only)

| Gap | Target |
|---|---|
| TLS certificate/chain/hostname/SNI and `WANT_READ`/`WANT_WRITE` unit tests | `native/src/tls.cpp` |
| UI sanitizer build with Xvfb + Mesa fixture | `native/tests/ui_regressions.cpp` |
| Sustained mixed classic/controller traffic regression proving classic progress at frame boundaries | bridge loopback suite |
| Successful reconnect end-to-end: backoff growth/reset, drop → re-entry, repeated cleanup | `client_regressions.cpp`, `integration.py` |
| LS2 timeout/cancel cleanup and connection recovery with a fake runtime | `ls2_transport.cpp` |
| Platform action cancellation: queued Home/Cycle invalidation, Stop during in-flight Luna action | `platform.cpp` |
| Incremental ingress budget edge near 256 KiB | `client.cpp`, `client_regressions.cpp` |

The >1 MiB proxy/native parity gap is already closed. None of these may weaken
strict warnings or the fail-closed budgets.

## Phase 3 — measure target, then optimize (owner-gated for hardware)

1. Instrument target latency, CPU, peak ingress, uinput backpressure, GPU text
   allocation and EOF wait. Host numbers are not TV numbers.
2. Only then optimize: priority lanes so key-up/COUT/Stop cannot queue behind a
   slow repeat batch — **must preserve wire order and release semantics**;
   draw-call batching and alpha-text GPU cost; size-opt ARM footprint.
3. Keep the D-013 single-active-session policy; do not pre-build multiplexing
   without a second-client requirement.

## Phase 4 — physical acceptance (owner-gated)

- 0.4.1 controller/media campaign per `native/docs/INPUT-0.4.1.md`: four XInput
  slots, both triggers, analog-to-neutral, hot-unplug, focus exit/re-entry while
  holding a button, disconnect/reconnect, pairing with the pinned certificate.
- Receiver neutralization/watchdog on a real TV: COUT, Stop, 500 ms expiry,
  wrong-capture and stale DGPD rejection, no stuck controls.
- Synergy 4 Dragon gate D0: in an isolated dry run with "Allow Synergy 1
  computers", capture the port-24800 handshake to determine plaintext vs TLS.
  Defensive `DKRP` parsing (`p.size() >= 12`) remains an open follow-up.
- Cold boot, autostart, sleep/wake, soak. Do not treat 0.4.0 pointer acceptance
  as 0.4.1/0.4.2 input acceptance.

## Phase 5 — integration and release engineering

1. Handoff local reference ↔ validated server config via `tools/synergy/`,
   manual apply only; migrate or retire the old Windows helper.
2. Rehearsed bridge rollout: CA → mode-0600 PEMs → private daemon upgrade →
   focus/controller acceptance. Keep LS2 narrow and default-deny.
3. Release engineering: version consistency, immutable candidate, tag/asset
   policy, catalog PR refresh. Keep app ID for upgrade compatibility.

## Standing constraints and decision split

- Do not add clipboard, dock, cloud discovery or a fleet layer before the input
  path is proven. mTLS is owner-scoped and already implemented for the bridge;
  it does not license the other items.
- No PC-side controller suppression without an authorized design and physical
  evidence; raw HID, gyro, touchpad, vendor paddles and rumble are non-goals.
- **Owner decisions:** merge/deploy, physical acceptance schedule, runtime
  staging/repair design, brand/repo rename, legacy `app/` fate, PC-side
  suppression, relaxation of any standing constraint.
- **Agent-actionable:** phases 1–2, drafting phase 3 instrumentation, and all
  host-reversible verification, documentation and handoff receipts.

Sequence: land the stabilization → close cheap truth gaps → close test gaps →
measure the target → prove the input path physically → only then optimize and
integrate further.
