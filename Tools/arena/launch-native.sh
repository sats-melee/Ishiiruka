#!/usr/bin/env bash
# Developer launcher transport: arguments, environment and private stdin belong
# to the launcher. Do not replace its ISO/profile or copy credentials to disk.
set -euo pipefail
arena_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
exec distrobox enter --name slippi-arena-build --no-tty -- \
  env GDK_BACKEND=x11 "$arena_root/build-arena/Binaries/dolphin-emu" "$@"
