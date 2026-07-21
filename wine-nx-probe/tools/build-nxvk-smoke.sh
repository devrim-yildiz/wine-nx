#!/usr/bin/env bash
# Build the NVK Vulkan driver for Switch (vendored at
# wine-nx-probe/third_party/nxvk) and link one of its smoke-test apps into
# a real, standalone, hardware-testable .nro.
#
# This is NOT part of the main wine-nx-runtime build (build-switch.sh) --
# nxvk is a separate, heavyweight Mesa/Rust cross-compilation pipeline with
# its own Docker toolchain image, and nvk_smoke.nro is a fully standalone
# homebrew app with no dependency on wine-nx-runtime at all. Copy the
# output directly to sdmc:/switch/ and it shows up as its own entry in
# hbmenu.
#
# See wine-nx-probe/third_party/nxvk/switch/README.md for the underlying
# project's own docs, and wine-nx-probe/3d-accel-scoping.md for why this
# exists and what it does/doesn't prove yet (build-verified, not yet
# hardware-tested).
#
# Usage:
#   ./wine-nx-probe/tools/build-nxvk-smoke.sh [app]
# app defaults to nvk_smoke. Other options (see third_party/nxvk/switch/smoke/):
#   nvk_tri nvk_logo nvk_scene nvk_indexed nvk_multi nvk_textures nvk_cubemap nvk_vi_swapchain
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NXVK_DIR="$REPO_ROOT/wine-nx-probe/third_party/nxvk"
APP="${1:-nvk_smoke}"
IMAGE="nvk-switch-build"

if [ ! -d "$NXVK_DIR/switch" ]; then
    echo "nxvk submodule not checked out. Run:" >&2
    echo "  git submodule update --init wine-nx-probe/third_party/nxvk" >&2
    exit 1
fi

echo "=== Building toolchain image (first run only unless the Dockerfile changed) ==="
docker build -t "$IMAGE" "$NXVK_DIR/switch/docker"

run() {
    docker run --rm -v "$NXVK_DIR:/work:z" -w /work "$IMAGE" bash -lc "$1"
}

echo "=== Rust std sysroot (needed once per toolchain image) ==="
run "ls switch/rust/sysroot/lib/rustlib/aarch64-switch-horizon/lib/libstd-*.rlib >/dev/null 2>&1 || bash switch/rust/build-std-sysroot.sh"

echo "=== Native tools + Mesa cross-configure ==="
run "bash switch/build/build-native-tools.sh && bash switch/build/configure-mesa.sh"

echo "=== Building driver archives (libnvk.a + deps) ==="
echo "    The libvulkan_nouveau.so link failing here is expected -- see"
echo "    switch/README.md. What matters is libnvk.a and the other archives."
run "ninja -k0 -C /work/switch/build/cross src/nouveau/vulkan/libvulkan_nouveau.so || true"

if [ ! -f "$NXVK_DIR/switch/build/cross/src/nouveau/vulkan/libnvk.a" ]; then
    echo "libnvk.a was not produced -- something upstream of the expected" >&2
    echo ".so failure actually broke. Check the build log above." >&2
    exit 1
fi

echo "=== Linking $APP.nro ==="
run "bash switch/build/build-nro.sh $APP"

OUT="$NXVK_DIR/switch/smoke/out/$APP.nro"
echo "=== Done: $OUT ==="
ls -la "$OUT"
echo "Copy this file directly to sdmc:/switch/ -- it's a standalone homebrew"
echo "app, independent of wine-nx-runtime, and will show up as its own entry"
echo "in hbmenu."
