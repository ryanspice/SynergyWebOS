# Input bridge v1 contract — 0.4.1 implementation

Status: implementation contract; no runtime, interoperability or physical-device
acceptance is implied by this document. This narrows the controller architecture
for disjoint implementation after the first broad Terra task returned incomplete.

## Transport and trust

A native desktop proxy terminates mutually authenticated TLS from the TV and
connects to the existing Synergy server on numeric loopback, normally port 24800.
It relays classic Synergy frames and observes server-to-TV CINN/COUT ownership.
The installed server is not modified. The TV remains an outbound client; no
network root listener or arbitrary RPC is added on the TV.

Use standard OpenSSL TLS APIs, TLS 1.2 minimum, a private pairing CA and separate
server/client certificates. Both peers verify the CA chain and certificate use;
the TV verifies a configured expected server identity. The proxy additionally
pins the permitted TV client certificate's SHA-256 DER fingerprint. No input or
controller capture begins for an unauthenticated peer. Missing credentials fail
closed; there is no TLS-to-plaintext fallback. Private keys are never logged,
included in status/profile exports, bundled, or committed. Credential generation
and live setup are separate from source implementation and build fixtures.

Direct classic mode remains supported with its existing explicit plaintext
consent. Only the explicit TLS bridge mode accepts the custom gamepad message.
TLS protects the whole proxied stream, including focus events; a readable classic
connection or focus counter alone is not sender authentication.

## Framing and controller packet

The outer framing remains a four-byte unsigned big-endian payload length followed
by payload bytes. Classic hello, key, pointer and control packets are forwarded
unchanged. Maintain one ordered writer per destination and bounded queues. Reject
malformed lengths. Existing clipboard/ignored-body limits must remain effective;
do not retain arbitrary megabyte frames just to observe their four-byte command.

The custom server-to-TV payload is exactly 32 bytes:

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 4 | ASCII DGPD |
| 4 | 1 | Version, exactly 1 |
| 5 | 1 | Controller slot 0..3 |
| 6 | 1 | Connected flag, exactly 0 or 1 |
| 7 | 1 | Reserved, exactly 0 |
| 8 | 4 | CINN sequence/capture ID, unsigned big-endian |
| 12 | 8 | Per-TLS-connection global sequence, unsigned big-endian, starts at 1 |
| 20 | 2 | XInput-layout button mask, unsigned big-endian, allowed bits 0xf3ff |
| 22 | 2 | Left X, signed big-endian 16-bit |
| 24 | 2 | Left Y, signed big-endian 16-bit |
| 26 | 2 | Right X, signed big-endian 16-bit |
| 28 | 2 | Right Y, signed big-endian 16-bit |
| 30 | 1 | Left trigger 0..255 |
| 31 | 1 | Right trigger 0..255 |

The shared standard-C++ header is `native/include/synergy/gamepad_wire.hpp`,
namespace `synergy::bridge`. It supplies `GamepadState` (buttons, lx, ly, rx, ry,
lt, rt, connected), `GamepadPacket` (slot, capture, sequence, state), constants,
and encode/decode helpers. No platform or OpenSSL headers belong in this header.
All fields must be explicitly serialized; no native-struct transmission. A
disconnected state is all zero/neutral. Reserved flags/buttons are rejected.

The wire coordinate convention is Linux-style: X positive right, Y positive
down. The Windows producer converts XInput's positive-up Y with saturating
negation (-32768 becomes +32767). Sticks range -32768..32767, neutral 0. Triggers
are independent, neutral 0. Button-mask names retain XInput meaning; map action
buttons by physical position in the TV backend (A south, B east, X west, Y north).

## Focus, cleanup and scheduling

CINN is 14 bytes; its existing capture sequence is bytes 8..11. The proxy relays
the CINN frame before controller snapshots for that capture. It polls XInput
only while this TV owns focus, at no more than 125 Hz, sending changes and a
refresh at least every 100 ms for connected pads. It sends a disconnected neutral
state on hot-unplug. Outside captured focus it does no controller polling or
forwarding. Socket waits remain cancellable; no uncontrolled busy loop.

The TV requires negotiated/ready classic protocol, authenticated bridge mode and
active focus before DGPD can inject. Capture ID must match the current CINN.
Sequence must strictly increase for the connection and must not reset on COUT or
re-entry. Stale/wrong-capture frames cannot re-press controls. Packet freshness
and a 500 ms controller watchdog must be enforced even during keyboard backlog;
do not reuse the Home/Alt timer deferral in a way that postpones neutralization.

On COUT, Stop, disconnect, error or hot-unplug, release buttons and neutralize
sticks/triggers/hats for every affected slot. The TV uses separate gamepad uinput
devices, not its absolute mouse axes. They follow Linux gamepad geometry and are
created/discovered only when needed. Do not change keyboard/mouse capabilities
or make a separate process/LS2 call for each controller report.

XInput supports up to four compatible controllers. This initial capture API does
not establish raw HID, gyro, touchpad, vendor paddles, rumble return, or exclusive
suppression from PC games. Forwarding and PC-side suppression are separate gates;
do not install a hiding driver or claim exclusivity without an authorized design
and physical evidence.

Primary references:

- https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate
- https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad
- https://docs.kernel.org/input/gamepad.html
- https://docs.openssl.org/3.0/man3/SSL_CTX_set_verify/

The proxy and shared header own only their new files. Native session/config/TLS
and uinput integration remain with the lead after the media-map writer finishes.
Independent tests, peer-authentication review and native/ARM/Windows builds are
required before this contract can be described as working support.
