# SynergyWebOS 0.4.1 release candidate

**Candidate, unreleased.** Source integration and a draft release may proceed separately from stable binary or store readiness. Physical 0.4.1 acceptance is still required.

This candidate builds on the native 0.4.0 baseline, including pointer bounds and discovery, bounded input and repeat processing, Stop and cancellation handling, and a smaller alpha text footprint. The earlier 0.4.0 TV pointer acceptance is recorded in [TV execution](../native/docs/TV-EXECUTION.md); it does not validate this candidate.

The keyboard stream gains media and browser mappings in both connection modes. The optional input bridge adds focus-gated forwarding for up to four XInput-compatible controllers. It authenticates the TV and companion with mutual TLS; setup and certificate handling are described in the [companion guide](../desktop/input-bridge/README.md) and [protocol](../native/docs/INPUT-BRIDGE-PROTOCOL.md). The classic connection remains available. Root provisioning and uinput are required for real TV input; review [security](../native/docs/SECURITY.md) and [building](../native/docs/BUILD.md) before use.

Host, synthetic loopback and sanitizer checks and an ARM32 package build have been reported; see [verification details](../native/docs/INPUT-0.4.1.md). These do not establish physical controller capture, TV media effects, runtime ABI closure or hardware performance. Pairing and a private runtime upgrade require target verification. Test cold boot and autostart, sustained backpressure, TV input release, and controller behavior on both PC and TV before stable distribution.

Mouse buttons 1–5 are mapped; unassigned IDs above 5 are ignored and ID 0 remains invalid. PC-side controller suppression, raw HID, vendor controls, gyro, touchpad and rumble return are not implemented. The legacy 0.3.4 web implementation is retained as [history](legacy/README-0.3.4.md).
