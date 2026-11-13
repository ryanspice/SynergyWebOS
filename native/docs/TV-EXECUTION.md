# Native 0.4.0 TV execution — 2026-11-12 to 2026-11-13

This public summary records the earlier accepted pointer candidate. It is dated
evidence, not a live status query or acceptance of the new 0.4.1 input bridge.
Private device logs, process identities and operational recovery scripts remain
in the project's local evidence archive.

## Exact baseline

- App identity: `dev.canopy.synergywebos`, version `0.4.0`.
- Accepted ARM IPK: 2,862,358 bytes, SHA-256
  `719ff310146c1d64d6dd5a5e653ed737166956c5894c36089c5d843b6a94120d`.
- Packaged daemon SHA-256:
  `c2b7a3847b5994449856b7c7081a8bbbd916811225fdb38075f5b6bc4c2e3c93`.
- The protected LS2 helper was retained separately from the app IPK. Rebuilding
  a helper does not prove that its bytes ran on the TV.

The package's 18 payload files and POSIX modes were checked after extraction.
Static symbol-version name coverage passed; it was not full target ABI closure.
The app launched normally through the application manager and rendered using
its packaged SDL2 library on the target GPU.

## Control and pointer observations

The normal jailed UI completed a schema-1 read-only status exchange through the
secondary LS2 service identity. Default denial and unrelated-executable name
registration denial were exercised. Preserve the SDK app permissions and the
exact secondary-client binding described in the [LS2 guide](../ls2/README.md).
This is not blanket proof of all mutation, role-reload or lifecycle operations.

Initial native mouse motion reached the input backend but its cursor was
invisible. After the TV opened the native mouse through explicit device
discovery, the owner confirmed cursor visibility. Later pointer changes used
absolute axes matching the screen bounds and synchronized entry state.
The owner then confirmed **full-screen range and smooth movement**, including
leaving the TV and entering again. Twitch was the foreground app for that
acceptance. No TV FPS or latency measurement follows from this observation.

The native client ran with the legacy client stopped. Root uinput injection and
visible pointer acceptance were established for these exact 0.4.0 bytes. UI
Start/Stop paths, cold boot/autostart, sleep/wake and long-running soak budgets
remain separate gates. Keep the exact package and protected runtime for rollback.

## Next candidate

The [0.4.1 candidate](INPUT-0.4.1.md) adds semantic media/browser mappings and an
optional authenticated controller bridge. Its host, loopback, Windows build and
ARM/package evidence does not establish physical TV/controller support. Do not
replace the accepted private runtime until the target upgrade is explicitly
scheduled and its recovery path has been inspected.
