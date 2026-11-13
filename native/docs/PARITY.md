# 0.3.4 → native parity and intentional boundaries

This table records the 0.4.0 baseline. The opt-in TLS/controller bridge and
semantic-key additions are described in [0.4.1 candidate scope](INPUT-0.4.1.md).
The table's classic-mode limits do not describe that separate bridge transport.

| Area | Native 0.4.0 candidate |
|---|---|
| App identity | Same dev.canopy.synergywebos; native main replaces web entrypoint only in an actual target package |
| Desktop connection | Classic Synergy/Barrier 1.4, manual numeric private/loopback IP, configurable port/name/size |
| Protocol parsing | Persistent bounded frame buffer; fragmentation across polling intervals supported; malformed lengths/TLS refused |
| Keyboard/mouse | Keys down/up/repeat, five buttons, absolute pointer backend with entry synchronization and clamped relative-packet accumulation, wheel remainder |
| Modifier entry | Shift/Ctrl/Alt mask inherited and reconciled with physical L/R events |
| Key cleanup | Leave, close, exception/disconnect and Stop release held native keys/buttons |
| Meta/Home | Both Meta keys consumed; system/off and 0–5000 ms hold; no repeat firing |
| Alt/Home | Ordinary Alt input plus five-second hold; release before Home action |
| Alt+Tab | Tab consumed during Alt; Shift reverses; pending Alt-Home canceled; running-app cache refreshed |
| Cursor | Absolute uinput X/Y using configured screen bounds; automatic device discovery; optional keepalive off by default. Full-range movement and smooth handoff accepted in the recorded Twitch session. |
| UI | Native raw-GLES2 Connection/Input/Handoff/Diagnostics, D-pad, pointer, physical and on-screen text editing |
| Root | Separate daemon + explicit provisioner, not accidental elevation of the UI |
| Persistence | Private atomic native config; active-session configuration separate from saved next-session config |
| Existing data | No automatic import, rewrite or deletion of 0.3.4 configuration |
| Autostart | Own opt-in managed boot hook; refuses unmanaged/legacy hooks; hardware behavior unverified |
| Handoff | Separate local reference schema; never changes the desktop server; original desktop helper source retained |
| Dock / Apps page | Removed at owner request; no companion package, launch button or native launcher page |
| CLI snapshot | Familiar section markers retained; complete compatibility with every legacy PowerShell parser is NOT established |
| Logs | Bounded in-memory state/error log and input-event counts, not keystroke contents |
| TLS/clipboard | Not implemented, as in the initial classic-client scope; no silent TLS downgrade |
| Runtime dependencies | SDL-webOS, FreeType, json-c, PNG, C++/system libraries; no browser/Python/Node in native runtime |

The legacy `app/`, `tools/`, `scripts/` and their historical documents
are retained migration source, with dock coupling removed, not assertions that their packaging/runtime path remains
the native default. New defaults and authoritative verification are under
`native/`. The old desktop controller still needs an explicit native CLI adapter
or deliberate invocation; it is not silently rerouted.

Gear-menu insertion is a separate researched spike in `spikes/gear-settings/`; no LG menu changes ship.
