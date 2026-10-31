#!/bin/sh
# Legacy compatibility wrapper for the old /var/lib/webosbrew init hook.
# The real control script and client live in client/.
set -u
BASE="$(CDPATH= cd "$(dirname "$0")" 2>/dev/null && pwd)"
exec sh "$BASE/client/tvctl.sh" "$@"
