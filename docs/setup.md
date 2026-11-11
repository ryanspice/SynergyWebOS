> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Setup

This is the intended setup for the rebuilt project. Steps marked **[pending]**
depend on workstreams that have not landed yet; the existing prototype evidence
is summarised in [`handoff.md`](handoff.md).

## 1. PC prerequisites

| Requirement | Notes |
| --- | --- |
| Windows 10/11, PowerShell 7.5+ | Scripts target PowerShell 7. |
| Synergy 3 (licensed) | Installed and running on the PC. Not bundled. Keep your license data private. |
| Git 2.40+ | For the repository and the optional hooks. |
| Node.js 20+ | Only needed to build the package. |

## 2. TV prerequisites

| Requirement | Notes |
| --- | --- |
| LG webOS TV on the same trusted LAN | Static/DHCP reservation recommended; the TV's address is stored in the client config. |
| Homebrew Channel | Provides the rooted service used to run the client as root. |
| Developer mode / adb access | Only for installing the package with the webOS CLI. |

## 3. Discover addresses (placeholders)

Record the real addresses locally, **not** in this repository:

```
PC LAN address : <PC-LAN-IP>       (Synergy server listens on :24800)
TV LAN address : <TV-LAN-IP>       (DevTools + install target)
Synergy REST   : http://127.0.0.1:24803/v1/     (PC-local only)
```

## 4. Configure the Synergy server **[pending: tools/synergy/]**

1. Add the TV as a normal screen/computer in Synergy so it can be moved in the
   GUI.
2. Set the handoff to the small top-left corner with the desired
   delay/override.
3. Validate with the tooling's dry-run against a temporary core, then apply
   with the documented API call and keep a rollback note.

Do not restart the Synergy core/service while applying config unless the
tooling documents that it is required.

## 5. Deploy the TV app **[pending: app/ + scripts/build/]**

1. Build the package (`docs/building.md`).
2. Install it with the webOS CLI to the TV.
3. Open the app, enter the PC address and screen name, and start the client.
4. Confirm status is *connected*, then run the root/uinput probe.

## 6. Verify the home key **[pending: scripts/home/]**

Press the Windows key with the TV focused; the TV's **system home** must
appear. Ensure no third-party launcher opens and no stray Meta key is sent.

## 7. Autostart

Enable autostart from the app once the client connects reliably. The autostart
entry lives on the TV under the Homebrew init directory and is re-created by
the app/tooling; it is not part of this repository.

## 8. Uninstall / rollback

- Disable autostart from the app, then remove the package with the webOS CLI.
- Restore the Synergy config from the rollback notes recorded when applying.
- Keep the TV and PC on a trusted LAN only; the client protocol is not
  encrypted, so do not expose the Synergy server port to the internet.
