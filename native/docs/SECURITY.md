# Native security boundaries

This is a developer candidate, not a security-reviewed production root service.

The input daemon runs separately from the UI. It exposes only a bounded Unix
socket protocol; there is no HTTP/WebSocket/TCP management listener, shell
command string, arbitrary executable selection, or network-supplied file path.
The client makes an outbound classic Synergy connection. TLS is not implemented.
A malicious accepted server can issue input, so use a trusted isolated LAN only.

Control authenticates kernel `SO_PEERCRED`: root or one explicitly configured
controller UID. **It does not authenticate webOS application identity.** When
multiple installed apps share that UID, they share this control authority.
The provisioner requires acknowledgment of this boundary. The socket is mode
0666 inside an immutable-to-controller directory so the authorized app UID can
reach it; credentials are checked before request bytes are read. Never interpret
that as app-level isolation. A compromised controller UID can configure/start
input and request the narrow supported TV actions.

On a real TV, jail filesystem reachability and UID assignment are unresolved
acceptance gates. Do not weaken confinement. Use a narrow authenticated LS2
bridge later only if measured platform behavior requires it. This implementation
does not add such a bridge or use Homebrew's arbitrary exec API from the UI.

The explicitly provisioned daemon/launcher/runtime copy is root-owned and not
writable by the controller. Startup is not automatically enabled. Legacy files
and hooks are never replaced. Provision only reviewed stage bytes. No exploit,
rooting procedure, modprobe, mknod or blanket chmod is part of the rebuild.

Input uses actual Linux headers/ioctl structs and RAII device lifetime. Desktop
builds reject real input even as UID 0. Native input is released on capture loss,
connection failure and normal stop. Power loss/SIGKILL cannot execute software
cleanup; destroying the process closes its virtual devices, but the compositor's
response still needs a physical-TV test.

I/O has bounded frames, buffers, operation deadlines and reconnect backoff.
Partial Synergy packets survive polling gaps. Local state uses private files,
symlink refusal and atomic replacement; malformed configuration is preserved and
reported. System Home/app launch calls use an absolute program path, fixed Luna
methods, literal argv, a clean environment, bounded output and a process-group
timeout. A successful Luna response is not proof of a visible TV action.

Not yet established: fuzz coverage beyond the supplied deterministic malformed
cases, target ABI/uinput behavior, app-level IPC authorization, firmware-specific
Luna permissions, clipboard, TLS/certificate pinning, non-US keyboard layouts,
absolute pointer synchronization, sleep/wake and long-running TV stability.
