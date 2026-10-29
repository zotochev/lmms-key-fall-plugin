#!/bin/sh
# Installs everything needed to build LMMS + KeyFall on Ubuntu 24.04 (Qt 5).
# Usage: sudo sh scripts/linux-deps.sh
# Package list based on LMMS CI (.github/workflows/deps-ubuntu-24.04-gcc.txt), trimmed to
# what the minimal plug-in set plus KeyFall needs.
set -e
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake git pkg-config ccache ca-certificates file \
  qtbase5-dev qtbase5-private-dev qtbase5-dev-tools qttools5-dev-tools libqt5svg5-dev libqt5x11extras5-dev \
  libasound2-dev libjack-jackd2-dev portaudio19-dev libsdl2-dev \
  libsndfile1-dev libfftw3-dev libsamplerate0-dev libvorbis-dev libogg-dev libmp3lame-dev \
  libfluidsynth-dev liblilv-dev lv2-dev libstk-dev stk \
  libx11-xcb-dev libxcb-keysyms1-dev libxcb-util0-dev libxft-dev libxinerama-dev
echo "KeyFall: build dependencies installed"
