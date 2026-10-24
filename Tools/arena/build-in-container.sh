#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
export CARGO_HOME="$ROOT/build-tools/cargo"
export RUSTUP_HOME="$ROOT/build-tools/rustup"
export PATH="$CARGO_HOME/bin:$PATH"
export RUSTUP_TOOLCHAIN=1.88.0
export CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-2}"

mkdir -p "$ROOT/build-tools"
if [[ ! -x "$CARGO_HOME/bin/rustup" ]]; then
  curl -fsSL https://sh.rustup.rs -o "$ROOT/build-tools/rustup-init.sh"
  sh "$ROOT/build-tools/rustup-init.sh" -y --no-modify-path --profile minimal --default-toolchain "$RUSTUP_TOOLCHAIN"
fi

cmake -S "$ROOT" -B "$ROOT/build-arena" -G Ninja \
  -DLINUX_LOCAL_DEV=ON -DENABLE_SATS_ARENA=ON -DENABLE_LLVM=OFF \
  -DDISTRIBUTOR="Sats Arena preview" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-arena" --target dolphin-emu --parallel "${BUILD_JOBS:-3}"
cp -a "$ROOT/Data/Sys" "$ROOT/build-arena/Binaries/"
