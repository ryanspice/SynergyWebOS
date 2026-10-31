#!/bin/sh
# Legacy compatibility wrapper for the old /var/lib/webosbrew init hook.
# The real control script and client live in client/.
# This TV can restore an older /var/lib/webosbrew on reboot, so keep this
# wrapper in the app package: the old 60-synergy-webos hook calls it directly.
set -u
BASE="$(CDPATH= cd "$(dirname "$0")" 2>/dev/null && pwd)"
exec sh "$BASE/client/tvctl.sh" "$@"
