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
        # win32u also needs wine'\''s widl-generated headers (objidlbase.h
        # etc.), which are not in git -- generate them on first build. The
        # devkitA64 image is Debian but ships without bison/flex, which
        # wine'\''s configure needs for the generator tools.
        if [ ! -f build-wine-headers/include/objidlbase.h ] && [ ! -f build-wine-arm64-pe-clean/include/objidlbase.h ]; then
            command -v bison >/dev/null 2>&1 || { apt-get update && apt-get install -y --no-install-recommends bison flex; }
            ./tools/generate-wine-headers.sh
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

if [ -f "$BUILD_DIR/wine-nx-runtime.nro" ] && { [ "$APP_KIND" != "curl" ] || [ -f "$SCRIPT_DIR/samples/curl-arm64/trurl.exe" ]; }; then
    echo "Preparing SD package for WINE_NX_APP=$APP_KIND"
    PACKAGE_DIR="$BUILD_DIR/sd-card/switch/wine"
    APP_DIR="$PACKAGE_DIR/drive_c/curl"
    SYSTEM_DIR="$PACKAGE_DIR/drive_c/windows/system32"
    WIN_FONTS_DIR="$PACKAGE_DIR/drive_c/windows/fonts"
    NLS_DIR="$PACKAGE_DIR/share/wine/nls"
    DATA_FONTS_DIR="$PACKAGE_DIR/share/wine/fonts"
    mkdir -p "$APP_DIR" "$SYSTEM_DIR" "$WIN_FONTS_DIR" "$NLS_DIR" "$DATA_FONTS_DIR"
    # pe-real-report.nro ships in every package and probes drive_c/curl for
    # a real console exe (its 2026-07-19 hardware run returned
    # real_exe_ready=NO purely because a gui-target package had stripped
    # curl.exe). Stage the curl sample binaries whenever they exist
    # (fetched via tools/fetch-samples.sh) so that smoke is meaningful
    # regardless of WINE_NX_APP; only the .args files, which steer the
    # curl *runtime target*, stay curl-target-only.
    if [ "$APP_KIND" != "curl" ]; then
        rm -f "$APP_DIR/curl.args" "$APP_DIR/trurl.args"
    fi
    for curl_file in curl.exe trurl.exe libcurl-arm64.dll; do
        if [ -f "$SCRIPT_DIR/samples/curl-arm64/$curl_file" ]; then
            cp "$SCRIPT_DIR/samples/curl-arm64/$curl_file" "$APP_DIR/$curl_file"
        fi
    done
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
    # Wine's fonts/ dir mixes two kinds of files: real, loadable TTFs and
    # bitmap-font build INTERMEDIATES (OS/2 vendor 'Wine' + EBSC tables:
    # fixedsys/system/courier/small_fonts/ms_sans_serif + _jp) that win32u
    # deliberately refuses to load in both of its parse paths
    # (dlls/win32u/opentype.c: "Wine uses ttfs as an intermediate step in
    # building its bitmap fonts; we don't want to load these"). Staging
    # those seven only produced 14 face_fail lines per boot, hardware-
    # confirmed twice. Stage only the loadable TTFs, plus the built .fon
    # bitmap fonts from the PE build tree -- the artifact upstream actually
    # installs; FreeType loads Windows FNT natively, the Switch scan
    # accepts the .fon extension and passes ADDFONT_ALLOW_BITMAP.
    WINE_LOADABLE_TTFS="marlett symbol tahoma tahomabd webdings wingding"
    rm -f "$WIN_FONTS_DIR"/*.ttf "$DATA_FONTS_DIR"/*.ttf
    ttf_count=0
    for font_name in $WINE_LOADABLE_TTFS; do
        if [ -f "$REPO_ROOT/fonts/$font_name.ttf" ]; then
            cp "$REPO_ROOT/fonts/$font_name.ttf" "$WIN_FONTS_DIR/"
            cp "$REPO_ROOT/fonts/$font_name.ttf" "$DATA_FONTS_DIR/"
            ttf_count=$((ttf_count + 1))
        fi
    done
    if [ "$ttf_count" -gt 0 ]; then
        echo "Copied $ttf_count Wine TTF fonts from $REPO_ROOT/fonts"
    else
        echo "WARNING: no loadable Wine .ttf fonts found in $REPO_ROOT/fonts; GDI text may not render" >&2
    fi
    if compgen -G "$WINE_PE_BUILD_DIR/fonts/*.fon" >/dev/null; then
        cp "$WINE_PE_BUILD_DIR/fonts/"*.fon "$WIN_FONTS_DIR/"
        cp "$WINE_PE_BUILD_DIR/fonts/"*.fon "$DATA_FONTS_DIR/"
        echo "Copied $(ls -1 "$WIN_FONTS_DIR"/*.fon 2>/dev/null | wc -l | tr -d ' ') Wine bitmap .fon fonts from $WINE_PE_BUILD_DIR/fonts"
    else
        echo "WARNING: no built .fon fonts in $WINE_PE_BUILD_DIR/fonts;" \
             "bitmap families (System/Fixedsys/Courier) will substitute to Tahoma" >&2
    fi
    if [ -d "$WINE_PE_DLL_ROOT" ]; then
        while IFS= read -r -d '' dll; do
            cp "$dll" "$SYSTEM_DIR/"
        done < <(find "$WINE_PE_DLL_ROOT" -path '*/aarch64-windows/*.dll' -print0)
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
        # Only require the PE compiler when a rebuild is actually needed --
        # a fresh gui_smoke.exe built elsewhere (e.g. inside the
        # docker-host-sim container, which has llvm-mingw) stages as-is.
        if [ -f "$GUI_SRC" ] && { [ ! -f "$GUI_EXE" ] || [ "$GUI_SRC" -nt "$GUI_EXE" ]; }; then
            if [ ! -x "$GUI_CC" ]; then
                GUI_CC="$(command -v aarch64-w64-mingw32-clang || true)"
            fi
            if [ -z "$GUI_CC" ] || [ ! -x "$GUI_CC" ]; then
                echo "Missing aarch64-w64-mingw32-clang; cannot build GUI smoke app" >&2
                exit 1
            fi
            "$GUI_CC" -municode -mwindows -O2 -Wall -Wextra \
                -o "$GUI_EXE" "$GUI_SRC" -luser32 -lgdi32
        fi
    fi
    # Direct-blit test app: WINE_NX_APP=blit stages the no-InvalidateRect/
    # no-BeginPaint GetDC+StretchDIBits comparison test (see
    # samples/direct-blit/direct_blit.c's file header for what this isolates).
    if [ "$APP_KIND" = "blit" ]; then
        BLIT_SRC="$SCRIPT_DIR/samples/direct-blit/direct_blit.c"
        BLIT_EXE="$SCRIPT_DIR/samples/direct-blit/direct_blit.exe"
        BLIT_CC="$LLVM_MINGW_BIN_DIR/aarch64-w64-mingw32-clang"
        # Same stale-check-first ordering as the GUI smoke app above.
        if [ -f "$BLIT_SRC" ] && { [ ! -f "$BLIT_EXE" ] || [ "$BLIT_SRC" -nt "$BLIT_EXE" ]; }; then
            if [ ! -x "$BLIT_CC" ]; then
                BLIT_CC="$(command -v aarch64-w64-mingw32-clang || true)"
            fi
            if [ -z "$BLIT_CC" ] || [ ! -x "$BLIT_CC" ]; then
                echo "Missing aarch64-w64-mingw32-clang; cannot build direct-blit test app" >&2
                exit 1
            fi
            "$BLIT_CC" -municode -mwindows -O2 -Wall -Wextra \
                -o "$BLIT_EXE" "$BLIT_SRC" -luser32 -lgdi32
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
    if [ "$missing" -ne 0 ]; then
        exit 1
    fi

    {
        echo "# wine-nx runtime package manifest"
        echo "# Copy this whole directory to sdmc:/switch/wine"
        echo
        # Provenance: a stale NRO on the SD card has burned multiple debug
        # sessions (the README checklist's build-marker gotcha) and the
        # 2026-07-19 deploy had to be *proven* current via a behavioral
        # change in the logs because this manifest carried no identity.
        echo "[build]"
        echo "git: $(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)$(git -C "$REPO_ROOT" diff --quiet 2>/dev/null || echo '-dirty')"
        echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo
        echo "[nro-sha256]"
        for nro in "$PACKAGE_DIR"/*.nro; do
            if [ -f "$nro" ]; then
                echo "$(basename "$nro"): $(shasum -a 256 "$nro" | cut -d' ' -f1)"
            fi
        done
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
