#!/usr/bin/env python3
"""Exercise the actual Linux daemon through the mTLS desktop proxy."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import runpy
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: native_proxy_e2e.py PROXY DAEMON CTL")
    proxy_bin, daemon_bin, ctl_bin = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
    project = Path(__file__).resolve().parents[3]
    evidence = project / ".ai/evidence/input-0.4.1-implementation-20261113/proxy-verification"
    evidence.mkdir(parents=True, exist_ok=True)
    private_root = tempfile.TemporaryDirectory(prefix="synergy-proxy-e2e-")
    os.chmod(private_root.name, 0o700)
    os.environ["SYNERGY_TEST_POSIX_ROOT"] = private_root.name

    native_test = project / "native/tests/bridge_tls_integration.py"
    old_argv = sys.argv
    sys.argv = [str(native_test), daemon_bin, ctl_bin]
    try:
        helpers = runpy.run_path(str(native_test))
    except Exception:
        private_root.cleanup()
        raise
    finally:
        sys.argv = old_argv

    checks: list[str] = []
    proxy: subprocess.Popen[str] | None = None
    daemon: subprocess.Popen[str] | None = None
    core_listener: socket.socket | None = None
    core: socket.socket | None = None
    fixture: Path | None = None
    runtime: tempfile.TemporaryDirectory[str] | None = None
    first_error: str | None = None
    started = time.monotonic()
    proxy_stderr = ""
    daemon_stderr = ""
    daemon_stdout = ""

    def check(value: bool, label: str) -> None:
        if not value:
            raise AssertionError(label)
        checks.append(label)

    try:
        with tempfile.TemporaryDirectory(prefix="ephemeral-proxy-daemon-pki-", dir=private_root.name) as pki_dir:
            certs = helpers["make_certificates"](Path(pki_dir))
            key_files = list(Path(pki_dir).glob("*.key"))
            check(bool(key_files) and all(os.stat(key).st_mode & 0o777 == 0o600 for key in key_files),
                  "all ephemeral TLS private key files are mode 0600")
            client_der = ssl.PEM_cert_to_DER_cert(certs["client_cert"].read_text(encoding="ascii"))
            pin = hashlib.sha256(client_der).hexdigest()

            fixture = evidence / "daemon-fixture.txt"
            fixture.write_text("0 1 4097 100 -200 300 -400 64 192\n", encoding="ascii")
            os.chmod(fixture, 0o600)

            core_listener = helpers["listener"]()
            core_port = core_listener.getsockname()[1]
            port_probe = socket.socket()
            port_probe.bind(("127.0.0.1", 0))
            bridge_port = port_probe.getsockname()[1]
            port_probe.close()

            proxy = subprocess.Popen(
                [proxy_bin, "--ca", str(certs["ca"]), "--cert", str(certs["server_cert"]),
                 "--key", str(certs["server_key"]), "--client-sha256", pin,
                 "--listen", "127.0.0.1", "--port", str(bridge_port),
                 "--upstream-port", str(core_port), "--fixture", str(fixture)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            ready: list[str] = []
            def read_proxy_ready() -> None:
                assert proxy and proxy.stdout
                ready.append(proxy.stdout.readline())
            ready_thread = threading.Thread(target=read_proxy_ready, daemon=True)
            ready_thread.start()
            ready_thread.join(4)
            check(bool(ready and "Bridge ready" in ready[0]),
                  "actual desktop proxy is ready in loopback fixture mode")

            runtime = tempfile.TemporaryDirectory(prefix="syn-bridge-runtime-", dir=private_root.name)
            check(len(os.fsencode(runtime.name)) <= 75, "daemon runtime socket path fits its 75-byte limit")
            runtime_path = Path(runtime.name)
            os.chmod(runtime_path, 0o755)
            daemon = subprocess.Popen(
                [daemon_bin, "--runtime-dir", runtime.name],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            try:
                helpers["wait"](lambda: (runtime_path / "control.sock").is_socket())
            except Exception as exc:
                if daemon.poll() is None:
                    daemon.terminate()
                    try:
                        daemon.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        daemon.kill()
                        daemon.wait(timeout=2)
                daemon_stdout = daemon.stdout.read() if daemon.stdout else ""
                daemon_stderr = daemon.stderr.read() if daemon.stderr else ""
                raise RuntimeError(
                    f"control socket did not become ready; exit={daemon.returncode}; "
                    f"stdout={daemon_stdout[-1500:]!r}; stderr={daemon_stderr[-1500:]!r}"
                ) from exc
            status = helpers["rpc"](runtime_path, "status")
            check(status["ok"] and status["saved_config"]["dry_run"],
                  "actual daemon starts in dry-run mode")
            config = helpers["bridge_config"](status["saved_config"], bridge_port, certs)
            helpers["configure"](runtime_path, config)
            helpers["start_once"](runtime_path)

            assert core_listener
            core_listener.settimeout(5)
            core, _ = core_listener.accept()
            core.settimeout(4)
            input_before = helpers["rpc"](runtime_path, "status")["client"]["input_events"]
            helpers["classic_handshake"](core, 4101)
            after_input = helpers["wait"](
                lambda: (s if (s := helpers["rpc"](runtime_path, "status"))["client"]["input_events"] > input_before else None),
                limit=2.0,
            )
            check(after_input["client"]["input_events"] > input_before,
                  "proxy-generated DGPD reaches the actual daemon input path")
            check(after_input["client"]["running"],
                  "actual daemon remains connected through authenticated proxy")

            before_leave = after_input["client"]["input_events"]
            helpers["send"](core, b"COUT")
            check(helpers["packet"](core) == b"CNOP", "actual daemon acknowledges focus leave")
            after_leave = helpers["wait"](
                lambda: (s if (s := helpers["rpc"](runtime_path, "status"))["client"]["input_events"] > before_leave else None),
                limit=2.0,
            )
            check(after_leave["client"]["input_events"] > before_leave,
                  "actual daemon leave cleanup emits controller neutralization")

            fixture.write_text("invalid fixture is not polled after COUT\n", encoding="ascii")
            time.sleep(0.2)
            stable = helpers["rpc"](runtime_path, "status")
            check(stable["client"]["running"] and not stable["client"]["error"] and
                  stable["client"]["input_events"] == after_leave["client"]["input_events"],
                  "focus-leave stops polling and preserves the live classic session")
            helpers["send"](core, b"CALV")
            # DGPD and COUT acknowledgements may already be queued. Wait for
            # this keepalive's distinct echo, then its ordered CNOP, rather
            # than mistaking an earlier acknowledgement for the reply.
            keepalive_deadline = time.monotonic() + 2.0
            for _ in range(256):
                remaining = keepalive_deadline - time.monotonic()
                if remaining <= 0:
                    raise AssertionError("classic keepalive echo deadline")
                core.settimeout(remaining)
                response = helpers["packet"](core)
                if response == b"CALV":
                    break
                if response != b"CNOP":
                    raise AssertionError(f"unexpected response before keepalive echo: {response[:16]!r}")
            else:
                raise AssertionError("classic acknowledgement budget before keepalive echo")
            core.settimeout(2)
            check(helpers["packet"](core) == b"CNOP", "live classic keepalive works after controller cleanup")
            stop_result = helpers["rpc"](runtime_path, "stop")
            check(stop_result["ok"], "actual daemon Stop is accepted during the bridge session")
            stopped = helpers["wait"](
                lambda: (s if not (s := helpers["rpc"](runtime_path, "status"))["client"]["running"] else None),
                limit=2.0,
            )
            check(not stopped["client"]["running"], "actual daemon Stop closes the active bridged session")
            core.settimeout(4)
            pending = bytearray()
            while True:
                try:
                    part = core.recv(4096)
                except socket.timeout as exc:
                    raise AssertionError("synthetic core did not observe stream closure after Stop") from exc
                if not part:
                    break
                pending.extend(part)
            while pending:
                if len(pending) < 4:
                    raise AssertionError("Stop left a partial classic frame at synthetic core")
                length = int.from_bytes(pending[:4], "big")
                if length > 1024 * 1024 or len(pending) < 4 + length:
                    raise AssertionError("Stop left an incomplete or oversized classic frame")
                body = bytes(pending[4:4 + length])
                if body != b"CNOP":
                    raise AssertionError(f"unexpected queued classic response before Stop close: {body[:16]!r}")
                del pending[:4 + length]
            checks.append("synthetic core saw only complete CNOP acknowledgements before clean Stop closure")
            checks.append("actual-daemon integration used dry-run input only")
    except Exception as exc:
        first_error = f"{type(exc).__name__}: {exc}"
    finally:
        if daemon and daemon.poll() is None:
            try:
                if runtime:
                    helpers["rpc"](Path(runtime.name), "stop")
            except Exception:
                pass
            daemon.terminate()
            try:
                daemon.wait(timeout=3)
            except subprocess.TimeoutExpired:
                daemon.kill()
                daemon.wait(timeout=2)
            if daemon.stdout and not daemon_stdout:
                daemon_stdout = daemon.stdout.read()
            if daemon.stderr and not daemon_stderr:
                daemon_stderr = daemon.stderr.read()
        if proxy:
            proxy.terminate()
            try:
                proxy.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proxy.kill()
                proxy.wait(timeout=2)
            if proxy.stderr:
                proxy_stderr = proxy.stderr.read()
        if core:
            core.close()
        if core_listener:
            core_listener.close()
        if fixture:
            fixture.unlink(missing_ok=True)
        if runtime:
            runtime.cleanup()
        private_root.cleanup()

    result = {
        "status": "PASS" if first_error is None else "FAIL",
        "proxy": proxy_bin,
        "daemon": daemon_bin,
        "ctl": ctl_bin,
        "checks_passed": checks,
        "first_error": first_error,
        "proxy_stderr": proxy_stderr[-3000:],
        "daemon_stderr": daemon_stderr[-3000:],
        "daemon_stdout": daemon_stdout[-3000:],
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "synthetic_upstream": "127.0.0.1 ephemeral port",
        "runtime_and_pki": "private mode-0700 WSL /tmp root removed; DrvFs project ancestors are mode 0777 and rejected by native safety checks",
        "scope_limits": [
            "dry-run input status only; no uinput device or physical controller",
            "no installed Synergy server, TV, or driver",
        ],
    }
    (evidence / "native-proxy-e2e-result.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, indent=2))
    return 0 if first_error is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
