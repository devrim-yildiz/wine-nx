#!/bin/sh
# Generate the widl/wrc-generated Wine headers (objidlbase.h and friends)
# that the Switch NRO CMake build needs but that are NOT tracked in git --
# upstream Wine generates them into its build tree during make, and the
# CMake build (wine-nx-probe/CMakeLists.txt) consumes wine's include/
# directly, so a fresh clone cannot compile win32u until these exist. (This
# went unnoticed for a while because the original dev tree had them lying
# around; a clean checkout is what CI builds.)
#
# Usage:
#   ./wine-nx-probe/tools/generate-wine-headers.sh [output-build-dir]
#
# Default output dir is wine-nx-probe/build-wine-headers; the generated
# headers land in <output-build-dir>/include, which is where
# CMakeLists.txt's WINE_GENERATED_INCLUDE_DIR default points. Needs a host
# C toolchain plus flex and bison >= 3.0 (Wine's configure requirement) --
# on macOS, Apple's /usr/bin/bison 2.3 is too old; run this inside the
# docker-host-sim container instead, or `brew install bison`.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
OUT="${1:-$ROOT/wine-nx-probe/build-wine-headers}"

mkdir -p "$OUT"
cd "$OUT"

if [ ! -f Makefile ]; then
    # On x86_64 hosts wine's configure defaults to a 32-bit build and dies
    # without multilib ("Cannot build a 32-bit program") -- seen on the
    # first real CI run, invisible on the aarch64 hosts this script was
    # developed on (aarch64 wine is 64-bit-only by default).
    win64_flag=""
    case "$(uname -m)" in
        x86_64|amd64) win64_flag="--enable-win64" ;;
    esac

    # Tools-only configure: every optional dependency off, no PE cross
    # compiler wanted -- we only ever run `make include` here.
    "$ROOT/configure" \
        $win64_flag \
        --disable-tests \
        --without-x --without-freetype --without-fontconfig --without-gettext \
        --without-gnutls --without-opengl --without-vulkan --without-sdl \
        --without-cups --without-coreaudio --without-alsa --without-pulse \
        --without-gstreamer --without-ffmpeg --without-dbus --without-gphoto \
        --without-gssapi --without-krb5 --without-netapi --without-opencl \
        --without-pcap --without-pcsclite --without-sane --without-usb \
        --without-v4l2 --without-wayland --without-unwind
fi

# The configure-generated Makefile has one rule per generated header
# (include/foo.h: .../foo.idl, built with tools/widl). There is no single
# alias target that builds them all without compiling wine itself, so
# extract every include/*.h rule and make exactly those.
targets=$(sed -n 's/^\(include\/[a-zA-Z0-9_.-]*\.h\):.*/\1/p' Makefile | sort -u)
[ -n "$targets" ] || { echo "no generated-header rules found in Makefile" >&2; exit 1; }
# shellcheck disable=SC2086
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" $targets

count=$(ls include/*.h 2>/dev/null | wc -l)
echo "generated $count headers into $OUT/include"
