# Build, stage and validate

## Host builds

Use `scripts/Build-Desktop.ps1` from Windows/PowerShell; it invokes WSL CMake,
Ninja and CTest and checks every exit code. PowerShell itself was not available
in the authoring environment, so that wrapper has not been executed here.
Equivalent Linux commands, run from `native/`:

```bash
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DSYNERGY_STRICT=ON
cmake --build build/desktop --parallel 4
ctest --test-dir build/desktop --output-on-failure
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSYNERGY_SANITIZE=ON -DSYNERGY_BUILD_UI=OFF
cmake --build build/asan --parallel 4
ctest --test-dir build/asan --output-on-failure
```

Host fallback ABI declarations are retained from Spice Home for environments
with runtime libraries but no development headers. A target build rejects this
fallback. Prefer actual SDL/FreeType development headers on local desktops too.

For rendered UI checks (Xvfb is optional under working WSLg):

```bash
LIBGL_ALWAYS_SOFTWARE=1 MESA_GLES_VERSION_OVERRIDE=2.0 \
  xvfb-run -a -s '-screen 0 3840x2160x24' \
  python3 tests/native_qa.py --binary build/desktop/synergy-tv --out build/native-qa
```

No font files are distributed. Set `--font` to an installed readable font only
when automatic discovery does not match the target's fonts.

## Real ARM32 target

The reviewed SkiFree 1.0.0 build brief used SDL-webOS 2.32.10 and libpng 1.6.43,
with a verified ARM webOS SDK. This application additionally uses the homescreen's
FreeType and json-c path. Their headers/libraries must be for that SAME target.
This is a reference to the recovered build, not a claim those are the currently
installed versions in the user's workspace.

Within the already configured SDK environment, supply the real CMake toolchain:

```bash
export SYNERGY_WEBOS_TOOLCHAIN=/actual/verified/toolchainfile.cmake
export SYNERGY_WEBOS_BUILD_DIR="$PWD/.ai/evidence/native-arm/build"
export SYNERGY_WEBOS_STAGE_DIR="$PWD/.ai/evidence/native-arm/stage"
bash scripts/build-webos.sh \
  -DSDL2_INCLUDE_DIR=/actual/target/include/SDL2 \
  -DFREETYPE_INCLUDE_DIR=/actual/target/include/freetype2 \
  -DJSONC_INCLUDE_DIR=/actual/target/include \
  -DJSONC_LIBRARY=/actual/target/lib/libjson-c.so
```

The paths above are placeholders, not invented local installations. The native
source uses json-c's strict UTF-8 parser and parse-end API; the target's headers
and runtime must both provide these. Do not override compiler/sysroot checks or
mix host libraries into the target link. Build and stage paths must be fresh.

The script records ELF dependencies and symbol versions for all three binaries.
Review them against the exact SDK sysroot and TV. Stage only needed compatible
runtime libraries under `stage/lib/`, with regular dereferenced files, retaining
upstream notices. Do not bundle libc, the loader, GPU/Wayland libraries or fonts.
The supplied source archive contains no target libraries.

```bash
python3 scripts/audit-stage.py "$SYNERGY_WEBOS_STAGE_DIR"
ares-package "$SYNERGY_WEBOS_STAGE_DIR" -o /fresh/owner-reviewed/output
```

The stage auditor gates identity, files, ARM32 ELF and checksums. It records
needed libraries/symbol versions; it does NOT prove their resolution on a TV.
Audit the resulting IPK's contents and executable modes against that stage
before installation. No IPK is supplied because no target build ran here.

## Explicit TV acceptance sequence

1. Keep the known-working 0.3.4 package/configuration available. Disable its boot
   hook and stop its client deliberately. Do not run two input daemons.
2. Install the independently audited native candidate. Launch through the TV's
   application manager, not a plain SSH process substituted for the real jail.
3. Check the actual app UID in its log/Diagnostics; verify SDL/GLES rendering,
   installed font selection, pointer scaling, D-pad, OK and Back.
4. From an existing owner-authorized root shell, inspect and run the packaged
   `scripts/provision-root.sh --controller-uid ACTUAL_UID --ack-shared-uid`.
   It copies a reviewed ARM runtime into a private root-owned version directory.
   It neither starts it nor enables autostart. Never guess the UID.
5. Start the printed fixed root launcher in a separate root shell. Verify the
   actual application jail can reach its UID-checked socket. If it cannot,
   stop here and design the narrow LS2 bridge using measured ACL/jail evidence;
   do not add blanket permissions or an arbitrary-command root API.
6. Run Probe, configure a trusted LAN server, and connect with dry run ON. Match
   the TV screen name in desktop Synergy. Check entry, leave, events, reconnect.
7. Only after that, explicitly approve plaintext and select real input. Verify
   keyboard layout, modifiers, click/scroll, Home timing, Alt+Tab and cursor
   drift in actual apps. Test unplug/reconnect and key release on disconnect.
8. Close/reopen the UI while the daemon runs. Test sleep/wake. Enable native
   autostart only after these pass. The helper refuses existing legacy hooks.

A writable uinput node is not evidence that the webOS shell accepts its devices.
No kernel modules, device nodes, system services or access rules are created.

## Rollback

Stop native input through its CLI/UI and disable its managed boot hook. Terminate
the known native daemon process cleanly. Reinstall the retained 0.3.4 package
and restore its own reviewed startup route. Native state lives under
`/var/lib/webosbrew/synergy-native`; it does not overwrite `junction.conf` or
`junction-state.json`. Do not use broad process kills or wildcard deletion.
