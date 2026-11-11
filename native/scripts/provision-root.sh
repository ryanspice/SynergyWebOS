#!/bin/sh
# Explicit developer provisioning, never invoked by the UI or package installer.
set -eu
[ "$(id -u)" = 0 ] || { echo 'Run from an existing, owner-authorized root shell on the TV.' >&2; exit 2; }
[ "$#" = 3 ] && [ "$1" = '--controller-uid' ] && [ "$3" = '--ack-shared-uid' ] || {
  echo 'Usage: provision-root.sh --controller-uid ACTUAL_NATIVE_UI_UID --ack-shared-uid' >&2
  echo 'This grants control to that whole Linux UID, not to one webOS app ID.' >&2
  exit 2
}
uid=$2
case "$uid" in ''|*[!0-9]*) echo 'Invalid controller UID' >&2; exit 2;; esac
[ "${#uid}" -le 9 ] || exit 2
[ -x /usr/bin/luna-send ] && [ -d /var/lib/webosbrew/init.d ] || { echo 'Requires the existing rooted webOS/Homebrew environment; no host setup is performed.' >&2; exit 2; }
base=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
root=/var/lib/webosbrew/synergy-native
version=$root/runtime/0.4.0
# The root-owned directory tree must not traverse symlinks or writable ancestors.
for path in /var/lib/webosbrew "$root" "$root/runtime"; do
  [ ! -L "$path" ] || { echo "Refusing symlink: $path" >&2; exit 2; }
  if [ ! -e "$path" ]; then mkdir "$path"; chmod 0755 "$path"; fi
  [ -d "$path" ] && [ "$(stat -c %u "$path")" = 0 ] || { echo "Not a root-owned directory: $path" >&2; exit 2; }
  mode=$(stat -c %a "$path")
  case "$mode" in 700|750|755) ;; *) echo "Unsafe directory mode: $path $mode" >&2; exit 2;; esac
 done
[ ! -e "$version" ] && [ ! -L "$version" ] || { echo 'Version already provisioned; review it explicitly instead of overwriting.' >&2; exit 2; }
[ ! -e "$root/start-native.sh" ] && [ ! -L "$root/start-native.sh" ] || { echo 'Existing launcher preserved; no automatic replacement.' >&2; exit 2; }
for name in synergy-daemon synergy-tvctl; do
  path=$base/$name
  [ -f "$path" ] && [ ! -L "$path" ] || { echo "Missing regular executable: $path" >&2; exit 2; }
  ident=$(od -An -tu1 -N6 "$path" | tr -s ' ' | sed 's/^ //;s/ $//')
  machine=$(od -An -tu1 -j18 -N2 "$path" | tr -s ' ' | sed 's/^ //;s/ $//')
  [ "$ident" = '127 69 76 70 1 1' ] && [ "$machine" = '40 0' ] || { echo 'Refusing anything other than ARM32 little-endian ELF.' >&2; exit 2; }
 done
# An audit generated from the exact target stage must travel with the package.
[ -f "$base/STAGE-SHA256SUMS.txt" ] && [ ! -L "$base/STAGE-SHA256SUMS.txt" ] || { echo 'Missing audited stage checksums.' >&2; exit 2; }
(cd "$base" && sha256sum -c STAGE-SHA256SUMS.txt) || { echo 'Staged bytes changed after audit.' >&2; exit 2; }
umask 077
mkdir "$version"
trap 'echo "Provisioning interrupted; retain and inspect the partial version before retrying." >&2' HUP INT TERM
for name in synergy-daemon synergy-tvctl; do cp "$base/$name" "$version/$name"; chmod 0700 "$version/$name"; done
mkdir "$version/lib"
if [ -d "$base/lib" ]; then
  [ ! -L "$base/lib" ] || exit 2
  for lib in "$base"/lib/*; do
    [ -e "$lib" ] || continue
    [ -f "$lib" ] && [ ! -L "$lib" ] || { echo 'Runtime files must be regular, dereferenced files.' >&2; exit 2; }
    name=${lib##*/}
    case "$name" in libjson-c.so*|libstdc++.so*|libgcc_s.so*|libSDL2*.so*|libfreetype.so*|libpng*.so*|libz.so*|libbrotli*.so*|libbz2.so*|libharfbuzz.so*) ;;
      *) echo "Unapproved bundled library: $name" >&2; exit 2;; esac
    cp "$lib" "$version/lib/$name"; chmod 0600 "$version/lib/$name"
  done
fi
launcher=$root/start-native.sh
# noclobber prevents replacing a concurrently provisioned launcher, including a symlink.
(set -C; cat > "$launcher" <<EOF
#!/bin/sh
# synergy-native provisioned launcher
exec env -i PATH=/usr/bin:/bin HOME=/var/lib/webosbrew LD_LIBRARY_PATH=$version/lib $version/synergy-daemon --runtime-dir $root --controller-uid $uid "\$@"
EOF
)
chmod 0700 "$launcher"
printf '\nProvisioned; nothing was started or enabled at boot.\n'
printf 'Review legacy client/hooks first, then start in a separate root shell:\n  %s\n' "$launcher"
printf 'Inspect from root:\n  %s/synergy-tvctl --runtime-dir %s status\n' "$version" "$root"
printf 'Confirm the native app jail can reach control.sock before enabling real input.\n'
