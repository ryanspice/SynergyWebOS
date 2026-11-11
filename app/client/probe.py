#!/usr/bin/env python3
"""SynergyWebOS capability probe.

Read-only with respect to input: it opens and immediately closes uinput when
accessible, but never creates a virtual input device and never injects an event.
"""

import json
import os
import platform
import sys

APP_NAME = "SynergyWebOS"
APP_VERSION = "0.3.4"
HOME_APP = "com.webos.app.home"
LUNA_SEND = "/usr/bin/luna-send"

UINPUT_CANDIDATES = ("/dev/uinput", "/dev/input/uinput")


def find_uinput():
    for candidate in UINPUT_CANDIDATES:
        if os.path.exists(candidate):
            return candidate
    return None


def main():
    uinput = find_uinput()
    can_open = False
    open_error = None
    if uinput:
        try:
            fd = os.open(uinput, os.O_WRONLY | os.O_NONBLOCK)
            os.close(fd)
            can_open = True
        except OSError as exc:
            open_error = "%s: %s" % (exc.__class__.__name__, exc)

    data = {
        "app": APP_NAME,
        "version": APP_VERSION,
        "uid": os.getuid() if hasattr(os, "getuid") else None,
        "euid": os.geteuid() if hasattr(os, "geteuid") else None,
        "is_root": bool(hasattr(os, "geteuid") and os.geteuid() == 0),
        "machine": platform.machine(),
        "platform": platform.platform(),
        "python": sys.version.split()[0],
        "python_executable": sys.executable,
        "uinput_path": uinput,
        "uinput_access": can_open,
        "uinput_error": open_error,
        "proc_input_devices": os.path.exists("/proc/bus/input/devices"),
        "webosbrew_root": os.path.isdir("/var/lib/webosbrew"),
        "home_app": HOME_APP,
        "home_launch": LUNA_SEND,
        "home_launch_exists": os.path.exists(LUNA_SEND),
    }
    print(json.dumps(data, indent=2, sort_keys=True))
    if not data["is_root"]:
        print("\nFAIL: backend is not running as root.")
        return 2
    if not uinput:
        print("\nFAIL: no /dev/uinput or /dev/input/uinput node.")
        return 3
    if not can_open:
        print("\nFAIL: uinput exists but cannot be opened for writing.")
        return 4
    print("\nPASS: root + writable uinput are available; real input injection can be attempted.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
