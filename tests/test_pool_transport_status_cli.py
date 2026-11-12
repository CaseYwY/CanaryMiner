"""Fresh-process status reads from owned loopback HTTP; no GPU, pool login or user configuration."""
import argparse
import copy
import http.server
import json
from pathlib import Path
import subprocess
import threading
import time


def run_cases(command_for_port, *, env=None, cwd=None, record=None):
    """The caller chooses only its status entry/explicit config; this fixture owns every HTTP peer."""
    private = "STATUS-WALLET-SENTINEL-NEVER-DISPLAY"
    base = {
        "schema": "canaryminer-stats-v1", "version": "cpu-status-fixture", "state": "running",
        "uptime_seconds": 1, "wallet": private, "devices": [], "outcomes": {},
        "pool": {"endpoint": "status-fixture.test:8048", "preset": "", "state": "mining"},
        "donation": {"state": "off", "note": "CPU fixture"},
    }
    cases = [
        ("verified", {"state": "verified_tls", "error": "none", "port": 8048, "age_ms": 0}, 0,
         "First user pool transport: Verified TLS (port 8048)", ""),
        ("plaintext", {"state": "plaintext", "error": "none", "port": 7048, "age_ms": 0}, 0,
         "First user pool transport: Plaintext (port 7048)", ""),
        ("verification-error", {"state": "disconnected", "error": "verification", "port": 0, "age_ms": 0}, 0,
         "First user pool transport: Disconnected", "Transport error: verification"),
        ("old-json", None, 0, "First user pool transport: Unknown", ""),
        ("invalid-transport", {"state": "verified_tls", "error": private, "port": 8048, "age_ms": 0}, 0,
         "First user pool transport: Unknown", ""),
        ("slow-response", {"state": "verified_tls", "error": "none", "port": 8048, "age_ms": 4800}, 0.3,
         "First user pool transport: Unknown", ""),
    ]
    results = []
    for name, transport, delay, expected, expected_error in cases:
        document = copy.deepcopy(base)
        if transport is not None:
            document["pool"]["transport"] = transport
        payload = json.dumps(document, separators=(",", ":")).encode("utf-8")
        requests = []
        failures = []

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"  # same bounded response protocol as the product StatsServer

            def do_GET(self):
                requests.append(self.path)
                try:
                    if delay:
                        time.sleep(delay)
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(payload)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(payload)
                except OSError as error:
                    failures.append(type(error).__name__)

            def log_message(self, *_):
                pass

        server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
        thread.start()
        args = list(map(str, command_for_port(server.server_port)))
        began = time.monotonic()
        try:
            process = subprocess.run(args, env=env, cwd=cwd, capture_output=True, timeout=15)
            elapsed = time.monotonic() - began
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
        output = (process.stdout + process.stderr).decode("utf-8", errors="replace")
        result = {
            "name": name, "argv": args, "exit_code": process.returncode,
            "seconds": round(elapsed, 3), "requests": requests,
            "fixture_delay_ms": int(delay * 1000), "expected": expected, "expected_error": expected_error,
            "output": output,
        }
        if record:
            record(result)
        assert process.returncode == 0, (name, process.returncode, output)
        assert requests == ["/api/v1/stats"] and not failures, (name, requests, failures)
        assert expected in output, (name, output)
        assert output.count("First user pool transport:") == 1, (name, output)
        assert private not in output, (name, "private fixture field was echoed")
        if expected_error:
            assert expected_error in output, (name, output)
        else:
            assert "Transport error:" not in output, (name, output)
        if name != "verified":
            assert "Verified TLS" not in output, (name, output)
        if name == "slow-response":
            assert elapsed >= delay, (name, elapsed)
        result["passed"] = True
        results.append(result)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    results = run_cases(lambda port: [executable, "--status-test", str(port)])
    print(f"pool transport status CLI: {len(results)} cases passed (fresh processes, owned HTTP)")


if __name__ == "__main__":
    main()
