#!/usr/bin/env bash
# Run PlatformIO with all mutable state kept inside this checkout.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pio="$project_dir/.venv/bin/pio"

if [[ ! -x "$pio" ]]; then
    echo "PlatformIO is not installed. Run: python3 -m venv .venv && .venv/bin/pip install 'platformio==6.2.0'" >&2
    exit 1
fi

export PLATFORMIO_CORE_DIR="$project_dir/.platformio"
exec "$pio" run -d "$project_dir" "$@"
