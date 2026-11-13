# 0.4.1 — focused PC input forwarding

Status: 0.4.1 candidate implemented, reviewed and built, 2026-11-13. Host,
sanitizer, ARM/package, Windows build and synthetic producer/receiver gates have
passed. Pairing setup, physical input and TV acceptance remain pending. The
working 0.4.0 installation remains the accepted baseline in `TV-EXECUTION.md`.

Terra implemented the semantic media/browser mappings, F13–F24 and keypad
coverage, toggle-repeat policy and unknown-button acknowledgement. The lead
integrated an optional mutually authenticated TLS connection, focus/sequence/
freshness validation, watchdog neutralization and separate lazy uinput gamepads.
The desktop producer uses XInput for up to four compatible controllers. See
`INPUT-BRIDGE-PROTOCOL.md`; source presence does not establish physical support.

## Candidate capabilities and evidence

The candidate maps verified media/browser controls, F13–F24 and keypad controls,
preserves volume repeats and suppresses media-toggle repeats. Mouse buttons 1–5
remain supported; additional unassigned IDs are acknowledged and ignored. This
does not implement arbitrary extra mouse-button or USB HID tunnelling.

The native PC companion and separate TV gamepad backend implement focus-gated
XInput state for up to four compatible controllers over mutually authenticated
TLS. They do not implement PC-side controller hiding/suppression, non-XInput raw
HID, gyro/touchpad/vendor controls, or rumble return. The owner goal is broader
than this initial capture API; these gaps must not be labelled as complete
“all input” support.

Release and ASan/UBSan native core passed 308 assertions plus 2,000 malformed
cases; IPC passed 72 and mTLS integration 64 checks. Eight scheduling modes,
111 UI assertions, eight source-bundle tests and seven stage-auditor fixtures
passed. The strict ARM32 Release build and exact 18-file payload/mode audit passed.
The 0.4.1 IPK SHA-256 is
`addebe911815ec9916675592448d10529986eb27ed3f94b3dc129294e6a92f65`.

The Linux proxy passed 81 loopback conformance checks; the real daemon/proxy/
synthetic-Core chain passed 14 smoke checks. Windows x64 compilation and binary
metadata checks passed; the release bundle contains executable, matching TLS
DLLs and licenses, with no pairing material. Physical Windows controller capture
and physical TV gamepad/media behavior remain untested.

Reports and artifacts are grouped under
`.ai/outbox/input-0.4.1-implementation-20261113/` and its evidence lane. Static
OpenSSL imports match the supplied TV libraries; full transitive system-library
closure is explicitly partial. No 0.4.1 install or publication occurred.

## Owner's contract

Route input from the computer to the TV whenever Synergy has captured focus for
the TV. This includes keyboard keys and consumer/media controls, mouse controls,
and PC-connected gamepads/controllers. It is not a request to use a TV-connected
controller to navigate the management app.

Synergy screen ownership is the focus authority; the currently foreground TV app
does not determine whether the computer owns input. On TV entry, synchronize the
current input state. On leave, Stop, disconnect or error, release held keys and
buttons, neutralize controller sticks/triggers/hats, cancel pending repeats and
discard stale events from the previous focus generation. TV input must not keep
receiving background computer activity after focus returns to the PC.

The intended behavior is exclusive routing while captured. A forwarding path
alone does not demonstrate that desktop applications have stopped consuming a
controller. That suppression behavior must be designed and tested explicitly.

## Historical planning scope (before implementation)

The pre-implementation classic parser handled keyboard down/up/repeat, mouse motion,
buttons and wheels. Its keyboard device declares Linux keycodes 1–255, but the
translator implements only a subset; a declared capability is not a mapping.
Mouse wire buttons 1–5 already map to left/right/middle/side/extra. The source
does not accept indices above five and has no gamepad device or packet path.

1. **Keyboard and consumer controls.** Add protocol-verified semantic mappings
   for volume up/down, mute, play/pause, stop, next/previous and browser controls;
   audit extended function, navigation and keypad keys. Verify repeat policy:
   held volume keys may repeat, while a toggle must not fire repeatedly by
   accident. Expand the backend capability set only for explicit, verified
   mappings; the current 255-code bound is an implementation limit to revisit,
   not a promise to reject every legitimate consumer code above it. Diagnostics
   should identify unmapped input without retaining typed text.
2. **Mouse controls.** Validate existing side/extra buttons on the TV and define
   their intended actions, such as Back/Forward, where supported. Add further
   buttons and wheel/tilt controls only after their desktop wire IDs are known.
   Preserve malformed-packet checks; well-formed but unassigned controls need an
   explicit policy rather than an accidental connection teardown. Keep absolute
   position synchronization, full-screen range and smooth handoff from 0.4.0.
3. **Actual controller forwarding.** Capture PC gamepad buttons, D-pad, sticks
   and triggers and deliver them to a separate virtual gamepad on the TV. Do not
   advertise arrow-key emulation as complete controller support. Identify the
   desktop capture API, the available protocol/transport and focus integration
   first. The current TV parser cannot manufacture controller state that the
   desktop does not send. Additional device-specific controls require explicit
   capabilities and mappings, not guessed codes.

## Desktop/transport gate

First establish which events the installed desktop can capture and send, with
which IDs and state semantics. Upstream's published features describe keyboard
and mouse sharing; that alone does not prove whether every other device is
supported or unsupported. Direct retrieval of the indexed upstream protocol
reference/header URLs returned 404 during this planning pass. The search index
is not a complete, version-matched controller wire contract.

If the existing desktop path does not carry gamepad state, include the required
desktop capture/transport component in the implementation scope. It must share
the same authoritative focus/session lifetime, authenticate its peer, use bounded
messages and queues, reject stale/replayed generations, and reserve cleanup/Stop
capacity. Analog samples may be coalesced within a generation; button transitions
and release/neutral state must survive load. Do not add a generic network root API
or per-motion LS2 calls. Keep the TV runtime native and event-driven.

“All input” is the routing goal for supported, captured input events; it is not a
claim of arbitrary USB-device tunnelling. Any control the desktop does not expose
must be shown as a capability gap rather than silently reported as working.

## Implementation order and acceptance

The bridge is explicit opt-in configuration. `input_bridge` defaults to false.
Enabling it requires absolute normalized `bridge_ca_file`, `bridge_cert_file`,
`bridge_key_file` paths plus the expected `bridge_server_name`. On the TV, these
PEM files must be regular, owned by the daemon UID and mode 0600; they are loaded
from checked descriptors. Certificate identity and peer trust are verified before
the input backend is created. Pairing keys are not part of the IPK. A GUI-only
upgrade is insufficient: the private daemon must also be upgraded before using
the new configuration. The existing LS2 endpoint/permissions do not widen.

Direct classic server operation is retained. The bridge adds TLS on the
TV-to-proxy leg; it does not implement proprietary Synergy TLS negotiation.
Operator pairing/setup and PC-side controller suppression are separate gates.

Start with desktop input/capability evidence, then keyboard/media mappings and
mouse controls, followed by the controller capture/transport and TV gamepad path.
Keep the currently accepted 0.4.0 package and private runtime as the recovery
baseline. Set package/CMake version 0.4.1 with the assembled release candidate.

Tests must cover exact mappings, down/up/repeat, duplicate/unknown controls,
focus entry/leave, disconnect, Stop and overload cleanup. Add controller axis
bounds, neutral values and hot-unplug cases. Retain the existing pointer,
scheduling, memory, UI, ARM32, stage-mode and exact-IPK checks. Verify no input is
delivered outside captured focus and separately verify PC-side suppression.

On the TV, test actual volume/mute effects (including audio-output modes), media
actions, mouse side/extra buttons and controller recognition/actions in applicable
apps. Check that the PC does not also act on captured controls. Connection, device
discovery or successful writes do not establish app behavior. A 0.4.1 acceptance
record must name supported controls/devices and remaining gaps; host tests and
the 0.4.0 pointer acceptance cannot substitute for these new physical checks.

## Planning provenance (historical)

- Source audit: `.ai/outbox/input-0.4.1-20261113/SOURCE-SCOPE.md`.
- Existing baseline: `native/docs/TV-EXECUTION.md` and its exact 0.4.0 receipt.
- Official feature description: https://symless.com/synergy/features
- Official keyboard/mouse settings: https://support.symless.com/hc/en-us/articles/51545647921041-Keyboard-and-mouse-settings-in-Synergy-3
- Indexed protocol reference (direct retrieval unavailable in this pass): https://github.com/symless/synergy/blob/master/docs/dev/protocol_reference.md

Prepared by `/root`, T3 Code through Codex, session-reported gpt-6.1-sol@t3-1m
with xhigh effort, after a bounded native Luna xhigh source-scope review was
requested. Requested model labels are not independent provider attestation. This
is planning/source evidence only; no input code, version, TV state or publication
was changed by this planning pass.
