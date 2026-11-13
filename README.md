# SynergyWebOS

![Candidate version 0.4.1](docs/media/badges/version.svg)
![C++20](docs/media/badges/cpp.svg)
![SDL2 and GLES2](docs/media/badges/runtime.svg)
[![MIT license](docs/media/badges/license.svg)](LICENSE)
[![Host verified; candidate TV acceptance pending](docs/media/badges/verification.svg)](native/docs/VERIFICATION.md)

**Share your desktop keyboard and mouse with a rooted webOS TV.**

![Native SynergyWebOS connection screen](docs/media/native-connection.png)

*Actual SDL2/GLES2 framebuffer capture from the Linux host build. This is a host
capture, not a TV photograph. Screenshots predate the 0.4.1 input additions.*

**0.4.1 is a build-verified candidate. Physical TV/controller acceptance is pending.**
The previous 0.4.0 native build ran on the target TV, and the owner confirmed
visible, smooth pointer movement across the full screen after leaving and
re-entering. That acceptance does not transfer to the new controller path.
See the [changelog](CHANGELOG.md), [release notes](docs/release-notes-0.4.1.md)
and [dated TV record](native/docs/TV-EXECUTION.md).

The app uses C++20, SDL2, raw GLES2 and FreeType. Its runtime has no Python, Node,
browser or Electron dependency. The management UI and input daemon are separate;
closing the UI does not have to end a connection. Real input requires an existing
root environment and Linux uinput. Installing the app alone does not provision
or elevate the daemon.

## Input and connection modes

- Classic Synergy protocol 1.4 carries keyboard, mouse, wheels and buttons 1–5.
  The candidate adds semantic browser/media keys, F13–F24 and keypad mappings.
  Volume keys repeat; media toggles suppress repeat packets. Unassigned mouse
  IDs are ignored without disconnecting.
- The optional [Windows input companion](desktop/input-bridge/README.md) relays
  the keyboard/mouse stream and adds focus-gated XInput state for up to four
  compatible PC controllers. The TV-to-companion connection uses mutual TLS.
  Pairing and a private daemon upgrade are explicit setup steps.
- The classic direct connection remains available with explicit plaintext
  consent. The companion does not add proprietary Synergy TLS negotiation.
  Use trusted private networks; do not expose either mode to the Internet.

Controller forwarding is not universal device tunnelling. PC-side controller
suppression, raw HID, gyro, touchpads, vendor controls and rumble return are not
implemented. Physical media effects, XInput capture and TV app recognition are
still unverified for 0.4.1. The PC can continue to consume controller input.
Clipboard, file transfer and Synergy cloud discovery are not implemented.

The pointer uses absolute screen coordinates and explicit input-device discovery
on the tested TV path. Keyboard Home and Alt+Tab are bounded daemon shortcuts.
There is no dock, launcher page or system-wide overlay. Handoff settings are a
local reference and do not rewrite the desktop server configuration.

## Build and verification

From a clone, install the host dependencies in your chosen WSL distribution:

```powershell
wsl -d YOUR_BUILD_DISTRO -- sudo apt-get update
wsl -d YOUR_BUILD_DISTRO -- sudo apt-get install -y build-essential cmake ninja-build python3 pkg-config libsdl2-dev libfreetype6-dev libjson-c-dev libpng-dev libssl-dev openssl xvfb xauth libgles2-mesa-dev
./native/scripts/Build-Desktop.ps1 -Distribution YOUR_BUILD_DISTRO
```

Read [native build instructions](native/docs/BUILD.md) before selecting a distro
or building for ARM32. A host build always refuses real input, including as root.
The TV build requires the actual ARM32 SDK and verified runtime inputs; a host
ELF is never a TV package. Python is a host test/audit dependency only.

Release and sanitizer checks, software-GLES UI regressions, ARM packaging and
Windows companion builds have passed. [0.4.1 verification](native/docs/INPUT-0.4.1.md)
records the scope and exact package checksum. These checks do not prove target
ABI closure, hardware performance, pairing or physical controller operation.
A source tag or draft release does not establish stable binary/store readiness.

## TV setup and security

Keep the accepted package and private runtime for rollback. Stop and disable the
legacy client before native input; never run two input daemons. The old 0.3.4
package used a different app ID and settings are not imported automatically.
The native identity is `dev.canopy.synergywebos`, version `0.4.1`.

The normally launched UI used a narrowly authorized LS2 helper on the tested
firmware. The helper and root daemon are provisioned separately and excluded
from automatic elevation by the app. Preserve unrelated platform permissions;
do not weaken the jail or replace a platform role with a template fragment.
See [security boundaries](native/docs/SECURITY.md), [LS2 integration](native/ls2/README.md)
and [release/rollback gates](docs/native-release.md).

Cold boot/autostart, sleep/wake, long-running stability and the new controller
path require their own target acceptance. No root exploit is supplied.

## Source and contribution

[Native verification](native/docs/VERIFICATION.md) ·
[Feature parity](native/docs/PARITY.md) ·
[Input bridge protocol](native/docs/INPUT-BRIDGE-PROTOCOL.md) ·
[Media provenance](docs/media/README.md) ·
[Licenses and foundation provenance](native/NOTICE.md)

The retained [legacy 0.3.4 source](docs/legacy/README-0.3.4.md) is migration history.
Its packaging scripts do not produce the native app. The removed dock is not
part of this release. A bounded [LG gear-menu research spike](spikes/gear-settings/README.md)
is separate from the application and installs no system-menu entry.

Independent implementation; not affiliated with Symless or LG. SynergyWebOS
shares input, not the desktop display. Preserve the MIT and bundled-library notices.
