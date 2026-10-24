#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
exec distrobox enter --name slippi-arena-build --no-tty -- bash "$ROOT/Tools/arena/build-in-container.sh"
