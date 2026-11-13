# Native security boundaries

This is a developer candidate for an already rooted TV. Source, automated tests
and one firmware's 0.4.0 execution evidence do not establish a production security
review or 0.4.1 hardware acceptance.

## Network input and pairing

The TV daemon initiates its connection. It has no TCP management listener,
arbitrary executable selector or network-supplied file path. An accepted input
server can issue keyboard/mouse and, in bridge mode, controller events. Trust
that server and use an isolated private network.

Direct classic Synergy 1.4 is plaintext and requires explicit consent. Optional
`input_bridge` mode uses OpenSSL 3 mutual TLS, minimum TLS 1.2, with a configured
CA and expected server identity. The companion also validates the client CA and
an explicit SHA-256 fingerprint of the TV certificate before opening the
loopback-only upstream Synergy connection. There is no TLS-to-plaintext fallback.
This is our bridge protocol, not proprietary Synergy TLS support.

On the TV, pairing PEMs must be regular files, mode 0600, owned by the daemon
user. Checked descriptors supply the bytes used for parsing; private key memory
is cleansed after loading. Keep pairing material out of source, IPKs, companion
archives and reports. No global certificate-store installation is required.
Certificate generation, rotation, revocation and physical pairing acceptance
remain explicit operator responsibilities. See the
[companion guide](../../desktop/input-bridge/README.md).

## Local control and privilege

The Unix broker checks kernel `SO_PEERCRED` before request bytes: root or one
explicitly configured controller UID. This authenticates a UID, not an app.
Apps sharing that UID share broker authority if they can reach the socket.
The provisioner acknowledges that boundary. The socket is mode 0666 inside a
protected directory; the peer check, not the mode alone, controls admission.

The optional separate root LS2 helper denies every caller by default. Enabling
`--verified-executable-role` is appropriate only after the effective hub role
binds the exact secondary UI service name exclusively to the protected installed
UI executable and an unrelated executable is rejected. Payload app IDs, sender
prefixes and shared UIDs do not establish identity. `--allow-input-control`
separately enables the bounded configure/start/restart/stop/profile-save set.
Read operations and mutation operations both retain broker schema validation.
There is no generic execution, arbitrary app-launch or boot-edit API.

The helper uses a bounded asynchronous scheduler with reserved Stop capacity.
It is excluded from the ordinary IPK and requires separate protected provisioning.
The [LS2 guide](../ls2/README.md) describes templates and firmware-specific
permissions. Preserve all unrelated effective permissions and manifest entries;
never disable confinement or restart the system bus to make admission succeed.
Normal jailed rendering and read-only status worked on the recorded 0.4.0 TV;
all mutation and lifecycle paths need their own acceptance.

## Lifecycle and resource bounds

The installed daemon/launcher/runtime must be root-owned and unwritable by the
controller. Startup is not enabled automatically. The project supplies no exploit,
modprobe, mknod, blanket chmod or automatic legacy-hook replacement.

Input uses Linux headers/ioctl structures and RAII device ownership. Host builds
refuse real input even as root. Capture loss, Stop, disconnect and errors release
held input. Controller frames additionally check capture generation, sequence,
freshness and a watchdog independent of deferred shortcut handling. Queues,
frames, ingress, output, operation deadlines and reconnect backoff are bounded.

Power loss or SIGKILL cannot run cleanup code. Closing virtual-device descriptors
and compositor behavior still need physical acceptance. Forwarding XInput does
not hide the PC controller; other PC applications may continue to receive it.

Local state uses private files, symlink refusal and atomic replacement. Malformed
configuration is preserved and reported. Bounded Home/app-switch operations use
an absolute executable path, fixed Luna methods, literal arguments, a clean
environment and a process-group deadline. Successful replies or writes do not
prove a visible TV action.

Remaining gates include complete target ABI/load evidence, physical pairing and
controller/media behavior, PC exclusivity, sleep/wake, cold boot/autostart and
long-running stability. Deterministic malformed-frame tests are not exhaustive
fuzzing. See [candidate verification](INPUT-0.4.1.md) and the
[dated TV execution summary](TV-EXECUTION.md).
