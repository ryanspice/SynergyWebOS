# Native 0.4.0 TV session runbook

**Owner-gated.** This runbook prepares the first real ARM32/TV session for the
native candidate. It does not authorize deployment, root changes or publication;
those remain separate owner decisions. Do not run it against the TV without the
owner present and the rollback path verified.

Source: `native/docs/BUILD.md` (build/stage), `native/docs/SECURITY.md`
(UID boundary), `docs/native-resynthesis-0.4.0.md` (gates).

## 0. Preconditions

- [ ] A committed candidate: record `git rev-parse HEAD`; the tree is clean.
- [ ] The verified SkiFree / Spice Home ARM32 SDK and target sysroot are
      available. Do **not** substitute a host build or invent paths.
- [ ] The TV is rooted, Homebrew Channel works, and the owner is present.
- [ ] The legacy 0.3.4 client, its autostart hook and any old dock are stopped or
      disabled. Never run two input daemons.
- [ ] A rollback package (`v0.3.4-web` / the retained 0.3.4 IPK) and a Synergy
      server-config backup exist.
- [ ] A working PC-side Synergy 3 server and a trusted LAN, with the TV screen
      name the server expects.
- [ ] The owner has approved a plaintext-input window. Real input is the last
      step, not the first.

## 1. ARM32 build and stage

Use the verified toolchain, not a guessed one. From the repo root (Linux/WSL
inside the SDK environment):

```bash
export SYNERGY_WEBOS_TOOLCHAIN=/actual/verified/toolchainfile.cmake
export SYNERGY_WEBOS_BUILD_DIR="$PWD/.ai/evidence/native-arm/build"
export SYNERGY_WEBOS_STAGE_DIR="$PWD/.ai/evidence/native-arm/stage"
bash native/scripts/build-webos.sh \
  -DSDL2_INCLUDE_DIR=/actual/target/include/SDL2 \
  -DFREETYPE_INCLUDE_DIR=/actual/target/include/freetype2 \
  -DJSONC_INCLUDE_DIR=/actual/target/include \
  -DJSONC_LIBRARY=/actual/target/lib/libjson-c.so
```

The paths above are placeholders. Record the exact commands, compiler, sysroot
and dependency versions in the evidence file.

## 2. Stage audit and IPK

```bash
python3 native/scripts/audit-stage.py "$SYNERGY_WEBOS_STAGE_DIR"
ares-package "$SYNERGY_WEBOS_STAGE_DIR" -o /fresh/owner-reviewed/output
ares-package -i /fresh/owner-reviewed/output/*.ipk
sha256sum /fresh/owner-reviewed/output/*.ipk
```

Checks before install:

- [ ] Package is `dev.canopy.synergywebos`, version `0.4.0`, architecture ARM.
- [ ] No x86_64 ELF, no host libraries, no fonts, no `spikes/` probe, no legacy
      `app/` web payload.
- [ ] Executable modes and the stage audit's dependency/symbol report reviewed.
- [ ] SHA-256 recorded in the session evidence.

## 3. Install and launch

Install through the TV's application manager (the owner's normal path). Do not
substitute an SSH process for the real jail launch.

- [ ] App launches and renders the native Connection / Input / Handoff /
      Diagnostics UI with D-pad, OK and Back.
- [ ] Record the app's **actual UID** (Diagnostics or the app's log).
- [ ] Confirm the app can reach the root-owned UID-checked control socket.

**First checkpoint:** if the jail cannot reach the socket, **stop**. Do not
disable confinement, add blanket permissions or build an HTTP root API. Record
the failure and design the narrow LS2 bridge instead.

## 4. Root provisioning and daemon

Only from an owner-authorized root shell, with the observed UID:

```bash
sh /path/to/provision-root.sh --controller-uid ACTUAL_UID --ack-shared-uid
```

- [ ] `--ack-shared-uid` recorded; the shared-UID tradeoff is understood.
- [ ] The fixed root launcher starts the daemon; it does not enable autostart.
- [ ] `synergy-tvctl status` reports the daemon, controller UID and socket.
- [ ] No legacy `60-synergy-webos`/`60-junction` hook is active.

## 5. Probe, dry run and input

1. **Probe** from the UI; confirm root and writable uinput, and record the report.
2. **Connect with dry run ON**: entry, leave, reconnect, and the idle timeout.
3. Only after the owner approves plaintext input:
   - [ ] Keyboard letters, modifiers held before entry, both Shift/Ctrl/Alt sides.
   - [ ] Meta/Home threshold and release; Alt 5 s; Alt+Tab 0/1/many apps and
         reverse.
   - [ ] Pointer entry, drag, edge, Magic Remote interference, 30 s parked idle.
   - [ ] **Physical release check:** hold keys/buttons, disconnect/leave/stop and
         observe the TV compositor receives the ups.
4. Stop input; confirm the daemon releases devices and the UI still reports.

## 6. Evidence to record

| Field | Value |
|---|---|
| Date / owner session | |
| Repo commit | |
| IPK SHA-256 / size | |
| TV model / webOS / firmware | |
| SDK / toolchain / dependency versions | |
| App UID / controller UID | |
| Socket path and reachability result | |
| Cold-boot / autostart result (session 2) | |
| Input/modifier/Home/Alt+Tab results | |
| Pointer drift / nudge visibility | |
| Failures and recovery | |

Do not commit device evidence to the repository; keep it in `.ai/evidence/` or
the owner's private record and reference only its path/hash.

## 7. Stop conditions

Stop and report, do not work around:

- Jail/socket unreachable, or requires a permission widening.
- Shell rejects the uinput devices, or releases are visibly wrong.
- ARM32 build impossible with the verified SDK.
- Any dual input daemon or legacy hook still active.
- A key/button stays held after disconnect/leave/stop.

## 8. Rollback

1. Stop native input; disable the native boot hook if enabled.
2. Terminate the native daemon cleanly; confirm no virtual devices remain.
3. Reinstall/restore the 0.3.4 package and its reviewed startup route.
4. Restore the Synergy server config backup if it changed.
5. Record the rollback result. Native state lives under
   `/var/lib/webosbrew/synergy-native`; do not delete legacy state.

## 9. Session 2 (acceptance soak)

- Three cold boots with autostart on, then disable and reboot.
- Sleep/wake and reconnect after a live drop; mid-frame stall + 10-minute soak.
- 2 h mixed use + 8 h idle.
- Rapid Home/Alt+Tab/app cycles; watch for focus/input wedge.
- Legacy-coexistence check; rollback drill to `v0.3.4-web`.
