#!/bin/sh
# Read-only, bounded reconnaissance. Execute through existing authorized SSH.
# No installs, settings writes, mounts, app launches, input injection or restarts.
set -u
PATH=/usr/bin:/bin:/usr/sbin:/sbin
LC_ALL=C
export PATH LC_ALL
section() { printf '\n===SYNERGY-GEAR-PROBE:%s===\n' "$1"; }
section identity
printf 'uid='; id -u
printf 'kernel='; uname -sr
printf 'architecture='; uname -m
printf 'probe_schema=1\n'
read_service() {
    section "$1"
    if [ ! -x /usr/bin/luna-send ]; then
        printf 'UNAVAILABLE: luna-send\n'
    elif ! command -v timeout >/dev/null 2>&1; then
        printf 'SKIPPED: timeout unavailable; refusing an unbounded service call\n'
    else
        # -w is luna-send's own deadline; timeout also bounds the child process.
        # A denied/unknown method is useful evidence, never a reason to change ACLs.
        timeout 3 /usr/bin/luna-send -n 1 -w 1800 "$2" "$3" 2>&1 | head -c 16384
        printf '\n'
    fi
}
read_service system-info luna://com.webos.service.tv.systemproperty/getSystemInfo '{"keys":["modelName","firmwareVersion","sdkVersion"]}'
read_service focused-window luna://com.webos.surfacemanager/getFocusedWindowInfo '{}'
read_service foreground-window luna://com.webos.surfacemanager/getForegroundWindowInfo '{}'
read_service foreground-app luna://com.webos.applicationManager/getForegroundAppInfo '{}'
read_service key-filter-config luna://com.webos.service.config/getConfigs '{"configNames":["com.webos.surfacemanager.keyFilters","com.webos.surfacemanager.keyFiltersCpp"]}'
section files
# Discovery leads only: paths are NOT assumed to exist on any particular TV.
# No symlink following; each directory scan is bounded in time and file count.
for base in \
    /usr/palm/applications/com.webos.app.quicksettings \
    /usr/palm/applications/com.palm.app.settings \
    /usr/palm/applications/com.webos.app.settings \
    /media/system/apps/usr/palm/applications/com.webos.app.quicksettings \
    /media/system/apps/usr/palm/applications/com.palm.app.settings \
    /media/system/apps/usr/palm/applications/com.webos.app.settings \
    /usr/lib/qt5/qml/KeyFilters \
    /usr/share/weston; do
    if [ ! -d "$base" ]; then printf 'ABSENT\t%s\n' "$base"; continue; fi
    printf 'DIRECTORY\t%s\n' "$base"
    if ! command -v timeout >/dev/null 2>&1; then printf 'SKIP\tno timeout\n'; continue; fi
    timeout 3 find "$base" -maxdepth 4 -type f 2>/dev/null | head -n 64 | while IFS= read -r file; do
        # File contents, account databases, URLs and LG code are not exported.
        # Fingerprint only small UI/config source candidates, not all images/binaries.
        case "$file" in *.qml|*.js|*.json|*.xml|*.ini|*.conf|*.rcc|*.so|*.pak) ;;
            *) continue ;; esac
        size=$(wc -c < "$file" 2>/dev/null) || continue
        case "$size" in *[!0-9\ ]*|'') continue ;; esac
        digest=not-hashed
        if [ "$size" -le 1048576 ] && command -v sha256sum >/dev/null 2>&1; then
            digest=$(timeout 2 sha256sum "$file" 2>/dev/null | cut -d ' ' -f 1)
            [ -n "$digest" ] || digest=hash-unavailable
        fi
        printf 'FILE\t%s\t%s\t%s\n' "$size" "$digest" "$file"
    done
    printf 'SCAN_LIMIT\tmaxdepth=4; first=64 files; partial discovery, not a complete inventory\n'
done
section complete
printf 'READ_ONLY=1\nTV_MENU_INTEGRATION_VERIFIED=0\n'
