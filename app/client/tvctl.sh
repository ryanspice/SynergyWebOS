#!/bin/sh
# SynergyWebOS client control for rooted LG webOS TVs.
# Runs the local Python client, its startup hook and the read-only profile bridge.
set -u

APP_ID="dev.canopy.synergywebos"
APP_VERSION="0.3.4"
BASE_DIR="$(dirname "$0")"
BASE="$(CDPATH= cd "$BASE_DIR" 2>/dev/null && pwd)"

DATA_DIR="/var/lib/webosbrew"
PROFILE_DIR="$DATA_DIR/junction"
LOG_FILE="$DATA_DIR/junction.log"
PID_FILE="$DATA_DIR/junction.pid"
STATE_FILE="$DATA_DIR/junction-state.json"
CONFIG_FILE="$DATA_DIR/junction.conf"
INIT_FILE="$DATA_DIR/init.d/60-junction"
PROFILE_FILE="$PROFILE_DIR/handoff-profile.json"

# Read-only migration awareness for the previous app's runtime files. The new
# control script uses SynergyWebOS names but still understands the old autostart
# and configuration for an in-place upgrade.
LEGACY_CONFIG="$DATA_DIR/synergy-webos.conf"
LEGACY_INIT="$DATA_DIR/init.d/60-synergy-webos"
LEGACY_PID="$DATA_DIR/synergy-webos.pid"
LEGACY_STATE="$DATA_DIR/synergy-webos-state.json"

find_python() {
    for p in /opt/bin/python3 /usr/bin/python3 /usr/local/bin/python3 python3; do
        if command -v "$p" >/dev/null 2>&1; then
            command -v "$p"
            return 0
        fi
        if [ -x "$p" ]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

quote_sh() {
    printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

ensure_uinput() {
    [ -e /dev/uinput ] && return 0
    minor="$(awk '$2 == "uinput" { print $1; exit }' /proc/misc 2>/dev/null)"
    [ -n "$minor" ] || {
        echo "uinput is not present in /proc/misc" >&2
        return 1
    }
    mknod /dev/uinput c 10 "$minor" 2>/dev/null || return 1
    chmod 660 /dev/uinput 2>/dev/null || true
    chgrp input /dev/uinput 2>/dev/null || true
    [ -e /dev/uinput ]
}

current_pid() {
    [ -f "$PID_FILE" ] || return 1
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    case "$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)" in
        *"$BASE/client.py"*) echo "$pid"; return 0 ;;
        *) return 1 ;;
    esac
}

find_client_pid() {
    for pid in $(client_pids); do
        [ -n "$pid" ] || continue
        echo "$pid"
        return 0
    done
    return 1
}

client_pids() {
    ps -ef 2>/dev/null | grep -F "$BASE/client.py" | grep -v grep | awk '{ print $2 }'
}

is_running() {
    pid="$(current_pid 2>/dev/null || find_client_pid 2>/dev/null || true)"
    [ -n "$pid" ]
}

active_pid() {
    pid="$(current_pid 2>/dev/null || find_client_pid 2>/dev/null || true)"
    [ -n "$pid" ] || return 1
    echo "$pid"
}

status() {
    if is_running; then
        pid="$(active_pid)"
        echo "running pid=$pid"
        if [ -f "$STATE_FILE" ]; then
            cat "$STATE_FILE"
        elif [ -f "$LEGACY_STATE" ]; then
            cat "$LEGACY_STATE"
        fi
        return 0
    fi
    echo "stopped"
    return 1
}

stop_client() {
    killed=0
    if is_running; then
        pid="$(active_pid 2>/dev/null || true)"
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null || true
            killed=1
            i=0
            while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 3 ]; do
                sleep 1
                i=$((i + 1))
            done
            if kill -0 "$pid" 2>/dev/null; then
                kill -9 "$pid" 2>/dev/null || true
            fi
        fi
    fi
    for pid in $(client_pids); do
        kill "$pid" 2>/dev/null || true
        killed=1
    done
    rm -f "$PID_FILE" "$STATE_FILE"
    if [ "$killed" -eq 1 ]; then
        echo "stopped"
    else
        echo "already stopped"
    fi
    return 0
}

validate_host() {
    case "$1" in
        ""|*[!A-Za-z0-9._:-]*) return 1 ;;
    esac
    return 0
}

validate_port() {
    case "$1" in
        ""|*[!0-9]*) return 1 ;;
    esac
    [ "$1" -ge 1 ] 2>/dev/null && [ "$1" -le 65535 ] 2>/dev/null
}

validate_name() {
    case "$1" in
        ""|*[!A-Za-z0-9._-]*) return 1 ;;
    esac
    return 0
}

validate_dims() {
    case "$1" in ""|*[!0-9]*) return 1 ;; esac
    case "$2" in ""|*[!0-9]*) return 1 ;; esac
    [ "$1" -ge 1 ] 2>/dev/null && [ "$1" -le 65535 ] 2>/dev/null &&
        [ "$2" -ge 1 ] 2>/dev/null && [ "$2" -le 65535 ] 2>/dev/null
}

validate_home_target() {
    case "$1" in system|off) return 0 ;; *) return 1 ;; esac
}

validate_home_delay() {
    case "$1" in ""|*[!0-9]*) return 1 ;; esac
    [ "$1" -ge 0 ] 2>/dev/null && [ "$1" -le 5000 ] 2>/dev/null
}

reap_duplicates() {
    current="$(current_pid 2>/dev/null || true)"
    for pid in $(client_pids); do
        [ "$pid" = "$current" ] && continue
        kill "$pid" 2>/dev/null || true
    done
}

start_client() {
    server="${1:-}"
    port="${2:-24800}"
    name="${3:-webos-tv}"
    mode="${4:-real}"
    width="${5:-1920}"
    height="${6:-1080}"
    home_target="${7:-system}"
    home_delay_ms="${8:-0}"

    validate_host "$server" || { echo "invalid server/host" >&2; return 2; }
    validate_port "$port" || { echo "invalid port" >&2; return 2; }
    validate_name "$name" || { echo "invalid screen name" >&2; return 2; }
    validate_dims "$width" "$height" || { echo "invalid width/height" >&2; return 2; }
    validate_home_target "$home_target" || { echo "invalid home target" >&2; return 2; }
    validate_home_delay "$home_delay_ms" || { echo "invalid home delay" >&2; return 2; }
    case "$mode" in real|dry) ;; *) echo "invalid mode" >&2; return 2 ;; esac

    if is_running; then
        echo "already running pid=$(active_pid)"
        return 0
    fi

    if [ "$mode" = "real" ]; then
        ensure_uinput || echo "warning: /dev/uinput is unavailable; real input may fail" >&2
    fi
    reap_duplicates

    py="$(find_python)" || {
        echo "Python 3 not found. Install Python 3 (for example through Entware) or use a native backend build." >&2
        return 3
    }

    mkdir -p "$DATA_DIR" "$PROFILE_DIR" 2>/dev/null || true
    : > "$LOG_FILE" 2>/dev/null || true
    rm -f "$STATE_FILE"

    dry=""
    [ "$mode" = "dry" ] && dry="--dry-run"

    # shellcheck disable=SC2086
    nohup "$py" "$BASE/client.py" \
        --server "$server" --port "$port" --name "$name" \
        --width "$width" --height "$height" \
        --home-target "$home_target" --home-delay-ms "$home_delay_ms" $dry \
        >> "$LOG_FILE" 2>&1 </dev/null &
    pid=$!
    echo "$pid" > "$PID_FILE"
    sleep 1
    if kill -0 "$pid" 2>/dev/null; then
        echo "started pid=$pid mode=$mode log=$LOG_FILE"
        return 0
    fi
    echo "client exited during startup" >&2
    tail -n 40 "$LOG_FILE" 2>/dev/null || true
    rm -f "$PID_FILE"
    return 4
}

restart_client() {
    stop_client >/dev/null 2>&1 || true
    start_client "$@"
}

save_config() {
    server="${1:-}"
    port="${2:-24800}"
    name="${3:-webos-tv}"
    mode="${4:-real}"
    width="${5:-1920}"
    height="${6:-1080}"
    home_target="${7:-system}"
    home_delay_ms="${8:-0}"

    validate_host "$server" || { echo "invalid server/host" >&2; return 2; }
    validate_port "$port" || { echo "invalid port" >&2; return 2; }
    validate_name "$name" || { echo "invalid screen name" >&2; return 2; }
    validate_dims "$width" "$height" || { echo "invalid width/height" >&2; return 2; }
    validate_home_target "$home_target" || { echo "invalid home target" >&2; return 2; }
    validate_home_delay "$home_delay_ms" || { echo "invalid home delay" >&2; return 2; }
    case "$mode" in real|dry) ;; *) echo "invalid mode" >&2; return 2 ;; esac

    mkdir -p "$DATA_DIR" || { echo "$DATA_DIR missing" >&2; return 3; }
    {
        echo "SERVER=$(quote_sh "$server")"
        echo "PORT=$(quote_sh "$port")"
        echo "NAME=$(quote_sh "$name")"
        echo "MODE=$(quote_sh "$mode")"
        echo "WIDTH=$(quote_sh "$width")"
        echo "HEIGHT=$(quote_sh "$height")"
        echo "HOME_TARGET=$(quote_sh "$home_target")"
        echo "HOME_DELAY_MS=$(quote_sh "$home_delay_ms")"
    } > "$CONFIG_FILE"
    chmod 600 "$CONFIG_FILE" 2>/dev/null || true
    echo "config saved: $CONFIG_FILE"
}

load_config() {
    if [ -f "$CONFIG_FILE" ]; then
        # Generated by this script only; values are shell-quoted.
        # shellcheck disable=SC1090
        . "$CONFIG_FILE"
        return 0
    fi
    if [ -f "$LEGACY_CONFIG" ]; then
        # Remove the old file's optional home keys by supplying defaults first.
        HOME_TARGET="system"
        HOME_DELAY_MS="0"
        # shellcheck disable=SC1090
        . "$LEGACY_CONFIG"
        return 0
    fi
    return 1
}

start_from_config() {
    load_config || { echo "autostart config missing" >&2; return 2; }
    start_client "$SERVER" "$PORT" "$NAME" "${MODE:-real}" "$WIDTH" "$HEIGHT" \
        "${HOME_TARGET:-system}" "${HOME_DELAY_MS:-0}"
}

autostart_status() {
    new=0
    legacy=0
    [ -f "$INIT_FILE" ] && new=1
    [ -f "$LEGACY_INIT" ] && legacy=1
    if [ "$new" -eq 1 ] || [ "$legacy" -eq 1 ]; then
        echo "enabled"
        [ "$legacy" -eq 1 ] && echo "legacy=$LEGACY_INIT"
        return 0
    fi
    echo "disabled"
    return 1
}

save_autostart() {
    save_config "$@" || return $?
    mkdir -p "$DATA_DIR/init.d" || { echo "cannot create init.d" >&2; return 3; }

    # Quarantine the previous app's generated startup hook so two clients do not
    # race at boot. It is renamed, not deleted, and the action is printed.
    if [ -f "$LEGACY_INIT" ]; then
        if grep -q "synergy-webos" "$LEGACY_INIT" 2>/dev/null; then
            stamp="$(date +%Y%m%d-%H%M%S 2>/dev/null || echo manual)"
            if mv "$LEGACY_INIT" "$LEGACY_INIT.disabled-$stamp" 2>/dev/null; then
                echo "legacy autostart disabled: $LEGACY_INIT.disabled-$stamp"
            else
                echo "warning: could not disable legacy autostart $LEGACY_INIT" >&2
            fi
        fi
    fi

    {
        echo "#!/bin/sh"
        echo "# Generated by $APP_ID (SynergyWebOS $APP_VERSION)."
        echo "sleep 8"
        echo "sh \"$BASE/tvctl.sh\" start-from-config >> \"$LOG_FILE\" 2>&1 || true"
    } > "$INIT_FILE"
    chmod 755 "$INIT_FILE" 2>/dev/null || true
    echo "autostart enabled: $INIT_FILE"
}

disable_autostart() {
    rm -f "$INIT_FILE" "$LEGACY_INIT"
    echo "autostart disabled"
}

active_config() {
    pid="$(active_pid 2>/dev/null || true)"
    target="system"
    delay="0"
    if [ -n "$pid" ]; then
        cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
        parsed_target="$(printf '%s' "$cmdline" | sed -n 's/.*--home-target \([^ ]*\).*/\1/p')"
        parsed_delay="$(printf '%s' "$cmdline" | sed -n 's/.*--home-delay-ms \([^ ]*\).*/\1/p')"
        [ -n "$parsed_target" ] && target="$parsed_target"
        [ -n "$parsed_delay" ] && delay="$parsed_delay"
    elif load_config 2>/dev/null; then
        target="${HOME_TARGET:-system}"
        delay="${HOME_DELAY_MS:-0}"
    fi
    echo "home_target=$target"
    echo "home_delay_ms=$delay"
}

snapshot() {
    printf '===STATE===\n'
    status 2>&1 || true
    printf '===AUTOSTART===\n'
    autostart_status 2>&1 || true
    printf '===CONFIG===\n'
    active_config 2>&1 || true
    printf '===LOG===\n'
    if [ -f "$LOG_FILE" ]; then
        tail -n 120 "$LOG_FILE" 2>&1
    else
        echo "No client log yet."
    fi
}

probe() {
    py="$(find_python)" || { echo "Python 3 not found" >&2; exit 3; }
    exec "$py" "$BASE/probe.py"
}

save_profile() {
    profile="${1:-}"
    [ -n "$profile" ] || { echo "profile JSON is empty" >&2; return 2; }
    mkdir -p "$PROFILE_DIR" || { echo "cannot create $PROFILE_DIR" >&2; return 3; }
    printf '%s\n' "$profile" > "$PROFILE_FILE" || {
        echo "cannot write $PROFILE_FILE" >&2
        return 4
    }
    chmod 600 "$PROFILE_FILE" 2>/dev/null || true
    echo "profile saved: $PROFILE_FILE"
}

server_status() {
    host="${1:-}"
    port="${2:-24803}"
    validate_host "$host" || { echo "invalid server host" >&2; return 2; }
    validate_port "$port" || { echo "invalid server port" >&2; return 2; }

    url="http://$host:$port/v1/settings"
    if command -v curl >/dev/null 2>&1; then
        curl -fsS --max-time 4 "$url" || {
            echo "desktop settings request failed" >&2
            return 5
        }
        return 0
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -q -O - -T 4 "$url" || {
            echo "desktop settings request failed" >&2
            return 5
        }
        return 0
    fi
    echo "Neither curl nor wget is available on this TV" >&2
    return 6
}

print_usage() {
    cat <<EOF
SynergyWebOS $APP_VERSION

Usage: $0 COMMAND
  probe
  start SERVER [PORT] [NAME] [real|dry] [WIDTH] [HEIGHT] [HOME_TARGET] [HOME_MS]
  restart SERVER [PORT] [NAME] [real|dry] [WIDTH] [HEIGHT] [HOME_TARGET] [HOME_MS]
  stop
  status
  snapshot
  logs [LINES]
  configure SERVER [PORT] [NAME] [real|dry] [WIDTH] [HEIGHT] [HOME_TARGET] [HOME_MS]
  autostart-enable SERVER [PORT] [NAME] [real|dry] [WIDTH] [HEIGHT] [HOME_TARGET] [HOME_MS]
  autostart-disable
  autostart-status
  start-from-config
  profile-save JSON
  server-status HOST [PORT]
  version
EOF
}

case "${1:-}" in
    probe)
        probe
        ;;
    start)
        shift
        start_client "$@"
        ;;
    restart)
        shift
        restart_client "$@"
        ;;
    stop)
        stop_client
        ;;
    status)
        status
        ;;
    snapshot)
        snapshot
        ;;
    logs)
        if [ -f "$LOG_FILE" ]; then
            tail -n "${2:-120}" "$LOG_FILE"
        else
            echo "no log yet"
        fi
        ;;
    configure)
        shift
        save_config "$@"
        ;;
    autostart-enable)
        shift
        save_autostart "$@"
        ;;
    autostart-disable)
        disable_autostart
        ;;
    autostart-status)
        autostart_status
        ;;
    start-from-config)
        start_from_config
        ;;
    profile-save)
        shift
        save_profile "$@"
        ;;
    server-status)
        shift
        server_status "$@"
        ;;
    version)
        echo "SynergyWebOS $APP_VERSION"
        ;;
    *)
        print_usage
        exit 2
        ;;
esac
