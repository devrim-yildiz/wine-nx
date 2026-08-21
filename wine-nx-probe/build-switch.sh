#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-switch"
APP_KIND="${WINE_NX_APP:-curl}"
WINE_PE_BUILD_DIR="${WINE_PE_BUILD_DIR:-$SCRIPT_DIR/build-wine-arm64-pe-clean}"
WINE_PE_DLL_ROOT="$WINE_PE_BUILD_DIR/dlls"
LLVM_MINGW_DIR="${LLVM_MINGW_DIR:-$SCRIPT_DIR/toolchains/llvm-mingw-20260505-ucrt-macos-universal}"
LLVM_MINGW_RUNTIME_DIR="$LLVM_MINGW_DIR/aarch64-w64-mingw32/bin"
LLVM_MINGW_BIN_DIR="$LLVM_MINGW_DIR/bin"
WINE_BUILD_PATH="$LLVM_MINGW_BIN_DIR:$PATH"
if [ -x /opt/homebrew/opt/bison/bin/bison ]; then
    WINE_BUILD_PATH="/opt/homebrew/opt/bison/bin:$WINE_BUILD_PATH"
fi
LOCAL_PE_BUILD_DIR="$SCRIPT_DIR/build-wine-arm64-pe-local"
# Host-side CMake build of the WowBox64 CPU backend (wowbox64.dll) --
# separate from both $BUILD_DIR (the Docker/devkitA64 NRO build) and
# $WINE_PE_BUILD_DIR (the desktop-Wine-configure PE tree): it needs the
# aarch64-w64-mingw32 toolchain, not devkitA64, and isn't part of Wine's
# own configure/make. Not built by this script -- see README for
# `cmake -S wine-nx-probe -B "$WOWBOX64_BUILD_DIR" -DWINE_NX_BUILD_WOWBOX64=ON`
# then `cmake --build ... --target wowbox64-dll`. If it hasn't been built,
# system32 just won't get a wowbox64.dll and 32-bit guests won't run.
WOWBOX64_BUILD_DIR="${WOWBOX64_BUILD_DIR:-$SCRIPT_DIR/build-wowbox64}"
WOWBOX64_DLL_PATH="$WOWBOX64_BUILD_DIR/wowbox_out/wowbox64.dll"
PROGRAM_PE_BUILD_DIR="${WINE_NX_PROGRAM_BUILD_DIR:-$WINE_PE_BUILD_DIR}"
if [ "$APP_KIND" = "notepad" ] && [ -z "${WINE_NX_PROGRAM_BUILD_DIR:-}" ] && [ -d "$LOCAL_PE_BUILD_DIR" ]; then
    PROGRAM_PE_BUILD_DIR="$LOCAL_PE_BUILD_DIR"
fi
PROGRAM_PE_DLL_ROOT="$PROGRAM_PE_BUILD_DIR/dlls"
NOTEPAD_EXE="$PROGRAM_PE_BUILD_DIR/programs/notepad/aarch64-windows/notepad.exe"
REQUIRED_WINE_DLL_MODULES=(
    cryptbase
)
NOTEPAD_WINE_DLL_MODULES=(
    combase
    comctl32
    comdlg32
    coml2
    imm32
    ole32
    oleaut32
    ntdll
    shcore
    shell32
    shlwapi
    version
)
NOTEPAD_SYSTEM_DLLS=(
    combase.dll
    comctl32.dll
    comdlg32.dll
    coml2.dll
    imm32.dll
    ole32.dll
    oleaut32.dll
    shcore.dll
    shell32.dll
    shlwapi.dll
    version.dll
)
# openttd.exe's own import table needs opengl32/winmm/imm32/usp10/winhttp/
# shell32/ole32/oleaut32 beyond REQUIRED_SYSTEM_DLLS (confirmed via objdump -p
# on the real openttd.exe). Reusing NOTEPAD_WINE_DLL_MODULES's combase/
# comctl32/comdlg32/coml2/shcore/shlwapi/version as well -- those are already
# proven-necessary transitive dependencies of shell32/ole32/oleaut32 for this
# same devkitA64/Wine PE build, so pulling in that same set here rather than
# guessing a smaller one avoids missing an internal import shell32.dll etc.
# themselves rely on.
OPENTTD_WINE_DLL_MODULES=(
    combase
    comctl32
    comdlg32
    coml2
    imm32
    ole32
    oleaut32
    opengl32
    shcore
    shell32
    shlwapi
    usp10
    version
    winhttp
    winmm
)
OPENTTD_SYSTEM_DLLS=(
    combase.dll
    comctl32.dll
    comdlg32.dll
    coml2.dll
    imm32.dll
    ole32.dll
    oleaut32.dll
    opengl32.dll
    shcore.dll
    shell32.dll
    shlwapi.dll
    usp10.dll
    version.dll
    winhttp.dll
    winmm.dll
)
REQUIRED_SYSTEM_DLLS=(
    advapi32.dll
    bcrypt.dll
    cryptbase.dll
    crypt32.dll
    dnsapi.dll
    gdi32.dll
    iphlpapi.dll
    kernel32.dll
    kernelbase.dll
    libc++.dll
    libomp.dll
    libunwind.dll
    libwinpthread-1.dll
    msvcrt.dll
    normaliz.dll
    nsi.dll
    ntdll.dll
    rpcrt4.dll
    sechost.dll
    secur32.dll
    ucrtbase.dll
    user32.dll
    win32u.dll
    wldap32.dll
    ws2_32.dll
)

if [ "${1:-}" = "clean" ]; then
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# NVK Vulkan support (wine-nx-probe/third_party/nxvk) is purely additive
# and opt-in: it only links in when its static archives have already been
# built (see wine-nx-probe/tools/build-nxvk-smoke.sh) AND the toolchain
# image that has its Switch-cross SPIRV-Tools baked in ("nvk-switch-build",
# built from nxvk's own switch/docker/Dockerfile -- a strict superset of
# plain devkitpro/devkita64, so it's safe to use unconditionally once it
# exists) is available locally. Neither is required for a normal build; a
# fresh checkout that hasn't touched nxvk at all builds exactly as before
# this feature existed.
NXVK_DIR="$SCRIPT_DIR/third_party/nxvk"
NXVK_LIBNVK="$NXVK_DIR/switch/build/cross/src/nouveau/vulkan/libnvk.a"
DOCKER_IMAGE="devkitpro/devkita64"
# A plain string, not a bash array: macOS ships bash 3.2 by default, which
# has a long-standing bug where "${arr[*]}" on a zero-element array trips
# "unbound variable" under set -u even though the array itself is defined.
CMAKE_VULKAN_ARGS=""
# `docker image inspect nvk-switch-build` has been observed to
# intermittently report a real, present image as missing, even after a
# `docker version` warm-up and 8 retries -- but `docker images -q
# nvk-switch-build` reliably found the same image in the exact same shell
# state where `inspect` was failing (confirmed side by side). Not fully
# understood why (Docker Desktop image-store lookup path difference
# between the two subcommands, seemingly), but `images -q` is what's
# actually reliable here, so that's the real check now; a miss is still
# retried a few times as cheap insurance. A false negative here silently
# drops Vulkan support while a stale CMakeCache.txt from a PRIOR
# successful configure still expects it -- WINE_NX_HAVE_VULKAN can end up
# true against the wrong (plain devkitpro/devkita64) image, and the
# resulting build failure is confusing.
docker version >/dev/null 2>&1
NVK_IMAGE_FOUND=0
for attempt in 1 2 3 4 5; do
    if [ -n "$(docker images -q nvk-switch-build 2>/dev/null)" ]; then
        NVK_IMAGE_FOUND=1
        break
    fi
    sleep 3
done
if [ -f "$NXVK_LIBNVK" ] && [ "$NVK_IMAGE_FOUND" = "1" ]; then
    echo "NVK Vulkan driver archives found -- building wine-nx-runtime with Vulkan support"
    DOCKER_IMAGE="nvk-switch-build"
    CMAKE_VULKAN_ARGS="-DNXVK_DIR=/project/wine-nx-probe/third_party/nxvk"
else
    echo "NVK not built (see wine-nx-probe/tools/build-nxvk-smoke.sh) -- building without Vulkan support"
fi

echo "Building Switch runtime NRO with devkitA64 Docker image ($DOCKER_IMAGE)..."
docker run --rm \
    -v "$REPO_ROOT:/project" \
    --workdir /project/wine-nx-probe \
    "$DOCKER_IMAGE" \
    sh -lc '
        # win32u font rendering (font.c/freetype.c) needs the freetype portlib
        if [ ! -f "$DEVKITPRO/portlibs/switch/include/freetype2/ft2build.h" ]; then
            dkp-pacman -Sy --noconfirm --needed switch-freetype switch-harfbuzz switch-bzip2 switch-libpng
        fi
        cmake -S . -B build-switch \
            -DCMAKE_TOOLCHAIN_FILE=cmake/switch-devkitA64.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            '"$CMAKE_VULKAN_ARGS"'
        cmake --build build-switch -j"$(nproc)"
    '

echo "Built $BUILD_DIR/wine-nx-probe.nro"
if [ -f "$BUILD_DIR/wine-nx-pe-smoke.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-pe-smoke.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-pe-real-report.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-pe-real-report.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-pe-real-run.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-pe-real-run.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-pe-ntdll-smoke.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-pe-ntdll-smoke.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-ntdll-smoke.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-ntdll-smoke.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-ntdll-file-smoke.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-ntdll-file-smoke.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-deko3d-smoke.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-deko3d-smoke.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-runtime.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-runtime.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-runtime-deko3d.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-runtime-deko3d.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-vulkan-runtime.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-vulkan-runtime.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-jit-smoke.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-jit-smoke.nro"
fi
if [ -f "$BUILD_DIR/wine-nx-launcher.nro" ]; then
    echo "Built $BUILD_DIR/wine-nx-launcher.nro"
fi

if [ -d "$WINE_PE_BUILD_DIR" ]; then
    for module in "${REQUIRED_WINE_DLL_MODULES[@]}"; do
        dll="$WINE_PE_BUILD_DIR/dlls/$module/aarch64-windows/$module.dll"
        if [ ! -f "$dll" ]; then
            echo "Building required Wine PE DLL $module.dll"
            PATH="$WINE_BUILD_PATH" \
                make -C "$WINE_PE_BUILD_DIR" "dlls/$module/aarch64-windows/$module.dll"
        fi
    done
fi

# vulkan_test.exe (32-bit guest, see the vulkan_test app-kind block below)
# needs winevulkan.dll/vulkan-1.dll built for i386-windows specifically --
# those are ordinary Wine PE DLLs, never built here before now since nothing
# needed them. Every other i386-windows DLL this app needs (kernel32,
# ntdll, user32, gdi32, ...) is assumed already built, same as OpenTTD/
# Luanti's own i386-windows dependencies -- see the generic bulk-copy loop
# further down that stages whatever i386-windows DLLs already exist.
VULKAN_TEST_WINE_DLL_MODULES=(vulkan-1 winevulkan)
if [ "$APP_KIND" = "vulkan_test" ] || [ "$APP_KIND" = "vulkan_triangle_test" ]; then
    if [ ! -d "$WINE_PE_BUILD_DIR" ]; then
        echo "Missing PE build dir for vulkan_test: $WINE_PE_BUILD_DIR" >&2
        exit 1
    fi
    for module in "${VULKAN_TEST_WINE_DLL_MODULES[@]}"; do
        dll="$WINE_PE_BUILD_DIR/dlls/$module/i386-windows/$module.dll"
        if [ ! -f "$dll" ]; then
            echo "Building vulkan_test dependency $module.dll (i386-windows)"
            PATH="$WINE_BUILD_PATH" \
                make -C "$WINE_PE_BUILD_DIR" "dlls/$module/i386-windows/$module.dll"
        fi
    done
fi

if [ "$APP_KIND" = "notepad" ]; then
    echo "Preparing Wine notepad.exe from $PROGRAM_PE_BUILD_DIR"
    if [ ! -d "$PROGRAM_PE_BUILD_DIR" ]; then
        echo "Missing PE build dir for notepad: $PROGRAM_PE_BUILD_DIR" >&2
        echo "Set WINE_NX_PROGRAM_BUILD_DIR to an ARM64 PE Wine build dir." >&2
        exit 1
    fi
    if [ ! -f "$NOTEPAD_EXE" ]; then
        echo "Building Wine PE notepad.exe"
        PATH="$WINE_BUILD_PATH" \
            make -C "$PROGRAM_PE_BUILD_DIR" programs/notepad/aarch64-windows/notepad.exe
    fi
    for module in "${NOTEPAD_WINE_DLL_MODULES[@]}"; do
        dll="$PROGRAM_PE_BUILD_DIR/dlls/$module/aarch64-windows/$module.dll"
        if [ ! -f "$dll" ]; then
            echo "Building notepad dependency $module.dll"
            PATH="$WINE_BUILD_PATH" \
                make -C "$PROGRAM_PE_BUILD_DIR" "dlls/$module/aarch64-windows/$module.dll"
        fi
    done
fi

# openttd.exe itself is not built here -- it's the user's own external game
# install, already placed directly on the SD card at drive_c/openttd (outside
# this script's local staging tree, see tools/sync-switch-wine-package.sh's
# --exclude handling below for why that matters). Only its missing Wine PE
# DLL dependencies get built here.
if [ "$APP_KIND" = "openttd" ]; then
    if [ ! -d "$WINE_PE_BUILD_DIR" ]; then
        echo "Missing PE build dir for openttd: $WINE_PE_BUILD_DIR" >&2
        exit 1
    fi
    for module in "${OPENTTD_WINE_DLL_MODULES[@]}"; do
        dll="$WINE_PE_BUILD_DIR/dlls/$module/aarch64-windows/$module.dll"
        if [ ! -f "$dll" ]; then
            echo "Building openttd dependency $module.dll"
            PATH="$WINE_BUILD_PATH" \
                make -C "$WINE_PE_BUILD_DIR" "dlls/$module/aarch64-windows/$module.dll"
        fi
    done
fi

# luanti.exe (Luanti, open-source voxel game engine) is the user's own
# external game install, same pattern as openttd above: not built here,
# already placed directly on the SD card at drive_c/luanti/bin/luanti.exe
# (upstream Luanti's own distribution layout -- bin/ holds the engine
# binary and its DLLs, data folders are siblings one level up). Its import
# table (checked directly via objdump -p) needs exactly the same DLL set
# openttd already needs -- opengl32/shell32/shlwapi/version/winmm plus the
# always-built REQUIRED_SYSTEM_DLLS -- so it reuses OPENTTD_WINE_DLL_MODULES
# rather than duplicating that list. Its own bundled runtime DLLs (SDL2,
# OpenAL32, libcurl, libleveldb, libc++, libunwind, ...) ship right next to
# luanti.exe itself and are not this port's concern, same as openttd's own
# bundled DLLs.
if [ "$APP_KIND" = "luanti" ]; then
    if [ ! -d "$WINE_PE_BUILD_DIR" ]; then
        echo "Missing PE build dir for luanti: $WINE_PE_BUILD_DIR" >&2
        exit 1
    fi
    for module in "${OPENTTD_WINE_DLL_MODULES[@]}"; do
        dll="$WINE_PE_BUILD_DIR/dlls/$module/aarch64-windows/$module.dll"
        if [ ! -f "$dll" ]; then
            echo "Building luanti dependency $module.dll"
            PATH="$WINE_BUILD_PATH" \
                make -C "$WINE_PE_BUILD_DIR" "dlls/$module/aarch64-windows/$module.dll"
        fi
    done
fi

if [ -f "$BUILD_DIR/wine-nx-runtime.nro" ] && { [ "$APP_KIND" != "curl" ] || [ -f "$SCRIPT_DIR/samples/curl-arm64/trurl.exe" ]; }; then
    echo "Preparing SD package for WINE_NX_APP=$APP_KIND"
    PACKAGE_DIR="$BUILD_DIR/sd-card/switch/wine"
    APP_DIR="$PACKAGE_DIR/drive_c/curl"
    SYSTEM_DIR="$PACKAGE_DIR/drive_c/windows/system32"
    SYSWOW64_DIR="$PACKAGE_DIR/drive_c/windows/syswow64"
    WIN_FONTS_DIR="$PACKAGE_DIR/drive_c/windows/fonts"
    NLS_DIR="$PACKAGE_DIR/share/wine/nls"
    DATA_FONTS_DIR="$PACKAGE_DIR/share/wine/fonts"
    mkdir -p "$APP_DIR" "$SYSTEM_DIR" "$SYSWOW64_DIR" "$WIN_FONTS_DIR" "$NLS_DIR" "$DATA_FONTS_DIR"
    if [ "$APP_KIND" != "curl" ]; then
        rm -f "$APP_DIR/curl.exe" "$APP_DIR/trurl.exe" "$APP_DIR/libcurl-arm64.dll" \
              "$APP_DIR/curl.args" "$APP_DIR/trurl.args"
    fi
    if [ "$APP_KIND" != "notepad" ]; then
        rm -f "$SYSTEM_DIR/notepad.exe"
    fi
    # NLS source layout differs by tree: Proton's vendored wine has them under
    # Proton/wine/nls, a standalone Wine checkout has them under nls/.
    NLS_SRC=""
    if [ -d "$REPO_ROOT/Proton/wine/nls" ]; then
        NLS_SRC="$REPO_ROOT/Proton/wine/nls"
    elif [ -d "$REPO_ROOT/nls" ]; then
        NLS_SRC="$REPO_ROOT/nls"
    fi
    if [ -n "$NLS_SRC" ] && compgen -G "$NLS_SRC/*.nls" >/dev/null; then
        cp "$NLS_SRC/"*.nls "$NLS_DIR/"
        echo "Copied $(ls -1 "$NLS_DIR"/*.nls 2>/dev/null | wc -l | tr -d ' ') NLS files from $NLS_SRC"
    else
        echo "WARNING: no .nls files found (searched Proton/wine/nls, nls/); locale init will fail" >&2
    fi
    if [ -d "$REPO_ROOT/fonts" ] && compgen -G "$REPO_ROOT/fonts/*.ttf" >/dev/null; then
        cp "$REPO_ROOT/fonts/"*.ttf "$WIN_FONTS_DIR/"
        cp "$REPO_ROOT/fonts/"*.ttf "$DATA_FONTS_DIR/"
        echo "Copied $(ls -1 "$WIN_FONTS_DIR"/*.ttf 2>/dev/null | wc -l | tr -d ' ') Wine fonts from $REPO_ROOT/fonts"
    else
        echo "WARNING: no Wine .ttf fonts found in $REPO_ROOT/fonts; GDI text may not render" >&2
    fi
    if [ -d "$WINE_PE_DLL_ROOT" ]; then
        while IFS= read -r -d '' dll; do
            cp "$dll" "$SYSTEM_DIR/"
        done < <(find "$WINE_PE_DLL_ROOT" -path '*/aarch64-windows/*.dll' -print0)
        # 32-bit guest-side DLLs (ntdll/kernel32/kernelbase -- pure, unpatched
        # upstream Wine, never touch the host OS directly, see WoW64 thunking
        # architecture notes) land in syswow64, same generic "copy whatever's
        # there" approach as the aarch64-windows loop above. wow64.dll/
        # wow64win.dll only ever build for aarch64 (never i386 -- confirmed,
        # WoW64 is inherently a 64-bit-host mechanism), so they're already
        # covered by the loop above and never appear here.
        while IFS= read -r -d '' dll; do
            cp "$dll" "$SYSWOW64_DIR/"
        done < <(find "$WINE_PE_DLL_ROOT" -path '*/i386-windows/*.dll' -print0)
    fi
    if [ -f "$WOWBOX64_DLL_PATH" ]; then
        cp "$WOWBOX64_DLL_PATH" "$SYSTEM_DIR/wowbox64.dll"
    else
        echo "WARNING: no wowbox64.dll at $WOWBOX64_DLL_PATH; 32-bit guests won't run. Build it with:" >&2
        echo "  cmake -S \"$SCRIPT_DIR\" -B \"$WOWBOX64_BUILD_DIR\" -DWINE_NX_BUILD_WOWBOX64=ON && cmake --build \"$WOWBOX64_BUILD_DIR\" --target wowbox64-dll" >&2
    fi
    if [ "$APP_KIND" = "notepad" ] && [ -d "$PROGRAM_PE_DLL_ROOT" ]; then
        while IFS= read -r -d '' dll; do
            cp "$dll" "$SYSTEM_DIR/"
        done < <(find "$PROGRAM_PE_DLL_ROOT" -path '*/aarch64-windows/*.dll' -print0)
    fi
    if [ -d "$LLVM_MINGW_RUNTIME_DIR" ]; then
        while IFS= read -r -d '' dll; do
            cp "$dll" "$SYSTEM_DIR/"
        done < <(find "$LLVM_MINGW_RUNTIME_DIR" -maxdepth 1 -name '*.dll' -print0)
    fi
    if [ -f "$BUILD_DIR/wine-nx-pe-real-report.nro" ]; then
        cp "$BUILD_DIR/wine-nx-pe-real-report.nro" "$PACKAGE_DIR/wine-nx-pe-real-report.nro"
    fi
    if [ -f "$BUILD_DIR/wine-nx-pe-real-run.nro" ]; then
        cp "$BUILD_DIR/wine-nx-pe-real-run.nro" "$PACKAGE_DIR/wine-nx-pe-real-run.nro"
    fi
    if [ -f "$BUILD_DIR/wine-nx-ntdll-file-smoke.nro" ]; then
        cp "$BUILD_DIR/wine-nx-ntdll-file-smoke.nro" "$PACKAGE_DIR/wine-nx-ntdll-file-smoke.nro"
    fi
    if [ -f "$BUILD_DIR/wine-nx-runtime.nro" ]; then
        cp "$BUILD_DIR/wine-nx-runtime.nro" "$PACKAGE_DIR/wine-nx-runtime.nro"
    fi
    if [ -f "$BUILD_DIR/wine-nx-runtime-deko3d.nro" ]; then
        cp "$BUILD_DIR/wine-nx-runtime-deko3d.nro" "$PACKAGE_DIR/wine-nx-runtime-deko3d.nro"
    fi
    # Same wine-nx-runtime.elf as "Wine-NX Runtime" above, just its own
    # distinctly-titled hbmenu entry (see CMakeLists.txt's
    # wine-nx-vulkan-runtime-nro target) so a Vulkan test run is
    # unambiguous. Reads the same target.txt as every other entry here.
    if [ -f "$BUILD_DIR/wine-nx-vulkan-runtime.nro" ]; then
        cp "$BUILD_DIR/wine-nx-vulkan-runtime.nro" "$PACKAGE_DIR/wine-nx-vulkan-runtime.nro"
    fi
    # source/gui/nxtest.c's auto-test mode envSetNextLoad's straight to this
    # by name (sdmc:/switch/wine/wine-nx-jit-smoke.nro) as its one real,
    # wired-end-to-end test entry -- see that file's own comment on why the
    # other 8 entries are protocol-only for now. Wasn't previously staged at
    # all (a pre-existing gap, unrelated to this feature, but auto-test mode
    # now depends on it being here).
    if [ -f "$BUILD_DIR/wine-nx-jit-smoke.nro" ]; then
        cp "$BUILD_DIR/wine-nx-jit-smoke.nro" "$PACKAGE_DIR/wine-nx-jit-smoke.nro"
    fi
    # The launcher itself -- see CMakeLists.txt's wine-nx-launcher-nro target
    # and source/launcher_main.c. This is the one users actually run from
    # hbmenu; every other .nro staged in this directory is either a backend
    # runtime it hands off to or a standalone smoke test its auto-test mode
    # chains through.
    if [ -f "$BUILD_DIR/wine-nx-launcher.nro" ]; then
        cp "$BUILD_DIR/wine-nx-launcher.nro" "$PACKAGE_DIR/wine-nx-launcher.nro"
    fi
    # GUI smoke app: WINE_NX_APP=gui stages the win32u software-window test exe.
    if [ "$APP_KIND" = "gui" ]; then
        GUI_SRC="$SCRIPT_DIR/samples/gui-smoke/gui_smoke.c"
        GUI_EXE="$SCRIPT_DIR/samples/gui-smoke/gui_smoke.exe"
        GUI_CC="$LLVM_MINGW_BIN_DIR/aarch64-w64-mingw32-clang"
        if [ -f "$GUI_SRC" ]; then
            if [ ! -x "$GUI_CC" ]; then
                GUI_CC="$(command -v aarch64-w64-mingw32-clang || true)"
            fi
            if [ -z "$GUI_CC" ] || [ ! -x "$GUI_CC" ]; then
                echo "Missing aarch64-w64-mingw32-clang; cannot build GUI smoke app" >&2
                exit 1
            fi
            if [ ! -f "$GUI_EXE" ] || [ "$GUI_SRC" -nt "$GUI_EXE" ]; then
                "$GUI_CC" -municode -mwindows -O2 -Wall -Wextra \
                    -o "$GUI_EXE" "$GUI_SRC" -luser32 -lgdi32
            fi
        fi
    fi
    # 32-bit WOW64 smoke test: WINE_NX_APP=cube32 stages the only sample that
    # actually exercises WOW64/box64 (see samples/cube32/cube32.c's file
    # header) -- built i686-w64-mingw32, not aarch64-w64-mingw32 like gui/
    # blit above. Previously staged by hand every time (copy exe, edit
    # target.txt/run-entry.txt) because this branch didn't exist yet.
    if [ "$APP_KIND" = "cube32" ]; then
        CUBE32_SRC="$SCRIPT_DIR/samples/cube32/cube32.c"
        CUBE32_EXE="$SCRIPT_DIR/samples/cube32/cube32.exe"
        CUBE32_CC="$LLVM_MINGW_BIN_DIR/i686-w64-mingw32-clang"
        if [ -f "$CUBE32_SRC" ]; then
            if [ ! -x "$CUBE32_CC" ]; then
                CUBE32_CC="$(command -v i686-w64-mingw32-clang || true)"
            fi
            if [ -z "$CUBE32_CC" ] || [ ! -x "$CUBE32_CC" ]; then
                echo "Missing i686-w64-mingw32-clang; cannot build cube32 test app" >&2
                exit 1
            fi
            if [ ! -f "$CUBE32_EXE" ] || [ "$CUBE32_SRC" -nt "$CUBE32_EXE" ]; then
                "$CUBE32_CC" -municode -mwindows -O2 -Wall -Wextra \
                    -o "$CUBE32_EXE" "$CUBE32_SRC" -luser32 -lgdi32
            fi
        fi
    fi
    # 64-bit (x86_64) variant of the same cube smoke test, built from the
    # same cube32.c source with the x86_64 cross target. The x86_64
    # execution path is not implemented yet -- today this target boots only
    # as far as runtime.c's machine detection/routing and logs a clean
    # "[RUN] 64-bit x86 guest detected" line instead of running; it exists
    # to hardware-verify exactly that routing, and becomes the real 64-bit
    # smoke test once an executor lands.
    if [ "$APP_KIND" = "cube64" ]; then
        CUBE64_SRC="$SCRIPT_DIR/samples/cube32/cube32.c"
        CUBE64_EXE="$SCRIPT_DIR/samples/cube32/cube64.exe"
        CUBE64_CC="$LLVM_MINGW_BIN_DIR/x86_64-w64-mingw32-clang"
        if [ -f "$CUBE64_SRC" ]; then
            if [ ! -x "$CUBE64_CC" ]; then
                CUBE64_CC="$(command -v x86_64-w64-mingw32-clang || true)"
            fi
            if [ -z "$CUBE64_CC" ] || [ ! -x "$CUBE64_CC" ]; then
                echo "Missing x86_64-w64-mingw32-clang; cannot build cube64 test app" >&2
                exit 1
            fi
            if [ ! -f "$CUBE64_EXE" ] || [ "$CUBE64_SRC" -nt "$CUBE64_EXE" ]; then
                "$CUBE64_CC" -municode -mwindows -O2 -Wall -Wextra \
                    -o "$CUBE64_EXE" "$CUBE64_SRC" -luser32 -lgdi32
            fi
        fi
    fi
    # First real end-to-end Vulkan test: a 32-bit (WOW64) guest .exe that
    # creates a window, loads vulkan-1.dll, and drives instance -> surface
    # -> device -> swapchain -> present through dlls/win32u/winnx_vulkan.c's
    # NVK-backed driver. Built i686-w64-mingw32 like cube32, for the same
    # reason: it's the real 32-bit guest path (WOW64/box64), not a native
    # ARM64 PE. winevulkan.dll/vulkan-1.dll themselves are ordinary Wine PE
    # DLLs, built for i386-windows below like any other guest dependency.
    if [ "$APP_KIND" = "vulkan_test" ]; then
        VULKAN_TEST_SRC="$SCRIPT_DIR/samples/vulkan_test/vulkan_test.c"
        VULKAN_TEST_EXE="$SCRIPT_DIR/samples/vulkan_test/vulkan_test.exe"
        VULKAN_TEST_CC="$LLVM_MINGW_BIN_DIR/i686-w64-mingw32-clang"
        if [ -f "$VULKAN_TEST_SRC" ]; then
            if [ ! -x "$VULKAN_TEST_CC" ]; then
                VULKAN_TEST_CC="$(command -v i686-w64-mingw32-clang || true)"
            fi
            if [ -z "$VULKAN_TEST_CC" ] || [ ! -x "$VULKAN_TEST_CC" ]; then
                echo "Missing i686-w64-mingw32-clang; cannot build vulkan_test app" >&2
                exit 1
            fi
            if [ ! -f "$VULKAN_TEST_EXE" ] || [ "$VULKAN_TEST_SRC" -nt "$VULKAN_TEST_EXE" ]; then
                # Headers only (vk_icdGetInstanceProcAddr's own vendored copy of
                # the standard Khronos headers) -- vulkan-1.dll is loaded
                # dynamically via LoadLibrary/GetProcAddress, not linked, so no
                # import lib is needed here.
                "$VULKAN_TEST_CC" -municode -mwindows -O2 -Wall -Wextra \
                    -I "$SCRIPT_DIR/third_party/nxvk/include" \
                    -o "$VULKAN_TEST_EXE" "$VULKAN_TEST_SRC" -luser32 -lgdi32
            fi
        fi
    fi
    # Triangle test: real render pass/pipeline/vkCmdDraw, one step up from
    # vulkan_test's bare clear. Shaders are pre-compiled SPIR-V C headers
    # (third_party/nxvk/switch/smoke/shaders/{tri_vert,solid_frag}.h) --
    # regenerate them with switch/smoke/shaders/gen-shaders.sh (needs
    # glslangValidator, inside the nvk-switch-build image) if they're ever
    # missing or the .vert/.frag sources change.
    if [ "$APP_KIND" = "vulkan_triangle_test" ]; then
        VULKAN_TRI_SRC="$SCRIPT_DIR/samples/vulkan_test/vulkan_triangle_test.c"
        VULKAN_TRI_EXE="$SCRIPT_DIR/samples/vulkan_test/vulkan_triangle_test.exe"
        VULKAN_TRI_CC="$LLVM_MINGW_BIN_DIR/i686-w64-mingw32-clang"
        VULKAN_TRI_SHADERS_DIR="$SCRIPT_DIR/third_party/nxvk/switch/smoke/shaders"
        if [ -f "$VULKAN_TRI_SRC" ]; then
            if [ ! -x "$VULKAN_TRI_CC" ]; then
                VULKAN_TRI_CC="$(command -v i686-w64-mingw32-clang || true)"
            fi
            if [ -z "$VULKAN_TRI_CC" ] || [ ! -x "$VULKAN_TRI_CC" ]; then
                echo "Missing i686-w64-mingw32-clang; cannot build vulkan_triangle_test app" >&2
                exit 1
            fi
            if [ ! -f "$VULKAN_TRI_SHADERS_DIR/tri_vert.h" ] || [ ! -f "$VULKAN_TRI_SHADERS_DIR/solid_frag.h" ]; then
                echo "Missing pre-compiled shader headers in $VULKAN_TRI_SHADERS_DIR" >&2
                echo "  Regenerate with: docker run --rm -v \"$SCRIPT_DIR/third_party/nxvk:/work:z\" -w /work nvk-switch-build bash switch/smoke/shaders/gen-shaders.sh" >&2
                exit 1
            fi
            if [ ! -f "$VULKAN_TRI_EXE" ] || [ "$VULKAN_TRI_SRC" -nt "$VULKAN_TRI_EXE" ]; then
                "$VULKAN_TRI_CC" -municode -mwindows -O2 -Wall -Wextra \
                    -I "$SCRIPT_DIR/third_party/nxvk/include" \
                    -I "$VULKAN_TRI_SHADERS_DIR" \
                    -o "$VULKAN_TRI_EXE" "$VULKAN_TRI_SRC" -luser32 -lgdi32
            fi
        fi
    fi
    # Direct-blit test app: WINE_NX_APP=blit stages the no-InvalidateRect/
    # no-BeginPaint GetDC+StretchDIBits comparison test (see
    # samples/direct-blit/direct_blit.c's file header for what this isolates).
    if [ "$APP_KIND" = "blit" ]; then
        BLIT_SRC="$SCRIPT_DIR/samples/direct-blit/direct_blit.c"
        BLIT_EXE="$SCRIPT_DIR/samples/direct-blit/direct_blit.exe"
        BLIT_CC="$LLVM_MINGW_BIN_DIR/aarch64-w64-mingw32-clang"
        if [ -f "$BLIT_SRC" ]; then
            if [ ! -x "$BLIT_CC" ]; then
                BLIT_CC="$(command -v aarch64-w64-mingw32-clang || true)"
            fi
            if [ -z "$BLIT_CC" ] || [ ! -x "$BLIT_CC" ]; then
                echo "Missing aarch64-w64-mingw32-clang; cannot build direct-blit test app" >&2
                exit 1
            fi
            if [ ! -f "$BLIT_EXE" ] || [ "$BLIT_SRC" -nt "$BLIT_EXE" ]; then
                "$BLIT_CC" -municode -mwindows -O2 -Wall -Wextra \
                    -o "$BLIT_EXE" "$BLIT_SRC" -luser32 -lgdi32
            fi
        fi
    fi
    if [ "$APP_KIND" = "gui" ] && [ -f "$SCRIPT_DIR/samples/gui-smoke/gui_smoke.exe" ]; then
        GUI_DIR="$PACKAGE_DIR/drive_c/gui"
        mkdir -p "$GUI_DIR"
        cp "$SCRIPT_DIR/samples/gui-smoke/gui_smoke.exe" "$GUI_DIR/gui_smoke.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/gui/gui_smoke.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged GUI smoke app as runtime target (WINE_NX_APP=gui)"
    elif [ "$APP_KIND" = "blit" ] && [ -f "$SCRIPT_DIR/samples/direct-blit/direct_blit.exe" ]; then
        BLIT_DIR="$PACKAGE_DIR/drive_c/blit"
        mkdir -p "$BLIT_DIR"
        cp "$SCRIPT_DIR/samples/direct-blit/direct_blit.exe" "$BLIT_DIR/direct_blit.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/blit/direct_blit.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged direct-blit test app as runtime target (WINE_NX_APP=blit)"
    elif [ "$APP_KIND" = "cube32" ] && [ -f "$SCRIPT_DIR/samples/cube32/cube32.exe" ]; then
        CUBE32_DIR="$PACKAGE_DIR/drive_c/cube32"
        mkdir -p "$CUBE32_DIR"
        cp "$SCRIPT_DIR/samples/cube32/cube32.exe" "$CUBE32_DIR/cube32.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/cube32/cube32.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        # This is specifically a performance smoke test -- never force
        # syscall tracing on for it (see the openttd branch's comment on why
        # that was hardware-confirmed to dominate the 1fps result).
        printf '%s\n' '0' > "$PACKAGE_DIR/systrace.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged cube32 (32-bit WOW64 smoke test) as runtime target (WINE_NX_APP=cube32)"
    elif [ "$APP_KIND" = "vulkan_test" ] && [ -f "$SCRIPT_DIR/samples/vulkan_test/vulkan_test.exe" ]; then
        VULKAN_TEST_DIR="$PACKAGE_DIR/drive_c/vulkan_test"
        mkdir -p "$VULKAN_TEST_DIR"
        cp "$SCRIPT_DIR/samples/vulkan_test/vulkan_test.exe" "$VULKAN_TEST_DIR/vulkan_test.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/vulkan_test/vulkan_test.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        printf '%s\n' '0' > "$PACKAGE_DIR/systrace.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged vulkan_test (32-bit Vulkan smoke test) as runtime target (WINE_NX_APP=vulkan_test)"
    elif [ "$APP_KIND" = "vulkan_triangle_test" ] && [ -f "$SCRIPT_DIR/samples/vulkan_test/vulkan_triangle_test.exe" ]; then
        VULKAN_TRI_DIR="$PACKAGE_DIR/drive_c/vulkan_triangle_test"
        mkdir -p "$VULKAN_TRI_DIR"
        cp "$SCRIPT_DIR/samples/vulkan_test/vulkan_triangle_test.exe" "$VULKAN_TRI_DIR/vulkan_triangle_test.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/vulkan_triangle_test/vulkan_triangle_test.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        printf '%s\n' '0' > "$PACKAGE_DIR/systrace.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged vulkan_triangle_test (32-bit Vulkan shader/pipeline test) as runtime target (WINE_NX_APP=vulkan_triangle_test)"
    elif [ "$APP_KIND" = "cube64" ] && [ -f "$SCRIPT_DIR/samples/cube32/cube64.exe" ]; then
        CUBE64_DIR="$PACKAGE_DIR/drive_c/cube64"
        mkdir -p "$CUBE64_DIR"
        cp "$SCRIPT_DIR/samples/cube32/cube64.exe" "$CUBE64_DIR/cube64.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/cube64/cube64.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        printf '%s\n' '0' > "$PACKAGE_DIR/systrace.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged cube64 (x86_64 detection smoke test) as runtime target (WINE_NX_APP=cube64)"
    elif [ "$APP_KIND" = "notepad" ] && [ -f "$NOTEPAD_EXE" ]; then
        cp "$NOTEPAD_EXE" "$SYSTEM_DIR/notepad.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/windows/system32/notepad.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged Wine notepad.exe as runtime target (WINE_NX_APP=notepad)"
    elif [ "$APP_KIND" = "openttd" ]; then
        # openttd.exe and its game data (baseset/lang/ai/game/scripts/...) are
        # not staged here -- they're the user's own external install, already
        # on the SD card at drive_c/openttd. Only the DLLs it needs and
        # target.txt/run-entry.txt get packaged; deploying must not run a
        # plain --delete mirror over that folder (see sync script's --exclude).
        printf '%s\n' 'sdmc:/switch/wine/drive_c/openttd/openttd.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        # Raw per-syscall tracing used to be force-enabled here to chase the
        # WoW64 boot-sequence silent-hang bugs (openttd.exe's bootstrap went
        # silent well before any window existed, and two narrow [NX-DIAG]
        # checkpoints already guessed wrong once). That whole bug chain is
        # fixed now (see README's "32-bit WOW64 Guest Boot" section) and this
        # forced-on trace was hardware-confirmed to be *the* dominant 1fps
        # cause on cube32 (19,358 of 23,067 log lines / 84%, each an
        # unconditional fflush-to-SD-card write) once a window is actually
        # painting -- it was never turned back off after the bugs it was
        # added for were fixed. Explicitly writing 0 leaves a clean,
        # unambiguous file rather than relying on config.txt/absence. Turn
        # it back on manually (WINE_NX_SYSCALL_TRACE=1 or edit
        # systrace.txt) only for actual boot-sequence debugging.
        printf '%s\n' '0' > "$PACKAGE_DIR/systrace.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged openttd.exe as runtime target (WINE_NX_APP=openttd); game files stay on the SD card as-is"
    elif [ "$APP_KIND" = "luanti" ]; then
        # Same external-game pattern as openttd: luanti.exe and everything
        # it ships with (SDL2.dll, OpenAL32.dll, libcurl.dll, etc.) are not
        # staged here -- they're expected already on the SD card at
        # drive_c/luanti, placed there directly (see sync script's
        # --exclude handling above so a resync doesn't wipe them out).
        # luanti.exe lives in bin/ alongside its engine DLLs (SDL2.dll,
        # OpenAL32.dll, ...); data folders (builtin/client/fonts/locale/
        # mods/textures) are siblings of bin/, one level up -- that's
        # upstream Luanti's real distribution layout, not worth asking
        # anyone to flatten just for this port.
        printf '%s\n' 'sdmc:/switch/wine/drive_c/luanti/bin/luanti.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
        printf '%s\n' '0' > "$PACKAGE_DIR/systrace.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged luanti.exe as runtime target (WINE_NX_APP=luanti); game files stay on the SD card as-is"
    elif [ "$APP_KIND" = "curl" ] && [ -f "$SCRIPT_DIR/samples/curl-arm64/curl.exe" ]; then
        cp "$SCRIPT_DIR/samples/curl-arm64/curl.exe" "$APP_DIR/curl.exe"
        printf '%s\n' 'sdmc:/switch/wine/drive_c/curl/curl.exe' > "$PACKAGE_DIR/target.txt"
        printf '%s\n' '1' > "$PACKAGE_DIR/run-entry.txt"
    else
        echo "Unsupported or missing runtime app target: WINE_NX_APP=$APP_KIND" >&2
        exit 1
    fi
    if [ "$APP_KIND" = "curl" ]; then
        if [ -f "$SCRIPT_DIR/samples/curl-arm64/libcurl-arm64.dll" ]; then
            cp "$SCRIPT_DIR/samples/curl-arm64/libcurl-arm64.dll" "$APP_DIR/libcurl-arm64.dll"
        fi
        cp "$SCRIPT_DIR/samples/curl-arm64/trurl.exe" "$APP_DIR/trurl.exe"
        if [ -f "$SCRIPT_DIR/samples/curl.args" ]; then
            cp "$SCRIPT_DIR/samples/curl.args" "$PACKAGE_DIR/args.txt"
            cp "$SCRIPT_DIR/samples/curl.args" "$APP_DIR/curl.args"
        elif [ -f "$SCRIPT_DIR/samples/trurl.args" ]; then
            cp "$SCRIPT_DIR/samples/trurl.args" "$PACKAGE_DIR/args.txt"
        fi
        if [ -f "$SCRIPT_DIR/samples/trurl.args" ]; then
            cp "$SCRIPT_DIR/samples/trurl.args" "$APP_DIR/trurl.args"
        fi
    fi

    missing=0
    for dll in "${REQUIRED_SYSTEM_DLLS[@]}"; do
        if [ ! -f "$SYSTEM_DIR/$dll" ]; then
            echo "Missing required runtime DLL: $SYSTEM_DIR/$dll" >&2
            missing=1
        fi
    done
    if [ "$APP_KIND" = "notepad" ]; then
        for dll in "${NOTEPAD_SYSTEM_DLLS[@]}"; do
            if [ ! -f "$SYSTEM_DIR/$dll" ]; then
                echo "Missing notepad runtime DLL: $SYSTEM_DIR/$dll" >&2
                missing=1
            fi
        done
    fi
    if [ "$APP_KIND" = "openttd" ] || [ "$APP_KIND" = "luanti" ]; then
        for dll in "${OPENTTD_SYSTEM_DLLS[@]}"; do
            if [ ! -f "$SYSTEM_DIR/$dll" ]; then
                echo "Missing $APP_KIND runtime DLL: $SYSTEM_DIR/$dll" >&2
                missing=1
            fi
        done
    fi
    if [ "$missing" -ne 0 ]; then
        exit 1
    fi

    {
        echo "# wine-nx runtime package manifest"
        echo "# Copy this whole directory to sdmc:/switch/wine"
        echo
        echo "[system32]"
        find "$SYSTEM_DIR" -maxdepth 1 -type f -name '*.dll' -exec basename {} \; | sort
        echo
        echo "[nls]"
        find "$NLS_DIR" -maxdepth 1 -type f -name '*.nls' -exec basename {} \; | sort
        echo
        echo "[fonts]"
        find "$WIN_FONTS_DIR" -maxdepth 1 -type f -name '*.ttf' -exec basename {} \; | sort
        echo
        if [ -f "$SYSTEM_DIR/notepad.exe" ]; then
            echo "[notepad]"
            echo "notepad.exe"
            echo
        fi
        if compgen -G "$APP_DIR/*" >/dev/null; then
            echo "[curl]"
            find "$APP_DIR" -maxdepth 1 -type f -exec basename {} \; | sort
        fi
    } > "$PACKAGE_DIR/runtime-manifest.txt"

    echo "Prepared SD package $PACKAGE_DIR"
    echo "Sync mounted SD with: $SCRIPT_DIR/tools/sync-switch-wine-package.sh"
fi
