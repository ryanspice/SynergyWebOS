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
