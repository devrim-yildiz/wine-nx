#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROBE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PACKAGE_DIR="${PACKAGE_DIR:-$PROBE_DIR/build-switch/sd-card/switch/wine}"
SD_ROOT="${1:-${SWITCH_SD_ROOT:-/Volumes/SWITCH SD}}"
DEST_DIR="$SD_ROOT/switch/wine"
SYSTEM_DIR="$DEST_DIR/drive_c/windows/system32"

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

if [ ! -d "$PACKAGE_DIR" ]; then
    echo "Package does not exist: $PACKAGE_DIR" >&2
    echo "Run ./wine-nx-probe/build-switch.sh first." >&2
    exit 1
fi

if [ ! -d "$SD_ROOT" ]; then
    echo "Switch SD root is not mounted: $SD_ROOT" >&2
    echo "Pass it explicitly, for example: $0 '/Volumes/SWITCH SD'" >&2
    exit 1
fi

mkdir -p "$SD_ROOT/switch"
# --exclude keeps externally-installed games (placed directly on the SD card,
# never part of this repo's local staging tree) safe from --delete's mirror
# cleanup -- without it, anything under drive_c on the SD card that isn't
# also in $PACKAGE_DIR gets deleted, wiping out drive_c/openttd (or any future
# external app placed the same way).
rsync -a --delete --exclude='drive_c/openttd' "$PACKAGE_DIR/" "$DEST_DIR/"

missing=0
for dll in "${REQUIRED_SYSTEM_DLLS[@]}"; do
    if [ ! -f "$SYSTEM_DIR/$dll" ]; then
        echo "Missing on SD: $SYSTEM_DIR/$dll" >&2
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    exit 1
fi

echo "Synced $PACKAGE_DIR"
echo "Verified runtime DLLs under $SYSTEM_DIR"
