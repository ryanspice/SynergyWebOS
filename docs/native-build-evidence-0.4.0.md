# Native 0.4.0 — local host and ARM32 build evidence

**Date:** 2026-11-11 · **Commit:** `496cf07` (`main`, after the P1 batch) ·
**Environment:** WSL `WebOSBuild` (x86_64 host) with the installed Starfish
webOS SDK 11.2.0 and the SkiFree/Spice Home target libraries.

This records what was actually built and tested locally. It does **not** claim a
TV install, uinput, jail/socket access, root provisioning or GLES behavior.

## Host builds and tests

| Step | Command (from `native/`) | Result |
|---|---|---|
| Release configure/build | `cmake -S . -B build/local-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSYNERGY_STRICT=ON && cmake --build build/local-release --parallel 4` | all three executables + tests built |
| Release CTest | `ctest --test-dir build/local-release --output-on-failure` | **4/4 pass** (core, integration, bundle, gear-spike) |
| ASan/UBSan configure/build | `cmake -S . -B build/local-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSYNERGY_STRICT=ON -DSYNERGY_SANITIZE=ON -DSYNERGY_BUILD_UI=OFF && cmake --build build/local-asan --parallel 4` | daemon, CLI, core, tests built (UI disabled) |
| ASan/UBSan CTest | `ctest --test-dir build/local-asan --output-on-failure` | **4/4 pass** |

Host toolchain observed: CMake 3.28.3, Ninja 1.11.1, GCC 13.3 (host),
SDL2 2.30.0, FreeType 26.1.20 headers, json-c 0.17, libpng 1.6.43.

## Native UI check

`native/tests/native_qa.py` under Xvfb with software GLES
(`LIBGL_ALWAYS_SOFTWARE=1 MESA_GLES_VERSION_OVERRIDE=2.0`):

- **17/17 checks pass**: 720p/1080p/4K rendering, pointer mapping, D-pad/OK/Back,
  text edit/apply/cancel and on-screen keyboard.
- Screenshots: `.ai/evidence/ui-qa-20261111/`; the Diagnostics capture shows
  `SynergyWebOS 0.4.0`.
- Renderer: llvmpipe / Mesa GLES 2.0. **Host rendering only, not TV GLES.**

## ARM32 target build

Environment (verified SkiFree recipe):

```bash
source /usr/local/starfish-sdk-x86_64/environment-setup-ca9v1-webosmllib32-linux-gnueabi
export CC="${CLANGCC/ -rdynamic/}"
export CXX="${CLANGCXX/ -rdynamic/}"
export LDFLAGS="${LDFLAGS:-} -rdynamic"
export SYNERGY_WEBOS_TOOLCHAIN="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake"
```

Inputs used:

| Input | Path |
|---|---|
| SDK / target sysroot | `/usr/local/starfish-sdk-x86_64` (`ca9v1-webosmllib32-linux-gnueabi`) |
| SDL2-webOS headers | `/opt/sdl2-webos/include/SDL2` |
| FreeType headers | `spice-home-cpp/.ai/runs/20261105-tv-install/downloads/freetype-2.13.2/include` |
| json-c | SDK sysroot `usr/include`, `usr/lib/libjson-c.so` |
| libpng (static target lib) | `SkifreeWebOS/native/build/deps/libpng-webos` |
| ARM SDL2 runtime | `/opt/sdl2-webos/lib/libSDL2-2.0.so.0` |

Result:

- `native/scripts/build-webos.sh` produced ARM32 ELF for `synergy-tv`,
  `synergy-daemon` and `synergy-tvctl`; `readelf` ARM gate passed.
- `native/scripts/audit-stage.py` → **PASS: ARM identity/files audited**,
  `tv_verified: false`.
- Max symbol requirements: `GLIBC_2.38`, `GLIBCXX_3.4.32` (the C3 target reports
  glibc 2.39 / GLIBCXX 3.4.32 per the recovered SkiFree evidence).
- Bundled under `lib/`: `libSDL2-2.0.so.0` (2.32.10), `libjson-c.so.5` (0.17),
  `libz.so.1` (1.3.1), each ARM32, with `licenses/` notices. libc, the loader,
  GPU/Wayland libraries and fonts are not bundled. FreeType is resolved from the
  TV at runtime.

## IPK

| Field | Value |
|---|---|
| Artifact | `dev.canopy.synergywebos_0.4.0_arm.ipk` |
| Size | 2,793,826 bytes |
| SHA-256 | `4801e5971ea8827daa0a9d0dfbc149885eeee799c8fb4e8b45ac137f7a9bd501` |
| Package / version / arch | `dev.canopy.synergywebos` / `0.4.0` / `arm` |
| App metadata | `type: native`, `main: synergy-tv` |
| Modes | executables **755**, data **644** (rebuilt on ext4; `/mnt/b` ignores chmod) |
| Location (untracked) | `.ai/evidence/native-arm-20261111/ipk-final/dev.canopy.synergywebos_0.4.0_arm.ipk` |

The package contains the three ARM executables, `appinfo.json`, icons, licence
and notice files, `scripts/provision-root.sh`, and the three bundled runtime
libraries. It does not contain `app/`, `dock/`, the gear spike, fonts or audit
receipts.

## Not established

- TV install/launch, application-manager jail behavior, app UID and socket
  reachability.
- Real uinput injection/release, modifier/Home/Alt+Tab effects, pointer drift.
- Root provisioning, cold-boot autostart, sleep/wake, rollback drill.
- Hardware GLES/font rendering or 1080p/4K behavior.

Those remain the owner-gated session in `docs/native-tv-runbook.md`.
