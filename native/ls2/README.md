# Native LS2 control transport

`src/ls2_service.cpp` implements the separate root control helper. The native UI
uses `src/ls2_transport.cpp`; enable it with `SYNERGY_ENABLE_LS2` in a verified
ARM32 build with the pinned official LS2 and target GLib headers. The older
`ls2_bridge.cpp` read-only `/control/status` prototype is unused by this path.

The SDK's SDL integration owns `dev.canopy.synergywebos`. The control connection
registers `dev.canopy.synergywebos.ui-control` and calls
`luna://dev.canopy.synergywebos.control/control/request`. The helper returns the
existing daemon schema through the bounded Unix broker; it exposes no arbitrary
execution, app-launch, or boot-configuration method.

The helper denies every caller by default. `--verified-executable-role` enables
the exact secondary identity only after the effective hub role has been shown
to bind it exclusively to the protected installed UI executable. An unrelated
executable's registration must be denied even when it shares the UI UID. A JSON
app ID, sender prefix, unnamed connection or shared UID does not establish this
binding. `--allow-input-control` separately enables the existing configure,
start, restart, stop and profile-save operations; read-only status, snapshot,
logs, probe and profile-load do not require that second flag.

Requests run through an asynchronous bounded scheduler. Stop has reserved
capacity and cancels queued starts/restarts. Replies are dispatched on the
owning GLib context. The service is separately supervised and is excluded from
the ordinary app IPK; installing these descriptors does not start or elevate it.

These templates describe one firmware-tested integration, not an automatic
installer. Preserve every unrelated name and permission in the effective app
role. Retain the app manifest's three SDK ACGs; the separate secondary client
permission maps only to the exact control service. Preserve unrelated manifest
references and reload only the app/helper manifests being changed. Do not
restart the system bus or replace a generated platform role with a fragment.

Normal jailed UI rendering, the exclusive-name negative check and read-only
schema exchange were exercised on the target; physical UI Start/Stop and all
lifecycle paths remain separate acceptance gates. See
[`TV-EXECUTION.md`](../docs/TV-EXECUTION.md) for dated package and device evidence.
