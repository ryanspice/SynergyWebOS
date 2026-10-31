#!/usr/bin/env python3
"""SynergyWebOS — Synergy protocol client for rooted LG webOS TVs.

No third-party Python packages are required. The client speaks the classic
Synergy 1.x wire protocol used by the Synergy core and injects keyboard/mouse
events through Linux uinput. The Windows/Meta key is consumed and launches the
TV's system Home app instead of a third-party launcher.

This is an independent compatibility client; it does not redistribute Synergy.
"""

import argparse
import fcntl
import json
import logging
import os
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from typing import Dict, Optional, Set, Tuple

APP_NAME = "SynergyWebOS"
APP_VERSION = "0.3.2"
DEFAULT_PORT = 24800
PROTOCOL_MAJOR = 1
PROTOCOL_MINOR = 4
STATE_FILE = "/var/lib/webosbrew/junction-state.json"
HOME_APP = "com.webos.app.home"
LUNA_SEND = "/usr/bin/luna-send"
HOME_LAUNCH_URI = "luna://com.webos.applicationManager/launch"

# Linux input constants (uapi/linux/input-event-codes.h)
EV_SYN = 0x00
EV_KEY = 0x01
EV_REL = 0x02
SYN_REPORT = 0
REL_X = 0x00
REL_Y = 0x01
REL_HWHEEL = 0x06
REL_WHEEL = 0x08
BTN_LEFT = 0x110
BTN_RIGHT = 0x111
BTN_MIDDLE = 0x112
BTN_SIDE = 0x113
BTN_EXTRA = 0x114
BUS_USB = 0x03

# _IOC constants from asm-generic/ioctl.h.
_IOC_NRBITS = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS
_IOC_NONE = 0
_IOC_WRITE = 1


def _ioc(direction: int, type_: int, nr: int, size: int) -> int:
    return ((direction << _IOC_DIRSHIFT) |
            (type_ << _IOC_TYPESHIFT) |
            (nr << _IOC_NRSHIFT) |
            (size << _IOC_SIZESHIFT))


def _io(type_: int, nr: int) -> int:
    return _ioc(_IOC_NONE, type_, nr, 0)


def _iow(type_: int, nr: int, size: int) -> int:
    return _ioc(_IOC_WRITE, type_, nr, size)


UINPUT_IOCTL_BASE = ord("U")
UI_DEV_CREATE = _io(UINPUT_IOCTL_BASE, 1)
UI_DEV_DESTROY = _io(UINPUT_IOCTL_BASE, 2)
UI_SET_EVBIT = _iow(UINPUT_IOCTL_BASE, 100, struct.calcsize("i"))
UI_SET_KEYBIT = _iow(UINPUT_IOCTL_BASE, 101, struct.calcsize("i"))
UI_SET_RELBIT = _iow(UINPUT_IOCTL_BASE, 102, struct.calcsize("i"))

# Common Linux key codes for extended Set-1 scan codes used in Synergy's
# keyButton field. Plain scan codes 1..127 line up with Linux evdev key codes.
EXTENDED_SCANCODES: Dict[int, int] = {
    0xE01C: 96,   # KEY_KPENTER
    0xE01D: 97,   # KEY_RIGHTCTRL
    0xE035: 98,   # KEY_KPSLASH
    0xE037: 99,   # KEY_SYSRQ / PrintScreen (best effort)
    0xE038: 100,  # KEY_RIGHTALT
    0xE047: 102,  # KEY_HOME
    0xE048: 103,  # KEY_UP
    0xE049: 104,  # KEY_PAGEUP
    0xE04B: 105,  # KEY_LEFT
    0xE04D: 106,  # KEY_RIGHT
    0xE04F: 107,  # KEY_END
    0xE050: 108,  # KEY_DOWN
    0xE051: 109,  # KEY_PAGEDOWN
    0xE052: 110,  # KEY_INSERT
    0xE053: 111,  # KEY_DELETE
    0xE05B: 125,  # KEY_LEFTMETA
    0xE05C: 126,  # KEY_RIGHTMETA
    0xE05D: 127,  # KEY_COMPOSE
}

# Windows sends the Windows key as Super (kKeySuper_L/R, 0xEFEB/0xEFEC);
# some Synergy configurations send the Meta aliases instead. Both are consumed
# as the same pair of HOME trigger keys rather than being injected.
HOME_KEY_IDS: Dict[int, int] = {
    0xEFE7: 125,  # kKeyMeta_L
    0xEFE8: 126,  # kKeyMeta_R
    0xEFEB: 125,  # kKeySuper_L (Windows key)
    0xEFEC: 126,  # kKeySuper_R (right Windows key)
}
ALT_KEY_IDS: Dict[int, int] = {
    0xEFE9: 56,   # kKeyAlt_L -> KEY_LEFTALT
    0xEFEA: 100,  # kKeyAlt_R -> KEY_RIGHTALT
}
# Windows sends some non-character keys with keyButton 0 and a KeyID only.
SPECIAL_KEY_IDS: Dict[int, int] = {
    0xEF09: 15,   # kKeyTab -> KEY_TAB
}
ALT_KEY_CODES = (56, 100)
ALT_HOLD_MS = 5.0
TAB_KEY = 15
APP_SWITCH_RUNNING_URI = "luna://com.webos.applicationManager/dev/running"
APP_SWITCH_FOREGROUND_URI = "luna://com.webos.applicationManager/getForegroundAppInfo"
APP_SWITCH_EXCLUDE = {"com.webos.app.home"}

# Fallback for the uncommon case where a server sends no usable keyButton.
ASCII_TO_EVDEV: Dict[int, int] = {
    ord("1"): 2, ord("2"): 3, ord("3"): 4, ord("4"): 5, ord("5"): 6,
    ord("6"): 7, ord("7"): 8, ord("8"): 9, ord("9"): 10, ord("0"): 11,
    ord("-"): 12, ord("="): 13, 8: 14, 9: 15,
    ord("q"): 16, ord("w"): 17, ord("e"): 18, ord("r"): 19, ord("t"): 20,
    ord("y"): 21, ord("u"): 22, ord("i"): 23, ord("o"): 24, ord("p"): 25,
    ord("["): 26, ord("]"): 27, 13: 28,
    ord("a"): 30, ord("s"): 31, ord("d"): 32, ord("f"): 33, ord("g"): 34,
    ord("h"): 35, ord("j"): 36, ord("k"): 37, ord("l"): 38,
    ord(";"): 39, ord("'"): 40, ord("`"): 41, ord("\\"): 43,
    ord("z"): 44, ord("x"): 45, ord("c"): 46, ord("v"): 47, ord("b"): 48,
    ord("n"): 49, ord("m"): 50, ord(","): 51, ord("."): 52, ord("/"): 53,
    ord(" "): 57,
}
# Uppercase chars share physical keys with lowercase chars.
for _char in range(ord("A"), ord("Z") + 1):
    ASCII_TO_EVDEV[_char] = ASCII_TO_EVDEV[ord(chr(_char).lower())]


class InputBackend:
    def key(self, code: int, value: int) -> None:
        raise NotImplementedError

    def move(self, dx: int, dy: int) -> None:
        raise NotImplementedError

    def button(self, code: int, value: int) -> None:
        raise NotImplementedError

    def wheel(self, horizontal: int, vertical: int) -> None:
        raise NotImplementedError

    def close(self) -> None:
        pass


class DryRunBackend(InputBackend):
    def key(self, code: int, value: int) -> None:
        logging.info("INPUT key code=%d value=%d", code, value)

    def move(self, dx: int, dy: int) -> None:
        logging.info("INPUT move dx=%d dy=%d", dx, dy)

    def button(self, code: int, value: int) -> None:
        logging.info("INPUT button code=%d value=%d", code, value)

    def wheel(self, horizontal: int, vertical: int) -> None:
        logging.info("INPUT wheel horizontal=%d vertical=%d", horizontal, vertical)


class UInputDevice:
    def __init__(self, path: str, name: str, key_codes=None, rel_codes=None) -> None:
        self.fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
        try:
            if key_codes:
                fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
                for code in key_codes:
                    fcntl.ioctl(self.fd, UI_SET_KEYBIT, int(code))
            if rel_codes:
                fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_REL)
                for code in rel_codes:
                    fcntl.ioctl(self.fd, UI_SET_RELBIT, int(code))

            name_bytes = name.encode("utf-8")[:79]
            name_field = name_bytes + b"\0" * (80 - len(name_bytes))
            # struct uinput_user_dev: name[80], input_id, ff_effects_max,
            # then four arrays of ABS_CNT (64) signed ints.
            descriptor = struct.pack(
                "80sHHHHI" + ("i" * 256),
                name_field,
                BUS_USB,
                0x1D6B,  # Linux Foundation vendor ID; harmless virtual-device marker.
                0x0104,
                1,
                0,
                *([0] * 256)
            )
            os.write(self.fd, descriptor)
            fcntl.ioctl(self.fd, UI_DEV_CREATE)
            time.sleep(0.05)
        except Exception:
            os.close(self.fd)
            self.fd = -1
            raise

    def emit(self, type_: int, code: int, value: int, sync: bool = True) -> None:
        now = time.time()
        sec = int(now)
        usec = int((now - sec) * 1000000)
        os.write(self.fd, struct.pack("llHHi", sec, usec, type_, code, value))
        if sync:
            os.write(self.fd, struct.pack("llHHi", sec, usec, EV_SYN, SYN_REPORT, 0))

    def close(self) -> None:
        if self.fd < 0:
            return
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        except OSError:
            pass
        try:
            os.close(self.fd)
        finally:
            self.fd = -1


class UInputBackend(InputBackend):
    def __init__(self) -> None:
        path = self._find_uinput()
        logging.info("Using uinput device %s", path)
        # Enable a broad but bounded keyboard range. This is simpler and more
        # compatible across TV kernels than changing capabilities at runtime.
        self.keyboard = UInputDevice(path, "SynergyWebOS Keyboard", range(1, 256), None)
        self.mouse = UInputDevice(
            path,
            "SynergyWebOS Mouse",
            [BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA],
            [REL_X, REL_Y, REL_WHEEL, REL_HWHEEL],
        )

    @staticmethod
    def _find_uinput() -> str:
        for candidate in ("/dev/uinput", "/dev/input/uinput"):
            if os.path.exists(candidate):
                return candidate
        raise RuntimeError(
            "uinput device not found (/dev/uinput or /dev/input/uinput). "
            "Run Probe TV in the SynergyWebOS app. This kernel may need a different input backend."
        )

    def key(self, code: int, value: int) -> None:
        if 0 < code < 256:
            self.keyboard.emit(EV_KEY, code, value)
        else:
            logging.warning("Skipping unsupported Linux key code %d", code)

    def move(self, dx: int, dy: int) -> None:
        if dx:
            self.mouse.emit(EV_REL, REL_X, int(dx), sync=False)
        if dy:
            self.mouse.emit(EV_REL, REL_Y, int(dy), sync=False)
        if dx or dy:
            self.mouse.emit(EV_SYN, SYN_REPORT, 0, sync=False)

    def button(self, code: int, value: int) -> None:
        self.mouse.emit(EV_KEY, code, value)

    def wheel(self, horizontal: int, vertical: int) -> None:
        if horizontal:
            self.mouse.emit(EV_REL, REL_HWHEEL, int(horizontal), sync=False)
        if vertical:
            self.mouse.emit(EV_REL, REL_WHEEL, int(vertical), sync=False)
        if horizontal or vertical:
            self.mouse.emit(EV_SYN, SYN_REPORT, 0, sync=False)

    def close(self) -> None:
        self.keyboard.close()
        self.mouse.close()


class SynergyClient:
    def __init__(self, host: str, port: int, name: str, width: int, height: int,
                 backend: InputBackend, once: bool = False,
                 home_target: str = "system", home_delay_ms: int = 0,
                 home_dry_run: bool = False) -> None:
        self.host = host
        self.port = port
        self.name = name
        self.width = width
        self.height = height
        self.backend = backend
        self.once = once
        self.sock: Optional[socket.socket] = None
        self.running = True
        self.active = False
        self.sequence = 0
        self.last_mouse: Optional[Tuple[int, int]] = None
        self.pressed_keys: Set[int] = set()
        self.pressed_buttons: Set[int] = set()
        self.protocol_name = b"Synergy"

        # Meta/Windows handling. Terra 3's home patch is folded in here so the
        # app's persisted setting can be passed as a normal client argument.
        self.home_target = home_target
        self.home_delay_ms = home_delay_ms
        self.home_dry_run = home_dry_run
        self.home_down: Set[int] = set()
        self.home_timer: Optional[threading.Timer] = None
        self.home_lock = threading.Lock()
        self.home_launching = False

        # Holding either Alt key for ALT_HOLD_MS opens the system Home screen.
        # Alt is still injected for normal shortcuts until the hold fires.
        self.alt_down: Set[int] = set()
        self.alt_timer: Optional[threading.Timer] = None
        self.alt_released = False

        # Alt+Tab cycles the currently running TV apps without animation.
        # The cycle list is refreshed when a new Alt hold starts and is reused
        # for repeated Tab presses during the same hold.
        self.tab_consumed = False
        self.cycle_list: list = []
        self.cycle_index = -1
        self.cycle_at = 0.0
        self.cycle_lock = threading.Lock()

    def _launch_home(self) -> None:
        params = json.dumps({"id": HOME_APP}, separators=(",", ":"))
        try:
            result = subprocess.run(
                [LUNA_SEND, "-n", "1", "-f", HOME_LAUNCH_URI, params],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=2.5,
                check=False,
            )
            if result.returncode == 0:
                logging.info("System HOME launch requested")
            else:
                logging.error("System HOME launch failed: luna-send exit %s", result.returncode)
        except (OSError, subprocess.SubprocessError) as exc:
            logging.error("System HOME launch failed: %s", exc)
        finally:
            with self.home_lock:
                self.home_launching = False

    def _home_fire(self) -> None:
        with self.home_lock:
            if not self.home_down or not self.running:
                return
            if self.home_dry_run:
                logging.info("DRY RUN: HOME action")
                return
            if self.home_launching:
                return
            self.home_launching = True
        # Launch off the protocol reader so a slow luna-send cannot stall input.
        threading.Thread(target=self._launch_home, daemon=True).start()

    def _home_key(self, code: int, down: bool) -> None:
        fire_now = False
        with self.home_lock:
            if down:
                if code in self.home_down:
                    return
                first = not self.home_down
                self.home_down.add(code)
                if first and self.home_target == "system":
                    if self.home_delay_ms <= 0:
                        fire_now = True
                    else:
                        self.home_timer = threading.Timer(
                            self.home_delay_ms / 1000.0, self._home_fire)
                        self.home_timer.daemon = True
                        self.home_timer.start()
            else:
                self.home_down.discard(code)
                if not self.home_down and self.home_timer:
                    self.home_timer.cancel()
                    self.home_timer = None
        if fire_now:
            self._home_fire()

    def _alt_fire(self) -> None:
        with self.home_lock:
            if not self.alt_down or not self.running:
                return
            self.alt_timer = None
            # Release the injected modifier before Home takes focus so the TV
            # does not keep a stuck Alt state after the hold is consumed.
            for code in list(self.alt_down):
                try:
                    self.backend.key(code, 0)
                except Exception:
                    pass
                self.pressed_keys.discard(code)
            self.alt_released = True
            if self.home_dry_run:
                logging.info("DRY RUN: ALT-HOME action")
                return
            if self.home_launching:
                return
            self.home_launching = True
        logging.info("Alt held for %.0fs; opening system HOME", ALT_HOLD_MS)
        threading.Thread(target=self._launch_home, daemon=True).start()

    def _alt_key(self, code: int, down: bool) -> None:
        inject = True
        with self.home_lock:
            if down:
                if code in self.alt_down:
                    return
                if self.alt_released:
                    self.alt_released = False
                first = not self.alt_down
                self.alt_down.add(code)
                if first:
                    self.tab_consumed = False
                    with self.cycle_lock:
                        self.cycle_list = []
                        self.cycle_index = -1
                if self.alt_timer is None and self.running:
                    self.alt_timer = threading.Timer(ALT_HOLD_MS, self._alt_fire)
                    self.alt_timer.daemon = True
                    self.alt_timer.start()
            else:
                self.alt_down.discard(code)
                if not self.alt_down:
                    if self.alt_timer:
                        self.alt_timer.cancel()
                        self.alt_timer = None
                    if self.alt_released:
                        inject = False
                        self.alt_released = False
                    self.tab_consumed = False
                    with self.cycle_lock:
                        self.cycle_list = []
                        self.cycle_index = -1
                elif self.alt_released:
                    inject = False
        if inject:
            self.backend.key(code, 1 if down else 0)
            if down:
                self.pressed_keys.add(code)
            else:
                self.pressed_keys.discard(code)

    def _alt_tab(self, reverse: bool) -> bool:
        with self.home_lock:
            if not self.alt_down or not self.running:
                return False
            # A Tab press means the user is switching apps, not asking for the
            # five-second Home hold.
            if self.alt_timer:
                self.alt_timer.cancel()
                self.alt_timer = None
            self.tab_consumed = True
        threading.Thread(target=self._cycle_app, args=(reverse,), daemon=True).start()
        return True

    def _luna_json(self, uri: str) -> dict:
        result = subprocess.run(
            [LUNA_SEND, "-n", "1", "-f", uri, "{}"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=2.0,
            check=False,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError("luna-send exit %s" % result.returncode)
        return json.loads(result.stdout)

    def _launch_app(self, app_id: str) -> bool:
        params = json.dumps({"id": app_id}, separators=(",", ":"))
        result = subprocess.run(
            [LUNA_SEND, "-n", "1", "-f", HOME_LAUNCH_URI, params],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=2.5,
            check=False,
        )
        if result.returncode != 0:
            logging.error("ALT-TAB launch failed: luna-send exit %s", result.returncode)
            return False
        return True

    def _cycle_app(self, reverse: bool) -> None:
        try:
            target = None
            with self.cycle_lock:
                now = time.time()
                if not self.cycle_list or now - self.cycle_at > 5.0:
                    running = self._luna_json(APP_SWITCH_RUNNING_URI)
                    foreground = self._luna_json(APP_SWITCH_FOREGROUND_URI)
                    items = running.get("running") if isinstance(running, dict) else None
                    ids = []
                    for item in items or []:
                        if not isinstance(item, dict):
                            continue
                        app_id = item.get("id") or item.get("appId")
                        if app_id and app_id not in APP_SWITCH_EXCLUDE and app_id not in ids:
                            ids.append(app_id)
                    if not ids:
                        logging.warning("ALT-TAB: no running apps reported")
                        return
                    current = foreground.get("appId") if isinstance(foreground, dict) else None
                    self.cycle_list = ids
                    self.cycle_index = ids.index(current) if current in ids else -1
                    self.cycle_at = now
                if len(self.cycle_list) == 1:
                    target = self.cycle_list[0]
                else:
                    step = -1 if reverse else 1
                    self.cycle_index = (self.cycle_index + step) % len(self.cycle_list)
                    target = self.cycle_list[self.cycle_index]
                self.cycle_at = now
            logging.info("ALT-TAB -> %s", target)
            self._launch_app(target)
        except (OSError, subprocess.SubprocessError, ValueError, json.JSONDecodeError) as exc:
            logging.error("ALT-TAB failed: %s", exc)

    def write_state(self, state: str) -> None:
        try:
            tmp = STATE_FILE + "." + str(os.getpid())
            with open(tmp, "w") as output:
                json.dump({
                    "state": state,
                    "pid": os.getpid(),
                    "server": self.host,
                    "port": self.port,
                    "screen": self.name,
                    "app": APP_NAME,
                    "version": APP_VERSION,
                }, output)
            os.replace(tmp, STATE_FILE)
        except OSError as exc:
            logging.warning("Unable to update connection status: %s", exc)

    def stop(self) -> None:
        self.running = False
        self._release_all()
        if self.sock:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def run(self) -> int:
        completed_session = False
        while self.running:
            try:
                self.write_state("connecting")
                self._run_session()
                completed_session = True
            except (ConnectionError, OSError, ValueError) as exc:
                if self.running:
                    logging.warning("Connection ended: %s", exc)
            finally:
                self.write_state("disconnected" if self.running else "stopped")
                self._release_all()
                self.active = False
                self.last_mouse = None
                if self.sock:
                    try:
                        self.sock.close()
                    except OSError:
                        pass
                    self.sock = None
            if self.once and completed_session:
                break
            if self.once and not self.running:
                break
            if self.running:
                time.sleep(1.0)
        return 0

    def _run_session(self) -> None:
        logging.info("Connecting to %s:%d as %s", self.host, self.port, self.name)
        sock = socket.create_connection((self.host, self.port), timeout=5.0)
        sock.settimeout(5.0)
        self.sock = sock
        logging.info("TCP connected; waiting for Synergy handshake")

        first = self._recv_frame()
        if first and first[0] == 0x16 and len(first) > 2 and first[1] == 0x03:
            raise ValueError(
                "Server is speaking TLS. This build uses classic unencrypted Synergy TCP; "
                "disable Synergy encryption for this TV connection on a trusted LAN."
            )
        if not (first.startswith(b"Synergy") or first.startswith(b"Barrier")):
            raise ValueError("Unexpected server handshake: %r" % (first[:24],))
        if len(first) < 11:
            raise ValueError("Truncated Synergy handshake")

        self.protocol_name = b"Barrier" if first.startswith(b"Barrier") else b"Synergy"
        offset = len(self.protocol_name)
        server_major, server_minor = struct.unpack("!HH", first[offset:offset + 4])
        logging.info("Server protocol %s %d.%d", self.protocol_name.decode(), server_major, server_minor)
        if server_major != 1:
            raise ValueError("Unsupported Synergy protocol major %d" % server_major)

        reply = (self.protocol_name +
                 struct.pack("!HH", PROTOCOL_MAJOR, PROTOCOL_MINOR) +
                 self._pack_string(self.name.encode("utf-8")))
        self._send_frame(reply)
        logging.info("Protocol handshake complete")
        sock.settimeout(3.0)

        while self.running:
            try:
                payload = self._recv_frame()
            except socket.timeout:
                # Synergy normally sends CALV. Keep the connection unless the OS says it is dead.
                continue
            if not payload:
                raise ConnectionError("server closed connection")
            if self._handle(payload) is False:
                logging.info("Server requested disconnect")
                return

    @staticmethod
    def _pack_string(data: bytes) -> bytes:
        return struct.pack("!I", len(data)) + data

    def _recv_exact(self, size: int) -> bytes:
        assert self.sock is not None
        chunks = []
        remaining = size
        while remaining:
            chunk = self.sock.recv(remaining)
            if not chunk:
                raise ConnectionError("socket closed")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _recv_frame(self) -> bytes:
        header = self._recv_exact(4)
        # A TLS record begins 16 03 xx; classic Synergy framing begins with a
        # 32-bit payload length. Detect TLS before interpreting it as a huge frame.
        if header[0] == 0x16 and header[1] == 0x03:
            raise ValueError(
                "Server is speaking TLS. This build uses classic unencrypted Synergy TCP; "
                "disable Synergy encryption for this TV connection on a trusted LAN."
            )
        size = struct.unpack("!I", header)[0]
        if size > 4 * 1024 * 1024:
            raise ValueError("Refusing oversized protocol frame: %d bytes" % size)
        return self._recv_exact(size)

    def _send_frame(self, payload: bytes) -> None:
        assert self.sock is not None
        self.sock.sendall(struct.pack("!I", len(payload)) + payload)

    def _send_cmd(self, command: bytes, data: bytes = b"") -> None:
        self._send_frame(command + data)

    def _handle(self, payload: bytes):
        if len(payload) < 4:
            logging.warning("Ignoring short frame (%d bytes)", len(payload))
            return True
        cmd = payload[:4]
        data = payload[4:]
        logging.debug("RX %s len=%d", cmd.decode("ascii", "replace"), len(data))

        if cmd == b"QINF":
            # DINF: x, y, width, height, warp-zone, mouse-x, mouse-y.
            self._send_cmd(b"DINF", struct.pack(
                "!HHHHHHH", 0, 0, self.width, self.height, 0, 0, 0
            ))
            self.write_state("connected")
            return True

        if cmd in (b"CIAK", b"CROP"):
            return True

        if cmd == b"CINN":
            if len(data) >= 10:
                x, y, self.sequence, _mask = struct.unpack("!HHIH", data[:10])
                self.last_mouse = (x, y)
            self.active = True
            logging.info("Pointer entered TV screen")
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"COUT":
            logging.info("Pointer left TV screen")
            self.active = False
            self.last_mouse = None
            self._release_all()
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"DMMV" and len(data) >= 4:
            x, y = struct.unpack("!HH", data[:4])
            if self.last_mouse is not None:
                dx = int(x) - int(self.last_mouse[0])
                dy = int(y) - int(self.last_mouse[1])
                if dx or dy:
                    self.backend.move(dx, dy)
            self.last_mouse = (x, y)
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"DMRM" and len(data) >= 4:
            dx, dy = struct.unpack("!hh", data[:4])
            self.backend.move(dx, dy)
            self._send_cmd(b"CNOP")
            return True

        if cmd in (b"DMDN", b"DMUP") and data:
            button_code = self._mouse_button(data[0])
            if button_code:
                down = 1 if cmd == b"DMDN" else 0
                self.backend.button(button_code, down)
                if down:
                    self.pressed_buttons.add(button_code)
                else:
                    self.pressed_buttons.discard(button_code)
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"DMWM" and len(data) >= 4:
            wheel_x, wheel_y = struct.unpack("!hh", data[:4])
            self.backend.wheel(self._wheel_steps(wheel_x), self._wheel_steps(wheel_y))
            self._send_cmd(b"CNOP")
            return True

        if cmd in (b"DKDN", b"DKUP") and len(data) >= 6:
            key_id, mods, key_button = struct.unpack("!HHH", data[:6])
            code = self._linux_key(key_button, key_id)
            if code == TAB_KEY and (self.alt_down or self.tab_consumed):
                if cmd == b"DKDN" and self.alt_down:
                    self._alt_tab(bool(mods & 0x0001))
                elif cmd == b"DKUP":
                    self.tab_consumed = False
                self._send_cmd(b"CNOP")
                return True
            if code in (125, 126):
                # Consume both Windows/Super/Meta keys. off mode deliberately
                # drops them too; no stray modifier event is ever injected.
                self._home_key(code, cmd == b"DKDN")
            elif code in ALT_KEY_CODES:
                self._alt_key(code, cmd == b"DKDN")
            elif code:
                down = 1 if cmd == b"DKDN" else 0
                self.backend.key(code, down)
                if down:
                    self.pressed_keys.add(code)
                else:
                    self.pressed_keys.discard(code)
            else:
                logging.warning("Unmapped key id=0x%04x button=0x%04x", key_id, key_button)
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"DKRP" and len(data) >= 8:
            key_id, _mods, _count, key_button = struct.unpack("!HHHH", data[:8])
            code = self._linux_key(key_button, key_id)
            if code in (125, 126):
                pass  # Consume repeats; never inject Meta or repeat HOME.
            elif code in ALT_KEY_CODES:
                pass  # The hold timer, not key repeat, drives Alt-Home.
            elif code == TAB_KEY and (self.alt_down or self.tab_consumed):
                pass  # Alt-Tab is an app switch, not Tab input.
            elif code:
                self.backend.key(code, 2)  # Linux EV_KEY autorepeat value.
                self.pressed_keys.add(code)
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"CALV":
            self._send_cmd(b"CALV")
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"DSOP":
            self._send_cmd(b"CNOP")
            return True

        if cmd == b"CBYE":
            return False

        # Clipboard and screensaver control are intentionally not injected.
        if cmd in (b"DCLP", b"CCLP", b"CSEC", b"CNOP"):
            return True

        logging.warning("Unknown Synergy command %r", cmd)
        return True

    @staticmethod
    def _mouse_button(button: int) -> Optional[int]:
        return {
            1: BTN_LEFT,
            2: BTN_MIDDLE,
            3: BTN_RIGHT,
            4: BTN_SIDE,
            5: BTN_EXTRA,
        }.get(button)

    @staticmethod
    def _wheel_steps(value: int) -> int:
        if value == 0:
            return 0
        magnitude = abs(value) // 120
        if magnitude == 0:
            magnitude = 1
        return magnitude if value > 0 else -magnitude

    @staticmethod
    def _linux_key(key_button: int, key_id: int) -> Optional[int]:
        # KeyID is authoritative for modifier keys: Windows sends Super with
        # keyButton 0, and ScanCode-to-KeyID mappings vary by server platform.
        if key_id in HOME_KEY_IDS:
            return HOME_KEY_IDS[key_id]
        if key_id in ALT_KEY_IDS:
            return ALT_KEY_IDS[key_id]
        if key_id in SPECIAL_KEY_IDS:
            return SPECIAL_KEY_IDS[key_id]
        if key_button in EXTENDED_SCANCODES:
            return EXTENDED_SCANCODES[key_button]
        if 0 < key_button < 0x80:
            return key_button
        # Some implementations encode extended scan codes as E0xx in 16 bits.
        if (key_button & 0xFF00) == 0xE000:
            return EXTENDED_SCANCODES.get(key_button)
        return ASCII_TO_EVDEV.get(key_id)

    def _release_all(self) -> None:
        with self.home_lock:
            self.home_down.clear()
            if self.home_timer:
                self.home_timer.cancel()
                self.home_timer = None
            self.alt_down.clear()
            if self.alt_timer:
                self.alt_timer.cancel()
                self.alt_timer = None
            self.alt_released = False
            self.tab_consumed = False
            with self.cycle_lock:
                self.cycle_list = []
                self.cycle_index = -1
        for code in list(self.pressed_keys):
            try:
                self.backend.key(code, 0)
            except Exception:
                pass
        self.pressed_keys.clear()
        for code in list(self.pressed_buttons):
            try:
                self.backend.button(code, 0)
            except Exception:
                pass
        self.pressed_buttons.clear()


def configure_logging(verbose: bool, log_file: Optional[str]) -> None:
    handlers = [logging.StreamHandler(sys.stdout)]
    if log_file:
        handlers.append(logging.FileHandler(log_file))
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=handlers,
    )


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="SynergyWebOS: Synergy client for rooted LG webOS TVs")
    parser.add_argument("--version", action="version", version="%(prog)s " + APP_VERSION)
    parser.add_argument("--server", required=True, help="Desktop Synergy server hostname/IP")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--name", default="webos-tv", help="Synergy screen/client name")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--dry-run", action="store_true", help="Log input instead of touching /dev/uinput")
    parser.add_argument("--once", action="store_true", help="Exit after the first completed server session")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--home-target", choices=("system", "off"), default="system",
                        help="system: send TV HOME on Windows key; off: consume it silently")
    parser.add_argument("--home-delay-ms", type=int, default=0,
                        help="Hold time before HOME, 0..5000 ms (release cancels)")
    parser.add_argument("--log-file")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    configure_logging(args.verbose, args.log_file)
    if not (1 <= args.port <= 65535):
        raise SystemExit("port must be 1..65535")
    if not (1 <= args.width <= 65535 and 1 <= args.height <= 65535):
        raise SystemExit("width/height must fit Synergy's 16-bit screen info fields")
    if not 0 <= args.home_delay_ms <= 5000:
        raise SystemExit("home-delay-ms must be 0..5000")

    backend: InputBackend
    if args.dry_run:
        logging.info("DRY RUN: protocol enabled; no input device will be created")
        backend = DryRunBackend()
    else:
        try:
            backend = UInputBackend()
        except (OSError, RuntimeError) as exc:
            logging.error("Cannot initialize system input: %s", exc)
            logging.error("Run Probe TV in the SynergyWebOS app, or start with --dry-run.")
            return 2

    client = SynergyClient(
        args.server, args.port, args.name, args.width, args.height, backend, args.once,
        args.home_target, args.home_delay_ms, args.dry_run,
    )

    def on_signal(signum, _frame):
        logging.info("Stopping on signal %d", signum)
        client.stop()

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)
    try:
        return client.run()
    finally:
        client.stop()
        backend.close()


if __name__ == "__main__":
    sys.exit(main())
