#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

# VS Code's Flatpak terminal cannot see the host's Distrobox installation.
# Re-enter on the host before inspecting binaries, profiles, or containers.
if [[ -f /.flatpak-info || -n "${FLATPAK_ID:-}" ]]; then
  if ! command -v flatpak-spawn >/dev/null 2>&1; then
    echo "This terminal is inside Flatpak and cannot access host tools." >&2
    echo "Run this script from Konsole, or enable flatpak-spawn host access." >&2
    exit 1
  fi
  exec flatpak-spawn --host --env="SATS_ARENA_FAKE_LOCAL=${SATS_ARENA_FAKE_LOCAL:-1}" \
    /bin/bash "$ROOT/Tools/arena/start.sh" "$@"
fi

export PATH="${PATH:-}:/usr/local/bin:/usr/bin:/bin"
if ! command -v distrobox >/dev/null 2>&1; then
  echo "Distrobox was not found on the host. Run this launcher on the SteamOS machine where the build container was created." >&2
  exit 1
fi

BINARY="$ROOT/build-arena/Binaries/dolphin-emu"
USER_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/SatsArenaDolphin"

if [[ ! -x "$BINARY" ]]; then
  echo "Build the native app first: bash $ROOT/Tools/arena/build.sh" >&2
  exit 1
fi

# One-time import of controller/video settings; never write into the stock Slippi profile.
if [[ ! -e "$USER_DIR/.arena-initialized" ]]; then
  mkdir -p "$USER_DIR"
  if [[ ! -d "$USER_DIR/Config" && -d "$HOME/.config/SlippiOnline/Config" ]]; then
    cp -a "$HOME/.config/SlippiOnline/Config" "$USER_DIR/Config"
  fi
  if [[ ! -d "$USER_DIR/GameSettings" && -d "$HOME/.config/SlippiOnline/GameSettings" ]]; then
    cp -a "$HOME/.config/SlippiOnline/GameSettings" "$USER_DIR/GameSettings"
  fi
  touch "$USER_DIR/.arena-initialized"
fi

args=("$@")
if [[ $# -eq 0 ]]; then
  iso="$(python3 - "$USER_DIR/Config/Dolphin.ini" <<'PY'
import configparser
import os
import sys
config = configparser.ConfigParser(interpolation=None, strict=False)
config.read(sys.argv[1])
iso = config.get('General', 'LastFilename', fallback='')
print(iso if os.path.isfile(iso) else '')
PY
)"
  if [[ -n "$iso" ]]; then
    args=(--exec "$iso")
  fi
fi

audio_env=()
pulse_socket="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/pulse/native"
if [[ -S "$pulse_socket" ]]; then
  # Native game audio uses Dolphin's Pulse backend; Rust music uses ALSA/CPAL.
  # Route both to the existing host server without changing system audio config.
  audio_env+=("PULSE_SERVER=${PULSE_SERVER:-unix:$pulse_socket}")
  audio_env+=("ALSA_CONFIG_PATH=$ROOT/Tools/arena/alsa-pulse.conf")
fi

exec distrobox enter --name slippi-arena-build --no-tty -- \
  env GDK_BACKEND=x11 "SATS_ARENA_FAKE_LOCAL=${SATS_ARENA_FAKE_LOCAL:-1}" \
  "${audio_env[@]}" "$BINARY" --user "$USER_DIR" "${args[@]}"
