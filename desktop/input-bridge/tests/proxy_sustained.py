#!/usr/bin/env python3
"""Sustained mixed-traffic fairness checks for the actual desktop proxy."""
from __future__ import annotations

import json
import os
from pathlib import Path
import runpy
import select
import signal
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time

MAX_FRAME = 4 * 1024 * 1024
QUEUE_LIMIT = 64 * 1024
CAPTURE = 77
# Hard overall deadline for the whole fixture.  Per-socket timeouts alone can
# leave a wedged phase (or a wedged thread) running until the CI job deadline;
# SIGALRM interrupts the main thread even inside a blocking socket call and the
# normal ``finally`` cleanup still runs.
OVERALL_DEADLINE_SECONDS = 90


def install_deadline() -> bool:
    if not hasattr(signal, "SIGALRM"):
        return False

    def expire(*_args: object) -> None:
        raise TimeoutError(
            f"sustained fixture exceeded its {OVERALL_DEADLINE_SECONDS} s overall deadline"
        )

    signal.signal(signal.SIGALRM, expire)
    signal.alarm(OVERALL_DEADLINE_SECONDS)
    return True


def process_cpu_seconds(pid: int) -> float | None:
    """CPU time (user+system) of the proxy, Linux /proc only."""
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().split()
        ticks = int(fields[13]) + int(fields[14])
    except (OSError, IndexError, ValueError):
        return None
    clock = getattr(os, "sysconf", None)
    if clock is None:
        return None
    return round(ticks / clock("SC_CLK_TCK"), 3)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: proxy_sustained.py PROXY [RESULT_JSON]")
    binary = str(Path(sys.argv[1]).resolve())
    project = Path(__file__).resolve().parents[3]
    evidence = project / ".ai/evidence/input-bridge-sustained-fairness"
    evidence.mkdir(parents=True, exist_ok=True)
    result_path = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else evidence / "proxy-sustained-result.json"
    helpers = runpy.run_path(str(project / "desktop/input-bridge/tests/proxy_loopback.py"))
    checks: list[str] = []
    first_error: str | None = None
    proxy_stderr = ""
    deadline_installed = install_deadline()
    started = time.monotonic()
    metrics: dict[str, object] = {}

    def check(value: bool, label: str) -> None:
        if not value:
            raise AssertionError(label)
        checks.append(label)

    def join_checked(thread: threading.Thread, label: str, timeout: float = 10.0) -> None:
        """Join with a real bound; a wedged helper thread is a failure."""
        thread.join(timeout)
        check(not thread.is_alive(), f"{label} thread stops within its deadline")
        check(getattr(thread, "error", None) is None,
              f"{label} thread ends without error: {getattr(thread, 'error', None)}")

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
        if not 4 <= length <= MAX_FRAME:
            raise AssertionError(f"invalid relayed frame length: {length}")
        return receive(sock, length)

    def parse_diagnostics(text: str) -> dict[str, int]:
        line = next((item for item in text.splitlines() if item.startswith("Relay stats:")), "")
        values: dict[str, int] = {}
        for part in line.partition(":")[2].split():
            key, _, value = part.partition("=")
            if key and value.isdigit():
                values[key] = int(value)
        return values

    def parse_send_buffer(text: str) -> dict[str, object]:
        line = next((item for item in text.splitlines() if item.startswith("TV send buffer:")), "")
        if not line:
            return {"reported": False}
        numbers = [int(token) for token in line.replace(",", " ").split() if token.isdigit()]
        return {
            "reported": True,
            "line": line,
            "requested_bytes": numbers[0] if numbers else None,
            "effective_bytes": numbers[1] if len(numbers) > 1 else None,
        }

    private_root = tempfile.TemporaryDirectory(prefix="sy-sustain-")
    os.chmod(private_root.name, 0o700)
    proxy: subprocess.Popen[str] | None = None
    stderr_collector: StderrCollector | None = None
    core_listener: socket.socket | None = None
    server: socket.socket | None = None
    tv: ssl.SSLSocket | None = None
    writer: FixtureWriter | None = None
    try:
        with tempfile.TemporaryDirectory(prefix="pki-", dir=private_root.name) as pki:
            certs = helpers["make_certificates"](Path(pki))
            fixture = Path(private_root.name) / "pad.txt"
            helpers["write_fixture"](fixture, [
                f"{slot} 1 0 {1000 + slot} {2000 + slot} {3000 + slot} {4000 + slot} 10 20"
                for slot in range(4)
            ])
            core_listener = helpers["listener"]()
            port_probe = socket.socket()
            port_probe.bind(("127.0.0.1", 0))
            proxy_port = port_probe.getsockname()[1]
            port_probe.close()
            proxy = subprocess.Popen(
                [binary, "--ca", str(certs["ca"]), "--cert", str(certs["server_cert"]),
                 "--key", str(certs["server_key"]), "--client-sha256", str(certs["pin"]),
                 "--listen", "127.0.0.1", "--port", str(proxy_port),
                 "--upstream-port", str(core_listener.getsockname()[1]), "--fixture", str(fixture)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            stderr_collector = StderrCollector(proxy.stderr)
            stderr_collector.start()
            ready: list[str] = []

            def read_ready() -> None:
                assert proxy and proxy.stdout
                ready.append(proxy.stdout.readline())

            ready_thread = threading.Thread(target=read_ready, daemon=True)
            ready_thread.start()
            join_checked(ready_thread, "proxy readiness", 4)
            check(bool(ready and "Bridge ready" in ready[0]), "actual proxy starts for sustained loopback")

            raw = socket.create_connection(("127.0.0.1", proxy_port), timeout=4)
            raw.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            receive_buffer = raw.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
            raw.settimeout(4)
            tv = helpers["client_context"](certs).wrap_socket(raw, server_hostname="bridge.test")
            server, _ = core_listener.accept()
            server.settimeout(10)

            hello = b"Synergy" + struct.pack("!HH", 1, 6)
            tv.sendall(helpers["framed"](hello))
            check(receive(server, len(helpers["framed"](hello))) == helpers["framed"](hello),
                  "classic hello enters the production relay")
            server.sendall(helpers["framed"](hello + b" sustained-core"))
            check(packet(tv) == hello + b" sustained-core", "classic hello response remains intact")

            cinn = b"CINN" + struct.pack("!HHIH", 100, 200, CAPTURE, 0)
            server.sendall(helpers["framed"](cinn))
            check(packet(tv) == cinn, "focus entry precedes controller traffic")
            tv.sendall(helpers["framed"](b"CNOP"))
            check(receive(server, 8) == helpers["framed"](b"CNOP"), "focus acknowledgement relays")

            initial_slots: set[int] = set()
            last_sequence = 0
            tv.settimeout(4)
            while len(initial_slots) < 4:
                fields = helpers["decode_pad"](packet(tv))
                check(fields[5] == CAPTURE and fields[6] > last_sequence,
                      "initial controller reports use the focused capture and ordered sequence")
                last_sequence = fields[6]
                initial_slots.add(fields[2])
            check(initial_slots == {0, 1, 2, 3}, "all four controller slots enter the production path")

            writer = FixtureWriter(fixture, helpers["write_fixture"])
            writer.start()

            flood = [
                b"DCLP" + struct.pack("!I", index) + bytes((index + offset * 17) & 0xff for offset in range(1020))
                for index in range(512)
            ]
            sender = SendThread(server, b"".join(helpers["framed"](body) for body in flood))
            sender.start()
            # Withhold the small TV receive window while offering eight times
            # the relay's 64 KiB classic queue budget plus live pad updates.
            time.sleep(0.25)

            classic_inputs: list[bytes] = []
            for index in range(96):
                command = index % 4
                if command == 0:
                    classic_inputs.append(b"DKDN" + struct.pack("!I", index))
                elif command == 1:
                    classic_inputs.append(b"DKUP" + struct.pack("!I", index))
                elif command == 2:
                    classic_inputs.append(b"DMMV" + struct.pack("!II", index, 1000 + index))
                else:
                    classic_inputs.append(b"CALV")
            input_stream = b"".join(helpers["framed"](body) for body in classic_inputs)
            upstream = ReceiveThread(server, len(input_stream))
            upstream.start()
            tv.sendall(input_stream)

            classic_times: list[float] = []
            classic_count = 0
            pad_count = 0
            alternations = 0
            pad_run = 0
            max_pad_run = 0
            button_phase = 0
            button_edges: dict[int, list[int]] = {slot: [] for slot in range(4)}
            phase_started = time.monotonic()
            deadline = phase_started + 20
            while classic_count < len(flood):
                if button_phase == 0 and classic_count >= 384:
                    writer.buttons = [1, 1, 1, 1]
                    button_phase = 1
                if button_phase == 1 and classic_count >= 460:
                    writer.buttons = [0, 0, 0, 0]
                    button_phase = 2
                tv.settimeout(max(0.01, deadline - time.monotonic()))
                body = packet(tv)
                observed = time.monotonic()
                if body[:4] == b"DGPD":
                    fields = helpers["decode_pad"](body)
                    check(fields[1] == 1 and fields[4] == 0 and fields[5] == CAPTURE,
                          "sustained controller frames retain wire and capture fields")
                    check(fields[6] > last_sequence, "controller sequence remains strictly increasing")
                    last_sequence = fields[6]
                    pad_count += 1
                    if classic_count:
                        pad_run += 1
                        max_pad_run = max(max_pad_run, pad_run)
                    if button_phase:
                        slot = fields[2]
                        buttons = fields[7]
                        if not button_edges[slot] or button_edges[slot][-1] != buttons:
                            button_edges[slot].append(buttons)
                else:
                    check(body == flood[classic_count],
                          f"classic frame {classic_count} retains exact bytes and order")
                    classic_count += 1
                    classic_times.append(observed)
                    if pad_run:
                        alternations += 1
                    pad_run = 0
                time.sleep(0.001)

            sender.join(10)
            upstream.join(10)
            join_checked(sender, "offered classic flood", 10)
            join_checked(upstream, "upstream relay", 10)
            check(not sender.error and sender.completed, "offered classic flood is fully accepted by the proxy")
            check(not upstream.error and upstream.data == input_stream,
                  "interleaved keyboard, pointer and keepalive bytes reach Core unchanged")
            check(not writer.error, f"continuous fixture writer remains healthy: {writer.error}")
            metrics["observed_before_assertions"] = {
                "classic_frames": classic_count,
                "controller_frames": pad_count,
                "alternations": alternations,
                "max_controller_burst": max_pad_run,
                "fixture_updates": writer.updates,
                "writer_error": writer.error,
            }
            check(pad_count >= 20 and alternations >= 8,
                  "both traffic classes make repeated progress under established contention")
            check(max_pad_run <= 4, f"controller burst exceeds four while classic is queued: {max_pad_run}")
            classic_gaps = [later - earlier for earlier, later in zip(classic_times, classic_times[1:])]
            max_classic_gap = max(classic_gaps, default=0.0)
            check(max_classic_gap < 3.0, "classic progress stays below the production relay deadline")
            for slot in range(4):
                edges = button_edges[slot]
                check(1 in edges and 0 in edges[edges.index(1) + 1:],
                      f"slot {slot} preserves its button press and subsequent release")
            check(writer.updates > pad_count, "analog fixture production exceeds delivery, exercising coalescing")
            metrics["contention"] = {
                "classic_frames": classic_count,
                "classic_payload_bytes": sum(map(len, flood)),
                "classic_wire_bytes": sum(len(body) + 4 for body in flood),
                "controller_frames": pad_count,
                "classic_controller_alternations": alternations,
                "fixture_updates": writer.updates,
                "initial_reader_hold_seconds": 0.25,
                "tv_receive_buffer_bytes": receive_buffer,
                "relay_queue_limit_bytes": QUEUE_LIMIT,
                "phase_seconds": round(time.monotonic() - phase_started, 3),
            }
            metrics["fairness"] = {
                "max_controller_burst": max_pad_run,
                "max_classic_progress_gap_seconds": round(max_classic_gap, 6),
                "classic_progress_seconds": round(classic_times[-1] - classic_times[0], 6),
                "upstream_classic_progress_seconds": round(upstream.elapsed, 6),
            }

            # Queue another classic burst and leave focus while both classes
            # remain live. COUT is ordered after those classic frames.
            leave_flood = [
                b"DCLP" + struct.pack("!I", 10000 + index) + bytes((index * 31 + offset) & 0xff for offset in range(508))
                for index in range(64)
            ]
            leave_sender = SendThread(
                server,
                b"".join(helpers["framed"](body) for body in leave_flood) + helpers["framed"](b"COUT"),
            )
            leave_sender.start()
            time.sleep(0.1)
            leave_classic = 0
            leave_pads = 0
            leave_deadline = time.monotonic() + 10
            while True:
                tv.settimeout(max(0.01, leave_deadline - time.monotonic()))
                body = packet(tv)
                if body == b"COUT":
                    break
                if body[:4] == b"DGPD":
                    fields = helpers["decode_pad"](body)
                    check(fields[5] == CAPTURE and fields[6] > last_sequence,
                          "pre-leave in-flight controller writes remain complete and ordered")
                    last_sequence = fields[6]
                    leave_pads += 1
                else:
                    check(body == leave_flood[leave_classic], "queued pre-leave classic bytes stay intact")
                    leave_classic += 1
            leave_sender.join(5)
            join_checked(leave_sender, "focus-loss classic burst", 5)
            check(leave_classic == len(leave_flood) and not leave_sender.error,
                  "focus boundary follows every earlier classic frame")
            writer.stop()
            join_checked(writer, "continuous fixture writer", 5)
            helpers["write_fixture"](fixture, ["invalid fixture must remain unread after COUT"])

            observation_started = time.monotonic()
            while time.monotonic() - observation_started < 0.3:
                if tv.pending() or select.select([tv], [], [], 0.05)[0]:
                    unexpected = packet(tv)
                    raise AssertionError(f"stale post-leave relay frame: {unexpected[:16]!r}")
            check(True, "no stale controller or classic frame escapes after COUT")

            server.sendall(helpers["framed"](b"CALV"))
            check(packet(tv) == b"CALV", "classic keepalive makes progress after queued focus loss")
            tv.sendall(helpers["framed"](b"CNOP"))
            check(receive(server, 8) == helpers["framed"](b"CNOP"), "off-focus keepalive acknowledgement relays")
            server.sendall(helpers["framed"](b"CBYE"))
            check(packet(tv) == b"CBYE", "existing disconnect command reaches the TV boundary")
            tv.close()
            tv = None
            server.settimeout(2)
            check(server.recv(1) == b"", "TV disconnect closes the upstream session within its deadline")
            metrics["focus_and_cleanup"] = {
                "queued_leave_classic_frames": leave_classic,
                "queued_leave_controller_frames": leave_pads,
                "post_leave_observation_seconds": 0.3,
                "post_leave_frames": 0,
                "disconnect_observed": True,
            }

            # Cost and retry observations.  The frozen-write invariant is
            # checked inside the proxy on every retry, so a nonzero classic
            # retry count proves the invariant ran under live backpressure.
            session_stderr = ""
            settle = time.monotonic() + 3.0
            while "Relay stats:" not in session_stderr and time.monotonic() < settle:
                session_stderr = stderr_collector.snapshot()
                if "Relay stats:" not in session_stderr:
                    time.sleep(0.02)
            proxy_stderr += session_stderr
            diagnostics = parse_diagnostics(session_stderr)
            metrics["session_diagnostics"] = diagnostics
            metrics["send_buffer"] = parse_send_buffer(session_stderr)
            cpu = process_cpu_seconds(proxy.pid)
            wall = time.monotonic() - started
            metrics["cost"] = {
                "proxy_cpu_seconds": cpu,
                "wall_seconds": round(wall, 3),
                "classic_wire_bytes_per_second": round(
                    metrics["contention"]["classic_wire_bytes"] / max(1e-9, metrics["contention"]["phase_seconds"]), 1),
                "counters_are_proxy_session_totals": True,
            }
            check(diagnostics.get("classic_write_retries", 0) > 0,
                  "classic TLS writes took at least one WANT retry under sustained backpressure")
            check(diagnostics.get("classic_writes", 0) > 0 and diagnostics.get("controller_writes", 0) > 0,
                  "session diagnostics report both write classes")
            buffer_report = metrics["send_buffer"]
            check(bool(buffer_report.get("reported")), "proxy reports the TV send-buffer request")
            effective_buffer = buffer_report.get("effective_bytes")
            check(isinstance(effective_buffer, int) and effective_buffer >= 16384,
                  f"proxy reads back a bound effective TV send buffer: {effective_buffer}")
            check(diagnostics.get("peak_queue_bytes", 0) <= QUEUE_LIMIT,
                  "peak classic queue depth stays inside its budget")
    except Exception as exc:
        first_error = f"{type(exc).__name__}: {exc}"
    finally:
        if deadline_installed:
            signal.alarm(0)
        if writer:
            writer.stop()
            writer.join(3)
        if tv:
            tv.close()
        if server:
            server.close()
        if core_listener:
            core_listener.close()
        if proxy:
            proxy.terminate()
            try:
                proxy.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proxy.kill()
                proxy.wait(timeout=2)
            if proxy.stderr:
                proxy_stderr += proxy.stderr.read()
        if stderr_collector:
            stderr_collector.join(2)
            snapshot = stderr_collector.snapshot()
            if snapshot and snapshot not in proxy_stderr:
                proxy_stderr += snapshot
        private_root.cleanup()

    result = {
        "status": "PASS" if first_error is None else "FAIL",
        "binary": binary,
        "checks_passed": checks,
        "first_error": first_error,
        "metrics": metrics,
        "proxy_stderr": proxy_stderr[-4000:],
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "overall_deadline_seconds": OVERALL_DEADLINE_SECONDS if deadline_installed else None,
        "production_path": "actual proxy binary, real authenticated TLS, synthetic Core and controller fixtures",
        "helper_level_evidence": [
            "bridge-wire-test separately covers exact queue budgets, stale-event pruning, safety projection, and in-flight sample ownership",
            "the proxy reports session write/retry counters and the effective TV send buffer on stderr; the fixture parses them here",
        ],
        "scope_limits": [
            "contention is established by a small receive window, a reader hold, and offered classic bytes exceeding the relay queue budget while live pads update",
            "no physical controller, TV, live Synergy server, driver, or installed service",
            "host progress timings are observations, not target latency guarantees",
        ],
    }
    result_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if first_error is None else 1


class StderrCollector(threading.Thread):
    """Continuously drain proxy stderr so the pipe can never fill and the
    session diagnostics are available immediately when they are printed."""

    def __init__(self, stream) -> None:
        super().__init__(daemon=True)
        self.stream = stream
        self.text = ""
        self._lock = threading.Lock()

    def run(self) -> None:
        try:
            for line in self.stream:
                with self._lock:
                    self.text += line
        except Exception:
            return

    def snapshot(self) -> str:
        with self._lock:
            return self.text


class SendThread(threading.Thread):
    def __init__(self, sock: socket.socket, data: bytes) -> None:
        super().__init__(daemon=True)
        self.sock = sock
        self.data = data
        self.error: str | None = None
        self.completed = False
        self.elapsed = 0.0

    def run(self) -> None:
        started = time.monotonic()
        try:
            self.sock.sendall(self.data)
            self.completed = True
        except Exception as exc:
            self.error = f"{type(exc).__name__}: {exc}"
        self.elapsed = time.monotonic() - started


class ReceiveThread(threading.Thread):
    def __init__(self, sock: socket.socket, size: int) -> None:
        super().__init__(daemon=True)
        self.sock = sock
        self.size = size
        self.data = b""
        self.error: str | None = None
        self.elapsed = 0.0

    def run(self) -> None:
        started = time.monotonic()
        try:
            data = bytearray()
            while len(data) < self.size:
                part = self.sock.recv(self.size - len(data))
                if not part:
                    raise EOFError("Core closed before receiving the classic input stream")
                data += part
            self.data = bytes(data)
        except Exception as exc:
            self.error = f"{type(exc).__name__}: {exc}"
        self.elapsed = time.monotonic() - started


class FixtureWriter(threading.Thread):
    def __init__(self, path: Path, write_fixture) -> None:
        super().__init__(daemon=True)
        self.path = path
        self.write_fixture = write_fixture
        self.buttons = [0, 0, 0, 0]
        self.updates = 0
        self.error: str | None = None
        self._stop_event = threading.Event()

    def stop(self) -> None:
        self._stop_event.set()

    def run(self) -> None:
        tick = 0
        try:
            while not self._stop_event.is_set():
                tick += 1
                rows = []
                for slot in range(4):
                    lx = 1 + (tick * 113 + slot * 997) % 30000
                    ly = 1 + (tick * 127 + slot * 1999) % 30000
                    rx = 1 + (tick * 139 + slot * 2999) % 30000
                    ry = 1 + (tick * 149 + slot * 3999) % 30000
                    lt = 1 + (tick + slot) % 254
                    rt = 1 + (tick * 3 + slot) % 254
                    rows.append(f"{slot} 1 {self.buttons[slot]} {lx} {ly} {rx} {ry} {lt} {rt}")
                self.write_fixture(self.path, rows)
                self.updates += 1
                time.sleep(0.002)
        except Exception as exc:
            self.error = f"{type(exc).__name__}: {exc}"


if __name__ == "__main__":
    raise SystemExit(main())
