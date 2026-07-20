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

echo "Building Switch runtime NRO with devkitA64 Docker image..."
docker run --rm \
    -v "$REPO_ROOT:/project" \
    --workdir /project/wine-nx-probe \
    devkitpro/devkita64 \
    sh -lc '
        # win32u font rendering (font.c/freetype.c) needs the freetype portlib
        if [ ! -f "$DEVKITPRO/portlibs/switch/include/freetype2/ft2build.h" ]; then
            dkp-pacman -Sy --noconfirm --needed switch-freetype switch-harfbuzz switch-bzip2 switch-libpng
        fi
        cmake -S . -B build-switch \
            -DCMAKE_TOOLCHAIN_FILE=cmake/switch-devkitA64.cmake \
            -DCMAKE_BUILD_TYPE=Release
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
        # Raw per-syscall tracing is off by default (real perf cost once a
        # window is painting, see wine_nx_syscall_trace_select()'s comment in
        # runtime.c) but openttd.exe's WoW64 bootstrap goes silent well before
        # any window exists, and two narrow [NX-DIAG] checkpoints already
        # guessed wrong once -- this gives full visibility into every native
        # syscall instead of guessing at specific functions again.
        printf '%s\n' '1' > "$PACKAGE_DIR/systrace.txt"
        rm -f "$PACKAGE_DIR/args.txt"
        echo "Staged openttd.exe as runtime target (WINE_NX_APP=openttd); game files stay on the SD card as-is"
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
    if [ "$APP_KIND" = "openttd" ]; then
        for dll in "${OPENTTD_SYSTEM_DLLS[@]}"; do
            if [ ! -f "$SYSTEM_DIR/$dll" ]; then
                echo "Missing openttd runtime DLL: $SYSTEM_DIR/$dll" >&2
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
