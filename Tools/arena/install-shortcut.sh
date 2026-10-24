#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
APPLICATIONS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPLICATIONS"
cat > "$APPLICATIONS/sats-arena.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Sats Arena
Comment=Slippi with an integrated preview sidebar — no real money
Exec=bash "$ROOT/Tools/arena/start.sh"
Icon=$ROOT/Data/dolphin-emu.png
Terminal=false
Categories=Game;Emulator;
StartupNotify=true
EOF
echo "Installed Sats Arena in your application menu."
