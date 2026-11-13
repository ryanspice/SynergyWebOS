# Native continuation — do not restart the implementation

**Next release scope:** [0.4.1 focused PC input forwarding](../native/docs/INPUT-0.4.1.md).
The 0.4.1 candidate and native PC XInput/mTLS companion are implemented and built;
the task's LEAD-SYNTHESIS and verification reports are in
`.ai/outbox/input-0.4.1-implementation-20261113/`. The ARM IPK is `addebe911815...`;
see that record for host, sanitizer, Windows and ARM gates. Physical acceptance,
broader HID controls and PC-side suppression remain open. The working 0.4.0 TV
installation remains the accepted baseline; no 0.4.1 deployment occurred.

**Current target continuation:** read
[`native/docs/TV-EXECUTION.md`](../native/docs/TV-EXECUTION.md) before treating the
older gates below as current. ARM packaging, jailed UI rendering and narrow LS2
status transport have target evidence. An explicit device rescan made the cursor
visible, confirmed by the owner, and automatic discovery is now target-verified.
The absolute-coordinate candidate IPK `719ff310...` is installed and connected;
the owner confirmed full-screen range and smooth movement after leave/re-entry
in Twitch. Broader input/lifecycle gates remain separate. The owner now
requests Twitch in front for
testing, superseding the earlier Crave/football constraint. No Git publication
or autostart change has been performed in this execution task.

Owner request: rebuild Synergy TV on the same native foundation as Spice Home C++
and SkiFree. This is implemented under `native/`; 0.3.4 source is retained as migration reference, with the dock removed.

## Next bounded task

Build the native candidate with the **existing verified SkiFree/Spice Home ARM32
SDK**, not a new speculative toolchain. Locate actual SDK/runtime paths locally;
none were inferred here. Build a fresh stage, inspect dependencies/symbol ABI,
and only then create an IPK. Do not call the supplied Linux binaries TV output.

Use the user's established `B:\Dev\webos\...` workspace only after inspecting
its real current tree. Do not replace a dirty repository or rename it merely to
match this handoff. Import/review `native/` and the docs deliberately. No automatic
AI Wiki/Svelte Lab ingestion, Git commit/push, install or publication is implied.

The first hardware blocker to resolve is not rendering: it is whether the
application-manager-launched native UI can reach the root-owned Unix control
socket with its actual UID. Record jail visibility, UID and ACL evidence. If it
fails, stop and specify a narrow native LS2 control bridge. Never substitute an
HTTP root API, arbitrary exec method, disabled jail or global permission grant.

## Acceptance gates

- Actual ARM32 ELF/runtime stage + exact IPK checksum/modes, not host syntax tests.
- C3 native UI rendering, D-pad/OK/Back/pointer and fonts with no bundled fonts.
- Explicit provisioning with observed controller UID and shared-UID tradeoff.
- Probe then real Synergy server dry run; confirm the exact desktop Core version.
- Root uinput actually recognized by shell/apps; fail closed otherwise.
- Real key/button releases, Meta and Alt holds, Alt+Tab, cursor drift and wheel.
- Background daemon when UI closes; sleep/wake/reconnect; boot opt-in last.
- Legacy 0.3.4 rollback intact; no simultaneous native/Python input daemon.

Do not add TLS, clipboard, a new global dock, cloud discovery or a generic fleet
management layer before this input path is proven. Do not claim corner handoff
or absolute cursor support that the client does not implement.

## Decisions made in this candidate

Actual homescreen runtime/renderer reuse, not just matching dependency names.
Keep protocol/input separate from UI. Remove Python from the new TV runtime, not
from preserved history or host test tools. Keep privacy-safe bounded telemetry.
Make root provisioning and plaintext input deliberate. Keep active settings
stable until an explicit stop/start. Keep the existing app ID.

## Known implementation follow-ups

The public native IPC is deliberately UID-scoped, not app-authenticated. Target
ABI, subprocess environment, Luna responses and uinput permissions need local
verification. Handoff profile persistence is a new local reference schema; the
old Windows helper has not been migrated. Snapshot section markers alone are
not evidence of full old-helper compatibility. The dock, native Apps page and generic launcher RPCs were removed at owner request. Physical text layout is best
validated against the owner's Windows keyboard layout before widening claims.


## Latest owner instructions (included)

Remove the dock from the bundle. Do not re-add the standalone dock, an Apps page,
companion catalog writing, or arbitrary app-launch commands. Home and Alt+Tab are
input shortcuts only. The UI is Connection / Input / Handoff / Diagnostics.

A researched gear-button settings spike lives in `spikes/gear-settings/`, with a
read-only collector and no automatic system edits. Obtain baseline, gear-panel,
and All Settings captures before proposing a firmware-specific menu adapter.
Preserve ordinary gear behavior; remapping the button is not menu insertion.

README badges are local static SVG snapshot labels. Screenshots are real GLES2
host captures, labeled as such, with no dock and no claim of TV validation.

## Host bug/performance fix pass — 2026-11-11

Owner requested rereview followed by fixes. The five findings from the preserved
`.ai/outbox/audit-1111/REVIEW.md` have been reconciled into native protocol,
subprocess, UI worker and renderer source, with core and actual UI regression
tests. See `native/docs/VERIFICATION.md` for the separately attributed host
results and `.ai/evidence/fix-audit-1111/synthesis-receipt.json` for provenance.
Preimages remain in `.ai/archive/fix-audit-1111/`; do not restore over later work.

Host acceptance is not TV acceptance. Continue with the existing ARM/IPK and
UID/jail/input/Luna/manual-server gates above only with the required separate
authority. UI sanitizer coverage, live shortcut/repeat cleanup, target EOF CPU
and real 4K text performance remain unverified. Publication is not authorized.

## Review findings 2–5 fixed on host — 2026-11-12

Findings 2–5 of `.ai/outbox/tv-cpp-review-1111/REVIEW.md` are fixed in source
with tests; see `native/docs/VERIFICATION.md` (November 12 section) and
`.ai/evidence/tv-cpp-fixes-1112/provenance.json`. **Finding 1 (reserved Stop
path) is still open** and is the next product fix. TV latency/CPU acceptance,
ARM build and target texture limit remain unmeasured.

## Integrated footprint and scheduling implementation - 2026-11-12

The owner requested three Terra 6.1 medium implementation delegates and lead
synthesis. The lead completed the partial returns and adopted all five host
source findings from `.ai/outbox/source-footprint-review-1112/REVIEW.md`.
The previous open-Stop, 16-repeat chunk and 50/100 ms UI polling notes above are
historical; the latest native/docs/VERIFICATION.md section records current behavior.

Reserved Stop cancels stale starts; client work is checked per repeat; packet
timestamps and delayed hold expiry avoid stale Home/Alt. A 256 KiB retained
payload budget plus a strict 4,096-frame cap fail closed on overload, with streamed
discard of ignored bodies. Eventfd/deadline client waits, a shutdown-aware idle
broker and worker-result SDL wakeups remove fixed normal-service polling.
Text uses alpha-only storage/upload. Size-focused Release settings are opt-in.

Stable-source host checks and source/receipt hashes are recorded under
`.ai/evidence/performance-implementation-1112/`; exact dirty preimages are under
`.ai/archive/performance-implementation-1112/`. See the synthesis report for
delegate attribution, numeric host results and implementation limits. Do not
infer ARM/TV acceptance from these checks. Ordered later input can still wait
behind a slow repeat batch; actual uinput/backpressure and release cleanup need
the existing target session. No commit/push/deploy/provisioning was authorized.
