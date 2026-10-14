#!/usr/bin/env python3
"""Run firmware logic tests on the host; no ESP32 or network is required."""
import argparse
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
TARGETS = {
    "protocol": ["tests/vendor_protocol_test.cpp"],
    "display": ["tests/vendor_display_test.cpp", "tests/test_runtime.cpp"],
    "wifi": ["tests/wifi_manager_test.cpp", "tests/test_runtime.cpp", "src/wifi_manager.cpp"],
    "frames": ["tests/frame_receiver_test.cpp"],
    "status": ["tests/network_status_test.cpp", "tests/test_runtime.cpp"],
    "boot": ["tests/boot_diagnostics_test.cpp", "tests/test_runtime.cpp"],
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="enable address/undefined-behaviour checks")
    args = parser.parse_args()
    flags = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-Itests/stubs", "-Isrc"]
    environment = dict(os.environ)
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g"]
        # LeakSanitizer cannot run in the ptrace-based terminal sandbox.
        environment["ASAN_OPTIONS"] = "detect_leaks=0"
    with tempfile.TemporaryDirectory(prefix="gdp075fw1-tests-") as directory:
        for name, sources in TARGETS.items():
            binary = str(Path(directory) / name)
            subprocess.run(shlex.split(os.environ.get("CXX", "g++")) + flags + sources +
                           ["-o", binary], cwd=PROJECT, check=True)
            subprocess.run([binary], cwd=PROJECT, env=environment, check=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests",
                    "-p", "test_*.py"], cwd=PROJECT, check=True)
    print(f"All {len(TARGETS)} C++ suites and Python tool tests passed")

if __name__ == "__main__":
    main()
