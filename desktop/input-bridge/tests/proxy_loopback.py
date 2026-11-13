#!/usr/bin/env python3
"""Loopback-only mTLS and frame conformance checks for the desktop proxy."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = ROOT / ".ai/evidence/input-0.4.1-implementation-20261113/proxy-verification"
checks: list[str] = []


def check(value: bool, label: str) -> None:
    if not value:
        raise AssertionError(label)
    checks.append(label)


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
    check(length <= 1024 * 1024, "received classic frames stay within the 1 MiB protocol cap")
    return receive(sock, length)


def framed(body: bytes) -> bytes:
    return struct.pack("!I", len(body)) + body


def send_fragments(sock: socket.socket, data: bytes) -> None:
    # Separate TLS records / TCP writes ensure framing works across read boundaries.
    cuts = (1, 3, 7, 19, 64, 257)
    start = 0
    for size in cuts:
        if start >= len(data):
            break
        sock.sendall(data[start:start + size])
        start += size
        time.sleep(0.004)
    if start < len(data):
        sock.sendall(data[start:])


def listener() -> socket.socket:
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(4)
    server.settimeout(1.5)
    return server


class FakeCore:
    """A tiny synthetic Synergy endpoint; it never contacts port 24800."""

    def __init__(self) -> None:
        self.sock = listener()
        self.port = self.sock.getsockname()[1]
        self.sessions: list[socket.socket] = []

    def accept(self, timeout: float = 4.0) -> socket.socket:
        self.sock.settimeout(timeout)
        peer, _ = self.sock.accept()
        peer.settimeout(4)
        self.sessions.append(peer)
        return peer

    def no_accept(self, timeout: float = 0.3) -> None:
        self.sock.settimeout(timeout)
        try:
            peer, _ = self.sock.accept()
        except TimeoutError:
            checks.append("TLS rejection occurred before any synthetic upstream accept")
            return
        peer.close()
        raise AssertionError("proxy connected upstream before authentication succeeded")

    def close(self) -> None:
        for peer in self.sessions:
            try:
                peer.close()
            except OSError:
                pass
        self.sock.close()


def openssl(*args: object) -> None:
    subprocess.run(
        ["openssl", *(str(arg) for arg in args)],
        check=True, timeout=25, stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


def make_certificates(folder: Path) -> dict[str, Path]:
    ca_key, ca_cert = folder / "ca.key", folder / "ca.crt"
    openssl(
        "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", ca_key, "-out", ca_cert, "-days", "1",
        "-subj", "/CN=Ephemeral input bridge fixture CA",
        "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign",
    )

    def signed(name: str, san: str, usage: str) -> tuple[Path, Path]:
        key, csr, cert, ext = (folder / f"{name}{suffix}" for suffix in (".key", ".csr", ".crt", ".ext"))
        ext.write_text(
            "basicConstraints=critical,CA:FALSE\n"
            "keyUsage=critical,digitalSignature,keyEncipherment\n"
            f"extendedKeyUsage={usage}\nsubjectAltName=DNS:{san}\n",
            encoding="ascii",
        )
        openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
                "-out", csr, "-subj", f"/CN={san}")
        openssl("x509", "-req", "-in", csr, "-CA", ca_cert, "-CAkey", ca_key,
                "-CAcreateserial", "-out", cert, "-days", "1", "-extfile", ext)
        return cert, key

    server_cert, server_key = signed("server", "bridge.test", "serverAuth")
    client_cert, client_key = signed("tv", "tv.bridge.test", "clientAuth")
    alternate_client_cert, alternate_client_key = signed("alternate-tv", "other-tv.bridge.test", "clientAuth")
    wrong_ca_key, wrong_ca_cert = folder / "wrong-ca.key", folder / "wrong-ca.crt"
    openssl(
        "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", wrong_ca_key, "-out", wrong_ca_cert, "-days", "1",
        "-subj", "/CN=Untrusted fixture CA",
        "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign",
    )
    for path in folder.iterdir():
        if path.is_file():
            os.chmod(path, 0o600)
    der = ssl.PEM_cert_to_DER_cert(client_cert.read_text(encoding="ascii"))
    return {
        "ca": ca_cert, "server_cert": server_cert, "server_key": server_key,
        "client_cert": client_cert, "client_key": client_key,
        "alternate_client_cert": alternate_client_cert,
        "alternate_client_key": alternate_client_key, "wrong_ca": wrong_ca_cert,
        "pin": Path(hashlib.sha256(der).hexdigest()),
    }


def client_context(certs: dict[str, Path], *, ca: Path | None = None,
                   alternate: bool = False) -> ssl.SSLContext:
    ctx = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=str(ca or certs["ca"]))
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    cert = certs["alternate_client_cert"] if alternate else certs["client_cert"]
    key = certs["alternate_client_key"] if alternate else certs["client_key"]
    ctx.load_cert_chain(str(cert), str(key))
    return ctx


def connect(proxy_port: int, context: ssl.SSLContext) -> ssl.SSLSocket:
    raw = socket.create_connection(("127.0.0.1", proxy_port), timeout=4)
    raw.settimeout(4)
    return context.wrap_socket(raw, server_hostname="bridge.test")


def write_fixture(path: Path, rows: list[str] | None) -> None:
    temp = path.with_suffix(".next")
    temp.write_text("\n".join(rows or []) + ("\n" if rows else ""), encoding="ascii")
    os.chmod(temp, 0o600)
    os.replace(temp, path)


def decode_pad(payload: bytes) -> tuple:
    if len(payload) != 32:
        raise AssertionError(f"DGPD payload length was {len(payload)}")
    return struct.unpack("!4sBBBBIQHhhhhBB", payload)


def expect_pad(sock: ssl.SSLSocket, *, slot: int, capture: int,
               sequence_after: int, state: tuple) -> int:
    message = packet(sock)
    fields = decode_pad(message)
    magic, version, actual_slot, connected, reserved, actual_capture, sequence, buttons, lx, ly, rx, ry, lt, rt = fields
    check(magic == b"DGPD" and version == 1 and reserved == 0, "DGPD version and reserved byte match wire contract")
    check(actual_slot == slot and connected == state[0] and actual_capture == capture,
          f"DGPD slot {slot} connected/capture fields match")
    check(sequence > sequence_after, "controller sequence increases across reports")
    check((buttons, lx, ly, rx, ry, lt, rt) == state[1:],
          f"DGPD slot {slot} forwards buttons, four axes, and independent triggers")
    return sequence


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: proxy_loopback.py /path/to/synergy-input-bridge")
    binary = str(Path(sys.argv[1]).resolve())
    if not shutil.which("openssl"):
        raise RuntimeError("OpenSSL CLI is required for ephemeral test certificates")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    result_path = EVIDENCE / "proxy-loopback-result.json"
    proxy_port_sock = listener()
    proxy_port = proxy_port_sock.getsockname()[1]
    proxy_port_sock.close()
    core = FakeCore()
    proxy: subprocess.Popen[str] | None = None
    first_error: str | None = None
    proxy_stderr = ""
    fixture: Path | None = None
    started = time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix="ephemeral-proxy-pki-", dir=EVIDENCE) as temp:
            temp_path = Path(temp)
            certs = make_certificates(temp_path)
            fixture = EVIDENCE / "controller-fixture.txt"
            write_fixture(fixture, [
                "0 1 62463 -32768 32767 -12345 23456 255 128",
                "1 1 1 -1 1 -2 2 1 254",
                "2 1 32768 32767 -32768 30000 -30000 0 255",
                "3 1 32 0 0 0 0 127 128",
            ])
            proxy = subprocess.Popen(
                [binary, "--ca", str(certs["ca"]), "--cert", str(certs["server_cert"]),
                 "--key", str(certs["server_key"]), "--client-sha256", str(certs["pin"]),
                 "--listen", "127.0.0.1", "--port", str(proxy_port),
                 "--upstream-port", str(core.port), "--fixture", str(fixture)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )

            ready: list[str] = []
            def read_ready() -> None:
                assert proxy and proxy.stdout
                ready.append(proxy.stdout.readline())
            reader = threading.Thread(target=read_ready, daemon=True)
            reader.start()
            reader.join(4)
            check(bool(ready and "Bridge ready" in ready[0]),
                  "Linux proxy starts in explicitly fixture-backed loopback mode")

            # Wrong server CA: TLS hostname/chain verification fails, and upstream stays untouched.
            try:
                with connect(proxy_port, client_context(certs, ca=certs["wrong_ca"])):
                    pass
            except (ssl.SSLError, OSError):
                checks.append("untrusted server CA rejected")
            else:
                raise AssertionError("client accepted a proxy certificate from an untrusted CA")
            core.no_accept()

            # Trusted CA but a different, CA-signed client cert: the proxy pin must reject pre-upstream.
            try:
                wrong_pin = connect(proxy_port, client_context(certs, alternate=True))
                wrong_pin.sendall(framed(b"Synergy" + struct.pack("!HH", 1, 6)))
                wrong_pin.settimeout(2)
                try:
                    closed_bytes = wrong_pin.recv(1)
                    check(closed_bytes == b"", "wrong client pin closes the authenticated TLS stream")
                except (ssl.SSLError, OSError):
                    checks.append("wrong client pin closes the authenticated TLS stream")
                wrong_pin.close()
                checks.append("wrong trusted client certificate pin rejected")
            except (ssl.SSLError, OSError):
                checks.append("wrong trusted client certificate pin rejected")
            core.no_accept()

            # Positive mTLS session with an exact pinned DER certificate.
            tv = connect(proxy_port, client_context(certs))
            server = core.accept()

            tv_hello = b"Synergy" + struct.pack("!HH", 1, 6)
            send_fragments(tv, framed(tv_hello))
            check(receive(server, len(framed(tv_hello))) == framed(tv_hello),
                  "fragmented classic hello relays byte-for-byte to synthetic core")
            send_fragments(server, framed(tv_hello + b" synthetic-core"))
            check(packet(tv) == tv_hello + b" synthetic-core",
                  "fragmented classic hello response relays byte-for-byte")

            send_fragments(tv, framed(b"QINF"))
            check(receive(server, len(framed(b"QINF"))) == framed(b"QINF"),
                  "fragmented QINF request relays byte-for-byte")
            dinf = b"DINF" + struct.pack("!HH", 1920, 1080)
            send_fragments(server, framed(dinf))
            check(packet(tv) == dinf, "fragmented DINF response relays byte-for-byte")

            # Custom classic keyboard, pointer and wheel frames go only to the core unchanged.
            classic_inputs = [
                b"DKDN" + struct.pack("!I", 0x00070004),
                b"DKUP" + struct.pack("!I", 0x00070004),
                b"DMMV" + struct.pack("!II", 1234, 4321),
                b"DMDN" + struct.pack("!IIH", 100, 200, 1),
                b"DMUP" + struct.pack("!IIH", 100, 200, 1),
                b"DWRL" + struct.pack("!hh", -120, 240),
            ]
            input_stream = b"".join(map(framed, classic_inputs))
            send_fragments(tv, input_stream)
            check(receive(server, len(input_stream)) == input_stream,
                  "keyboard, mouse, button, and wheel bytes relay unchanged")

            # Before focus, malformed fixture text must not be read or close the live session.
            write_fixture(fixture, ["invalid fixture must remain unread outside focus"])
            time.sleep(0.18)
            check(not select.select([tv], [], [], 0.05)[0],
                  "no controller frame is forwarded before captured focus")

            capture = 41
            write_fixture(fixture, [
                "0 1 62463 -32768 32767 -12345 23456 255 128",
                "1 1 1 -1 1 -2 2 1 254",
                "2 1 32768 32767 -32768 30000 -30000 0 255",
                "3 1 32 0 0 0 0 127 128",
            ])
            cinn = b"CINN" + struct.pack("!HHIH", 100, 200, capture, 0)
            send_fragments(server, framed(cinn))
            check(packet(tv) == cinn, "fragmented CINN is delivered before controller data")
            send_fragments(tv, framed(b"CNOP"))
            check(receive(server, len(framed(b"CNOP"))) == framed(b"CNOP"),
                  "classic CINN acknowledgement relays unchanged")
            # Restore a valid fixture immediately after verified focus entry.
            expected_rows = [
                (1, 62463, -32768, 32767, -12345, 23456, 255, 128),
                (1, 1, -1, 1, -2, 2, 1, 254),
                (1, 32768, 32767, -32768, 30000, -30000, 0, 255),
                (1, 32, 0, 0, 0, 0, 127, 128),
            ]
            last_sequence = 0
            for slot, state in enumerate(expected_rows):
                last_sequence = expect_pad(tv, slot=slot, capture=capture,
                                           sequence_after=last_sequence, state=state)

            # A state transition to disconnected must be an all-zero neutral report.
            write_fixture(fixture, [
                "0 0 0 0 0 0 0 0 0",
                "1 0 0 0 0 0 0 0 0",
                "2 0 0 0 0 0 0 0 0",
                "3 0 0 0 0 0 0 0 0",
            ])
            for slot in range(4):
                last_sequence = expect_pad(tv, slot=slot, capture=capture,
                                           sequence_after=last_sequence,
                                           state=(0, 0, 0, 0, 0, 0, 0, 0))
            checks.append("hot-unplug emits neutral disconnected reports for all four slots")

            # A >256 KiB opaque clipboard-like body must stream intact without packet interleaving.
            body = b"DCLP" + bytes((i * 17) & 0xff for i in range(320 * 1024))
            send_fragments(server, framed(body))
            check(packet(tv) == body, "320 KiB classic body streams intact with frame boundaries preserved")

            # COUT cancels polling/forwarding; invalid fixture proves no post-leave capture read.
            cout = b"COUT"
            send_fragments(server, framed(cout))
            check(packet(tv) == cout, "fragmented COUT is delivered byte-for-byte")
            send_fragments(tv, framed(b"CNOP"))
            check(receive(server, len(framed(b"CNOP"))) == framed(b"CNOP"),
                  "COUT acknowledgement relays unchanged")
            write_fixture(fixture, ["broken fixture must remain unread after COUT"])
            time.sleep(0.18)
            check(not select.select([tv], [], [], 0.05)[0],
                  "no controller reports are forwarded outside focus")
            send_fragments(server, framed(b"CALV"))
            check(packet(tv) == b"CALV", "session remains live with invalid fixture outside focus")
            send_fragments(tv, framed(b"CNOP"))
            check(receive(server, len(framed(b"CNOP"))) == framed(b"CNOP"),
                  "off-focus keepalive response relays unchanged")

            # New focus re-reads the new state; it does not replay the old capture's state.
            reentry_rows = ["0 1 48 300 -400 500 -600 10 20"]
            write_fixture(fixture, reentry_rows)
            new_capture = 42
            send_fragments(server, framed(b"CINN" + struct.pack("!HHIH", 110, 210, new_capture, 0)))
            check(packet(tv).startswith(b"CINN"), "second fragmented CINN is delivered")
            send_fragments(tv, framed(b"CNOP"))
            check(receive(server, len(framed(b"CNOP"))) == framed(b"CNOP"),
                  "second CINN acknowledgement relays unchanged")
            fields = decode_pad(packet(tv))
            check(fields[5] == new_capture and fields[6] > last_sequence,
                  "re-entry uses the new capture ID and continues global sequence")
            check(fields[2:4] == (0, 1) and fields[7:] == (48, 300, -400, 500, -600, 10, 20),
                  "re-entry forwards current fixture state instead of old controller state")
            last_sequence = fields[6]

            # Explicitly close the valid connection and authenticate a fresh mTLS connection.
            tv.close()
            server.close()
            tv2 = connect(proxy_port, client_context(certs))
            server2 = core.accept()
            send_fragments(server2, framed(b"CINN" + struct.pack("!HHIH", 120, 220, 99, 0)))
            check(packet(tv2).startswith(b"CINN"), "reconnected TLS session receives classic focus")
            send_fragments(tv2, framed(b"CNOP"))
            check(receive(server2, len(framed(b"CNOP"))) == framed(b"CNOP"),
                  "reconnected focus acknowledgement relays")
            first = decode_pad(packet(tv2))
            check(first[5] == 99 and first[6] == 1,
                  "pinned mTLS reconnect succeeds and controller sequence restarts at one per TLS session")
            tv2.close()
            server2.close()

            # Malformed outer lengths close the authenticated session and do not reach upstream.
            tv3 = connect(proxy_port, client_context(certs))
            server3 = core.accept()
            tv3.sendall(struct.pack("!I", 3))
            tv3.settimeout(2)
            closed = False
            try:
                closed = tv3.recv(1) == b""
            except (ssl.SSLError, OSError):
                closed = True
            check(closed, "malformed short classic frame closes the TLS relay")
            server3.settimeout(1)
            check(server3.recv(1) == b"", "malformed frame bytes are not forwarded upstream")
            tv3.close()
            server3.close()

            check(not fixture.with_name(fixture.name + ".next").exists(),
                  "atomic fixture replacement leaves no temporary file")
            fixture.unlink(missing_ok=True)
    except Exception as exc:
        first_error = f"{type(exc).__name__}: {exc}"
    finally:
        if proxy:
            proxy.terminate()
            try:
                proxy.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proxy.kill()
                proxy.wait(timeout=2)
            if proxy.stderr:
                proxy_stderr = proxy.stderr.read()
        if fixture:
            fixture.unlink(missing_ok=True)
        core.close()

    result = {
        "status": "PASS" if first_error is None else "FAIL",
        "binary": binary,
        "checks_passed": checks,
        "first_error": first_error,
        "proxy_stderr": proxy_stderr[-4000:],
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "synthetic_upstream": "127.0.0.1 ephemeral port; never port 24800",
        "fixture_mode": "loopback-only",
        "pki": "ephemeral CA/certificates/keys removed on exit",
        "scope_limits": [
            "no Windows XInput runtime or physical controller",
            "queue saturation, controller freshness timeout, and process-memory bounds not stress-tested",
            "no live Synergy server, TV, driver, or installed service",
        ],
    }
    result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if first_error is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
