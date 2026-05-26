#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

(cd "$ROOT/Proton/wine" && ./tools/make_specfiles)

docker run --rm \
    -v "$ROOT:/work" \
    --workdir /work \
    devkitpro/devkita64 \
    sh -lc '
set -eu
cc=/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc
flags="-std=gnu99 \
    -D__SWITCH__ \
    -D__WINESRC__ \
    -DWINE_UNIX_LIB \
    -D_WIN64 \
    -D_NTSYSTEM_ \
    -D_ACRTIMP= \
    -DWINBASEAPI= \
    -Wall \
    -Wextra \
    -Werror=implicit-function-declaration \
    -I/work/wine-nx-probe/syntax \
    -I/work/Proton/wine/include \
    -I/work/Proton/wine/dlls/ntdll \
    -I/work/Proton/wine/dlls/ntdll/unix \
    -I/opt/devkitpro/libnx/include"

for f in \
    Proton/wine/dlls/ntdll/unix/cdrom.c \
    Proton/wine/dlls/ntdll/unix/debug.c \
    Proton/wine/dlls/ntdll/unix/env.c \
    Proton/wine/dlls/ntdll/unix/file.c \
    Proton/wine/dlls/ntdll/unix/fsync.c \
    Proton/wine/dlls/ntdll/unix/horizon.c \
    Proton/wine/dlls/ntdll/unix/loader.c \
    Proton/wine/dlls/ntdll/unix/loadorder.c \
    Proton/wine/dlls/ntdll/unix/process.c \
    Proton/wine/dlls/ntdll/unix/registry.c \
    Proton/wine/dlls/ntdll/unix/security.c \
    Proton/wine/dlls/ntdll/unix/serial.c \
    Proton/wine/dlls/ntdll/unix/server.c \
    Proton/wine/dlls/ntdll/unix/signal_arm.c \
    Proton/wine/dlls/ntdll/unix/signal_arm64.c \
    Proton/wine/dlls/ntdll/unix/signal_i386.c \
    Proton/wine/dlls/ntdll/unix/signal_x86_64.c \
    Proton/wine/dlls/ntdll/unix/socket.c \
    Proton/wine/dlls/ntdll/unix/sync.c \
    Proton/wine/dlls/ntdll/unix/system.c \
    Proton/wine/dlls/ntdll/unix/tape.c \
    Proton/wine/dlls/ntdll/unix/thread.c \
    Proton/wine/dlls/ntdll/unix/virtual.c
do
    echo "== $f"
    $cc $flags -c -o "/tmp/$(basename "$f").o" "/work/$f"
done
'
