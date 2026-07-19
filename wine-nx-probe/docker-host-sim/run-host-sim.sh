#!/usr/bin/env bash
# Configure+build the wine-nx host-sim inside the Linux container, run a
# self-checking GUI smoke PE through the SDL2-backed presentation path under
# Xvfb, and screenshot it. Run via:
#   docker run --rm -v <repo-root>:/work -w /work wine-nx-host-sim:latest \
#       ./wine-nx-probe/docker-host-sim/run-host-sim.sh
#
# Exits nonzero unless the smoke app actually rendered: the host-sim's
# "[NXSIM] SDL window ready" marker must appear in the wine log AND the
# screenshot must contain a minimum number of distinct colors. That makes
# this runnable as a CI gate, not just an eyeball tool.
#
# WINE_NX_HOST_SIM_APP=hello (default) | notepad | /path/to/some.exe
# WINE_NX_HOST_SIM_WAIT=<seconds to let the app run before screenshotting>
set -euo pipefail

BUILD_DIR=wine-nx-probe/build-host-sim-linux
OUT_DIR=/work/wine-nx-probe/docker-host-sim
SCREENSHOT="$OUT_DIR/host-sim-screenshot.png"
WINE_LOG=/tmp/host-sim-wine.log
SMOKE_APP="${WINE_NX_HOST_SIM_APP:-hello}"
RUN_WAIT="${WINE_NX_HOST_SIM_WAIT:-25}"

XVFB_PID=""
WINE_PID=""
cleanup() {
    status=$?
    [ -n "$WINE_PID" ] && kill "$WINE_PID" 2>/dev/null || true
    [ -x server/wineserver ] && ./server/wineserver -k 2>/dev/null || true
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null || true
    # The wine log is the single most useful artifact, especially on
    # failure -- always print it, whatever exit path we're on.
    echo "== wine log =="
    cat "$WINE_LOG" 2>/dev/null || true
    exit "$status"
}
trap cleanup EXIT

echo "== configure =="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
if [ ! -f Makefile ]; then
    ../../configure \
        --enable-archs=aarch64 \
        --disable-tests \
        --without-coreaudio --without-alsa --without-pulse --without-gstreamer \
        --without-cups --without-sane --without-usb --without-gphoto --without-opencl \
        --without-pcap --without-v4l2 --without-pcsclite --without-netapi --without-krb5 \
        --without-gssapi --without-dbus --without-wayland --without-gnutls \
        --with-mingw=clang
fi

echo "== build =="
# Full build, deliberately: the old target list here (wine, loader, server,
# nls, notepad.exe) never built any PE DLL or wineboot.exe, so every run
# died at "failed to load kernel32.dll" right after the wineboot wait --
# there was no kernel32.dll to load, and that error got misread as a
# fork-loader bug for a while. Any PE needs the DLLs; build everything.
make -j"$(nproc)"

echo "== build smoke PE =="
aarch64-w64-mingw32-clang -O1 -o "$OUT_DIR/hello.exe" \
    "$OUT_DIR/hello.c" -luser32 -lgdi32

case "$SMOKE_APP" in
    hello)   APP="$OUT_DIR/hello.exe" ;;
    notepad) APP=programs/notepad/aarch64-windows/notepad.exe ;;
    *)       APP="$SMOKE_APP" ;;
esac

echo "== run under Xvfb =="
Xvfb :99 -screen 0 1280x720x24 &
XVFB_PID=$!
sleep 2
export DISPLAY=:99
export WINEPREFIX=/tmp/wine-nx-hostsim-docker
export WINE_NX_HOST_SIM=1
# Suppress the wine-mono/gecko first-run installer dialogs: they pop up over
# the smoke app on a fresh prefix and make the screenshot nondeterministic.
export WINEDLLOVERRIDES="mscoree,mshtml="
rm -rf "$WINEPREFIX"

./wine "$APP" > "$WINE_LOG" 2>&1 &
WINE_PID=$!
# First run also pays full wineboot prefix initialization.
sleep "$RUN_WAIT"

echo "== screenshot =="
if ! import -window root "$SCREENSHOT"; then
    # ImageMagick's import can fail on some Xvfb setups; xwd is the fallback.
    # (This used to be a one-liner whose ||/&& precedence ran `convert` on a
    # file xwd never wrote whenever import SUCCEEDED, aborting the script on
    # its own success path under set -e.)
    xwd -root -display :99 -out /tmp/root.xwd
    convert /tmp/root.xwd "$SCREENSHOT"
fi
ls -la "$SCREENSHOT"

echo "== self-check =="
if ! grep -q "\[NXSIM\] SDL window ready" "$WINE_LOG"; then
    echo "FAIL: '[NXSIM] SDL window ready' never appeared in the wine log --" >&2
    echo "      the SDL2 host-sim surface was never created (see log below)." >&2
    exit 1
fi
COLORS=$(convert "$SCREENSHOT" -format "%k" info:)
echo "distinct colors in screenshot: $COLORS"
if [ "$COLORS" -lt 8 ]; then
    echo "FAIL: screenshot looks blank/uniform ($COLORS distinct colors, need >= 8) --" >&2
    echo "      the host-sim surface exists but nothing rendered into it." >&2
    exit 1
fi
echo "PASS: host-sim rendered the smoke app ($COLORS distinct colors)."
