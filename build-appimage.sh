#!/bin/bash
set -euo pipefail
# Materialize the complete native Arena runtime; never point this fork at upstream updates.
export NO_STRIP=on
for tool in linuxdeploy linuxdeploy-plugin-appimage; do
  if [[ ! -f "Tools/$tool" ]]; then
    wget "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage" -O "Tools/$tool"
  fi
  chmod +x "Tools/$tool"
done
rm -rf AppDir
mkdir -p AppDir/apprun-hooks
cp Data/linux-env.sh AppDir/apprun-hooks/
Tools/linuxdeploy --appdir=AppDir \
  -e build/Binaries/dolphin-emu -e build/Binaries/sats-arena-wallet-check \
  -d Data/slippi-online.desktop -i Data/dolphin-emu.png
cp -r build/Binaries/Sys AppDir/usr/bin/
cp Tools/arena/alsa-pulse.conf AppDir/usr/bin/
cp build/Source/Core/DolphinWX/libsats_arena_local.so AppDir/usr/lib/
cp build/Source/Core/DolphinWX/libslippi_rust_extensions.so AppDir/usr/lib/
rm -f AppDir/usr/lib/libgmodule* AppDir/usr/lib/librsvg-2.so.2
output=Sats-Arena-x86_64.AppImage
if [[ "${1:-netplay}" == playback ]]; then output=Slippi_Playback-x86_64.AppImage; fi
OUTPUT="$output" Tools/linuxdeploy-plugin-appimage --appdir=AppDir
