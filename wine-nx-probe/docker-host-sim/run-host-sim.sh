#!/usr/bin/env bash
# Configure+build the wine-nx host-sim inside the Linux container, run
# notepad.exe through the SDL2-backed presentation path under Xvfb, and
# screenshot it. Run via:
#   docker run --rm -v <repo-root>:/work -w /work wine-nx-host-sim:latest \
#       ./wine-nx-probe/docker-host-sim/run-host-sim.sh
set -euo pipefail

BUILD_DIR=wine-nx-probe/build-host-sim-linux
SCREENSHOT=/work/wine-nx-probe/docker-host-sim/notepad-screenshot.png

echo "== configure =="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
../../configure \
    --enable-archs=aarch64 \
    --disable-tests \
    --without-coreaudio --without-alsa --without-pulse --without-gstreamer \
    --without-cups --without-sane --without-usb --without-gphoto --without-opencl \
    --without-pcap --without-v4l2 --without-pcsclite --without-netapi --without-krb5 \
    --without-gssapi --without-dbus --without-wayland --without-gnutls \
    --with-mingw=clang

echo "== build =="
make -j"$(nproc)" \
    wine \
    loader/wine \
    server/wineserver \
    nls/all \
    programs/notepad/aarch64-windows/notepad.exe

echo "== run under Xvfb =="
Xvfb :99 -screen 0 1280x720x24 &
XVFB_PID=$!
sleep 2
export DISPLAY=:99
export WINEPREFIX=/tmp/wine-nx-hostsim-docker
export WINE_NX_HOST_SIM=1
rm -rf "$WINEPREFIX"

./wine programs/notepad/aarch64-windows/notepad.exe > /tmp/notepad-docker.log 2>&1 &
WINE_PID=$!
sleep 8

echo "== screenshot =="
import -window root "$SCREENSHOT" || xwd -root -display :99 -out /tmp/root.xwd && convert /tmp/root.xwd "$SCREENSHOT"
ls -la "$SCREENSHOT"

kill "$WINE_PID" 2>/dev/null || true
kill "$XVFB_PID" 2>/dev/null || true

echo "== notepad log =="
cat /tmp/notepad-docker.log || true
