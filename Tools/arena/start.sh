#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
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

exec distrobox enter --name slippi-arena-build --no-tty -- \
  env GDK_BACKEND=x11 "$BINARY" --user "$USER_DIR" "${args[@]}"
