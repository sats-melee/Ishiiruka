#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$ROOT/build-arena/tests"
distrobox enter --name slippi-arena-build --no-tty -- \
  g++ -std=c++14 -Wall -Wextra -Werror -pedantic -pthread \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/TrackerTest.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaTracker.cpp" \
  -o "$ROOT/build-arena/tests/arena-tracker-test"
exec distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/arena-tracker-test" "$@"
