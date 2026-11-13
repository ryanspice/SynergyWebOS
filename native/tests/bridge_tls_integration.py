#!/usr/bin/env python3
"""Actual daemon/CLI mTLS bridge fixtures; loopback only, dry-run, no TV input."""
from __future__ import annotations

import contextlib
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time

DAEMON, CTL = map(str, map(Path, sys.argv[1:3]))
ROOT = Path(__file__).resolve().parents[2]
FIXTURE_ROOT = Path(os.environ.get(
    "SYNERGY_TEST_POSIX_ROOT",
    str(ROOT / ".ai/evidence/input-0.4.1-implementation-20261113/gamepad-tests"),
))
checks = 0


def check(value: bool, label: str) -> None:
    global checks
    checks += 1
    if not value:
        raise AssertionError(label)


def wait(predicate, limit: float = 4.0):
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
    check(4 <= length <= 1048576, "bounded classic server response")
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


def listener() -> socket.socket:
    sock = socket.socket()
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", 0))
    sock.listen(1)
    sock.settimeout(4)
    return sock


def make_certificates(folder: Path) -> dict[str, Path]:
    if not shutil.which("openssl"):
        raise AssertionError("openssl is required for ephemeral bridge TLS fixtures")

    def openssl(*args: object) -> None:
        try:
            subprocess.run(
                ["openssl", *(str(arg) for arg in args)],
                check=True,
                timeout=20,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except (OSError, subprocess.SubprocessError):
            raise AssertionError("openssl could not create the ephemeral bridge identity") from None

    ca_key = folder / "ca.key"
    ca_cert = folder / "ca.crt"
    openssl(
        "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", ca_key, "-out", ca_cert, "-days", "1",
        "-subj", "/CN=Synergy bridge fixture CA",
        "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign",
    )

    def signed(name: str, common_name: str, san: str, usage: str) -> tuple[Path, Path]:
        key = folder / f"{name}.key"
        csr = folder / f"{name}.csr"
        cert = folder / f"{name}.crt"
        ext = folder / f"{name}.ext"
        ext.write_text(
            "basicConstraints=critical,CA:FALSE\n"
            "keyUsage=critical,digitalSignature,keyEncipherment\n"
            f"extendedKeyUsage={usage}\n"
            f"subjectAltName=DNS:{san}\n",
            encoding="ascii",
        )
        os.chmod(ext, 0o600)
        openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
                "-out", csr, "-subj", f"/CN={common_name}")
        openssl("x509", "-req", "-in", csr, "-CA", ca_cert, "-CAkey", ca_key,
                "-CAcreateserial", "-out", cert, "-days", "1", "-extfile", ext)
        return cert, key

    server_cert, server_key = signed("server", "bridge.test", "bridge.test", "serverAuth")
    client_cert, client_key = signed("client", "tv.bridge.test", "tv.bridge.test", "clientAuth")
    wrong_ca_key = folder / "wrong-ca.key"
    wrong_ca_cert = folder / "wrong-ca.crt"
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", wrong_ca_key,
            "-out", wrong_ca_cert, "-days", "1", "-subj", "/CN=Wrong bridge fixture CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign")

    for path in folder.iterdir():
        if path.is_file():
            os.chmod(path, 0o600)
    return {
        "ca": ca_cert,
        "server_cert": server_cert,
        "server_key": server_key,
        "client_cert": client_cert,
        "client_key": client_key,
        "wrong_ca": wrong_ca_cert,
    }


def server_context(certs: dict[str, Path]) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(certs["server_cert"]), str(certs["server_key"]))
    context.load_verify_locations(cafile=str(certs["ca"]))
    context.verify_mode = ssl.CERT_REQUIRED
    return context


def bridge_config(saved: dict, port: int, certs: dict[str, Path], *,
                  ca: Path | None = None, identity: str = "bridge.test",
                  client_cert: Path | None = None) -> dict:
    return dict(
        saved,
        server="127.0.0.1",
        port=port,
        name="native-test",
        dry_run=True,
        allow_plaintext=False,
        input_bridge=True,
        bridge_ca_file=str(ca or certs["ca"]),
        bridge_cert_file=str(client_cert or certs["client_cert"]),
        bridge_key_file=str(certs["client_key"]),
        bridge_server_name=identity,
    )


def configure(root: Path, config: dict) -> None:
    result = rpc(root, "configure", config=config)
    check(result["ok"], "explicit bridge configuration accepted")


def start_once(root: Path) -> None:
    result = rpc(root, "start", once=True)
    check(result["ok"], "one-shot client start accepted")


def stopped_status(root: Path) -> dict:
    return wait(lambda: (state if not (state := rpc(root, "status"))["client"]["running"] else None))


def accept_failed_tls(root: Path, server: socket.socket, certs: dict[str, Path]) -> None:
    raw, _ = server.accept()
    raw.settimeout(3)
    try:
        hello = raw.recv(5, socket.MSG_PEEK)
        check(hello[:1] == b"\x16", "failed-auth scenario still starts with TLS")
        rejected = False
        try:
            with server_context(certs).wrap_socket(raw, server_side=True):
                pass
        except ssl.SSLError:
            rejected = True
        check(rejected, "mTLS server rejected a TV TLS verification failure")
    finally:
        with contextlib.suppress(OSError):
            raw.close()
    status = stopped_status(root)
    check(bool(status["client"]["error"]), "failed bridge connection reports a closed error state")


def classic_handshake(sock: socket.socket, capture: int) -> None:
    send(sock, b"Synergy" + struct.pack("!HH", 1, 6))
    hello = packet(sock)
    check(hello.startswith(b"Synergy") and b"native-test" in hello,
          "authenticated bridge preserves the classic Synergy hello")
    send(sock, b"QINF")
    check(packet(sock).startswith(b"DINF"), "classic screen information negotiation")
    cinn = b"CINN" + struct.pack("!HHIH", 100, 200, capture, 0)
    send(sock, cinn)
    check(packet(sock) == b"CNOP", "classic CINN capture is acknowledged")


def gamepad(slot: int, capture: int, sequence: int) -> bytes:
    # Independent raw-byte oracle for the 32-byte DGPD contract.
    return struct.pack("!4sBBBBIQHhhhhBB", b"DGPD", 1, slot, 1, 0,
                       capture, sequence, 0x1000, -32768, 32767, -12345, 23456, 255, 128)


def main() -> None:
    runtime_probe = os.fsencode(FIXTURE_ROOT / "syn-bridge-runtime-XXXXXX")
    if len(runtime_probe) > 75:
        raise AssertionError("SYNERGY_TEST_POSIX_ROOT must keep the TLS daemon runtime path at or below 75 bytes")
    FIXTURE_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bridge-tls-", dir=FIXTURE_ROOT) as temp:
        certs = make_certificates(Path(temp))

        # Daemon control state is temporary POSIX runtime data. Its path remains
        # short enough for the native runtime validator; all PEM fixtures stay
        # under this task-owned evidence directory and are removed on exit.
        with tempfile.TemporaryDirectory(prefix="syn-bridge-runtime-", dir=FIXTURE_ROOT) as runtime:
            if len(os.fsencode(runtime)) > 75:
                raise AssertionError("TLS daemon runtime path exceeds 75 bytes")
            root = Path(runtime)
            os.chmod(root, 0o755)
            daemon = subprocess.Popen(
                [DAEMON, "--runtime-dir", runtime],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            try:
                wait(lambda: (root / "control.sock").is_socket())
                status = rpc(root, "status")
                check(status["ok"] and status["saved_config"]["dry_run"],
                      "fixture runs against the actual daemon in dry-run mode")
                check(not rpc(root, "probe")["devices_created"],
                      "bridge verification does not create input devices")

                # Positive: mTLS server-auth and client-auth, then classic
                # negotiation and raw DGPD reports over the encrypted stream.
                server = listener()
                configure(root, bridge_config(status["saved_config"], server.getsockname()[1], certs))
                start_once(root)
                raw, _ = server.accept()
                raw.settimeout(3)
                with server_context(certs).wrap_socket(raw, server_side=True) as tls:
                    peer_cert = tls.getpeercert()
                    check(bool(peer_cert.get("subject")), "server authenticated the TV client certificate")
                    classic_handshake(tls, 41)
                    before = rpc(root, "status")["client"]["input_events"]
                    send(tls, gamepad(0, 41, 1))
                    after_positive = wait(lambda: (count if (count := rpc(root, "status")["client"]["input_events"]) > before else None))
                    check(after_positive == before + 1, "authenticated DGPD report reaches dry-run input once")
                    check(packet(tls) == b"CNOP", "authenticated DGPD report is acknowledged")

                    send(tls, b"COUT")
                    check(packet(tls) == b"CNOP", "COUT cleanup is acknowledged")
                    after_cout = wait(lambda: (count if (count := rpc(root, "status")["client"]["input_events"]) > after_positive else None))
                    check(after_cout == after_positive + 1, "COUT emits one neutral controller cleanup frame")
                    send(tls, gamepad(0, 41, 2))
                    check(packet(tls) == b"CNOP", "off-focus DGPD is acknowledged without input")
                    time.sleep(0.12)
                    check(rpc(root, "status")["client"]["input_events"] == after_cout,
                          "off-focus DGPD after COUT is ignored")

                    send(tls, b"CINN" + struct.pack("!HHIH", 100, 200, 42, 0))
                    check(packet(tls) == b"CNOP", "second capture is acknowledged")
                    send(tls, gamepad(0, 42, 3))
                    check(packet(tls) == b"CNOP", "connection-global sequence continues after COUT")
                    after_second = wait(lambda: (count if (count := rpc(root, "status")["client"]["input_events"]) > after_cout else None))
                    check(after_second == after_cout + 1, "new capture accepts the next connection sequence")
                    # A partial classic frame must not inherit the Home/Alt
                    # timer deferral and postpone controller neutralization.
                    partial_noop = struct.pack("!I", 4) + b"CNOP"
                    tls.sendall(partial_noop[:1])
                    watched = wait(lambda: (count if (count := rpc(root, "status")["client"]["input_events"]) > after_second else None), 1.5)
                    check(watched == after_second + 1, "500 ms watchdog neutralizes input during a partial classic frame")
                    tls.sendall(partial_noop[1:])

                    send(tls, gamepad(0, 42, 4))
                    check(packet(tls) == b"CNOP", "gamepad remains live after watchdog neutralization")
                    before_stop = wait(lambda: (count if (count := rpc(root, "status")["client"]["input_events"]) > watched else None))
                    check(before_stop == watched + 1, "post-watchdog gamepad state is accepted")
                    started = time.monotonic()
                    check(rpc(root, "stop")["ok"], "Stop accepted during authenticated bridge session")
                    check(time.monotonic() - started < 1.0, "Stop closes the active TLS bridge promptly")
                    after_stop = rpc(root, "status")["client"]["input_events"]
                    check(after_stop == before_stop + 1, "Stop emits final neutral controller cleanup")
                server.close()

                # Wrong trust root: no TLS downgrade or classic hello is sent.
                wrong_ca_server = listener()
                base = rpc(root, "status")["saved_config"]
                configure(root, bridge_config(base, wrong_ca_server.getsockname()[1], certs, ca=certs["wrong_ca"]))
                start_once(root)
                accept_failed_tls(root, wrong_ca_server, certs)
                wrong_ca_server.close()

                # Wrong expected server identity: the CA is trusted, the SAN is not.
                wrong_name_server = listener()
                base = rpc(root, "status")["saved_config"]
                configure(root, bridge_config(base, wrong_name_server.getsockname()[1], certs, identity="wrong.test"))
                start_once(root)
                accept_failed_tls(root, wrong_name_server, certs)
                wrong_name_server.close()

                # Missing client certificate fails after loopback connect and
                # before ClientHello; the peer receives no plaintext protocol.
                missing_server = listener()
                base = rpc(root, "status")["saved_config"]
                missing_cert = Path(temp) / "missing-client.crt"
                configure(root, bridge_config(base, missing_server.getsockname()[1], certs, client_cert=missing_cert))
                start_once(root)
                missing_peer, _ = missing_server.accept()
                missing_peer.settimeout(3)
                status = stopped_status(root)
                check(bool(status["client"]["error"]), "missing client credentials fail closed")
                check(missing_peer.recv(1) == b"", "missing credentials never fall back to classic plaintext")
                missing_peer.close()
                missing_server.close()

                # Direct classic mode remains available but cannot inject DGPD.
                classic_server = listener()
                base = rpc(root, "status")["saved_config"]
                classic = dict(base, server="127.0.0.1", port=classic_server.getsockname()[1],
                               name="native-test", dry_run=True, allow_plaintext=True,
                               input_bridge=False, bridge_ca_file="", bridge_cert_file="",
                               bridge_key_file="", bridge_server_name="")
                configure(root, classic)
                start_once(root)
                peer, _ = classic_server.accept()
                peer.settimeout(3)
                with peer:
                    classic_handshake(peer, 51)
                    before = rpc(root, "status")["client"]["input_events"]
                    send(peer, gamepad(0, 51, 1))
                    status = stopped_status(root)
                    check(bool(status["client"]["error"]), "classic connection refuses custom DGPD")
                    check(status["client"]["input_events"] == before,
                          "classic DGPD refusal does not reach input")
                classic_server.close()

                # Stop must wake the TLS handshake while the loopback peer
                # deliberately withholds its server hello and client-auth step.
                stalled_server = listener()
                base = rpc(root, "status")["saved_config"]
                configure(root, bridge_config(base, stalled_server.getsockname()[1], certs))
                check(rpc(root, "start")["ok"], "stalled TLS client start accepted")
                stalled_peer, _ = stalled_server.accept()
                stalled_peer.settimeout(3)
                hello = stalled_peer.recv(5)
                check(hello[:1] == b"\x16", "TLS handshake began before the peer stalled")
                started = time.monotonic()
                check(rpc(root, "stop")["ok"], "Stop accepted during stalled TLS negotiation")
                check(time.monotonic() - started < 1.0, "Stop interrupts TLS handshake promptly")
                check(not rpc(root, "status")["client"]["running"], "stalled TLS client is no longer running")
                # Drain ClientHello bytes already buffered before checking close.
                drained = len(hello)
                drain_deadline = time.monotonic() + 1.0
                while True:
                    remaining = drain_deadline - time.monotonic()
                    if remaining <= 0:
                        raise AssertionError("Stop did not close stalled TLS socket within 1 second")
                    stalled_peer.settimeout(remaining)
                    try:
                        chunk = stalled_peer.recv(min(4096, 64 * 1024 - drained + 1))
                    except socket.timeout:
                        raise AssertionError("Stop did not close stalled TLS socket within 1 second") from None
                    if not chunk:
                        break
                    drained += len(chunk)
                    if drained > 64 * 1024:
                        raise AssertionError("Stalled TLS close exceeded 64 KiB")
                check(True, "Stop closes stalled TLS socket after bounded ClientHello drain")
                stalled_peer.close()
                stalled_server.close()
            finally:
                if daemon.poll() is None:
                    daemon.terminate()
                daemon.communicate(timeout=5)
                check(daemon.returncode == 0, "daemon shuts down cleanly")

    print(json.dumps({
        "result": "PASS",
        "checks": checks,
        "scope": "actual daemon/CLI, loopback mTLS fixtures, dry-run input",
        "pem_contents_logged": False,
        "tv_or_controller_used": False,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
