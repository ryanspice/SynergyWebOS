# Synergy input bridge 0.4.1 candidate

This native companion adds focus-gated XInput forwarding to the 0.4.1 TV client.
It terminates mutually authenticated TLS from the TV, checks the permitted TV
certificate fingerprint, then connects to the existing Synergy server on IPv4
loopback. It does not modify or replace the installed Synergy service.

The ordinary Synergy keyboard/mouse stream is relayed. Controller state is added
only after CINN has been delivered and while both observed and delivered focus
agree. COUT cancels pending state; the TV independently neutralizes controls on
leave, disconnect and its watchdog. Queues and freshness/backpressure waits are
bounded; stale controller delivery closes the session instead of replaying it.

## Supported capture and limits

Windows uses the documented XInput API for up to four compatible pads. Standard
buttons, D-pad, sticks and separate triggers are forwarded. Y axes are converted
to Linux convention with saturating negation. Capture is paced at no more than
125 Hz while focused, with connected-state refreshes at least every 100 ms.

This does not hide the controller from PC games. Exclusive PC suppression,
non-XInput raw HID, gyro, touchpads, extra vendor controls and rumble return have
not been implemented. These are capability gaps, not claimed working features.
TV app recognition and physical-device behavior require separate acceptance.

The 0.4.1 candidate passed Linux/Windows compilation, 81 loopback conformance
checks and a 14-check actual-daemon/proxy/synthetic-Core smoke test. Test sources
are under `tests/`; build and cleanup receipts are in the project evidence lane.
Queue saturation, timing under sustained backpressure, real XInput hardware and
physical TV operation remain separate gates. The relay caps both queued payload
bytes and allocation chunks.

## Build

Use CMake, a C++20 compiler and OpenSSL 3 development libraries. Windows also uses
the system WinSock and XInput libraries; no capture driver is installed.

From the repository root, set the path to an OpenSSL 3 x64 development install
that matches your compiler and architecture, then build with MSVC:

```powershell
$opensslDir = 'C:/path/to/openssl-3-x64'
cmake -S desktop/input-bridge -B .ai/evidence/input-041-proxy-win -G "Visual Studio 17 2022" -A x64 "-DOPENSSL_ROOT_DIR=$opensslDir"
cmake --build .ai/evidence/input-041-proxy-win --config Release
```

Place the matching OpenSSL runtime DLLs beside the executable for a standalone
Windows bundle; retain the project MIT license and OpenSSL's Apache 2.0 license.
Never bundle pairing credentials. Linux builds are for loopback fixture testing:

```sh
cmake -S desktop/input-bridge -B .ai/evidence/input-041-proxy-host -DCMAKE_BUILD_TYPE=Release
cmake --build .ai/evidence/input-041-proxy-host
```

## Pairing and operation

Provision a private pairing CA and separate server/client certificates through
an explicit operator setup. Server certificate SAN must match the TV's expected
server identity; certificate EKUs must be serverAuth and clientAuth respectively.
Keep private keys protected. The TV additionally requires mode 0600 and daemon
ownership for all three pairing PEM files. No certificate-store installation or
real credential generation occurs during this source implementation.

Obtain the SHA-256 fingerprint of the permitted TV **public certificate's DER**
using a standard certificate tool. Pass exactly 64 hexadecimal characters.

```text
synergy-input-bridge --ca CA.pem --cert SERVER.pem --key SERVER.key --client-sha256 TV_CERT_DER_SHA256 --listen PRIVATE_PC_IPV4 --port 24801 --upstream-port 24800
```

Listen defaults to 127.0.0.1; exposing a private LAN address is explicit. Upstream
is always 127.0.0.1, not an arbitrary forwarding target. The TV uses the proxy's
private IP and port, with `input_bridge=true`, private `bridge_ca_file`,
`bridge_cert_file`, `bridge_key_file`, and `bridge_server_name` configuration.
There is no TLS-to-plaintext fallback. Direct classic mode remains available
when the bridge option is disabled. Upgrade the private TV daemon along with
the app before configuring new fields; leave the accepted 0.4.0 runtime retained.

For Linux conformance tests only, `--fixture FILE` supplies up to four text rows:
`slot connected buttons lx ly rx ry lt rt`, using decimal numbers and the wire
coordinate convention. Fixture mode is restricted to loopback. Replace the
fixture atomically when changing it. An invalid fixture while outside focus is
not read; the test harness uses that property to verify capture gating. Windows
defaults to actual XInput capture when no fixture is specified.

The authoritative frame contract is
[`INPUT-BRIDGE-PROTOCOL.md`](../../native/docs/INPUT-BRIDGE-PROTOCOL.md). Passing
host or Windows builds does not establish controller functionality on the TV or
PC-side suppression. No tests, live input or deployment are implied by this README.
