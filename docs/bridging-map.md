> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Bridging map: SynergyWebOS web app vs. native C++20 (SkifreeWebOS-style)

Purpose: compare the current SynergyWebOS **static web app** implementation with a
native **C++20/GLES2** implementation (as used by SkifreeWebOS) and map how a
native port could address the blockers recorded in thread
`40f3e519-145a-4206-935b-f52cdd1d5547`.

## 1. Layer comparison

| Concern | Current (web app) | Native C++20 (Skifree-style) | Notes |
|---|---|---|---|
| Runtime | Built-in Chromium 87 floor, static JS/HTML/CSS, no bundler | Native ARM binary, C++20, GLES2, SDL2-style window/input | Web app is lighter to ship/update; native has full OS access. |
| Root access | Via HBC `exec` of `tvctl.sh` from the web app; ACG-dependent; lost on reboot (handoff.md) | Binary can be installed as a privileged service or launched with elevated role; root is a property of the process, not a bridge call | **Native removes the HBC-exec dependency that breaks the Start button after reboot.** |
| Input injection | Python client (`client.py`) spawned by `tvctl.sh`; uinput keyboard/mouse | Direct uinput from the native process; no Python subprocess, no shell wrapper | Eliminates the partial-frame/socket-desync class of bugs at the transport layer; input path becomes in-process. |
| Network/Synergy protocol | Python `_recv_exact` with timeout handling (bug §2.1) | C++ socket read with explicit partial-buffer state and reconnect | **Directly fixes the partial-frame timeout defect.** |
| Cursor visibility | JS-driven; keepalive nudge (unverified) | Native compositor/cursor control or direct evdev; can hold a visible cursor without a wiggle | Removes the need for the net-zero nudge hack. |
| Modifier state on entry | Discarded in CINN handler | Applied to the virtual keyboard at entry in native code | **Fixes modifier-loss on entry.** |
| Pointer sync on entry | Relative deltas only | Native can query/set cursor position via evdev or compositor APIs | Better sync options than the web app. |
| Status/monitoring | JS polling of `tvctl.sh snapshot`; busy-vs-failed ambiguity | Native reports state via a local socket/file or luna-service; structured, no shell round-trip | Removes the busy-vs-failed stall. |
| Packaging | `.ipk` web app, `ares-package`, small (~70 KB) | `.ipk` native, larger, arch-specific (ARM32), needs toolchain | Web app ships faster; native needs build infra (as Skifree has). |
| Update cadence | Edit HTML/JS, rebuild IPK, reinstall | Recompile, repackage, reinstall | Web app is cheaper to iterate. |

## 2. Issue-by-issue: how a native port would solve thread 40f3e519 blockers

| Blocker (thread 40f3e519) | Web-app root cause | Native mitigation |
|---|---|---|
| Partial-frame timeout desync (High) | `_recv_exact` buffers only in locals; timeout discards consumed bytes | C++ keeps a persistent receive buffer and resumes partial frames; or hard-disconnects cleanly. |
| Root access lost after reboot (High) | App UI depends on HBC `exec`, which is ACG/role state that did not persist | Native service runs with its own role; root is not gated by a bridge call from a web app. |
| Cursor visibility unverified (Medium) | JS keepalive nudge, untested | Native holds cursor via compositor/evdev; no nudge needed. |
| Reboot persistence (Medium) | Autostart hook + HBC exec both reverted | Native service + init hook are more robust; still needs re-provision but less fragile. |
| Modifier state discarded (Medium) | CINN handler ignores masks | Apply masks to virtual keyboard at entry. |
| Pointer not synced on entry (Medium) | Relative deltas only | Native can position the cursor. |
| Status monitoring stall (Low) | busy vs failed conflated | Structured native status. |

## 3. What a native port would NOT solve

- The 2px corner is a **Synergy server config** concern (edge intervals), independent of client runtime. Native or web, the corner remains an edge-interval approximation.
- The dock is not a true overlay on either runtime unless webOS grants overlay permission to third-party apps.
- Classic unencrypted Synergy TCP is a protocol choice, not a runtime choice.
- TV model/firmware-specific input behavior (Magic Remote coexistence) still needs hardware verification.

## 4. Recommended path

- **Short term (1.0.0):** keep the web app. Fix P0 items in-place (partial-frame reconnect, root fallback status, verify keepalive). The web app ships and iterates faster and matches the existing build/test pipeline.
- **Medium term (1.x/2.0):** prototype a native C++20 client behind the same `tvctl`/snapshot contract, reusing the proven uinput + Synergy 1.x protocol logic. Validate on the LG C3 before replacing the web client. Keep the web app as the fallback/management UI.
- **Decision gate:** native port is justified only if root persistence and input reliability cannot be made robust in the web app. Record the decision in `docs/decisions.md` with the exact blocker and next gate (per the SvelteKit/standard exception pattern).

## 5. Open questions

- Does the webOS SDK toolchain for C++20 (as Skifree uses) support the same HBC role grants needed for uinput? Skifree is a non-root app; SynergyWebOS needs root. Verify HBC role persistence for a native service before committing.
- Can the web app remain the management UI while a native daemon handles input? Likely yes, via the existing `tvctl.sh`/snapshot contract, but needs a defined IPC boundary.