#!/usr/bin/env python3
"""Actual daemon/CLI + fragmented TCP + kernel peer-credential checks. No real input."""
from __future__ import annotations
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

DAEMON, CTL = map(str, map(Path, sys.argv[1:3]))
checks = 0

def check(value: bool, label: str) -> None:
    global checks
    checks += 1
    if not value:
        raise AssertionError(label)

def wait(predicate, limit: float = 3.0):
    end = time.monotonic() + limit
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.015)
    raise AssertionError("condition deadline")

def receive(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise EOFError("peer closed")
        data += part
    return bytes(data)

def packet(sock: socket.socket) -> bytes:
    length, = struct.unpack("!I", receive(sock, 4))
    check(4 <= length <= 1048576, "bounded server response")
    return receive(sock, length)

def send(sock: socket.socket, body: bytes) -> None:
    sock.sendall(struct.pack("!I", len(body)) + body)

def rpc(root: Path, op: str, **fields):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(3)
        sock.connect(str(root / "control.sock"))
        data = json.dumps({"op": op, **fields}).encode()
        sock.sendall(struct.pack("!I", len(data)) + data)
        count, = struct.unpack("!I", receive(sock, 4))
        return json.loads(receive(sock, count))

def main():
    with tempfile.TemporaryDirectory(prefix="syn-it-", dir="/tmp") as temp:
        root = Path(temp)
        os.chmod(root, 0o755)
        daemon = subprocess.Popen([DAEMON, "--runtime-dir", temp], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            wait(lambda: (root / "control.sock").is_socket())
            status = rpc(root, "status")
            check(status["ok"] and not status["client"]["running"], "idle startup does not connect")
            check(status["saved_config"]["dry_run"], "dry-run default")
            probe = rpc(root, "probe")
            check(probe["ok"] and not probe["devices_created"] and not probe["webos_build"], "probe is not input activation")
            check(not rpc(root, "exec", command="id")["ok"], "arbitrary exec rejected")
            for removed in ("apps", "launch", "home"):
                check(not rpc(root, removed, id="dev.canopy.tvdock")["ok"], "removed launcher RPC rejected: " + removed)
            check(not rpc(root, "start")["ok"], "empty desktop rejected")
            check(not rpc(root, "autostart-enable")["ok"], "host autostart disabled")
            duplicate = subprocess.run([DAEMON, "--runtime-dir", temp], capture_output=True, text=True, timeout=2)
            check(duplicate.returncode != 0 and "already owns" in duplicate.stderr, "duplicate daemon lock")
            check(rpc(root, "status")["ok"], "duplicate did not unlink active socket")

            listener = socket.socket()
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)
            port = listener.getsockname()[1]
            config = dict(status["saved_config"], server="127.0.0.1", port=port, name="native-test", idle_timeout_ms=3000)
            check(rpc(root, "configure", config=config)["ok"], "config persisted")
            check((root / "config.json").stat().st_mode & 0o777 == 0o600, "private state mode")
            check(rpc(root, "start", once=True)["ok"], "start accepted")
            peer, _ = listener.accept()
            peer.settimeout(3)
            with peer:
                hello = b"Synergy" + struct.pack("!HH", 1, 6)
                wire = struct.pack("!I", len(hello)) + hello
                # Gaps span multiple native poll intervals; the old partial-read hazard is exercised.
                for part in (wire[:2], wire[2:6], wire[6:9], wire[9:]):
                    peer.sendall(part)
                    time.sleep(0.07)
                response = packet(peer)
                check(response == b"Synergy" + struct.pack("!HHI", 1, 4, 11) + b"native-test", "fragmented handshake")
                send(peer, b"QINF")
                check(packet(peer) == b"DINF" + struct.pack("!7H", 0, 0, 1920, 1080, 0, 0, 0), "screen info")
                send(peer, b"CINN" + struct.pack("!HHIH", 100, 200, 7, 3))
                check(packet(peer) == b"CNOP", "entry acknowledged")
                wait(lambda: rpc(root, "status")["client"]["state"] == "active")
                for body in (
                    b"DMMV" + struct.pack("!HH", 140, 210),
                    b"DMDN\x01",
                    b"DKDN" + struct.pack("!HHH", ord("a"), 3, 30),
                    b"DKRP" + struct.pack("!HHHH", ord("a"), 3, 1, 30),
                    b"DMWM" + struct.pack("!hh", 0, 120),
                ):
                    send(peer, body)
                    check(packet(peer) == b"CNOP", "input packet acknowledged")
                snapshot = rpc(root, "status")["client"]
                check(snapshot["input_events"] >= 7, "dry-run input counters advance")
                new_config = dict(config, name="next-session")
                check(rpc(root, "configure", config=new_config)["ok"], "save during connection")
                state = rpc(root, "status")
                check(state["saved_config"]["name"] == "next-session" and state["client"]["active_config"]["name"] == "native-test", "saved and active settings separated")
                send(peer, b"COUT")
                packet(peer)
                after = wait(lambda: (s if (s := rpc(root, "status"))["client"]["state"] == "connected" else None))
                check(after["client"]["input_events"] >= snapshot["input_events"] + 4, "COUT releases held modifiers/key/button")
                send(peer, b"CALV")
                check(packet(peer) == b"CALV" and packet(peer) == b"CNOP", "keepalive response ordering")
                send(peer, b"CBYE")
            wait(lambda: not rpc(root, "status")["client"]["running"])
            check(rpc(root, "status")["ok"], "remote close does not kill control daemon")
            check(rpc(root, "start", once=True)["ok"], "TLS probe start")
            tls, _ = listener.accept()
            with tls:
                tls.sendall(b"\x16\x03\x03\x00\x10")
                wait(lambda: not rpc(root, "status")["client"]["running"])
                check("TLS" in rpc(root, "status")["client"]["error"], "TLS is refused, not downgraded")
            check(rpc(root, "start", once=True)["ok"], "malformed frame start")
            bad, _ = listener.accept()
            with bad:
                bad.sendall(b"\xff\xff\xff\xff")
                wait(lambda: not rpc(root, "status")["client"]["running"])
                check("length" in rpc(root, "status")["client"]["error"], "oversized frame closes connection")
            check(rpc(root, "start", once=True)["ok"], "idle timeout start")
            idle, _ = listener.accept()
            with idle:
                send(idle, hello)
                packet(idle)
                send(idle, b"QINF")
                packet(idle)
                wait(lambda: not rpc(root, "status")["client"]["running"], 4)
                check("timeout" in rpc(root, "status")["client"]["error"], "negotiated idle deadline")
            listener.close()
            check(rpc(root, "start", once=True)["ok"], "failed connect --once start")
            wait(lambda: not rpc(root, "status")["client"]["running"])
            check("unavailable" in rpc(root, "status")["client"]["error"] or "failed" in rpc(root, "status")["client"]["error"], "--once ends on connection failure")
            check(rpc(root, "start")["ok"], "reconnect-mode start")
            started = time.monotonic()
            check(rpc(root, "stop")["ok"], "Stop accepted")
            check(time.monotonic() - started < 1, "Stop interrupts connection/backoff promptly")
            check(rpc(root, "profile-save", profile={"corner": "bottom-right", "cornerPixels": 2, "switchDelayMs": 250})["applied_to_desktop"] is False, "local handoff does not apply desktop changes")
            check(rpc(root, "profile-load")["profile"]["corner"] == "bottom-right", "local handoff persistence")
            check(rpc(root, "configure", config=dict(config, dry_run=False, allow_plaintext=False))["ok"], "real-input intent may be saved without starting")
            check(not rpc(root, "start")["ok"], "real input requires approval")
            check(rpc(root, "configure", config=dict(config, dry_run=False, allow_plaintext=True))["ok"], "approved intent saved")
            check(not rpc(root, "start")["ok"], "desktop binary still forbids real input")
            check(rpc(root, "configure", config=config)["ok"], "restore safe test config")
            # Invalid length, malformed JSON, and stalled local input do not stop the daemon.
            for wire in (b"\xff\xff\xff\xff", struct.pack("!I", 5) + b"{bad}", b"\x00"):
                with socket.socket(socket.AF_UNIX) as adversary:
                    adversary.settimeout(2)
                    adversary.connect(str(root / "control.sock"))
                    adversary.sendall(wire)
                    try:
                        adversary.recv(1)
                    except ConnectionResetError:
                        pass
                check(rpc(root, "status")["ok"], "malformed/slow IPC did not kill daemon")
            if os.geteuid() == 0:
                code = '''import socket,sys,struct,json
s=socket.socket(socket.AF_UNIX);s.settimeout(2);s.connect(sys.argv[1])
try:
 b=b'{"op":"status"}';s.sendall(struct.pack('!I',len(b))+b);x=s.recv(4);sys.exit(0 if x==b'' else 3)
except (ConnectionResetError,BrokenPipeError):sys.exit(0)
'''
                unauthorized = subprocess.run([sys.executable, "-c", code, str(root / "control.sock")], user=65534, group=65534, capture_output=True, timeout=3)
                check(unauthorized.returncode == 0, "kernel peer credentials reject other UID")
            result = subprocess.run([CTL, "--runtime-dir", temp, "snapshot"], capture_output=True, text=True, timeout=3)
            check(result.returncode == 0 and all(x in result.stdout for x in ("===STATE===", "===AUTOSTART===", "===CONFIG===", "===LOG===")), "legacy-compatible snapshot markers")
        finally:
            daemon.terminate()
            stdout, stderr = daemon.communicate(timeout=5)
            check(daemon.returncode == 0, "clean SIGTERM: " + stderr)
        # Invalid existing config is retained, never silently overwritten with defaults.
        (root / "config.json").write_text("{broken")
        original = (root / "config.json").read_bytes()
        daemon = subprocess.Popen([DAEMON, "--runtime-dir", temp], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            wait(lambda: (root / "control.sock").is_socket())
            check(bool(rpc(root, "status")["config_error"]), "malformed persisted config surfaced")
            check(not rpc(root, "configure", config=config)["ok"], "malformed config requires explicit repair")
            check((root / "config.json").read_bytes() == original, "malformed original preserved")
        finally:
            daemon.terminate()
            daemon.communicate(timeout=5)
    print(f"PASS: {checks} process/TCP/IPC assertions; actual daemon + CLI; no TV input")

if __name__ == "__main__":
    main()
