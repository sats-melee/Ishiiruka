#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
ENGINE="${SATS_ARENA_ENGINE_DIR:-$ROOT/../sats-arena}"
RUNNER=()
if ! command -v g++ >/dev/null; then
  RUNNER=(distrobox enter --name slippi-arena-build --no-tty --)
fi
mkdir -p "$ROOT/build-arena/tests"
"${RUNNER[@]}" \
  g++ -std=c++14 -Wall -Wextra -Werror -pedantic -pthread \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/TrackerTest.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaTracker.cpp" \
  -o "$ROOT/build-arena/tests/arena-tracker-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/arena-tracker-test" "$@"
"${RUNNER[@]}" \
  g++ -std=c++14 -Wall -Wextra -Werror -pedantic -pthread \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/PaymentGateTest.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaTracker.cpp" \
  -o "$ROOT/build-arena/tests/arena-payment-gate-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/arena-payment-gate-test"
"${RUNNER[@]}" \
  g++ -std=c++14 -Wall -Wextra -Werror -pedantic \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/LocalLayoutTest.cpp" \
  -o "$ROOT/build-arena/tests/local-layout-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/local-layout-test"
"${RUNNER[@]}" \
  g++ -std=c++14 -pthread -ffunction-sections -fdata-sections \
  -I "$ROOT/Source/Core" "$ROOT/Tools/arena/VertexLoaderCacheTest.cpp" \
  -Wl,--gc-sections -o "$ROOT/build-arena/tests/vertex-loader-cache-test"
"${RUNNER[@]}" \
  g++ -std=c++14 -fno-exceptions -DSATS_ARENA -Wall -Wextra -Werror -pedantic \
  -I "$ROOT/Source/Core" -I "$ROOT/Externals/nlohmann" \
  "$ROOT/Tools/arena/NetplayRoomTest.cpp" -o "$ROOT/build-arena/tests/netplay-room-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/netplay-room-test"
"${RUNNER[@]}" \
  g++ -std=c++14 -fno-exceptions -DSATS_ARENA -Wall -Wextra -Werror -pedantic \
  -I "$ROOT/Source/Core" -I "$ROOT/Externals/nlohmann" \
  "$ROOT/Tools/arena/RoomObservationTest.cpp" -o "$ROOT/build-arena/tests/room-observation-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/room-observation-test"
"${RUNNER[@]}" \
  g++ -std=c++14 -fno-exceptions -DSATS_ARENA -Wall -Wextra -Werror -pedantic \
  -I "$ROOT/Source/Core" -I "$ROOT/Externals/nlohmann" -I "$ENGINE/local/include" \
  "$ROOT/Tools/arena/RoomViewTest.cpp" -o "$ROOT/build-arena/tests/room-view-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/room-view-test"
"${RUNNER[@]}" \
  g++ -std=c++14 -fno-exceptions -DSATS_ARENA -Wall -Wextra -Werror -pedantic \
  -I "$ROOT/Source/Core" -I "$ROOT/Externals/nlohmann" \
  "$ROOT/Tools/arena/RoomGameplayTest.cpp" -o "$ROOT/build-arena/tests/room-gameplay-test"
distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/room-gameplay-test"
"${RUNNER[@]}" \
  g++ -std=c++14 -fno-exceptions -DSATS_ARENA -Wall -Wextra -Werror -pedantic -pthread \
  -I "$ROOT/Source/Core" -I "$ROOT/Externals/nlohmann" \
  "$ROOT/Tools/arena/RoomClientFinalizeTest.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaRoomClient.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaTracker.cpp" -lcurl \
  -o "$ROOT/build-arena/tests/room-client-finalize-test"
"${RUNNER[@]}" \
  python3 "$ROOT/Tools/arena/test-room-client-finalize.py"
"${RUNNER[@]}" \
  g++ -std=c++14 -fno-exceptions -DSATS_ARENA -Wall -Wextra -Werror -pedantic -pthread \
  -I "$ROOT/Source/Core" -I "$ROOT/Externals/nlohmann" \
  "$ROOT/Tools/arena/RoomClientRecoveryTest.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaRoomClient.cpp" \
  "$ROOT/Source/Core/Core/Slippi/SatsArenaTracker.cpp" -lcurl \
  -o "$ROOT/build-arena/tests/room-client-recovery-test"
"${RUNNER[@]}" \
  python3 "$ROOT/Tools/arena/test-room-client-recovery.py"
exec distrobox enter --name slippi-arena-build --no-tty -- "$ROOT/build-arena/tests/vertex-loader-cache-test"
