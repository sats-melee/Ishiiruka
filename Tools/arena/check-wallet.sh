#!/usr/bin/env bash
set -euo pipefail
arena_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
exec distrobox enter --name slippi-arena-build --no-tty -- \
  "$arena_root/build-arena/Binaries/sats-arena-wallet-check" "$@"
