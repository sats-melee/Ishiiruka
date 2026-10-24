#!/usr/bin/env bash
set -euo pipefail
# Ubuntu 22.04 matches upstream CI. These packages live only in the Distrobox.
distrobox create --yes --name slippi-arena-build --no-entry \
  --image docker.io/library/ubuntu:22.04 \
  --additional-packages "build-essential cmake ninja-build pkg-config git curl ca-certificates libao-dev libasound2-dev libavcodec-dev libavformat-dev libbluetooth-dev libenet-dev libgtk2.0-dev liblzo2-dev libminiupnpc-dev libopenal-dev libpulse-dev libreadline-dev libsfml-dev libsoil-dev libsoundtouch-dev libswscale-dev libusb-1.0-0-dev libwebkit2gtk-4.0-dev libxext-dev libxrandr-dev portaudio19-dev zlib1g-dev libudev-dev libevdev-dev libmbedtls-dev libcurl4-openssl-dev libegl1-mesa-dev libpng-dev qtbase5-private-dev libxxf86vm-dev x11proto-xinerama-dev libfuse2 libgl1-mesa-dri"
distrobox enter --name slippi-arena-build --no-tty -- true
