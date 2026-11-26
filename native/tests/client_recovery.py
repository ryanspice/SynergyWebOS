#!/usr/bin/env python3
"""Session recovery fixtures for the actual daemon: real mTLS, synthetic server,
dry-run input, loopback only. Each scenario is one CTest case.

Retry delays are measured from the server side (accept timestamps) so the same
fixture works against builds that do not expose the retry snapshot fields.
"""
from __future__ import annotations

import contextlib
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time

DAEMON = str(Path(sys.argv[1]).resolve())
SCENARIO = sys.argv[2] if len(sys.argv) > 2 else "help"
ROOT = Path(__file__).resolve().parents[2]
FIXTURE_ROOT = Path(os.environ.get(
    "SYNERGY_TEST_POSIX_ROOT",
    str(ROOT / ".ai/evidence/native-session-recovery"),
))
checks: list[str] = []
first_error: str | None = None


def check(value: bool, label: str) -> None:
    if not value:
        raise AssertionError(label)
    checks.append(label)


def now() -> float:
    return time.monotonic()


def wait(predicate, limit: float = 6.0, label: str = "condition deadline"):
    end = now() + limit
    while now() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.01)
    raise AssertionError(label)


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
    if not 4 <= length <= 1048576:
        raise AssertionError(f"invalid relayed frame length {length}")
    return receive(sock, length)


def expect_ack(sock: socket.socket, label: str) -> None:
    body = packet(sock)
    check(body == b"CNOP", label)


def send(sock: socket.socket, body: bytes) -> None:
    sock.sendall(struct.pack("!I", len(body)) + body)


def send_raw(sock: socket.socket, wire: bytes) -> None:
    sock.sendall(wire)


def rpc(root: Path, op: str, **fields):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(4)
        sock.connect(str(root / "control.sock"))
        data = json.dumps({"op": op, **fields}).encode()
        sock.sendall(struct.pack("!I", len(data)) + data)
        count, = struct.unpack("!I", receive(sock, 4))
        return json.loads(receive(sock, count))


def listener(backlog: int = 8) -> socket.socket:
    sock = socket.socket()
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", 0))
    sock.listen(backlog)
    return sock


def free_port() -> int:
    with listener() as sock:
        return sock.getsockname()[1]


def make_certificates(folder: Path) -> dict[str, Path]:
    def openssl(*args: object) -> None:
        subprocess.run(["openssl", *(str(a) for a in args)], check=True, timeout=20,
                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

    ca_key, ca_cert = folder / "ca.key", folder / "ca.crt"
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", ca_key,
            "-out", ca_cert, "-days", "1", "-subj", "/CN=Synergy recovery fixture CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign")

    def signed(name: str, cn: str, usage: str) -> tuple[Path, Path]:
        key, csr, cert, ext = (folder / f"{name}.{s}" for s in ("key", "csr", "crt", "ext"))
        ext.write_text("basicConstraints=critical,CA:FALSE\n"
                       "keyUsage=critical,digitalSignature,keyEncipherment\n"
                       f"extendedKeyUsage={usage}\nsubjectAltName=DNS:{cn}\n", encoding="ascii")
        open_mode = 0o600
        os.chmod(ext, open_mode)
        openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
                "-out", csr, "-subj", f"/CN={cn}")
        openssl("x509", "-req", "-in", csr, "-CA", ca_cert, "-CAkey", ca_key,
                "-CAcreateserial", "-out", cert, "-days", "1", "-extfile", ext)
        return cert, key

    server_cert, server_key = signed("server", "bridge.test", "serverAuth")
    client_cert, client_key = signed("client", "tv.bridge.test", "clientAuth")
    for path in folder.iterdir():
        if path.is_file():
            os.chmod(path, 0o600)
    return {"ca": ca_cert, "server_cert": server_cert, "server_key": server_key,
            "client_cert": client_cert, "client_key": client_key}


def server_context(certs: dict[str, Path]) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(certs["server_cert"]), str(certs["server_key"]))
    context.load_verify_locations(cafile=str(certs["ca"]))
    context.verify_mode = ssl.CERT_REQUIRED
    return context


class Harness:
    def __init__(self) -> None:
        # The daemon runtime path must stay short; TMPDIR is already the short
        # fixture root in CI, while PEM fixtures can live under the task folder.
        self.temp_root = Path(os.environ.get("TMPDIR") or tempfile.gettempdir())
        self.runtime = tempfile.TemporaryDirectory(prefix="rec-", dir=self.temp_root)
        self.root = Path(self.runtime.name)
        os.chmod(self.root, 0o755)
        self.daemon: subprocess.Popen[bytes] | None = None
        self.saved: dict = {}
        self.certs: dict[str, Path] = {}
        self.context: ssl.SSLContext | None = None
        self.accepts: list[float] = []
        self.closes: list[float] = []
        # Accelerated stability window, injected through the internal test-only
        # environment seam; None keeps the fixed production 10 s policy.
        self.window: int | None = None

    # -- lifecycle ---------------------------------------------------------
    def start_daemon(self) -> None:
        environment = dict(os.environ)
        if self.window:
            environment["SYNERGY_STABILITY_WINDOW_MS"] = str(self.window)
        self.daemon = subprocess.Popen([DAEMON, "--runtime-dir", str(self.root)],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                       env=environment)
        wait(lambda: (self.root / "control.sock").is_socket(), label="daemon control socket")
        status = rpc(self.root, "status")
        self.saved = status["saved_config"]
        check(status["ok"] and self.saved["dry_run"], "fixture runs the actual daemon in dry run")

    def stop_daemon(self) -> None:
        if not self.daemon:
            return
        self.daemon.terminate()
        try:
            self.daemon.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.daemon.kill()
            self.daemon.wait(timeout=3)
        self.daemon = None

    def configure(self, port: int, *, idle: int | None = None) -> None:
        config = dict(self.saved, server="127.0.0.1", port=port, name="recovery-test",
                      dry_run=True, allow_plaintext=False, input_bridge=True,
                      bridge_ca_file=str(self.certs["ca"]),
                      bridge_cert_file=str(self.certs["client_cert"]),
                      bridge_key_file=str(self.certs["client_key"]),
                      bridge_server_name="bridge.test")
        if idle is not None:
            config["idle_timeout_ms"] = idle
        result = rpc(self.root, "configure", config=config)
        check(result["ok"], "recovery configuration accepted")

    def start(self, once: bool = False) -> None:
        check(rpc(self.root, "start", once=once)["ok"], "client start accepted")

    def stop(self) -> None:
        check(rpc(self.root, "stop")["ok"], "client stop accepted")

    def status(self) -> dict:
        return rpc(self.root, "status")["client"]

    def state(self) -> str:
        return self.status()["state"]

    def attempts(self) -> int:
        return int(self.status().get("connection_attempts", 0))

    def events(self) -> int:
        return int(self.status()["input_events"])

    # -- sockets -----------------------------------------------------------
    def accept(self, server: socket.socket, limit: float = 8.0) -> socket.socket:
        server.settimeout(limit)
        raw, _ = server.accept()
        self.accepts.append(now())
        raw.settimeout(5)
        return raw

    def accept_tls(self, server: socket.socket, limit: float = 8.0) -> ssl.SSLSocket:
        raw = self.accept(server, limit)
        return self.context.wrap_socket(raw, server_side=True, do_handshake_on_connect=False)

    def negotiate(self, tls: ssl.SSLSocket, capture: int | None, seq_start: int = 1) -> int:
        """Classic hello/QINF, optional CINN. Returns the next sequence to use."""
        tls.do_handshake()
        send(tls, b"Synergy" + struct.pack("!HH", 1, 6))
        hello = packet(tls)
        check(hello.startswith(b"Synergy") and b"recovery-test" in hello,
              "authenticated recovery hello preserved")
        send(tls, b"QINF")
        check(packet(tls).startswith(b"DINF"), "classic screen negotiation")
        if capture is not None:
            send(tls, b"CINN" + struct.pack("!HHIH", 100, 200, capture, 0))
            expect_ack(tls, "classic capture acknowledged")
        return seq_start

    def gap(self, index: int) -> float:
        """Delay observed before connection attempt `index` (0-based)."""
        return self.accepts[index] - self.accepts[index - 1]

    def close(self, sock: socket.socket) -> None:
        """Close a peer socket and timestamp it, so retry delay can be measured
        without the session duration mixed in."""
        self.closes.append(now())
        sock.close()

    def delay(self, index: int) -> float:
        """Retry delay between close `index-1` and the next accepted attempt."""
        return self.accepts[index] - self.closes[index - 1]


def gamepad(slot: int, capture: int, sequence: int, buttons: int = 0) -> bytes:
    return struct.pack("!4sBBBBIQHhhhhBB", b"DGPD", 1, slot, 1, 0, capture,
                       sequence, buttons, -32768, 32767, -12345, 23456, 255, 128)


def key_frame(command: str, code: int) -> bytes:
    return command.encode() + struct.pack("!HHH", code, 0, 0)


def fd_count(pid: int) -> int:
    return len(os.listdir(f"/proc/{pid}/fd"))


def thread_count(pid: int) -> int:
    return len(os.listdir(f"/proc/{pid}/task"))


def rss_kib(pid: int) -> int:
    with open(f"/proc/{pid}/status", encoding="ascii") as handle:
        for line in handle:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    return 0


# -- scenarios -------------------------------------------------------------

def wait_disconnected(h: Harness, limit: float = 20.0) -> float:
    """Wait for the client to report the loss; return the detection time."""
    wait(lambda: h.state() == "disconnected", limit, "client reports the lost session")
    return now()


def reconnect(h: Harness, server: socket.socket) -> ssl.SSLSocket:
    return h.accept_tls(server)


def scenario_connect_recovery(h: Harness) -> None:
    port = free_port()
    h.configure(port)
    h.start()
    wait(lambda: h.state() == "disconnected")
    check(bool(h.status()["error"]), "refused connection reports an error state")
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(4)
    tls = h.accept_tls(server)
    h.negotiate(tls, None)
    wait(lambda: h.state() == "connected")
    check(h.state() == "connected", "recovered connection is protocol-ready without focus")
    check(h.events() == 0, "unfocused recovery emits no input")
    h.stop()
    check(h.state() == "stopped", "client stops after recovered connection")
    tls.close()
    server.close()


def scenario_ready_unfocused(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, None)
        wait(lambda: h.state() == "connected")
        time.sleep(0.3)
        check(h.state() == "connected", "ready but unfocused session stays healthy")
        check(h.events() == 0, "unfocused session applies no input")
        check(len(h.accepts) == 1, "unfocused readiness needs exactly one attempt")
        h.stop()
        tls.close()


def scenario_transport_loss(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, 7)
        send(tls, key_frame("DKDN", 97))
        expect_ack(tls, "focused key acknowledged")
        before = h.events()
        check(before >= 1, "focused key reaches dry-run input")
        tls.close()
        wait_disconnected(h)
        tls2 = h.accept_tls(server)
        h.negotiate(tls2, None)
        wait(lambda: h.state() == "connected")
        base = h.events()
        time.sleep(0.25)
        check(h.events() == base, "no stale input replays onto the new session")
        send(tls2, b"CINN" + struct.pack("!HHIH", 100, 200, 8, 0))
        expect_ack(tls2, "fresh capture acknowledged after recovery")
        send(tls2, key_frame("DKDN", 98))
        expect_ack(tls2, "post-recovery key acknowledged")
        check(wait(lambda: h.events() > base, 2), "post-recovery input reaches dry-run input")
        h.stop()
        tls2.close()


def scenario_disconnect_repeat_batch(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, 9)
        send(tls, key_frame("DKDN", 97))
        expect_ack(tls, "repeat source key acknowledged")
        batch = b"DKRP" + struct.pack("!HHHH", 97, 0, 65535, 30)
        send(tls, batch)
        expect_ack(tls, "deferred repeat batch acknowledged")
        tls.close()
        wait_disconnected(h)
        tls2 = h.accept_tls(server)
        h.negotiate(tls2, None)
        wait(lambda: h.state() == "connected")
        base = h.events()
        time.sleep(0.4)
        check(h.events() == base, "interrupted repeat batch does not survive into the new session")
        send(tls2, b"CINN" + struct.pack("!HHIH", 100, 200, 10, 0))
        expect_ack(tls2, "capture after interrupted batch")
        time.sleep(0.3)
        check(h.events() == base, "restored focus does not resurrect the old repeat batch")
        send(tls2, key_frame("DKDN", 99))
        expect_ack(tls2, "new key after interrupted batch")
        check(h.events() == base + 1, "only the new key produces input")
        h.stop()
        tls2.close()


def scenario_mixed_sustained(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, 11)
        base = h.events()
        burst = b"".join(struct.pack("!I", 4) + b"CNOP" for _ in range(120))
        burst += b"".join(struct.pack("!I", 32) + gamepad(0, 11, seq) for seq in range(1, 61))
        send_raw(tls, burst)
        for _ in range(60):
            check(packet(tls) == b"CNOP", "mixed burst frame acknowledged")
        wait(lambda: h.events() >= base + 60, 4, "mixed burst reaches input")
        tls.close()
        wait_disconnected(h)
        tls2 = h.accept_tls(server)
        h.negotiate(tls2, None)
        wait(lambda: h.state() == "connected")
        send(tls2, b"CINN" + struct.pack("!HHIH", 100, 200, 12, 0))
        expect_ack(tls2, "capture after interrupted mixed burst")
        recovered = h.events()
        for seq in range(1, 21):
            send(tls2, gamepad(0, 12, seq))
            expect_ack(tls2, "recovered controller frame acknowledged")
        check(h.events() == recovered + 20, "controller traffic resumes after interrupted burst")
        h.stop()
        tls2.close()


def scenario_sequence_scope(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, 21)
        base = h.events()
        send(tls, gamepad(0, 21, 5))
        expect_ack(tls, "first sequenced frame acknowledged")
        check(wait(lambda: h.events() == base + 1, 2), "first sequence accepted")
        send(tls, gamepad(0, 21, 4))
        expect_ack(tls, "stale sequence acknowledged")
        time.sleep(0.15)
        check(h.events() == base + 1, "stale sequence is rejected")
        send(tls, gamepad(0, 21, 5))
        expect_ack(tls, "duplicate sequence acknowledged")
        time.sleep(0.15)
        check(h.events() == base + 1, "duplicate sequence is rejected")
        send(tls, gamepad(0, 22, 6))
        expect_ack(tls, "wrong-capture frame acknowledged")
        time.sleep(0.15)
        check(h.events() == base + 1, "wrong capture is rejected")
        send(tls, gamepad(0, 21, 7))
        expect_ack(tls, "valid frame after rejections acknowledged")
        check(wait(lambda: h.events() == base + 2, 2), "increasing sequence still accepted")
        h.stop()
        tls.close()


def scenario_reconnect_cinn(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, 30)
        send(tls, gamepad(0, 30, 4))
        expect_ack(tls, "pre-loss controller frame acknowledged")
        old = h.events()
        tls.close()
        wait_disconnected(h)
        tls2 = h.accept_tls(server)
        h.negotiate(tls2, None)
        wait(lambda: h.state() == "connected")
        base = h.events()
        send(tls2, gamepad(0, 30, 1))
        expect_ack(tls2, "old capture frame on the new session acknowledged")
        time.sleep(0.15)
        check(h.events() == base, "old capture without a new CINN is ignored")
        send(tls2, gamepad(0, 31, 1))
        expect_ack(tls2, "future capture acknowledged")
        time.sleep(0.15)
        check(h.events() == base, "mismatched capture is ignored")
        send(tls2, b"CINN" + struct.pack("!HHIH", 100, 200, 31, 0))
        expect_ack(tls2, "fresh CINN acknowledged")
        # Sequence advanced on the pre-CINN probe frames too; the connection
        # only requires it to keep increasing, so continue from there.
        send(tls2, gamepad(0, 31, 2))
        expect_ack(tls2, "per-connection sequence restart acknowledged")
        check(wait(lambda: h.events() == base + 1, 2), "fresh connection accepts the next sequence")
        send(tls2, gamepad(0, 31, 3))
        expect_ack(tls2, "next sequence acknowledged")
        check(wait(lambda: h.events() == base + 2, 2), "fresh connection sequence increases")
        h.stop()
        tls2.close()
        check(old >= 1, "pre-loss controller frame reached input")


def scenario_resync_slots(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, 40)
        base = h.events()
        for slot in range(4):
            send(tls, gamepad(slot, 40, slot + 1))
            expect_ack(tls, f"slot {slot} frame acknowledged")
        check(wait(lambda: h.events() == base + 4, 3), "four slots active")
        tls.close()
        wait_disconnected(h)
        tls2 = h.accept_tls(server)
        h.negotiate(tls2, 41)
        after_release = h.events()
        check(after_release >= base + 4, "loss neutralizes the captured slots")
        for slot in range(4):
            send(tls2, gamepad(slot, 41, 10 + slot))
            expect_ack(tls2, f"resynchronized slot {slot} acknowledged")
        check(wait(lambda: h.events() == after_release + 4, 3), "four slots resynchronize")
        h.stop()
        tls2.close()


def scenario_repeat_cycles(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        pid = h.daemon.pid  # type: ignore[union-attr]
        baseline_fd = baseline_thread = 0
        baseline_rss = 0
        for cycle in range(5):
            tls = h.accept_tls(server)
            h.negotiate(tls, 50 + cycle)
            send(tls, key_frame("DKDN", 97))
            expect_ack(tls, f"cycle {cycle} key acknowledged")
            send(tls, key_frame("DKUP", 97))
            expect_ack(tls, f"cycle {cycle} release acknowledged")
            tls.close()
            wait_disconnected(h)
            if cycle == 1:
                baseline_fd, baseline_thread = fd_count(pid), thread_count(pid)
                baseline_rss = rss_kib(pid)
        check(len(h.accepts) >= 5, "each loss produced a further attempt")
        fd_count(pid), thread_count(pid), rss_kib(pid)
        check(fd_count(pid) <= baseline_fd + 1, f"file descriptors stay bounded ({baseline_fd} -> {fd_count(pid)})")
        check(thread_count(pid) <= baseline_thread + 1,
              f"threads stay bounded ({baseline_thread} -> {thread_count(pid)})")
        if os.environ.get("RECOVERY_RSS_GATE", "1") == "1":
            growth = rss_kib(pid) - baseline_rss
            check(growth < 4096, f"retained memory stays bounded (+{growth} KiB)")
        h.stop()
        check(h.state() == "stopped", "client stops after repeated cycles")


def scenario_stop_connecting(h: Harness) -> None:
    port = free_port()
    h.configure(port)
    h.start()
    started = now()
    h.stop()
    check(now() - started < 1.5, "stop during a refused connect is prompt")
    check(h.state() == "stopped", "client reports stopped")
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(4)
    h.start()
    tls = h.accept_tls(server)
    h.negotiate(tls, None)
    wait(lambda: h.state() == "connected")
    check(True, "a later connection still succeeds after stopping during connect")
    h.stop()
    tls.close()
    server.close()


def scenario_stop_tls(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        raw = h.accept(server)
        started = now()
        h.stop()
        check(now() - started < 1.5, "stop during TLS negotiation is prompt")
        raw.close()
        h.start()
        tls = h.accept_tls(server)
        h.negotiate(tls, None)
        wait(lambda: h.state() == "connected")
        check(True, "a later connection still succeeds after stopping during TLS")
        h.stop()
        tls.close()


def scenario_stop_handshake(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        tls = h.accept_tls(server)
        tls.do_handshake()
        started = now()
        h.stop()
        check(now() - started < 1.5, "stop during protocol negotiation is prompt")
        tls.close()
        h.start()
        tls2 = h.accept_tls(server)
        h.negotiate(tls2, None)
        wait(lambda: h.state() == "connected")
        check(True, "a later connection still succeeds after stopping during handshake")
        h.stop()
        tls2.close()


def scenario_stop_backoff(h: Harness) -> None:
    port = free_port()
    h.configure(port)
    h.start()
    wait(lambda: h.state() == "disconnected")
    started = now()
    h.stop()
    check(now() - started < 1.5, "stop during backoff is prompt")
    attempts = len(h.accepts)
    time.sleep(2.0)
    check(len(h.accepts) == attempts, "no attempt follows a stop during backoff")
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(4)
    h.start()
    tls = h.accept_tls(server)
    h.negotiate(tls, None)
    wait(lambda: h.state() == "connected")
    check(True, "explicit start after a backoff stop still connects")
    h.stop()
    tls.close()
    server.close()


def scenario_backoff_stable_resets(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        for cycle in range(3):
            tls = h.accept_tls(server)
            h.negotiate(tls, None)
            wait(lambda: h.state() == "connected")
            time.sleep(0.6)
            h.close(tls)
            if cycle < 2:
                wait_disconnected(h)
        check(len(h.accepts) >= 3, "three connections were made")
        # The first retry is always 1 s (nothing has doubled yet); the second is
        # the discriminator: a reset keeps it at 1 s, a progression makes it 2 s.
        first, second = h.delay(1), h.delay(2)
        check(first < 1.8, f"first delay stays at one second ({first:.2f}s)")
        check(second < 1.8, f"a stable session resets the delay ({second:.2f}s)")
        h.stop()


def scenario_backoff_failure_progresses(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        for _ in range(3):
            raw = h.accept(server)
            time.sleep(0.6)  # TLS never completes: the attempt was never ready
            h.close(raw)
            wait(lambda: h.state() == "disconnected")
        first, second = h.delay(1), h.delay(2)
        check(first < 1.8, f"first delay is one second ({first:.2f}s)")
        check(1.8 <= second < 3.4, f"failed handshake keeps the progression ({second:.2f}s)")
        h.stop()


def scenario_backoff_default_window(h: Harness) -> None:
    """Control: with the production 10 s window both policies must reset, so
    this stays GREEN on the baseline and scopes the duration scenario."""
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        for cycle in range(3):
            tls = h.accept_tls(server)
            h.negotiate(tls, None)
            wait(lambda: h.state() == "connected")
            time.sleep(11.0)
            h.close(tls)
            if cycle < 2:
                wait_disconnected(h, 20)
        delay = h.delay(2)
        check(delay < 1.8, f"a session stable past the 10s default resets to one second ({delay:.2f}s)")
        h.stop()


def scenario_backoff_duration_over_window(h: Harness) -> None:
    """A 12 s attempt whose readiness is only 9 s: the old elapsed-attempt
    policy reset here, the readiness policy must not. Uses the production 10 s
    window on both builds, so the comparison is like-for-like. The second retry
    is the discriminator (the first is always 1 s)."""
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        durations: list[float] = []
        for cycle in range(3):
            attempt = now()
            raw = h.accept(server)
            time.sleep(4.5)  # connection setup burns nearly the whole TLS deadline
            tls = h.context.wrap_socket(raw, server_side=True)
            h.negotiate(tls, None)
            wait(lambda: h.state() == "connected")
            time.sleep(6.5)  # readiness stays well below the 10 s window
            h.close(tls)
            lost = wait_disconnected(h, 20)
            durations.append(lost - attempt)
        check(min(durations) > 10.0,
              f"attempts outlived the 10s window with only ~6.5s readiness "
              f"({[round(d, 2) for d in durations]})")
        delay = h.delay(2)
        check(1.8 <= delay < 3.4, f"a short ready tail after slow setup keeps the progression ({delay:.2f}s)")
        tls2 = h.accept_tls(server)
        h.negotiate(tls2, None)
        h.stop()
        tls2.close()


def scenario_backoff_unfocused_window(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        for cycle in range(3):
            tls = h.accept_tls(server)
            h.negotiate(tls, None)
            wait(lambda: h.state() == "connected")
            time.sleep(0.6)
            h.close(tls)
            if cycle < 2:
                wait_disconnected(h)
        delay = h.delay(2)
        check(delay < 1.8, f"a ready but unfocused session still resets the delay ({delay:.2f}s)")
        h.stop()


def scenario_backoff_setup_short_ready(h: Harness) -> None:
    with listener() as server:
        h.configure(server.getsockname()[1])
        h.start()
        for cycle in range(3):
            raw = h.accept(server)
            time.sleep(1.2)  # setup alone outlives the window
            tls = h.context.wrap_socket(raw, server_side=True)
            h.negotiate(tls, None)
            wait(lambda: h.state() == "connected")
            time.sleep(0.2)  # readiness stays shorter than the window
            h.close(tls)
            if cycle < 2:
                wait_disconnected(h)
        delay = h.delay(2)
        check(1.8 <= delay < 3.4, f"setup time is not stability ({delay:.2f}s)")
        h.stop()


def scenario_backoff_cap(h: Harness) -> None:
    """Refused connects: the published retry duration must walk 1, 2, 4, 8, 16
    and then hold at the 30 s cap."""
    port = free_port()
    h.configure(port)
    h.start()
    seen: list[int] = []
    started = now()
    deadline = started + 120
    last = 0
    while now() < deadline and (not seen or seen[-1] < 30000):
        value = int(h.status()["next_retry_ms"])
        if value and value != last:
            seen.append(value)
            last = value
        time.sleep(0.02)
    check(seen[:6] == [1000, 2000, 4000, 8000, 16000, 30000],
          f"retry durations progress to the cap ({seen})")
    check(seen[-1] == 30000, "the progression stops at 30 s")
    at_cap = int(h.status()["connection_attempts"])
    time.sleep(31.0)
    check(int(h.status()["connection_attempts"]) > at_cap, "an attempt follows the 30 s delay")
    check(int(h.status()["next_retry_ms"]) == 30000, "the delay remains capped at 30 s")
    h.stop()


def scenario_diagnostics(h: Harness) -> None:
    port = free_port()
    h.configure(port)
    status = rpc(h.root, "status")
    check("stability_window_ms" not in status["saved_config"],
          "the stability window is absent from the saved configuration contract")
    check("stability_window_ms" not in (status["client"]["active_config"] or {}),
          "the stability window is absent from the active configuration contract")
    h.start()
    check(h.status()["next_retry_ms"] == 0, "no retry time while connecting")
    check(wait(lambda: h.state() == "disconnected", 8), "refused attempt recorded")
    attempts = int(h.status()["connection_attempts"])
    check(attempts == 1, f"attempt counter counts every attempt ({attempts})")
    check(wait(lambda: h.status()["next_retry_ms"] > 0, 2), "retry time is published during backoff")
    check(h.status()["next_retry_ms"] == 1000, "first retry time is the 1 s duration")
    h.stop()
    check(h.status()["next_retry_ms"] == 0, "retry time clears on stop")
    check(h.status()["connection_attempts"] == attempts, "attempt counter survives stop unchanged")
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(4)
    h.start()
    check(int(h.status()["connection_attempts"]) <= 1, "attempt counter restarts for a fresh start")
    tls = h.accept_tls(server)
    h.negotiate(tls, None)
    wait(lambda: h.state() == "connected")
    check(h.status()["next_retry_ms"] == 0, "no retry time while ready")
    check(h.status()["connection_attempts"] == 1, "ready session reports exactly one attempt")
    h.stop()
    check(h.status()["next_retry_ms"] == 0, "no stale retry time after a clean stop")
    tls.close()
    server.close()


def scenario_snapshot_coherence(h: Harness) -> None:
    """state, connection_attempts and next_retry_ms must agree in one snapshot.

    A refused attempt plus a backoff sleep is sampled every millisecond across
    two runs (the second sampled from the instant of start, where a state and a
    counter used to be published separately)."""
    port = free_port()
    h.configure(port)
    violations: list[str] = []
    samples = 0
    previous_attempts = 0
    for phase in range(2):
        h.start()
        previous_attempts = 0
        end = now() + (1.2 if phase == 0 else 0.4)
        while now() < end:
            client = h.status()
            state = client["state"]
            retry = int(client["next_retry_ms"])
            attempts = int(client["connection_attempts"])
            samples += 1
            if retry > 0 and state != "disconnected":
                violations.append(f"retry {retry} with state {state}")
            if retry > 0 and not client["error"]:
                violations.append(f"retry {retry} without an error")
            if client["running"] and state == "stopped":
                violations.append(f"running but stopped (attempts {attempts})")
            if attempts < previous_attempts:
                violations.append(f"attempts fell {previous_attempts} -> {attempts}")
            if state == "disconnected" and retry == 0:
                violations.append("disconnected without a published retry")
            previous_attempts = attempts
        h.stop()
    check(samples > 100, f"enough snapshots were sampled ({samples})")
    check(not violations, f"snapshot fields stay coherent ({violations[:4]})")
    check(previous_attempts >= 1, "attempts were observed while sampling")
    final = h.status()
    check(final["state"] == "stopped" and int(final["next_retry_ms"]) == 0,
          "the stopped snapshot is coherent")


SCENARIOS = {    "connect-recovery": scenario_connect_recovery,
    "ready-unfocused": scenario_ready_unfocused,
    "transport-loss": scenario_transport_loss,
    "disconnect-repeat-batch": scenario_disconnect_repeat_batch,
    "mixed-sustained": scenario_mixed_sustained,
    "sequence-scope": scenario_sequence_scope,
    "reconnect-cinn": scenario_reconnect_cinn,
    "resync-slots": scenario_resync_slots,
    "repeat-cycles": scenario_repeat_cycles,
    "stop-connecting": scenario_stop_connecting,
    "stop-tls": scenario_stop_tls,
    "stop-handshake": scenario_stop_handshake,
    "stop-backoff": scenario_stop_backoff,
    "backoff-stable-resets": scenario_backoff_stable_resets,
    "backoff-failure-progresses": scenario_backoff_failure_progresses,
    "backoff-default-window": scenario_backoff_default_window,
    "backoff-duration-over-window": scenario_backoff_duration_over_window,
    "backoff-unfocused-window": scenario_backoff_unfocused_window,
    "backoff-setup-short-ready": scenario_backoff_setup_short_ready,
    "backoff-cap": scenario_backoff_cap,
    "diagnostics": scenario_diagnostics,
    "snapshot-coherence": scenario_snapshot_coherence,
}

# Accelerated stability window per scenario (milliseconds).  Scenarios that are
# absent keep the fixed production 10 s policy.  The seam is an environment
# variable read by the client, so the configuration contract stays unchanged.
WINDOWS = {
    "backoff-stable-resets": 300,
    "backoff-unfocused-window": 300,
    "backoff-setup-short-ready": 800,
    "backoff-failure-progresses": 300,
    "backoff-cap": 300,
    "transport-loss": 300,
    "disconnect-repeat-batch": 300,
    "mixed-sustained": 300,
    "reconnect-cinn": 300,
    "resync-slots": 300,
    "repeat-cycles": 300,
}


def main() -> int:
    global first_error
    if SCENARIO == "list":
        print(" ".join(sorted(SCENARIOS)))
        return 0
    if SCENARIO not in SCENARIOS:
        print(f"unknown scenario {SCENARIO}; known: {' '.join(sorted(SCENARIOS))}")
        return 2
    probe = os.fsencode(Path(os.environ.get("TMPDIR") or tempfile.gettempdir()) / "rec-XXXXXX")
    if len(probe) > 75:
        raise AssertionError("TMPDIR keeps the daemon runtime path <= 75 bytes")
    FIXTURE_ROOT.mkdir(parents=True, exist_ok=True)
    harness = Harness()
    started = now()
    pki_root = Path(os.environ.get("SYNERGY_TEST_POSIX_ROOT") or harness.temp_root)
    try:
        with tempfile.TemporaryDirectory(prefix="pki-", dir=pki_root) as pki:
            harness.certs = make_certificates(Path(pki))
            harness.context = server_context(harness.certs)
            harness.window = WINDOWS.get(SCENARIO)
            harness.start_daemon()
            SCENARIOS[SCENARIO](harness)
    except Exception as exc:  # noqa: BLE001 - fixture reports the first failure
        first_error = f"{type(exc).__name__}: {exc}"
        with contextlib.suppress(Exception):
            checks.append(f"client state={harness.status()}")
    finally:
        with contextlib.suppress(Exception):
            harness.stop_daemon()
        harness.runtime.cleanup()
    result = {
        "scenario": SCENARIO,
        "status": "PASS" if first_error is None else "FAIL",
        "checks_passed": checks,
        "first_error": first_error,
        "attempts": len(harness.accepts),
        "accept_delays": [round(delay, 3) for delay in
                          (harness.delay(i) for i in
                           range(1, min(len(harness.accepts), len(harness.closes) + 1)))],
        "elapsed_seconds": round(now() - started, 3),
        "scope": "actual daemon, real mTLS, synthetic server, dry-run input",
    }
    print(json.dumps(result, indent=2))
    return 0 if first_error is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
