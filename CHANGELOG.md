# Changelog

## 0.4.1 candidate — unreleased

### Focused input
- Adds explicit media, browser, extended function and keypad mappings, with volume repeats and suppressed media-toggle repeats. Mouse IDs 1–5 are mapped; unassigned IDs above 5 are ignored. ID 0 remains invalid.
- Adds an optional mutually authenticated TLS input bridge and focus-gated XInput forwarding for up to four compatible controllers. The companion is separate from the TV package. See [setup](desktop/input-bridge/README.md) and the [protocol](native/docs/INPUT-BRIDGE-PROTOCOL.md).
- Adds sequence, focus and freshness checks, bounded queues and watchdog neutralization on lost input. The classic connection remains available.

### Native baseline improvements
- Improves absolute pointer discovery and screen bounds, input cleanup and cancellation, bounded ingress and repeat handling, and Stop priority.
- Reduces text texture footprint using alpha coverage and avoids unnecessary redraws. These are implementation changes, not TV performance claims.
- Adds native service, LS2 transport, package auditing and regression coverage. Root provisioning and uinput are required for real input; review the [security boundary](native/docs/SECURITY.md) and [build instructions](native/docs/BUILD.md).

### Evidence and limits
Host and loopback checks, sanitizer runs, ARM32 packaging and Windows companion compilation are recorded in [input verification](native/docs/INPUT-0.4.1.md). The candidate has not been physically installed or accepted on a TV. Controller and media effects, PC controller exclusivity, cold boot, autostart and full target ABI remain open. Raw HID, vendor controls, gyro, touchpad and rumble return are not implemented. The previously accepted 0.4.0 pointer evidence is documented separately in [TV execution](native/docs/TV-EXECUTION.md).

## 0.4.0 native baseline — TV pointer acceptance, 2026-11-13

- Replaces the web runtime with C++20, SDL2/GLES2 and FreeType. The UI and root input daemon have separate lifetimes; native packaging retains the app identity and third-party notices.
- Pins the packaged SDL2 path for ordinary application-manager launch, and adds a narrow, separately provisioned LS2 control helper for the jailed UI. Unverified callers are denied by default; input-control permission is explicit.
- Uses USB-compatible virtual input identities, explicit TV device discovery and absolute pointer synchronization. The owner confirmed a visible cursor, full-screen range and smooth leave/re-entry in Twitch on the recorded target.
- Retains the exact accepted package and private runtime as recovery material. This acceptance covers the pointer path; it does not establish cold boot/autostart, sleep/wake, all UI mutation paths or the later 0.4.1 controller bridge. See the [dated record](native/docs/TV-EXECUTION.md).

## 0.3.4 legacy

The earlier web implementation remains historical migration material; see [legacy documentation](docs/legacy/README-0.3.4.md). It is not the native package.
