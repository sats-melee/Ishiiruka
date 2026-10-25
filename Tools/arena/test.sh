#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$ROOT/build-arena/tests"
distrobox enter --name slippi-arena-build --no-tty -- \
  g++ -std=c++14 -Wall -Wextra -Werror -pedantic -pthread \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/TrackerTest.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaTracker.cpp" \
  -o "$ROOT/build-arena/tests/arena-tracker-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/arena-tracker-test" "$@"
distrobox enter --name slippi-arena-build --no-tty -- \
  g++ -std=c++14 -Wall -Wextra -Werror -pedantic -pthread \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/PaymentGateTest.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaTracker.cpp" \
  -o "$ROOT/build-arena/tests/arena-payment-gate-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/arena-payment-gate-test"
distrobox enter --name slippi-arena-build --no-tty -- \
  g++ -std=c++14 -pthread -ffunction-sections -fdata-sections \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/VertexLoaderCacheTest.cpp" \
  -Wl,--gc-sections -o "$ROOT/build-arena/tests/vertex-loader-cache-test"
exec distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/vertex-loader-cache-test"
