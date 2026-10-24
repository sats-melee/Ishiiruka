#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
ASM="${ARENA_ASM_ROOT:-$(dirname "$ROOT")/slippi-ssbm-asm}"
if [[ $# -ne 1 ]]; then
  echo "Usage: bash Tools/arena/build-codeset.sh /path/to/NTSC-U-1.02.iso" >&2
  exit 1
fi
distrobox enter -n slippi-arena-build --no-tty -- bash "$ASM/build-arena.sh"
python3 "$ASM/Arena/validate-codeset.py" "$1"
cp "$ASM/Output/Arena/GALE01r2.ini" "$ROOT/Data/Sys/GameSettings/GALE01r2.ini"
cp "$ASM/Output/Arena/arena-manifest.json" "$ROOT/Tools/arena/codeset-manifest.json"
echo "Validated Arena codeset installed in this fork's Data/Sys. Rebuild before launching."
