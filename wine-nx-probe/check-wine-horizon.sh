#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

docker run --rm \
    -v "$ROOT:/work" \
    --workdir /work \
    devkitpro/devkita64 \
    sh -lc '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc \
        -std=gnu99 \
        -D__SWITCH__ \
        -DHORIZON_STANDALONE_SYNTAX \
        -Werror=implicit-function-declaration \
        -I/work/wine-nx-probe/syntax \
        -I/work/Proton/wine/dlls/ntdll/unix \
        -I/opt/devkitpro/libnx/include \
        -c \
        -o /tmp/horizon.o \
        /work/Proton/wine/dlls/ntdll/unix/horizon.c'
