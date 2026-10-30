"""Native host-stub startup refusal; no devices, helpers, games or external endpoints."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="canary-game-cli-") as directory:
        root = Path(directory)
        config = root / "config.json"
        config.write_text(json.dumps({"schema_version": 1, "state_dir": str(root)}), encoding="utf8")
        preferences = root / "game-preferences.json"
        result = subprocess.run([executable, "--config", str(config)], capture_output=True, timeout=8)
        assert result.returncode == 97 and b"FORBIDDEN_DEVICE_SETUP" in result.stderr and not preferences.exists()
        checks += 1
        value = {"schema_version": 1, "mode": "pause", "target_executable": "fixture-game.exe",
                 "target_scope": "windows_session_image", "poll_ms": 500,
                 "exit_debounce_ms": 2000, "maximum_detection_age_ms": 2000}
        for mode in ("pause", "coexist"):
            value["mode"] = mode
            original = json.dumps(value)
            preferences.write_text(original, encoding="utf8")
            result = subprocess.run([executable, "--config", str(config)], capture_output=True, timeout=8)
            assert result.returncode == 3 and b"configured game mode requires" in result.stderr
            assert b"fixture-game" not in result.stdout + result.stderr and preferences.read_text(encoding="utf8") == original
            checks += 2
        for original in ('{"schema_version":99}', '[]', json.dumps({**value, "target_executable": "../private-fixture"})):
            preferences.write_text(original, encoding="utf8")
            result = subprocess.run([executable, "--config", str(config)], capture_output=True, timeout=8)
            assert result.returncode == 3 and b"game preferences unavailable" in result.stderr
            assert b"private-fixture" not in result.stdout + result.stderr and preferences.read_text(encoding="utf8") == original
            checks += 2
        result = subprocess.run([executable, "settings", "--config", str(config), "--help"], capture_output=True, timeout=8)
        assert result.returncode == 0 and preferences.read_text(encoding="utf8") == original
        checks += 1
        value["mode"] = "off"
        preferences.write_text(json.dumps(value), encoding="utf8")
        result = subprocess.run([executable, "--config", str(config)], capture_output=True, timeout=8)
        assert result.returncode == 97 and b"FORBIDDEN_DEVICE_SETUP" in result.stderr
        assert b"fixture-game" not in result.stdout + result.stderr
        checks += 1
    print(f"game settings CLI: {checks} admission/preservation checks passed")


if __name__ == "__main__":
    main()
