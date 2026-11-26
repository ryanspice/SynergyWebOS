# Verification — native baseline and 0.4.1 candidate

For the current candidate's build, loopback and capability results, read
[0.4.1 verification](INPUT-0.4.1.md). The dated sections below preserve the
scope and limits of their original runs; they are not current missing-feature
claims when a later section supersedes them.

For the later ARM package, actual jailed UI/LS2 execution and input-discovery
evidence, read [the dated TV execution record](TV-EXECUTION.md). The original
host-only sections below retain the limits of their own runs. Cursor visibility,
full-screen range and smooth leave/re-entry are owner-confirmed for the final
0.4.0 Twitch session; broader input and lifecycle acceptance remain separate.

The original import evidence below was produced November 11, 2026, in an isolated
Linux x86_64 authoring environment. That original run contacted no TV, user's
desktop server, local Windows repository or SDK. The later local fix pass is
attributed separately at the end of this file.

## Executed and passed

| Check | Result |
|---|---|
| C++20 Debug and Release host builds | All three executables compile/link with `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror` |
| Native core test executable | 123 assertions plus 2,000 deterministic malformed-packet cases |
| Real daemon + CLI integration | 68 assertions, using real subprocesses, Unix sockets and synthetic TCP server |
| ASan + UBSan | Core and process/TCP/IPC CTest suites pass |
| Native UI | 17 checks using actual SDL events and actual GLES2 framebuffer rendering |
| Resolution checks | 1280×720, 1920×1080 and 3840×2160; 720p/4K pointer-edit mapping |
| UI scenarios | Four panels (no Apps page), D-pad/OK, Back/focus restoration, physical text input, apply/cancel, on-screen keyboard opening, invalid launch/font/view rejection |
| Host→TV false-claim gates | CMake rejects `SYNERGY_WEBOS=ON` with a host compiler; stage audit rejects x86_64 binaries |
| TV-only conditional branches | Strict HOST `-fsyntax-only` check; explicitly not an ARM or hardware test |
| Source-bundle regressions | Eight tests: dock removed, launch wiring removed, no obsolete Apps captures, native identity, current docs links, no fonts, no probe installed into the TV stage |
| Gear-settings spike | Eleven host fixture tests; Python collector and shell syntax, no TV connection or menu change |
| Scripts | Bash/POSIX shell syntax checked; stage auditor executed against a host stage and rejected it |

The integration suite covers fragmented hello packets separated by multiple poll
intervals, screen negotiation, active input, leave cleanup, keepalive ordering,
TLS/oversized-frame refusal, idle timeout, once-on-failure, stop during reconnect,
active-versus-saved configuration, private persistence, duplicate daemon lock,
malformed/stalled IPC, another UID rejected by kernel peer credentials, native
profile persistence, and preservation of malformed existing configuration.

The delivered Linux preview binaries are the strict Release build used by the
release CTest/UI runs. Host runtime libraries/fonts are not bundled. Rendering
used Mesa llvmpipe with an OpenGL ES 2 context. This verifies real native code,
not a browser mockup or an illustration, and does not measure TV performance.

Fresh logs are in `native/.ai/evidence/`; screenshots/reports are in
`native/verification/ui/`. Do not confuse retained 0.3.4 historical records with
these native results. Use the package checksum manifest to tie evidence/source
and preview binaries to this exact handoff.

## Not performed / not established

**No ARM32 target build, installable IPK, actual LG TV launch, uinput injection,
root provisioning, Luna action, native-jail socket access, physical Synergy 3
server connection, sleep/wake or boot persistence test was performed.**

The compiler/sysroot and target runtime used for SkiFree/Spice Home are not
available in this environment; no replacement was guessed. Container networking
could not resolve package downloads. The source includes a guarded build/stage
route for the user's existing verified environment rather than fake TV output.

No PowerShell runtime was available, so the PowerShell WSL wrapper is supplied
but unexecuted. The root provisioner and boot hooks were syntax-checked only;
they were not run. Stage dependency availability and symbol-version compatibility
require the exact SDK/TV. The Unix broker authenticates a UID, not an app identity;
shared app UIDs and jail visibility need explicit owner-reviewed target evidence.

Clipboard, TLS, full international key mapping, Caps/Num/Scroll lock-state
inheritance, true absolute pointer positioning are not implemented. The dock, companion launcher wiring and native Apps page
were deliberately removed at owner request. Existing Windows helper interoperability is not proven merely
by preserving snapshot section markers.


## Final owner-request changes

The standalone dock directory, native Apps page, companion launch/catalog-write
wiring, and generic launcher control RPCs are absent. The integration suite now
also rejects the removed `apps`, `launch` and `home` RPCs; Home and Alt+Tab remain
protocol-driven input shortcuts, not manually exposed launcher APIs.

The gear-menu spike is researched/documented and includes a read-only collector;
no private UI insertion point, key remap or menu patch was run on the TV. Its
fixture tests are distinct from hardware evidence. README screenshots were
recaptured after dock removal from the real host UI with an isolated idle daemon.

## Local rereview and fixes — November 11, 2026

Executed against the Windows checkout at baseline HEAD `92dba7c`, using the
existing `WebOSBuild` WSL Linux x86_64 environment. All five audit findings were
reconfirmed before fixes. The original report and defect-reproducing binaries
are preserved; a probe's successful reproduction is not a correctness pass.

Accepted host fixes:

- A Home-consumed Alt hold stays uninjected until physical release, inherited
  mask clearance or capture cleanup. Synthetic provenance is retained so mask
  clearance rearms the next hold; replacing an inherited Alt with its physical
  peer cannot restart a consumed hold.
- `DKRP` emits the full wire count (0–65,535), using a non-wrapping bounded loop.
  Inactive, released and consumed shortcut keys still do not repeat.
- The subprocess poll disables its pipe descriptor after EOF, retaining exit
  status, output bounds and the deadline without persistent `POLLHUP` busy-spin.
- Width/ellipsis layout has a separate, bounded scalar cache. Only drawn text
  gets string bitmaps/textures; measurement and drawing share hinted glyph
  bounds, kerning and padding. Measurement still uses FreeType glyph rendering
  on cache misses, but does not upload candidate-string textures.
- Save + Connect owns configure and conditional start as one worker FIFO item.
  Later saves and queue rejection cannot replace or cancel that item's intent.
  Configure failure prevents start; start failure is reported without retry.
  This orders one UI worker, not transactions from unrelated IPC clients.

Fresh strict Release and strict Debug ASan/UBSan core/daemon/CLI builds passed.
Core: **148 assertions plus 2,000 malformed-packet cases** in both builds.
Actual daemon/CLI synthetic TCP/IPC: **70 assertions** in both builds.
Release UI: **50 new regression assertions** plus **17 existing SDL/GLES UI flow
checks**, including 720p/1080p/4K. Source bundle: **8 tests**; gear-settings:
**11 fixture tests**. UI sanitizers were not run (sanitizer build has UI disabled).

The Release EOF probe used about 0.001 seconds of parent CPU over a 0.4-second
child wait, versus about 0.4 seconds before. Repeated unchanged 4K edit frames
uploaded **zero textures / zero bytes**, versus **101 uploads / ~14.6 MB** before.
Draw timings used host Mesa llvmpipe and are not TV measurements or a frame-rate
guarantee. Initial/cold frames still lay out glyphs and upload drawn text.

Evidence and exact commands: repository-root `.ai/evidence/fix-audit-1111/`.
Core/integration fixture paths were changed only in preserved test copies;
POSIX socket/private-mode and Xvfb fixtures required isolated synthetic `/run`
storage because WSL drvfs lacks those facilities. Removal receipts were verified.
No real server, TV, uinput, Luna, ARM/IPK, jail/UID, boot or sleep/wake acceptance
was run. No provisioning, service restart, deployment, commit or push occurred.

## Review findings 2–5 fix pass — November 12, 2026

Owner requested fixes for findings 2, 3, 4 and 5 of
`.ai/outbox/tv-cpp-review-1111/REVIEW.md`. **Finding 1 (Stop has no reserved
control capacity/priority) is not fixed and remains open.** Same `WebOSBuild`
WSL x86_64 environment; host only.

- **2 — repeat budget.** In client mode `Session` defers a `DKRP` batch;
  `Client` delivers it in chunks of 16 within a 4 ms slice, then checks Stop,
  ticks shortcut timers and reads the socket. Later packets wait in a bounded
  (4,096-frame) FIFO behind the batch, so key-up/COUT order is unchanged; TCP
  flow control applies when it is full. Release/COUT/leave discards undelivered
  repeats. Direct `Session` use keeps synchronous delivery. uinput event+SYN
  are now one `write` (whole-record partial writes resumed; 100 ms
  backpressure deadline kept).
- **3 — texture limit.** Layout extents are no longer clamped. Textures are
  still bounded by `GL_MAX_TEXTURE_SIZE` and drawn at their stored width
  (clipped, not stretched); `width()` reports the full extent and `ellipsis()`
  requires a result to fit both the field and one texture.
- **4 — idle redraw.** The UI redraws only after input that changes state,
  window events (expose/resize/show/focus), or worker results that differ from
  what is shown (identical periodic status is ignored). Otherwise it waits in
  `SDL_WaitEventTimeout` (50 ms visible, 100 ms hidden). `--frames` capture/QA
  runs still render every frame. Draw-call batching was not attempted.
- **5 — cold ellipsis.** Glyph bounds/advances are cached per (pixel size, code
  point) from the same hinted FreeType render drawing uses; ellipsis lays a
  string out once and evaluates every prefix + `...` from prefix extents and
  kerning. Ellipsis results are cached (bounded, 384).

| Check | Result |
|---|---|
| Strict Release + Debug ASan/UBSan core | 155 assertions (7 new) + 2,000 malformed cases |
| Actual daemon/CLI integration (both builds) | 70 assertions |
| UI regressions (Release) | 75 assertions (25 new: texture limits 2,048/512, glyph-load counts, ellipsis == previous algorithm over 126 cases × 3 densities, redraw gating) |
| UI flow / bundle / gear fixtures | 17 / 8 / 11 passed |
| Review probe, slow fake Input, Stop after repeat 10 of 256 | Stop join 268 ms → 7.6 ms; 16 more repeats delivered (was all 256) |
| Review probe, 64-W name, 4K, cold | glyph loads for width 64 → 1, ellipsis 1,332 → 1; 2,048 limit now truncates (43 bytes + `...`) |
| Cold Connection Deck frame | 2,699 → 836 glyph loads |
| Visible-idle UI, 6 s, daemon offline | 19.9 s → 0.26 s user CPU (llvmpipe) |

Numbers are host Mesa llvmpipe/synthetic-fixture measurements, not TV
performance. UI sanitizers remain unrun. No ARM build, TV, server, uinput, Luna,
provisioning, commit or push. Evidence and commands:
`.ai/evidence/tv-cpp-fixes-1112/`.

## Integrated source-footprint fixes - November 12, 2026

This section supersedes the earlier open-Stop and 16-repeat/periodic-wait
descriptions above. The owner requested three GPT-6.1 Terra medium delegates,
then lead integration. The UI delegate returned no changes; the client delegate
changed repeat pumping to one report; the renderer delegate supplied the alpha
path without completed verification. The lead completed the remaining source,
regression coverage and combined checks. Delegate reports remain preserved in
`.ai/outbox/performance-implementation-1112/`; partial reports are not acceptance.

Adopted source behavior:

- Stop has reserved/coalesced control capacity, runs before unstarted ordinary
  jobs, cancels queued connection starts and invalidates an in-flight configure
  continuation. A transport request already in flight finishes before Stop is
  dispatched. A later explicit Connect remains valid. Bounded result overflow
  retains status/Stop results so status refresh cannot become stuck pending.
- Client repeat work checks cancellation and its 4 ms slice between individual
  reports. One report already in flight and subsequent release cleanup remain
  separately bounded by the input backend. Normal repeat counts and wire order
  are preserved. Received packet timestamps drive ordered state; wall-clock hold
  expiry waits until repeats, queued packets and any partial frame are drained.
  This prevents stale Home/Alt intents after a queued release. Later packets do
  not overtake a slow repeat batch, so this is not a universal key-up latency
  guarantee; sustained backpressure still needs target testing.
- Retained decoder/backlog payload vector capacities share a **256 KiB** budget.
  There are at most **4,096 queued retained frames**, checked before insertion.
  Fixed receive storage is another 16 KiB, the framing prefix is eight bytes,
  and queue/allocator metadata is additional. A budget violation disconnects
  explicitly and releases held input. Large otherwise-valid nonignored frames
  that exceed the smaller retention budget are rejected deliberately.
- Ignored clipboard/screen-power bodies are streamed through the fixed receive
  buffer without heap retention. First-frame handshake, TLS, maximum wire length
  and partial-frame deadline checks remain. Status exposes the numeric
  `client.peak_ingress_bytes` for this Start interval; no input content is logged.
- Client connect/send/read/backoff waits use readiness, deadlines and an eventfd
  cancellation wakeup. Normal daemon control waits block on requests plus a
  signal-safe shutdown pipe. Diagnostic daemon `--once` retains its 25 ms worker
  completion check. The UI receives worker-result SDL events, waits until useful
  visible status deadlines and pauses hidden status refresh. Only a swap interval
  of zero uses the extra 16 ms pacing fallback. Identical offline errors do not
  force a redraw.
- Text stores and uploads one coverage byte per texel with GLES2 ALPHA. Color
  textures and the white texture remain RGBA, and the cache counts the selected
  format. CPU bitmap/upload bytes are reduced; physical TV GPU allocation has
  not been measured. `SYNERGY_OPTIMIZE_SIZE=ON` offers checked opt-in Release
  IPO/size/section removal; it is not a claim of better target latency.

Stable-source strict host checks passed in ordinary Release, size-optimized
Release, and Debug ASan/UBSan core/daemon/CLI configurations:

| Check | Result |
|---|---|
| Core in all three configurations | 162 assertions plus 2,000 malformed-packet cases |
| Actual daemon/CLI integration in all three | 72 assertions, including idle broker waits and SIGINT/SIGTERM shutdown |
| Actual Client/Session in all three | 8 regression modes: slow Stop, ignored-MiB memory, Meta, Alt, ordered release/COUT, idle waits, frame overflow and byte overflow |
| Both Release UI configurations | 110 assertions, including saturated Stop/cancelled starts, result wakeup, hidden refresh, bounded results, text limits and alpha-versus-RGBA pixel equivalence at 720p/1080p/4K |
| Both Release UI flows / gear fixtures | 17 / 11 passed per configuration |
| Source bundle / repository guards / docs links | 8 tests / no violations / no broken links |

The ordinary Release synthetic 20 ms-per-report Stop probe now completed after
about 20 ms and one repeat, versus about 321 ms and 16 repeats in the preserved
review. Eight ignored 1 MiB frames added about 0.23 MiB RSS in the Release
fixture, with a measured retained-payload peak of 14 bytes. Idle client reads
used four readiness waits including setup over the 2.2-second window, versus 113
20 ms polls in the review. These are host/synthetic observations, not TV numbers.

The first sanitizer RSS threshold failed because ASan process RSS includes
instrumentation/allocator overhead. The test now enforces the exact positive
ingress-capacity bound in every configuration and the RSS bound in non-ASan
builds. No sanitizer report was suppressed. Earlier compiler/test-drift failures
and the failed RSS attempt remain in the evidence logs; final runs have stable
source hashes. UI sanitizers remain unrun.

Full commands, hashes, copied test-path exceptions and verified ephemeral `/run`
cleanup receipts are in `.ai/evidence/performance-implementation-1112/`.
Recovery copies preserve the complete pre-existing dirty bytes under
`.ai/archive/performance-implementation-1112/`; restore only after matching the
current destination to an expected hash. No ARM/IPK, real server, TV/uinput/Luna,
UID/jail, provisioning, boot, sleep/wake or human acceptance was performed.
No commit, push, deployment or publication occurred.

## Focused protocol/input/UI/TLS re-review - November 23, 2026

A read-only re-review of `native/src/protocol.cpp`, `input.cpp`, `ui.cpp` and
`tls.cpp`, their headers, and the related client/ipc/daemon/platform/config/
render paths checked the current source against the November 11 and November 12
audit rounds. **All ten prior findings remain fixed and no new correctness,
security or resource-exhaustion defect was found.** The older HANDOFF note that
reserved Stop capacity was open is historical: the current `ControlWorker`
implements reserved Stop priority, coalescing, queued-start cancellation and
epoch-based in-flight start invalidation.

That review was static source inspection only - no build, test or runtime probe
was run in it, and no TV or target hardware was involved.

### Low observations (not defects)

- `FrameDecoder::feed()` (`protocol.cpp:62`) uses a 2 MiB budget as a
  synchronous test convenience; the Client's `consume()` (`client.cpp:248`)
  uses 256 KiB. Future `feed()` callers should know the larger budget.
- Unknown Synergy commands disconnect (`protocol.cpp:644`); this fail-closed
  behavior is intentional.
- UI text input is ASCII-only (`ui.cpp:443`); a known limitation.
- UI status comparison uses JSON dump string equality (`ui.cpp:268`); the
  daemon emits deterministic JSON, so this can only cause a redundant redraw.

### Verification gaps (recorded, not defects)

| Gap | Scope | Status |
|---|---|---|
| G1: no automated TLS test for certificate load/chain verification, hostname/IP matching, SNI, handshake, and WANT_READ/WANT_WRITE | `native/src/tls.cpp` | OPEN - follow-up gate |
| G2: UI sanitizer build not run (sanitizer config disables UI) | `native/tests/ui_regressions.cpp` | OPEN - needs Xvfb + Mesa fixture |
| G3: target EOF CPU unmeasured (host ~0.001 s CPU over a 0.4 s wait) | `platform.cpp` | PENDING target |
| G4: 4K text GPU cost unmeasured; host pixel equivalence passed | `render.cpp` | PENDING target |
| G5: no explicit test of incremental ingress budget near 256 KiB | `client.cpp`, `client_regressions.cpp` | OPEN - low risk |

Host conclusions come from source inspection plus the existing host evidence;
target GPU, scheduler and physical-input behavior remain speculative until
measured. The review thread is settled. These gaps are follow-up work, not
blockers for the v0.4.2 bridge stabilization.

## Production readiness / Synergy 4 Dragon review - November 23, 2026

A read-only production-readiness review covered `native/`, `desktop/input-bridge/`
and the reverse-engineered Synergy 4 Dragon core (`.ai` research note). Its
verdict: the 0.4.0 pointer baseline is production verified on hardware; the
0.4.1 media/controller candidate is host-verified with physical TV acceptance
still pending; the desktop bridge was flagged on a classic-traffic starvation
risk; Dragon compatibility is conditional on its "Allow Synergy 1 computers"
mode.

### Applied in this stabilization pass

- **Classic-traffic starvation (bridge).** `main.cpp` selected `Write::Pad`
  whenever the controller queue was non-empty, ahead of `Write::Classic`.
  Added a bounded `PadClassicArbiter` (at most four consecutive controller
  writes before a classic write at a frame boundary) with host unit coverage.
  An in-flight TLS write is never preempted. A loopback probe with continuous
  analog changes and a small receive window showed no regression; the claimed
  sustained starvation could not be reproduced in that short probe because
  8 ms sampling produces pads far below the TLS drain rate.
- **Bridge/native frame-size mismatch.** The bridge accepts 4 MiB frames but
  `FrameDecoder::MaxFrame` was still 1 MiB, so the 1.5 MiB `DCLP` body that the
  proxy conformance test forwards would disconnect the native receiver before
  ignored-body streaming. Raised the native limit to 4 MiB (retained commands
  still obey the 256 KiB caller budget, ignored bodies stream without
  allocation) and added a native test proving the 1.5 MiB ignored body streams
  with zero retained bytes. Updated the outbound-limit test to derive from
  `MaxFrame` instead of a hard-coded 1 MiB.

### Recorded follow-ups (outside this bridge PR)

| Item | Location | Status |
|---|---|---|
| `DKRP` strict 12-byte check may reject Dragon's optional language suffix | `native/src/protocol.cpp:631` | OPEN - allow `p.size() >= 12`, parse first 12 bytes |
| TLS on port 24800: Dragon Core defaults to TLS, client fails closed | `native/src/protocol.cpp:74` | GATED - verify Dragon compatibility mode emits plaintext Synergy 1.4 |
| UI text claimed "No TLS or clipboard" and no absolute positioning, both implemented | `native/src/i18n.cpp` `input.warning` / `input.note` | RESOLVED - both locales now state that clipboard is unsupported, the optional mTLS bridge carries controller input, and absolute positioning is implemented |
| Hardcoded `libim.so.1.0.0` soname may change on newer webOS | `native/src/input.cpp:27` | OPEN - add soname fallback list |
| Gamepad sharing is not part of Dragon Core port 24800 | desktop companion | BY DESIGN - custom mTLS bridge remains required |
| Cluster mDNS/gRPC discovery | Dragon 24801/5353 | OUT OF SCOPE - manual configuration retained |

These are compatibility and documentation follow-ups, not defects in the
current 0.4.0/0.4.1 code paths. No Dragon binary, real Synergy server, TV or
controller was contacted; the Dragon analysis is static/reverse-engineered
evidence, not an interoperability test.

## Focused transport/runtime review - November 23, 2026

A read-only review of `native/src/client.cpp`, `daemon.cpp`, `ipc.cpp`,
`platform.cpp` and `ls2_transport.cpp` (plus their headers) found **no new
actionable findings**. The current `client.cpp` hash matches the independently
reviewed 2026-11-21 version; its small delta is the already-reviewed descriptor
ownership and shared errno-classification refactor. No earlier fix regressed:
cancellable reconnect/backoff and eventfd waits, handshake/idle/partial-frame
and gamepad deadlines, ordered repeat pumping with bounded ingress, the
shutdown-aware daemon idle wait, UID admission before request-body reads, and
LS2 request/reply bounds with deadline and cancellation cleanup all hold in
source.

Fresh host verification in that review: strict Release build (GNU 13.3.0, UI
disabled) passed; all 8/8 Client regression modes passed (`slice`, `memory`,
`timer`, `alt`, `release`, `idle`, `overflow`, `byte-limit`); a protected
core-test copy passed 300 assertions before a DrvFS runtime-path fixture
restriction stopped the run. The fresh mTLS suite was blocked by fixture-path
limits (long path exceeded the runtime limit; the shorter project-local path
could not create Unix sockets) - a fixture-environment failure, not a
transport assertion failure. Earlier records of 72 process/TCP/IPC assertions
and 64 mTLS checks were retained rather than rerun.

### Test gaps / target-unverified risk (recorded, not defects)

| Gap | Scope | Status |
|---|---|---|
| Successful reconnect end-to-end: backoff growth/reset, active-session drop then re-entry, repeated cleanup, sleep/wake | `client.cpp`, `client_recovery.py` | CLOSED - `native-recovery-*` covers initial failure, transport loss, repeat-batch interruption, mixed traffic, fresh CINN, slot resynchronization, repeated cycles, stop in every phase and the backoff contract (Batch 4b) |
| LS2 lifecycle: timeout/cancel cleanup, connection recovery, helper/bus restart | `ls2_transport.cpp` | OPEN - no fake-runtime host coverage; target evidence covers schema exchange and caller denial |
| Platform action cancellation: queued Home/Cycle invalidation, Stop during in-flight Luna action | `platform.cpp`, `core_tests.cpp` | OPEN - source-inspected only |
| Target UID/jail admission matrix, physical Start/Stop lifecycle, real Home/Cycle effects | target runtime | PENDING target |

Host conclusions come from source inspection plus the fresh host runs above;
target UID/jail, Luna and reconnect behavior remain unverified. The review
thread is settled. These are follow-up work, not blockers for the v0.4.2
bridge stabilization.

## Focused desktop-bridge review disposition - November 22, 2026

A focused review of `desktop/input-bridge/main.cpp`, `bridge_wire.hpp` and
`tests/bridge_wire_test.cpp` at commit `0d6ebf3` produced six findings. Their
current disposition:

| Review claim | Disposition |
|---|---|
| Framer invalid-length test uses a 1 MiB literal | Not reproduced - the test uses `MaxFrameLength + 1` both at `0d6ebf3` and currently |
| `0xac1` is a malformed IPv4 literal | Not reproduced - it is the top 12 bits of `172.16.0.0/12` (verified numerically); an explanatory comment was added |
| No test for focus loss during a backpressured TLS write | Valid - added `test_inflight_focus_loss()`; the full `WANT_WRITE` path is covered by a local forced-slow-reader probe |
| `mark_sent` missing in the coalescing test | Not reproduced in the current test |
| No Windows listener test | Valid - an MSVC x64 CI job (VS 2022, strict warnings) now builds and tests the bridge |
| Header comments claim platform headers | Verified the shared gamepad header is pure standard C++; comments clarified |

No product defect from that review remained unaddressed. The review thread is
settled; these dispositions are recorded here for traceability.

## Batch 4a — sustained bidirectional fairness recovery - November 24, 2026

The sustained mixed-traffic regression (`desktop/input-bridge/tests/proxy_sustained.py`)
reproduced a production-path starvation defect against baseline `c7bfdef`: the
actual proxy delivered all 512 classic frames and relayed all 96 upstream frames
but delivered only the four initial controller frames (zero during the flood)
despite thousands of successful fixture updates. The arbiter required
`delivered.boundary()`, while classic TLS writes consumed arbitrary receive
chunks that rarely ended on a frame boundary, so sustained classic traffic kept
the delivered framer permanently mid-frame and controller arbitration never
became eligible.

Fix (commit `fix(input-bridge): arbitrate controller writes at classic frame
boundaries`):

- `bridge_wire.hpp`: added the pure, bounded `classic_write_to_boundary()` helper.
  It copies the incremental framer, advances it over the queued bytes, and
  returns the length up to the next legal frame boundary. Partial length
  prefixes, payloads split across chunks, several frames in one chunk, and frames
  larger than the queue budget are all handled without retaining a payload.
- `main.cpp`: the classic write now uses the frozen boundary length instead of
  the whole receive chunk, so `delivered.boundary()` becomes a reachable
  scheduling point and the arbiter grants the controller a bounded turn. The
  length is frozen across OpenSSL WANT_READ/WANT_WRITE retries and checked. The TV
  socket send buffer is requested at `SO_SNDBUF` 16 KiB (best effort; the
  effective size is read back and logged) so a slow reader applies real TCP
  backpressure instead of an unbounded kernel backlog.
- `bridge_wire_test.cpp`: added `test_classic_write_to_boundary()` covering
  whole frames, multiple frames per chunk, split prefixes, split payloads,
  non-divisible sizes, a 4 MiB streamed frame, and malformed lengths.

Verification (WSL `WebOSBuild`, GCC 13.3, OpenSSL 3.0.13, real loopback TLS):

- Strict Release native build + CTest: **15/15** passed.
- ASan/UBSan native build + CTest: **14/14** passed (the separate 1,032-byte
  `libdbus` leak reproduces on the untouched baseline).
- `bridge-wire-test`: **349** checks passed (was 78).
- `proxy_loopback.py`: **95** checks passed.
- `proxy_sustained.py` (production proxy, real authenticated TLS, synthetic Core
  and four-slot fixtures): **PASS 5/5** before/after; 140 controller frames, 34
  classic/controller alternations, max controller burst 4, max classic progress
  gap ~0.22 s. ASan build: **PASS** (184 pads).
- Regression validity: the old whole-chunk classic write fails the same
  regression (4 pads, 0 alternations) even with the bounded buffer, and the
  baseline fails at the contention assertion; the corrected implementation
  passes. Mutation checks confirm both the frame-boundary helper and the bounded
  buffer are necessary.

Remaining limitations: host loopback timings are observations, not a target
latency bound; no physical TV, controller, live Synergy server, driver or
installed service was exercised; PC-side controller suppression and physical
acceptance remain separate gates. Batch 4b (reconnect) is not started.

### Batch 4a acceptance closeout - November 24, 2026

PR #20 was held for a small, focused closeout before merge readiness:

- **CI registration.** `proxy_sustained.py` now runs in the Linux `bridge` CI
  job. The step is bounded twice: an outer `timeout --signal=TERM
  --kill-after=15s 180s`, and the fixture's own 90 s overall `SIGALRM`
  deadline. Output is streamed through `tee` to a preserved log and the JSON
  verdict is re-printed on failure, so a red run stays diagnosable. The job
  timeout rose from 15 to 20 minutes.
- **`SO_SNDBUF` is best effort.** The request result is now checked, the
  effective value is read back with `getsockopt` and logged, and a refused
  request is a hardening loss rather than a session failure. Linux reports
  32,768 for a 16,384 request (kernels double it), which is why documentation no
  longer calls 16 KiB a guaranteed limit.
- **Frozen retry invariant is now verified, not assumed.** The selected classic
  address, length and bytes are retained; a byte copy bounded by one receive
  chunk is compared on every retry and a mismatch fails with `TLS`. The harness
  proves the path ran live (31 classic WANT retries in the accepted run) while
  new classic data kept queuing behind the frozen write (peak queue 65,536
  bytes, the exact budget).
- **Fixture boundedness.** The overall `SIGALRM` deadline interrupts the main
  thread even inside a blocking socket call, `finally` cleanup still runs, and
  every helper thread is joined through a bounded, asserted helper instead of a
  bare `join`.
- **Cost and write-call observations.** The proxy reports per-session counters
  (`Relay stats: classic_writes= controller_writes= tls_retries=
  classic_write_retries= peak_queue_bytes=`), and the fixture records proxy CPU
  seconds and classic wire bytes/second. Arbitration semantics are unchanged.
- **RED result preserved.** The whole-chunk-writer control still fails the
  regression with 4 pads and 0 alternations, even with the bounded buffer;
  evidence stays in `.ai/evidence/input-bridge-sustained-fairness/`.

Accepted run (GCC 13.3, OpenSSL 3.0.13, release build, WSL host): `PASS`,
512/512 classic frames, 144+ controller frames, 35 alternations, max controller
burst 4, max classic gap 0.22 s, proxy CPU 0.12-0.25 s over a ~13 s contention
phase (~40 KB/s), clean teardown and no stale frames. CI is the authority for the
final verdict; these host numbers remain observations.

## Batch 4b - session recovery and reconnection - November 25, 2026

Per-connection state was already function-local in `Client::session()`, so a
reconnect starts from clean decoder, repeat, sample and platform generations.
What was wrong was the reset rule: `Client::loop` compared the *total attempt
duration* against 10 s, so a ready session's own runtime was mixed with its
setup cost. The rule is now `Attempt::ready_at` plus the fixed 10 000 ms
`StabilityWindowMs`: only sustained protocol readiness resets the 1 s delay,
while a slow failed connect or handshake keeps the progression. `--once`, the
eventfd cancellation, `Restart` and saved/active configuration semantics are
unchanged, and no global sequence counter was introduced.

The window is deliberately **outside the configuration contract**. `Client` reads
`SYNERGY_STABILITY_WINDOW_MS` once per run (digits only, 200..60000, ignored when
absent or invalid) so fixtures can accelerate it, while `Config`, `json()` and
`from_json()` are untouched and every saved file is byte-identical to the
baseline.

`state`, `error`, `connection_attempts` and `next_retry_ms` are published through
one lock and read under the same lock in `snapshot()`, repairing the failure edge
where a state could be observed before its retry delay.

`native/tests/client_recovery.py` (22 CTest cases, real daemon, synthetic mTLS
server, dry-run input) covers initial-failure recovery, unfocused readiness,
transport loss during input, an interrupted deferred repeat batch, an
interrupted mixed classic/controller burst, reconnect plus fresh CINN, four-slot
resynchronization, five repeated cycles with fd/thread/RSS bounds, stop during
connect/TLS/handshake/backoff, the backoff contract (duration versus readiness,
unfocused stability, setup-only time, the 30 s cap), status-snapshot coherence
and stale/duplicate/wrong-capture rejection.

Baseline `8379be9` fails the production-timed `backoff-duration-over-window`
(1.00 s instead of 2.00 s) and the accelerated readiness cases, and passes the
controls; the fixed build passes 34/34 Release and 33/33 ASan/UBSan with leak
detection. Recovery stays host-only: sleep/wake, DAC restart, physical
controller and live server behavior remain unverified.

