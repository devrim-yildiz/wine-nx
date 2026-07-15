cmake_minimum_required(VERSION 3.18)

project(wine_nx_probe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(wine-horizon OBJECT
    ../dlls/ntdll/unix/horizon.c
)

target_compile_definitions(wine-horizon PRIVATE
    __SWITCH__
    HORIZON_STANDALONE_SYNTAX
    HORIZON_NO_LIBNX_EXCEPTION_HANDLER
)

target_compile_options(wine-horizon PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
)

target_include_directories(wine-horizon PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/syntax"
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/ntdll/unix"
)

set(WINE_SWITCH_NTDLL_DEFS
    __SWITCH__
    __WINESRC__
    WINE_UNIX_LIB
    _WIN64
    _NTSYSTEM_
    _ACRTIMP=
    WINBASEAPI=
    WINE_NO_TRACE_MSGS
    WINE_NO_DEBUG_MSGS
)

set(WINE_SWITCH_NTDLL_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/syntax"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/ntdll"
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/ntdll/unix"
)

add_library(wine-horizon-real OBJECT
    ../dlls/ntdll/unix/horizon.c
)

target_compile_definitions(wine-horizon-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-horizon-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-horizon-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-server-real OBJECT
    ../dlls/ntdll/unix/server.c
)

target_compile_definitions(wine-server-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-server-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-server-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-sync-real OBJECT
    ../dlls/ntdll/unix/sync.c
    ../dlls/ntdll/unix/fsync.c
)

target_compile_definitions(wine-sync-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-sync-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-sync-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-file-real OBJECT
    ../dlls/ntdll/unix/file.c
)

target_compile_definitions(wine-file-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-file-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-file-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-thread-real OBJECT
    ../dlls/ntdll/unix/thread.c
)

target_compile_definitions(wine-thread-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-thread-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-thread-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-virtual-real OBJECT
    ../dlls/ntdll/unix/virtual.c
)

target_compile_definitions(wine-virtual-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-virtual-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-virtual-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-signal-arm64-real OBJECT
    ../dlls/ntdll/unix/signal_arm64.c
)

target_compile_definitions(wine-signal-arm64-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-signal-arm64-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-signal-arm64-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-env-real OBJECT
    ../dlls/ntdll/unix/env.c
)

target_compile_definitions(wine-env-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-env-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-env-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-debug-real OBJECT
    ../dlls/ntdll/unix/debug.c
)

target_compile_definitions(wine-debug-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-debug-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-debug-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-system-real OBJECT
    ../dlls/ntdll/unix/system.c
)

target_compile_definitions(wine-system-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-system-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-system-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-loadorder-real OBJECT
    ../dlls/ntdll/unix/loadorder.c
)

target_compile_definitions(wine-loadorder-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-loadorder-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-loadorder-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-loader-unix-real OBJECT
    ../dlls/ntdll/unix/loader.c
)

target_compile_definitions(wine-loader-unix-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-loader-unix-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-loader-unix-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

add_library(wine-ntdll-loader-real OBJECT
    ../dlls/ntdll/loader.c
)

target_compile_definitions(wine-ntdll-loader-real PRIVATE
    __SWITCH__
    __WINESRC__
    WINE_NX_PE_LOADER
    _WIN64
    _NTSYSTEM_
    _ACRTIMP=
    WINBASEAPI=
    WINE_NO_TRACE_MSGS
    WINE_NO_DEBUG_MSGS
    HORIZON_NO_LIBNX_EXCEPTION_HANDLER
)

target_compile_options(wine-ntdll-loader-real PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
    -fshort-wchar
    -include
    "${CMAKE_CURRENT_SOURCE_DIR}/source/ntdll_loader_compat.h"
)

target_include_directories(wine-ntdll-loader-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

foreach(WINE_SWITCH_EXTRA_UNIX_MODULE IN ITEMS registry security process socket serial cdrom tape)
    add_library(wine-${WINE_SWITCH_EXTRA_UNIX_MODULE}-real OBJECT
        ../dlls/ntdll/unix/${WINE_SWITCH_EXTRA_UNIX_MODULE}.c
    )

    target_compile_definitions(wine-${WINE_SWITCH_EXTRA_UNIX_MODULE}-real PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

    target_compile_options(wine-${WINE_SWITCH_EXTRA_UNIX_MODULE}-real PRIVATE
        -std=gnu99
        -Wall
        -Wextra
        -Werror=implicit-function-declaration
        -ffunction-sections
        -fdata-sections
    )

    target_include_directories(wine-${WINE_SWITCH_EXTRA_UNIX_MODULE}-real PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})
endforeach()

# win32u unix side: GDI/USER kernel implementation + syscall table (KeServiceDescriptorTable[1]).
# This is what NtUser*/NtGdi* syscalls dispatch into; required for any GUI app.
set(WINE_SWITCH_WIN32U_INCLUDES
    ${WINE_SWITCH_NTDLL_INCLUDES}
    # <dlfcn.h> shim: Horizon has no dynamic linker; dlopen() returns NULL so
    # opengl/vulkan/freetype fall back to "no host driver". Must precede any
    # toolchain dlfcn.h (there is none on devkitA64, but keep it first).
    "${CMAKE_CURRENT_SOURCE_DIR}/switch-shims"
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/win32u"
    # widl-generated COM headers (objidlbase.h etc.) produced by the PE build;
    # last so checked-in include/ and syntax/config.h still win.
    "${CMAKE_CURRENT_SOURCE_DIR}/build-wine-arm64-pe-clean/include"
    "$ENV{DEVKITPRO}/portlibs/switch/include/freetype2"
)

add_library(wine-win32u-real OBJECT
    ../dlls/win32u/bitblt.c
    ../dlls/win32u/bitmap.c
    ../dlls/win32u/brush.c
    ../dlls/win32u/class.c
    ../dlls/win32u/clipboard.c
    ../dlls/win32u/clipping.c
    ../dlls/win32u/cursoricon.c
    ../dlls/win32u/d3dkmt_switch_stub.c
    ../dlls/win32u/dc.c
    ../dlls/win32u/dce.c
    ../dlls/win32u/defwnd.c
    ../dlls/win32u/dib.c
    ../dlls/win32u/driver.c
    ../dlls/win32u/emfdrv.c
    ../dlls/win32u/font.c
    ../dlls/win32u/freetype.c
    ../dlls/win32u/gdiobj.c
    ../dlls/win32u/hook.c
    ../dlls/win32u/imm.c
    ../dlls/win32u/input.c
    ../dlls/win32u/mapping.c
    ../dlls/win32u/menu.c
    ../dlls/win32u/message.c
    ../dlls/win32u/opengl.c
    ../dlls/win32u/opentype.c
    ../dlls/win32u/painting.c
    ../dlls/win32u/palette.c
    ../dlls/win32u/path.c
    ../dlls/win32u/pen.c
    ../dlls/win32u/printdrv.c
    ../dlls/win32u/rawinput.c
    ../dlls/win32u/region.c
    ../dlls/win32u/scroll.c
    ../dlls/win32u/spy.c
    ../dlls/win32u/syscall.c
    ../dlls/win32u/sysparams.c
    ../dlls/win32u/systray.c
    ../dlls/win32u/vertical.c
    ../dlls/win32u/vulkan.c
    ../dlls/win32u/window.c
    ../dlls/win32u/winstation.c
    ../dlls/win32u/winnx_drv.c
    # dibdrv: the software DIB rasterizer — the actual pixel engine that draws
    # into memory bitmaps / window surfaces. Core of the software window path.
    ../dlls/win32u/dibdrv/bitblt.c
    ../dlls/win32u/dibdrv/dc.c
    ../dlls/win32u/dibdrv/graphics.c
    ../dlls/win32u/dibdrv/objects.c
    ../dlls/win32u/dibdrv/primitives.c
)

target_compile_definitions(wine-win32u-real PRIVATE
    ${WINE_SWITCH_NTDLL_DEFS}
    HAVE_FT2BUILD_H=1
    SONAME_LIBFREETYPE="libfreetype.a"
)

target_compile_options(wine-win32u-real PRIVATE
    -std=gnu11
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-win32u-real PRIVATE ${WINE_SWITCH_WIN32U_INCLUDES})

add_executable(wine-nx-probe
    source/main.cpp
    $<TARGET_OBJECTS:wine-horizon>
)

set_target_properties(wine-nx-probe PROPERTIES SUFFIX ".elf")

target_compile_options(wine-nx-probe PRIVATE
    -Wall
    -Wextra
    -fno-exceptions
    -fno-rtti
)

target_include_directories(wine-nx-probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/ntdll/unix"
)

target_link_libraries(wine-nx-probe PRIVATE nx)

add_executable(wine-nx-pe-smoke
    source/pe_smoke.c
    $<TARGET_OBJECTS:wine-horizon>
)

set_target_properties(wine-nx-pe-smoke PROPERTIES SUFFIX ".elf")

target_compile_options(wine-nx-pe-smoke PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
)

target_include_directories(wine-nx-pe-smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/ntdll/unix"
)

target_link_libraries(wine-nx-pe-smoke PRIVATE nx)

add_executable(wine-nx-pe-real-report
    source/pe_real_report.c
    $<TARGET_OBJECTS:wine-horizon>
)

set_target_properties(wine-nx-pe-real-report PROPERTIES SUFFIX ".elf")

target_compile_options(wine-nx-pe-real-report PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
)

target_include_directories(wine-nx-pe-real-report PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/ntdll/unix"
)

target_link_libraries(wine-nx-pe-real-report PRIVATE nx)

add_executable(wine-nx-pe-real-run
    source/pe_real_report.c
    $<TARGET_OBJECTS:wine-horizon>
)

set_target_properties(wine-nx-pe-real-run PROPERTIES SUFFIX ".elf")

target_compile_definitions(wine-nx-pe-real-run PRIVATE PE_REAL_EXECUTE=1)

target_compile_options(wine-nx-pe-real-run PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
)

target_include_directories(wine-nx-pe-real-run PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/ntdll/unix"
)

target_link_libraries(wine-nx-pe-real-run PRIVATE nx)

add_executable(wine-nx-pe-ntdll-smoke
    source/pe_ntdll_smoke.c
    source/ntdll_server_smoke_stubs.c
    $<TARGET_OBJECTS:wine-horizon-real>
    $<TARGET_OBJECTS:wine-server-real>
    $<TARGET_OBJECTS:wine-sync-real>
)

set_target_properties(wine-nx-pe-ntdll-smoke PROPERTIES SUFFIX ".elf")

target_compile_definitions(wine-nx-pe-ntdll-smoke PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-nx-pe-ntdll-smoke PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-nx-pe-ntdll-smoke PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

target_link_options(wine-nx-pe-ntdll-smoke PRIVATE -Wl,--gc-sections)

target_link_libraries(wine-nx-pe-ntdll-smoke PRIVATE nx)

add_executable(wine-nx-ntdll-smoke
    source/ntdll_server_smoke.c
    source/ntdll_server_smoke_stubs.c
    $<TARGET_OBJECTS:wine-horizon-real>
    $<TARGET_OBJECTS:wine-server-real>
)

set_target_properties(wine-nx-ntdll-smoke PROPERTIES SUFFIX ".elf")

target_compile_definitions(wine-nx-ntdll-smoke PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-nx-ntdll-smoke PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-nx-ntdll-smoke PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

target_link_options(wine-nx-ntdll-smoke PRIVATE -Wl,--gc-sections)

target_link_libraries(wine-nx-ntdll-smoke PRIVATE nx)

add_executable(wine-nx-ntdll-file-smoke
    source/ntdll_file_smoke.c
    $<TARGET_OBJECTS:wine-horizon-real>
    $<TARGET_OBJECTS:wine-server-real>
    $<TARGET_OBJECTS:wine-sync-real>
    $<TARGET_OBJECTS:wine-file-real>
    $<TARGET_OBJECTS:wine-thread-real>
    $<TARGET_OBJECTS:wine-virtual-real>
    $<TARGET_OBJECTS:wine-signal-arm64-real>
    $<TARGET_OBJECTS:wine-env-real>
    $<TARGET_OBJECTS:wine-debug-real>
    $<TARGET_OBJECTS:wine-system-real>
)

set_target_properties(wine-nx-ntdll-file-smoke PROPERTIES SUFFIX ".elf")

target_compile_definitions(wine-nx-ntdll-file-smoke PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-nx-ntdll-file-smoke PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-nx-ntdll-file-smoke PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

target_link_options(wine-nx-ntdll-file-smoke PRIVATE -Wl,--gc-sections)

target_link_libraries(wine-nx-ntdll-file-smoke PRIVATE nx)

add_executable(wine-nx-deko3d-smoke
    source/deko3d_smoke.c
)

set_target_properties(wine-nx-deko3d-smoke PROPERTIES SUFFIX ".elf")

target_compile_options(wine-nx-deko3d-smoke PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_link_options(wine-nx-deko3d-smoke PRIVATE -Wl,--gc-sections)

target_link_libraries(wine-nx-deko3d-smoke PRIVATE deko3d nx m)

add_executable(wine-nx-runtime
    source/runtime.c
    source/runtime_deko3d.c
    source/runtime_platform.c
    source/ntdll_pe_compat.c
    source/ws2_32_unix_stub.c
    $<TARGET_OBJECTS:wine-horizon-real>
    $<TARGET_OBJECTS:wine-server-real>
    $<TARGET_OBJECTS:wine-sync-real>
    $<TARGET_OBJECTS:wine-file-real>
    $<TARGET_OBJECTS:wine-thread-real>
    $<TARGET_OBJECTS:wine-virtual-real>
    $<TARGET_OBJECTS:wine-signal-arm64-real>
    $<TARGET_OBJECTS:wine-env-real>
    $<TARGET_OBJECTS:wine-debug-real>
    $<TARGET_OBJECTS:wine-system-real>
    $<TARGET_OBJECTS:wine-loadorder-real>
    $<TARGET_OBJECTS:wine-loader-unix-real>
    $<TARGET_OBJECTS:wine-ntdll-loader-real>
    $<TARGET_OBJECTS:wine-registry-real>
    $<TARGET_OBJECTS:wine-security-real>
    $<TARGET_OBJECTS:wine-process-real>
    $<TARGET_OBJECTS:wine-socket-real>
    $<TARGET_OBJECTS:wine-serial-real>
    $<TARGET_OBJECTS:wine-cdrom-real>
    $<TARGET_OBJECTS:wine-tape-real>
    $<TARGET_OBJECTS:wine-win32u-real>
)

set_target_properties(wine-nx-runtime PROPERTIES SUFFIX ".elf")

target_compile_definitions(wine-nx-runtime PRIVATE ${WINE_SWITCH_NTDLL_DEFS})

target_compile_options(wine-nx-runtime PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-nx-runtime PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

target_link_options(wine-nx-runtime PRIVATE -Wl,--gc-sections)

# freetype (+ its deps) for win32u font rendering; nx last for the libnx syscalls
find_library(SWITCH_FREETYPE freetype HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
find_library(SWITCH_HARFBUZZ harfbuzz HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
find_library(SWITCH_PNG png HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
find_library(SWITCH_BZ2 bz2 HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
find_library(SWITCH_Z z HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
target_link_libraries(wine-nx-runtime PRIVATE
    ${SWITCH_FREETYPE} ${SWITCH_PNG} ${SWITCH_BZ2} ${SWITCH_Z} ${SWITCH_HARFBUZZ} deko3d nx m)

# Deko3d-only runtime: a genuinely separate binary, not a runtime flag inside
# wine-nx-runtime, specifically so it never references nwindowGetDefault() or
# anything (consoleInit() included -- see runtime.c's WINE_NX_DEKO3D_ONLY
# comment) that pulls in libnx's default_window.c. That reference is what
# makes libnx's __appInit() run __nx_win_init() before main(), unconditionally
# opening a vi display before deko3d ever gets a chance to be the first
# consumer -- see wine-nx-probe/3d-accel-scoping.md. Compiles source/runtime.c
# a second time with WINE_NX_DEKO3D_ONLY defined, which compiles out every
# libnx-framebuffer/console code path entirely (not just branches around it at
# runtime -- the symbol references themselves must not exist in this binary).
# wine-nx-runtime itself is untouched: its own compilation of runtime.c never
# defines this macro, so its behavior is byte-for-byte identical to before.
add_executable(wine-nx-runtime-deko3d
    source/runtime.c
    source/runtime_deko3d.c
    source/runtime_platform.c
    source/ntdll_pe_compat.c
    source/ws2_32_unix_stub.c
    $<TARGET_OBJECTS:wine-horizon-real>
    $<TARGET_OBJECTS:wine-server-real>
    $<TARGET_OBJECTS:wine-sync-real>
    $<TARGET_OBJECTS:wine-file-real>
    $<TARGET_OBJECTS:wine-thread-real>
    $<TARGET_OBJECTS:wine-virtual-real>
    $<TARGET_OBJECTS:wine-signal-arm64-real>
    $<TARGET_OBJECTS:wine-env-real>
    $<TARGET_OBJECTS:wine-debug-real>
    $<TARGET_OBJECTS:wine-system-real>
    $<TARGET_OBJECTS:wine-loadorder-real>
    $<TARGET_OBJECTS:wine-loader-unix-real>
    $<TARGET_OBJECTS:wine-ntdll-loader-real>
    $<TARGET_OBJECTS:wine-registry-real>
    $<TARGET_OBJECTS:wine-security-real>
    $<TARGET_OBJECTS:wine-process-real>
    $<TARGET_OBJECTS:wine-socket-real>
    $<TARGET_OBJECTS:wine-serial-real>
    $<TARGET_OBJECTS:wine-cdrom-real>
    $<TARGET_OBJECTS:wine-tape-real>
    $<TARGET_OBJECTS:wine-win32u-real>
)

set_target_properties(wine-nx-runtime-deko3d PROPERTIES SUFFIX ".elf")

target_compile_definitions(wine-nx-runtime-deko3d PRIVATE ${WINE_SWITCH_NTDLL_DEFS} WINE_NX_DEKO3D_ONLY=1)

target_compile_options(wine-nx-runtime-deko3d PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-nx-runtime-deko3d PRIVATE ${WINE_SWITCH_NTDLL_INCLUDES})

# --gc-sections is not just an optimization here: with -ffunction-sections it's
# also what actually drops any never-called libnx console/framebuffer code
# that might otherwise still end up in the final binary from other object
# files pulled in for unrelated symbols. It does not, by itself, prevent
# __appInit() from invoking __nx_win_init() -- that trigger is a symbol
# *reference*, resolved at link time before gc-sections prunes anything -- so
# it's this binary's lack of any nwindowGetDefault()/consoleInit() call at
# all (see runtime.c) that matters, not this flag.
target_link_options(wine-nx-runtime-deko3d PRIVATE -Wl,--gc-sections)

target_link_libraries(wine-nx-runtime-deko3d PRIVATE
    ${SWITCH_FREETYPE} ${SWITCH_PNG} ${SWITCH_BZ2} ${SWITCH_Z} ${SWITCH_HARFBUZZ} deko3d nx m)

find_program(NACPTOOL nacptool HINTS "$ENV{DEVKITPRO}/tools/bin" "/opt/devkitpro/tools/bin")
find_program(ELF2NRO elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin" "/opt/devkitpro/tools/bin")
find_program(UAM uam HINTS "$ENV{DEVKITPRO}/tools/bin" "/opt/devkitpro/tools/bin")

# deko3d smoke test's stage-3 cube needs two compiled shaders in a romfs
# image (dksh files loaded at runtime via fopen("romfs:/shaders/...")).
# uam invocation (-s vert/frag -o out.dksh in.glsl) matches the pattern
# devkitPro's own example Makefiles use (see deko_basic/Makefile).
if(UAM)
    set(DEKO3D_SMOKE_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/deko3d-smoke-romfs")
    set(DEKO3D_SMOKE_SHADERS_DIR "${DEKO3D_SMOKE_ROMFS_DIR}/shaders")
    set(DEKO3D_VSH_DKSH "${DEKO3D_SMOKE_SHADERS_DIR}/transform_vsh.dksh")
    set(DEKO3D_FSH_DKSH "${DEKO3D_SMOKE_SHADERS_DIR}/color_fsh.dksh")

    add_custom_command(
        OUTPUT "${DEKO3D_VSH_DKSH}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEKO3D_SMOKE_SHADERS_DIR}"
        COMMAND "${UAM}" -s vert -o "${DEKO3D_VSH_DKSH}" "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders/transform_vsh.glsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders/transform_vsh.glsl"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${DEKO3D_FSH_DKSH}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEKO3D_SMOKE_SHADERS_DIR}"
        COMMAND "${UAM}" -s frag -o "${DEKO3D_FSH_DKSH}" "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders/color_fsh.glsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders/color_fsh.glsl"
        VERBATIM
    )

    add_custom_target(wine-nx-deko3d-smoke-shaders DEPENDS "${DEKO3D_VSH_DKSH}" "${DEKO3D_FSH_DKSH}")
endif()

if(NACPTOOL AND ELF2NRO)
    set(NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-probe.nacp")
    set(NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-probe.nro")
    set(PE_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-smoke.nacp")
    set(PE_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-smoke.nro")
    set(PE_REAL_REPORT_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-real-report.nacp")
    set(PE_REAL_REPORT_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-real-report.nro")
    set(PE_REAL_RUN_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-real-run.nacp")
    set(PE_REAL_RUN_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-real-run.nro")
    set(PE_NTDLL_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-ntdll-smoke.nacp")
    set(PE_NTDLL_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-pe-ntdll-smoke.nro")
    set(NTDLL_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-ntdll-smoke.nacp")
    set(NTDLL_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-ntdll-smoke.nro")
    set(NTDLL_FILE_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-ntdll-file-smoke.nacp")
    set(NTDLL_FILE_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-ntdll-file-smoke.nro")
    set(DEKO3D_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-deko3d-smoke.nacp")
    set(DEKO3D_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-deko3d-smoke.nro")
    set(RUNTIME_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime.nacp")
    set(RUNTIME_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime.nro")
    set(RUNTIME_DEKO3D_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime-deko3d.nacp")
    set(RUNTIME_DEKO3D_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime-deko3d.nro")

    add_custom_command(
        OUTPUT "${NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX Probe" "Codex" "0.1.0" "${NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-probe>" "${NRO_FILE}" "--nacp=${NACP_FILE}"
        DEPENDS wine-nx-probe "${NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-probe-nro ALL DEPENDS "${NRO_FILE}")

    add_custom_command(
        OUTPUT "${PE_SMOKE_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX PE Smoke" "Codex" "0.1.0" "${PE_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${PE_SMOKE_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-pe-smoke>" "${PE_SMOKE_NRO_FILE}" "--nacp=${PE_SMOKE_NACP_FILE}"
        DEPENDS wine-nx-pe-smoke "${PE_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-pe-smoke-nro ALL DEPENDS "${PE_SMOKE_NRO_FILE}")

    add_custom_command(
        OUTPUT "${PE_REAL_REPORT_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX PE Real Report" "Codex" "0.1.0" "${PE_REAL_REPORT_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${PE_REAL_REPORT_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-pe-real-report>" "${PE_REAL_REPORT_NRO_FILE}" "--nacp=${PE_REAL_REPORT_NACP_FILE}"
        DEPENDS wine-nx-pe-real-report "${PE_REAL_REPORT_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-pe-real-report-nro ALL DEPENDS "${PE_REAL_REPORT_NRO_FILE}")

    add_custom_command(
        OUTPUT "${PE_REAL_RUN_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX PE Real Run" "Codex" "0.1.0" "${PE_REAL_RUN_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${PE_REAL_RUN_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-pe-real-run>" "${PE_REAL_RUN_NRO_FILE}" "--nacp=${PE_REAL_RUN_NACP_FILE}"
        DEPENDS wine-nx-pe-real-run "${PE_REAL_RUN_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-pe-real-run-nro ALL DEPENDS "${PE_REAL_RUN_NRO_FILE}")

    add_custom_command(
        OUTPUT "${PE_NTDLL_SMOKE_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX PE NTDLL Smoke" "Codex" "0.1.0" "${PE_NTDLL_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${PE_NTDLL_SMOKE_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-pe-ntdll-smoke>" "${PE_NTDLL_SMOKE_NRO_FILE}" "--nacp=${PE_NTDLL_SMOKE_NACP_FILE}"
        DEPENDS wine-nx-pe-ntdll-smoke "${PE_NTDLL_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-pe-ntdll-smoke-nro ALL DEPENDS "${PE_NTDLL_SMOKE_NRO_FILE}")

    add_custom_command(
        OUTPUT "${NTDLL_SMOKE_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX NTDLL Smoke" "Codex" "0.1.0" "${NTDLL_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${NTDLL_SMOKE_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-ntdll-smoke>" "${NTDLL_SMOKE_NRO_FILE}" "--nacp=${NTDLL_SMOKE_NACP_FILE}"
        DEPENDS wine-nx-ntdll-smoke "${NTDLL_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-ntdll-smoke-nro ALL DEPENDS "${NTDLL_SMOKE_NRO_FILE}")

    add_custom_command(
        OUTPUT "${NTDLL_FILE_SMOKE_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX NTDLL File Smoke" "Codex" "0.1.0" "${NTDLL_FILE_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${NTDLL_FILE_SMOKE_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-ntdll-file-smoke>" "${NTDLL_FILE_SMOKE_NRO_FILE}" "--nacp=${NTDLL_FILE_SMOKE_NACP_FILE}"
        DEPENDS wine-nx-ntdll-file-smoke "${NTDLL_FILE_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-ntdll-file-smoke-nro ALL DEPENDS "${NTDLL_FILE_SMOKE_NRO_FILE}")

    add_custom_command(
        OUTPUT "${DEKO3D_SMOKE_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX deko3d Smoke" "Codex" "0.1.0" "${DEKO3D_SMOKE_NACP_FILE}"
        VERBATIM
    )

    set(DEKO3D_SMOKE_ROMFS_ARG "")
    set(DEKO3D_SMOKE_ROMFS_DEPS "")
    if(UAM)
        set(DEKO3D_SMOKE_ROMFS_ARG "--romfsdir=${DEKO3D_SMOKE_ROMFS_DIR}")
        set(DEKO3D_SMOKE_ROMFS_DEPS wine-nx-deko3d-smoke-shaders)
    endif()

    add_custom_command(
        OUTPUT "${DEKO3D_SMOKE_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-deko3d-smoke>" "${DEKO3D_SMOKE_NRO_FILE}" "--nacp=${DEKO3D_SMOKE_NACP_FILE}" ${DEKO3D_SMOKE_ROMFS_ARG}
        DEPENDS wine-nx-deko3d-smoke "${DEKO3D_SMOKE_NACP_FILE}" ${DEKO3D_SMOKE_ROMFS_DEPS}
        VERBATIM
    )

    add_custom_target(wine-nx-deko3d-smoke-nro ALL DEPENDS "${DEKO3D_SMOKE_NRO_FILE}")

    add_custom_command(
        OUTPUT "${RUNTIME_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX Runtime" "Codex" "0.1.0" "${RUNTIME_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${RUNTIME_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-runtime>" "${RUNTIME_NRO_FILE}" "--nacp=${RUNTIME_NACP_FILE}"
        DEPENDS wine-nx-runtime "${RUNTIME_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-runtime-nro ALL DEPENDS "${RUNTIME_NRO_FILE}")

    add_custom_command(
        OUTPUT "${RUNTIME_DEKO3D_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX Runtime (deko3d)" "Codex" "0.1.0" "${RUNTIME_DEKO3D_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${RUNTIME_DEKO3D_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-runtime-deko3d>" "${RUNTIME_DEKO3D_NRO_FILE}" "--nacp=${RUNTIME_DEKO3D_NACP_FILE}"
        DEPENDS wine-nx-runtime-deko3d "${RUNTIME_DEKO3D_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-runtime-deko3d-nro ALL DEPENDS "${RUNTIME_DEKO3D_NRO_FILE}")
endif()
