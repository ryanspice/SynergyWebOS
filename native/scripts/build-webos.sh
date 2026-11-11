#!/usr/bin/env bash
# Adapted from Spice Home C++: cross-build/stage only; no install or root changes.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${SYNERGY_WEBOS_TOOLCHAIN:?Set this to the verified SkiFree/Spice Home ARM32 CMake toolchain file}"
[[ -f "$SYNERGY_WEBOS_TOOLCHAIN" ]] || { echo 'Toolchain file missing' >&2; exit 2; }
command -v readelf >/dev/null || { echo 'readelf is required' >&2; exit 2; }
BUILD="${SYNERGY_WEBOS_BUILD_DIR:-$ROOT/build/webos}"
STAGE="${SYNERGY_WEBOS_STAGE_DIR:-$ROOT/dist/webos-stage}"
[[ ! -e "$BUILD" && ! -e "$STAGE" ]] || { echo 'Choose fresh build and stage directories; no automatic clearing.' >&2; exit 2; }
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$SYNERGY_WEBOS_TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=Release -DSYNERGY_WEBOS=ON -DSYNERGY_STRICT=ON -DBUILD_TESTING=OFF "$@"
cmake --build "$BUILD" --parallel "${SYNERGY_JOBS:-4}"
for name in synergy-tv synergy-daemon synergy-tvctl; do
  readelf -h "$BUILD/$name" | grep -Eq 'Class:.*ELF32' || { echo 'Refusing host executable' >&2; exit 2; }
  readelf -h "$BUILD/$name" | grep -Eq 'Machine:.*ARM$' || { echo 'Refusing non-ARM executable' >&2; exit 2; }
  readelf -d "$BUILD/$name" > "$BUILD/$name.dependencies.txt"
  readelf --version-info "$BUILD/$name" > "$BUILD/$name.symbol-versions.txt"
done
cmake --install "$BUILD" --prefix "$STAGE"
printf '\nARM32 stage created: %s\n' "$STAGE"
printf 'Not install-ready. Stage compatible runtime libraries; audit against the TV/SDK, then package. See docs/BUILD.md.\n'
