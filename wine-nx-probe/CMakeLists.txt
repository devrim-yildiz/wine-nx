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

# Optional NVK Vulkan driver (wine-nx-probe/third_party/nxvk) -- linked in
# only when its static archives have already been built (see
# wine-nx-probe/tools/build-nxvk-smoke.sh) and the archive path was passed
# in via -DNXVK_DIR=... (build-switch.sh does this automatically once it
# detects a built nxvk). Without it, wine-nx-runtime builds exactly as
# before this feature existed -- Vulkan support is purely additive, same
# opt-in philosophy as WINE_NX_DEKO3D.
set(NXVK_DIR "" CACHE PATH "Path to the vendored nxvk submodule, if its Switch archives have been built")
set(WINE_NX_HAVE_VULKAN FALSE)
if(NXVK_DIR)
    set(NXVK_CROSS_DIR "${NXVK_DIR}/switch/build/cross")
    set(NXVK_LIBNVK "${NXVK_CROSS_DIR}/src/nouveau/vulkan/libnvk.a")
    if(EXISTS "${NXVK_LIBNVK}")
        set(WINE_NX_HAVE_VULKAN TRUE)
        # Support archives libnvk.a pulls in, in dependency order -- mirrors
        # third_party/nxvk/switch/build/build-nro.sh's own ARCHIVES list
        # exactly, so this stays in sync with nxvk's own proven-working link
        # line rather than re-deriving it.
        set(NXVK_ARCHIVES
            "${NXVK_CROSS_DIR}/src/util/libmesa_util.a"
            "${NXVK_CROSS_DIR}/src/util/libmesa_util_simd.a"
            "${NXVK_CROSS_DIR}/src/util/blake3/libblake3.a"
            "${NXVK_CROSS_DIR}/src/c11/impl/libmesa_util_c11.a"
            "${NXVK_CROSS_DIR}/src/nouveau/compiler/libnak.a"
            "${NXVK_CROSS_DIR}/src/nouveau/compiler/libnak_rs.a"
            "${NXVK_CROSS_DIR}/src/compiler/rust/libcompiler_c_helpers.a"
            "${NXVK_CROSS_DIR}/src/nouveau/headers/libnvidia_headers_c.a"
            "${NXVK_CROSS_DIR}/src/nouveau/nil/libnil.a"
            "${NXVK_CROSS_DIR}/src/nouveau/nil/liblibnil_format_table.a"
            "${NXVK_CROSS_DIR}/src/compiler/nir/libnir.a"
            "${NXVK_CROSS_DIR}/src/compiler/libcompiler.a"
            "${NXVK_CROSS_DIR}/src/nouveau/mme/libnouveau_mme.a"
            "${NXVK_CROSS_DIR}/src/nouveau/winsys/libnouveau_ws.a"
            "${NXVK_CROSS_DIR}/src/vulkan/util/libvulkan_util.a"
            "${NXVK_CROSS_DIR}/src/compiler/spirv/libvtn.a"
            "${NXVK_CROSS_DIR}/src/util/libxmlconfig.a"
        )
        # Switch-target-cross-compiled SPIRV-Tools, built by nxvk's own
        # Dockerfile into the toolchain image's portlibs dir -- libvtn.a's
        # debug SPIR-V disassembly path needs its C API. Only present inside
        # the nvk-switch-build image, not plain devkitpro/devkita64; see
        # build-switch.sh's image selection.
        find_library(NXVK_SPIRV_TOOLS SPIRV-Tools HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
        message(STATUS "NVK Vulkan driver found at ${NXVK_LIBNVK} -- wine-nx-runtime will link it in")
    else()
        message(STATUS "NXVK_DIR set but ${NXVK_LIBNVK} not found -- building wine-nx-runtime without Vulkan support")
    endif()
endif()
if(WINE_NX_HAVE_VULKAN)
    target_sources(wine-win32u-real PRIVATE ../dlls/win32u/winnx_vulkan.c)
    target_compile_definitions(wine-win32u-real PRIVATE WINE_NX_HAVE_VULKAN)

    # winevulkan's own native ("unix side") implementation -- separate from
    # dlls/win32u/vulkan.c (the driver-loading glue patched above).
    # winevulkan.dll (PE, both aarch64-windows and i386-windows builds) is
    # its own unixlib module and needs its own __wine_init_unix_call()
    # target compiled in, exactly like win32u.dll's; see the
    # wine_nx_static_unix_libs table in dlls/ntdll/unix/virtual.c. vulkan.c
    # is hand-written; vulkan_thunks.c is real, unmodified Wine codegen
    # output (per-Vulkan-function marshaling, both 64-bit and WOW64/32-bit
    # variants from one source file, selected by _WIN64 at compile time).
    target_sources(wine-win32u-real PRIVATE
        ../dlls/winevulkan/vulkan.c
        ../dlls/winevulkan/vulkan_thunks.c)
    target_include_directories(wine-win32u-real PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/winevulkan")

    # virtual.c's wine_nx_static_unix_libs table (dlls/ntdll/unix/virtual.c,
    # a DIFFERENT object library) also needs to know Vulkan is linked in, to
    # reference wine_nx_winevulkan_unix_funcs/_wow64_funcs.
    target_compile_definitions(wine-virtual-real PRIVATE WINE_NX_HAVE_VULKAN)
endif()

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

# Standalone libnx JIT (W^X-bypass) smoke test -- see source/jit_smoke.c's
# header comment. Zero Wine dependency by design, unlike the ntdll-smoke
# targets above; only links libnx.
add_executable(wine-nx-jit-smoke
    source/jit_smoke.c
)

set_target_properties(wine-nx-jit-smoke PROPERTIES SUFFIX ".elf")

target_compile_options(wine-nx-jit-smoke PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_link_options(wine-nx-jit-smoke PRIVATE -Wl,--gc-sections)

target_link_libraries(wine-nx-jit-smoke PRIVATE nx)

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
find_library(SWITCH_EXPAT expat HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
target_link_libraries(wine-nx-runtime PRIVATE
    ${SWITCH_FREETYPE} ${SWITCH_PNG} ${SWITCH_BZ2} ${SWITCH_Z} ${SWITCH_HARFBUZZ} deko3d nx m)

# Links NVK's archives into a wine-nx-runtime* target. A function (not
# inlined at each call site) because wine-win32u-real is an OBJECT library
# -- $<TARGET_OBJECTS:wine-win32u-real> pulls its .o files into whichever
# executable references it, but plain object-library usage does NOT
# propagate that library's own target_link_libraries/target_sources
# requirements the way a real STATIC/SHARED library target would, so every
# executable consuming wine-win32u-real's objects (winnx_vulkan.c among
# them, once WINE_NX_HAVE_VULKAN is on) needs this applied to it directly.
function(wine_nx_link_vulkan target)
    if(NOT WINE_NX_HAVE_VULKAN)
        return()
    endif()
    # Mesa/NVK expects a handful of ordinary POSIX functions
    # (posix_memalign, getuid/geteuid/getgid/getegid, regcomp/regexec/
    # regfree, a CSRNG-backed getrandom) that devkitA64's newlib doesn't
    # provide and nothing in wine-nx-runtime needed before now. Reuse
    # nxvk's own gap-fill file instead of duplicating it -- see
    # third_party/nxvk/switch/smoke/nvk_compat.c's header comment. Safe to
    # add unconditionally: none of these symbols exist anywhere else in
    # wine-nx-runtime today (confirmed by grep), so there's nothing to
    # collide with.
    target_sources(${target} PRIVATE third_party/nxvk/switch/smoke/nvk_compat.c)
    target_include_directories(${target} PRIVATE "${NXVK_DIR}/include")

    # libnvk.a must be whole-archived (matches
    # third_party/nxvk/switch/build/build-nro.sh's own link line exactly);
    # the rest go in a start-group since several of these Mesa/NAK/NIR
    # archives reference each other circularly. m/z/expat are repeated
    # here, not just relied on from this target's own earlier
    # target_link_libraries call -- that call is earlier in final link
    # order, so by the time the linker reaches these NVK archives it has
    # already passed those libraries and won't revisit them for
    # powf/log2f/exp2f/XML_ParserCreate/etc without this.
    target_link_options(${target} PRIVATE -Wl,-u,vk_icdGetInstanceProcAddr)
    target_link_libraries(${target} PRIVATE
        -Wl,--whole-archive "${NXVK_LIBNVK}" -Wl,--no-whole-archive
        -Wl,--start-group ${NXVK_ARCHIVES} ${NXVK_SPIRV_TOOLS} ${SWITCH_Z} ${SWITCH_EXPAT} m -Wl,--end-group
        # This target's own sources are all C, so CMake picks the C linker
        # driver (gcc) for the final link, which -- unlike g++ -- doesn't
        # auto-link the C++ runtime. libSPIRV-Tools.a (C++, pulled in
        # transitively by libvtn.a's debug SPIR-V path) needs it
        # explicitly: operator new/delete, std::string, std::__throw_*.
        stdc++)
endfunction()

wine_nx_link_vulkan(wine-nx-runtime)

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

wine_nx_link_vulkan(wine-nx-runtime-deko3d)

# --- Launcher: the single "run this from hbmenu" entry point ---------------
# See source/launcher_main.c's own header comment and source/gui/ for the
# full design (Cemu-style game library: touch+joycon, per-game backend/
# bitness overrides, a debug overlay, auto-test mode). Deliberately its own
# small binary with no deko3d/Vulkan/box64 linked in at all -- it never
# references nwindowGetDefault() for any GPU-backed purpose, only the same
# plain libnx Framebuffer path wine-nx-runtime.nro itself falls back to, so
# it can't trip the pre-main() __nx_win_init() conflict documented above and
# in wine-nx-probe/3d-accel-scoping.md. Picking a backend/game hands off to a
# separate already-built binary (wine-nx-runtime.nro or
# wine-nx-vulkan-runtime.nro) via envSetNextLoad; see resolve_runtime_nro()
# in launcher_main.c.
# wine-nx-launcher is the "single NRO" merge: this project's actual PE
# loader/box64 engine (the exact same sources and $<TARGET_OBJECTS:...>
# object libraries wine-nx-runtime links below, byte-for-byte -- not a
# second, parallel implementation of any of it) compiled into the same
# binary as the GUI. See source/nx_arch.c for the in-process entry point
# that replaces envSetNextLoad+exit for GDI-backend games, and
# source/runtime_boot.h for why "the game closes, you're back at the
# library screen" is NOT what this buys you -- wine_nx_x64_handoff()/
# wine_nx_wow64_handoff() never return once a guest is running
# successfully, merged into one binary or not; when the guest process
# exits, the whole Horizon process exits with it, same as running
# wine-nx-runtime.nro standalone today. Vulkan-backend games still hand
# off to the separate wine-nx-vulkan-runtime.nro exactly as before --
# deliberately not merged, since this binary already references
# nwindowGetDefault() for its own GUI framebuffer (see
# 3d-accel-scoping.md for why that specific combination is the one that
# actually breaks).
add_executable(wine-nx-launcher
    source/launcher_main.c
    source/nx_arch.c
    source/nx_diagnostics.c
    source/runtime.c
    source/runtime_deko3d.c
    source/runtime_platform.c
    source/ntdll_pe_compat.c
    source/ws2_32_unix_stub.c
    source/gui/nxgui.c
    source/gui/nxgui_font.c
    source/gui/nxgui_pe_icon.c
    source/gui/nxgui_png_mini.c
    source/gui/nxinput.c
    source/gui/nxlib.c
    source/gui/nxmenu.c
    source/gui/nxmenu_library.c
    source/gui/nxmenu_detail.c
    source/gui/nxmenu_mod_manager.c
    source/gui/nxmenu_controller.c
    source/gui/nxmenu_quickmenu.c
    source/gui/nx_discord.c
    source/gui/nxtoast.c
    source/gui/nxdebug.c
    source/gui/nxtest.c
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

set_target_properties(wine-nx-launcher PROPERTIES SUFFIX ".elf")

# WINE_NX_MERGED_LAUNCHER suppresses runtime.c's own main() (see that
# file's own #ifndef around it) -- this binary's real entry point is
# launcher_main.c's main(), which owns the GUI's own boot sequence and
# only reaches into runtime.c's boot-and-run path (via nx_arch.c) once
# the user actually picks a game.
target_compile_definitions(wine-nx-launcher PRIVATE ${WINE_SWITCH_NTDLL_DEFS} WINE_NX_MERGED_LAUNCHER)

target_compile_options(wine-nx-launcher PRIVATE
    -std=gnu99
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -ffunction-sections
    -fdata-sections
)

target_include_directories(wine-nx-launcher PRIVATE
    ${WINE_SWITCH_NTDLL_INCLUDES}
    "${CMAKE_CURRENT_SOURCE_DIR}/source"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/gui"
)

target_link_options(wine-nx-launcher PRIVATE -Wl,--gc-sections)

# Same freetype/harfbuzz/png/bz2/z/deko3d/nx/m set wine-nx-runtime links
# below, for the same reason (win32u's font rendering + the loader's own
# needs) -- deko3d itself is linked but, same as wine-nx-runtime's own
# plain (non -deko3d-only) build, never actually initialized by this
# binary; only wine-nx-runtime-deko3d.elf (wrapped as both
# wine-nx-runtime-deko3d.nro and wine-nx-vulkan-runtime.nro) is compiled
# with WINE_NX_DEKO3D_ONLY and calls it. nxgui_png_mini.c also needs
# zlib's inflate for PNG-format icon resources, already covered by the
# same ${SWITCH_Z}.
target_link_libraries(wine-nx-launcher PRIVATE
    ${SWITCH_FREETYPE} ${SWITCH_PNG} ${SWITCH_BZ2} ${SWITCH_Z} ${SWITCH_HARFBUZZ} deko3d nx m)
wine_nx_link_vulkan(wine-nx-launcher)

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
    set(JIT_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-jit-smoke.nacp")
    set(JIT_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-jit-smoke.nro")
    set(NTDLL_FILE_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-ntdll-file-smoke.nacp")
    set(NTDLL_FILE_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-ntdll-file-smoke.nro")
    set(DEKO3D_SMOKE_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-deko3d-smoke.nacp")
    set(DEKO3D_SMOKE_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-deko3d-smoke.nro")
    set(RUNTIME_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime.nacp")
    set(RUNTIME_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime.nro")
    set(RUNTIME_DEKO3D_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime-deko3d.nacp")
    set(RUNTIME_DEKO3D_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-runtime-deko3d.nro")
    set(VULKAN_RUNTIME_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-vulkan-runtime.nacp")
    set(VULKAN_RUNTIME_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-vulkan-runtime.nro")
    set(LAUNCHER_NACP_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-launcher.nacp")
    set(LAUNCHER_NRO_FILE "${CMAKE_CURRENT_BINARY_DIR}/wine-nx-launcher.nro")

    add_custom_command(
        OUTPUT "${NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX Probe" "voric" "0.1.0" "${NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX PE Smoke" "voric" "0.1.0" "${PE_SMOKE_NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX PE Real Report" "voric" "0.1.0" "${PE_REAL_REPORT_NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX PE Real Run" "voric" "0.1.0" "${PE_REAL_RUN_NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX PE NTDLL Smoke" "voric" "0.1.0" "${PE_NTDLL_SMOKE_NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX NTDLL Smoke" "voric" "0.1.0" "${NTDLL_SMOKE_NACP_FILE}"
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
        OUTPUT "${JIT_SMOKE_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX JIT Smoke" "voric" "0.1.0" "${JIT_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${JIT_SMOKE_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-jit-smoke>" "${JIT_SMOKE_NRO_FILE}" "--nacp=${JIT_SMOKE_NACP_FILE}"
        DEPENDS wine-nx-jit-smoke "${JIT_SMOKE_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-jit-smoke-nro ALL DEPENDS "${JIT_SMOKE_NRO_FILE}")

    add_custom_command(
        OUTPUT "${NTDLL_FILE_SMOKE_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX NTDLL File Smoke" "voric" "0.1.0" "${NTDLL_FILE_SMOKE_NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX deko3d Smoke" "voric" "0.1.0" "${DEKO3D_SMOKE_NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX Runtime" "voric" "0.1.0" "${RUNTIME_NACP_FILE}"
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
        COMMAND "${NACPTOOL}" --create "Wine-NX Runtime (deko3d)" "voric" "0.1.0" "${RUNTIME_DEKO3D_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${RUNTIME_DEKO3D_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-runtime-deko3d>" "${RUNTIME_DEKO3D_NRO_FILE}" "--nacp=${RUNTIME_DEKO3D_NACP_FILE}"
        DEPENDS wine-nx-runtime-deko3d "${RUNTIME_DEKO3D_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-runtime-deko3d-nro ALL DEPENDS "${RUNTIME_DEKO3D_NRO_FILE}")

    # "Vulkan Runtime": wraps wine-nx-runtime-deko3d.elf, NOT the plain
    # wine-nx-runtime.elf. Hardware-confirmed why: my own Vulkan surface
    # creation needs viOpenDefaultDisplay() to succeed (a dedicated
    # ViDisplay/ViLayer, same as deko3d's own -- see winnx_vulkan.c), but
    # the plain runtime still references nwindowGetDefault()/consoleInit()
    # for its libnx-framebuffer fallback path, which trips libnx's
    # __nx_win_init() pre-main() bootstrap and claims the default vi
    # session before any user code runs -- the exact same conflict
    # documented for deko3d in wine-nx-probe/3d-accel-scoping.md, and the
    # reason wine-nx-runtime-deko3d (WINE_NX_DEKO3D_ONLY, compiles those
    # references out entirely) exists as a separate binary in the first
    # place. Confirmed on real hardware: with the plain runtime, this
    # Vulkan path got exactly viOpenDefaultDisplay rc=0x1272 (LibnxError_
    # AlreadyInitialized). wine_nx_link_vulkan() already links NVK into
    # wine-nx-runtime-deko3d too (see above), so no new build wiring is
    # needed here -- just pointing elf2nro at the right target.
    add_custom_command(
        OUTPUT "${VULKAN_RUNTIME_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Vulkan Runtime" "voric" "0.1.0" "${VULKAN_RUNTIME_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${VULKAN_RUNTIME_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-runtime-deko3d>" "${VULKAN_RUNTIME_NRO_FILE}" "--nacp=${VULKAN_RUNTIME_NACP_FILE}"
        DEPENDS wine-nx-runtime-deko3d "${VULKAN_RUNTIME_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-vulkan-runtime-nro ALL DEPENDS "${VULKAN_RUNTIME_NRO_FILE}")

    add_custom_command(
        OUTPUT "${LAUNCHER_NACP_FILE}"
        COMMAND "${NACPTOOL}" --create "Wine-NX" "voric" "0.1.0" "${LAUNCHER_NACP_FILE}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${LAUNCHER_NRO_FILE}"
        COMMAND "${ELF2NRO}" "$<TARGET_FILE:wine-nx-launcher>" "${LAUNCHER_NRO_FILE}" "--nacp=${LAUNCHER_NACP_FILE}"
        DEPENDS wine-nx-launcher "${LAUNCHER_NACP_FILE}"
        VERBATIM
    )

    add_custom_target(wine-nx-launcher-nro ALL DEPENDS "${LAUNCHER_NRO_FILE}")
endif()

# --- WowBox64 (Box64's WOW64 CPU-backend build) -----------------------------
# Opt-in, independent of the devkitA64 NRO build above: this drives Box64's
# OWN, complete wine/wow64 CMake project (its "-DWOW64=ON -DARM_DYNAREC=ON"
# target) with our pinned llvm-mingw toolchain, producing wowbox64.dll -- a
# real ARM64 Windows PE DLL. Verified by an actual build on 2026-07-17
# (unmodified upstream Box64 at commit 86f3d6d): "wowbox64.dll: PE32+
# executable (DLL) (GUI) Aarch64, for MS Windows", 4.6MB. This only builds
# the CPU-backend DLL itself -- it does not touch the dual-alias allocator
# (that's the os_horizon.c addition below) or wire it into wine-nx's own
# loader; it exists so that piece can be iterated on independently of the
# devkitA64 toolchain used for the rest of this project.
option(WINE_NX_BUILD_WOWBOX64 "Build wowbox64.dll from the vendored Box64 submodule" OFF)

if(WINE_NX_BUILD_WOWBOX64)
    include(ExternalProject)

    set(WOWBOX64_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64")
    if(NOT EXISTS "${WOWBOX64_SRC_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "WINE_NX_BUILD_WOWBOX64 is ON but ${WOWBOX64_SRC_DIR} is empty. "
            "Run: git submodule update --init wine-nx-probe/third_party/box64")
    endif()

    # Same LLVM_MINGW_DIR convention build-switch.sh already uses for the
    # PE-DLL rebuild path (README.md's --enable-archs=aarch64,i386 section) --
    # reused here rather than inventing a second toolchain variable.
    set(WOWBOX64_LLVM_MINGW_DIR "$ENV{LLVM_MINGW_DIR}"
        CACHE PATH "llvm-mingw root (defaults to the pinned toolchain under wine-nx-probe/toolchains)")
    if(NOT WOWBOX64_LLVM_MINGW_DIR)
        set(WOWBOX64_LLVM_MINGW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/toolchains/llvm-mingw-20260505-ucrt-macos-universal")
    endif()
    set(WOWBOX64_LLVM_MINGW_BIN "${WOWBOX64_LLVM_MINGW_DIR}/bin")
    if(NOT EXISTS "${WOWBOX64_LLVM_MINGW_BIN}/aarch64-w64-mingw32-clang")
        message(FATAL_ERROR
            "WINE_NX_BUILD_WOWBOX64 is ON but no aarch64-w64-mingw32-clang under "
            "${WOWBOX64_LLVM_MINGW_BIN}. Set LLVM_MINGW_DIR or fetch the pinned "
            "toolchain per README.md.")
    endif()

    # ExternalProject_Add configures Box64's REAL root CMakeLists.txt (not a
    # copy) with -DWOW64=ON -DARM_DYNAREC=ON, which is what makes it emit the
    # "wowbox64" ExternalProject target internally -- see CMakeLists.txt:1610
    # in the submodule. That inner target uses Box64's own
    # wine/toolchain_mingw.cmake, which references the mingw binaries by bare
    # name (relying on PATH); we prepend our pinned bin dir for both the
    # configure and build steps so it resolves to the exact toolchain this
    # project already pins, not whatever happens to be on PATH.
    ExternalProject_Add(
        wowbox64_external
        SOURCE_DIR "${WOWBOX64_SRC_DIR}"
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env "PATH=${WOWBOX64_LLVM_MINGW_BIN}:$ENV{PATH}"
            "${CMAKE_COMMAND}" -S "${WOWBOX64_SRC_DIR}" -B "<BINARY_DIR>"
            -DWOW64=ON -DARM_DYNAREC=ON -DCMAKE_BUILD_TYPE=Release
        BUILD_COMMAND "${CMAKE_COMMAND}" -E env "PATH=${WOWBOX64_LLVM_MINGW_BIN}:$ENV{PATH}"
            "${CMAKE_COMMAND}" --build "<BINARY_DIR>" --target wowbox64
        INSTALL_COMMAND ""
        TEST_COMMAND ""
        BUILD_ALWAYS ON
        BUILD_BYPRODUCTS "<BINARY_DIR>/wowbox64-prefix/src/wowbox64-build/wowbox64.dll"
    )

    ExternalProject_Get_Property(wowbox64_external BINARY_DIR)
    set(WOWBOX64_DLL "${BINARY_DIR}/wowbox64-prefix/src/wowbox64-build/wowbox64.dll")

    # build-switch.sh (and anything else staging this DLL) shouldn't need to
    # know ExternalProject's nested scratch layout -- stage a copy at a
    # fixed, predictable path once the real build produces it.
    set(WOWBOX64_OUTPUT_DIR "${CMAKE_BINARY_DIR}/wowbox_out")
    set(WOWBOX64_DLL_OUT "${WOWBOX64_OUTPUT_DIR}/wowbox64.dll")

    add_custom_command(
        OUTPUT "${WOWBOX64_DLL_OUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WOWBOX64_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${WOWBOX64_DLL}" "${WOWBOX64_DLL_OUT}"
        DEPENDS wowbox64_external "${WOWBOX64_DLL}"
        COMMENT "Staging wowbox64.dll to a predictable, top-level output path"
        VERBATIM
    )

    add_custom_target(wowbox64-dll ALL DEPENDS "${WOWBOX64_DLL_OUT}")
endif()

# --- x64native (Box64's core, linked natively into wine-nx-runtime) --------
# Completely independent of WINE_NX_BUILD_WOWBOX64 above -- that option
# stays exactly as-is; 32-bit guest support (wowbox64.dll, WOW64) is not
# touched by anything in this section. This is the OTHER of the two box64
# integrations, for x86_64 guests: unlike wowbox64.dll, box64's core here is
# compiled with the SAME devkitA64 toolchain as the rest of this file (no
# nested ExternalProject_Add, no separate aarch64-w64-mingw32 sysroot) and
# linked directly into wine-nx-runtime's own ELF binary, matching how every
# other wine-*-real object library in this file already works. See
# x86_64-scoping.md's "Revised plan: no separate PE DLL" section for why --
# there is no wow64.dll loader in this path to demand a genuinely separate
# PE module the way wowbox64.dll needs one.
#
# NOT YET BUILT END-TO-END. Written from the same source lists
# wine/wow64/CMakeLists.txt (WOW64_BOX64CPU_SRC) and the top-level
# CMakeLists.txt's ARM_DYNAREC block (both read in full, both proven to
# compile for ARM64 -- just via aarch64-w64-mingw32, not devkitA64), with
# two deliberate differences: MUSL_SRC is dropped (devkitA64/libnx already
# links a full newlib libm -- `m` is already in wine-nx-runtime's own
# target_link_libraries above; box64's vendored musl math sources would
# fight it with duplicate symbols, not complement it) and CMAKE_ASM_FLAGS
# is NOT overridden with wowbox64's own `-mcpu=cortex-a76` (that value was
# specific to its own build; the devkitA64 toolchain file already sets
# `-mtune=cortex-a57` globally, matching the Switch's actual CPU, and every
# other object library in this file already inherits it -- box64's ASM
# files should too, not diverge). Neither devkitA64 nor a Docker build
# environment were available in the session that wrote this to actually
# run the compile -- this is reasoned from the real, working patterns
# elsewhere in this same file, not verified end-to-end. Expect real
# compile errors on first attempt (box64's custommem.c/dynarec assume more
# POSIX than newlib/libnx provides in places) and iterate from there.
option(WINE_NX_BUILD_X64NATIVE "Link Box64's x86-64 core natively into wine-nx-runtime" OFF)

if(WINE_NX_BUILD_X64NATIVE)
    # box64's ARM64 dynarec prolog/epilog/next/lock (X64NATIVE_DYNAREC_ASM
    # below) are real .S files, forced through CMake's ASM language via
    # set_source_files_properties(... LANGUAGE ASM) later in this block --
    # this project's top-level project() call only enables C CXX, so that
    # property use fails at generate time ("The language ASM was requested
    # ... but was not enabled") without this. Scoped to this option's own
    # block rather than added to the top-level project() call, since
    # nothing else in this file needs it.
    enable_language(ASM)

    set(BOX64_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64")
    if(NOT EXISTS "${BOX64_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "WINE_NX_BUILD_X64NATIVE is ON but ${BOX64_ROOT} is empty. "
            "Run: git submodule update --init wine-nx-probe/third_party/box64")
    endif()

    # Same minimal "Wine-hosted, no ELF loader" core box64 core source list
    # wine/wow64/CMakeLists.txt uses for wowbox64.dll (WOW64_BOX64CPU_SRC
    # there) -- proven sufficient to build a working Wine-hosted CPU backend
    # without box64's own program loader/Linux-process machinery. MUSL_SRC
    # deliberately omitted -- see the section comment above.
    set(X64NATIVE_BOX64CPU_SRC
        "${BOX64_ROOT}/src/build_info.c"
        "${BOX64_ROOT}/src/custommem.c"
        "${BOX64_ROOT}/src/dynarec/arm64/arm64_immenc.c"
        "${BOX64_ROOT}/src/dynarec/arm64/arm64_printer.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_arch.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_functions.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_jmpnext.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_consts.c"
        "${BOX64_ROOT}/src/dynarec/arm64/updateflags_arm64.c"
        "${BOX64_ROOT}/src/dynarec/dynablock.c"
        "${BOX64_ROOT}/src/dynarec/dynarec_native_functions.c"
        "${BOX64_ROOT}/src/dynarec/dynarec_native.c"
        "${BOX64_ROOT}/src/dynarec/dynarec.c"
        "${BOX64_ROOT}/src/dynarec/dynacache_reloc.c"
        "${BOX64_ROOT}/src/emu/x64compstrings.c"
        "${BOX64_ROOT}/src/emu/x64emu.c"
        "${BOX64_ROOT}/src/emu/x64primop.c"
        "${BOX64_ROOT}/src/emu/x64shaext.c"
        "${BOX64_ROOT}/src/emu/x64test.c"
        "${BOX64_ROOT}/src/emu/x64trace.c"
        "${BOX64_ROOT}/src/emu/x64run_private.c"
        "${BOX64_ROOT}/src/emu/x87emu_private.c"
        "${BOX64_ROOT}/src/os/backtrace.c"
        "${BOX64_ROOT}/src/os/os_horizon.c"
        "${BOX64_ROOT}/src/os/hostext_common.c"
        "${BOX64_ROOT}/src/os/hostext_wine.c"
        "${BOX64_ROOT}/src/os/freq_wine.c"
        "${BOX64_ROOT}/src/os/symbolfuncs_wine.c"
        "${BOX64_ROOT}/src/os/emit_signal_wine.c"
        "${BOX64_ROOT}/src/os/perfmap.c"
        "${BOX64_ROOT}/src/os/random_wine.c"
        "${BOX64_ROOT}/src/os/my_cpuid.c"
        "${BOX64_ROOT}/src/os/sysinfo.c"
        "${BOX64_ROOT}/src/tools/alternate.c"
        "${BOX64_ROOT}/src/tools/bitutils.c"
        "${BOX64_ROOT}/src/tools/env.c"
        "${BOX64_ROOT}/src/tools/dynacache.c"
        "${BOX64_ROOT}/src/tools/rbtree.c"
        "${BOX64_ROOT}/src/tools/cpumask.c"
    )

    # Same ARM64 dynarec file lists as the top-level CMakeLists.txt's
    # ARM_DYNAREC block (CMakeLists.txt:1033-1091 there), copied explicitly
    # rather than including that file, since it does a great deal else
    # (package finds, install() rules, non-ARM64 dynarec backends) this
    # target has no business depending on.
    set(X64NATIVE_DYNAREC_ASM
        "${BOX64_ROOT}/src/dynarec/arm64/arm64_prolog.S"
        "${BOX64_ROOT}/src/dynarec/arm64/arm64_epilog.S"
        "${BOX64_ROOT}/src/dynarec/arm64/arm64_next.S"
        "${BOX64_ROOT}/src/dynarec/arm64/arm64_lock.S"
    )
    set(X64NATIVE_DYNAREC_PASS
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_helper.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_emit_tests.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_emit_math.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_emit_logic.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_emit_shift.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_00.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_0f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_66.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_d8.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_d9.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_da.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_db.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_dc.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_dd.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_de.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_df.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_f0.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_660f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_66f20f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_66f30f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_66f0.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_f20f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_f30f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_0f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_0f38.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_66_0f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_f2_0f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_f3_0f.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_66_0f38.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_66_0f3a.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_f2_0f38.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_f2_0f3a.c"
        "${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx_f3_0f38.c"
        "${BOX64_ROOT}/src/dynarec/arm64/updateflags_arm64_pass.c"
        # Added by the real CMakeLists.txt's own `if(DYNAREC)` block
        # (CMakeLists.txt:1248-1253) on top of the ARM_DYNAREC-specific
        # list above -- confirmed genuinely needed (not the also-listed
        # wrapper.h alongside it there, which src/wrapped/generated/
        # wrapper.h turns out to be a pre-generated, already-checked-in
        # file that dynarec_native_pass.c doesn't even #include -- no
        # extra generation step needed for it, verified by grep and by
        # `git log` on the file itself). wowbox64.dll inherits this same
        # file via the nested-config mechanism described in the
        # WINE_NX_BUILD_WOWBOX64 section's own comments above; an earlier
        # draft of this list omitted it by not tracing that inheritance.
        "${BOX64_ROOT}/src/dynarec/dynarec_native_pass.c"
    )
    # Copied field-for-field from the top-level CMakeLists.txt's own
    # `set(INTERPRETER ...)` (CMakeLists.txt:544-577), actually re-read in
    # full to produce this list (an earlier draft of this file reconstructed
    # it from memory and got it wrong: missing x64runavx.c/x64runavxf20f38.c/
    # x64runavxf30f38.c/x64runavxf20f3a.c/x64runavxf30f3a.c, and wrongly
    # included x64syscall.c/x86int3.c/x64int3.c -- see below for why those
    # three are excluded on purpose, not by omission).
    set(X64NATIVE_INTERPRETER
        "${BOX64_ROOT}/src/emu/x64run.c"
        "${BOX64_ROOT}/src/emu/x64run0f.c"
        "${BOX64_ROOT}/src/emu/x64run66.c"
        "${BOX64_ROOT}/src/emu/x64run660f.c"
        "${BOX64_ROOT}/src/emu/x64run66f20f.c"
        "${BOX64_ROOT}/src/emu/x64run66f30f.c"
        "${BOX64_ROOT}/src/emu/x64run66d9.c"
        "${BOX64_ROOT}/src/emu/x64run66dd.c"
        "${BOX64_ROOT}/src/emu/x64run66f0.c"
        "${BOX64_ROOT}/src/emu/x64rund8.c"
        "${BOX64_ROOT}/src/emu/x64rund9.c"
        "${BOX64_ROOT}/src/emu/x64runda.c"
        "${BOX64_ROOT}/src/emu/x64rundb.c"
        "${BOX64_ROOT}/src/emu/x64rundc.c"
        "${BOX64_ROOT}/src/emu/x64rundd.c"
        "${BOX64_ROOT}/src/emu/x64runde.c"
        "${BOX64_ROOT}/src/emu/x64rundf.c"
        "${BOX64_ROOT}/src/emu/x64runf0.c"
        "${BOX64_ROOT}/src/emu/x64runf20f.c"
        "${BOX64_ROOT}/src/emu/x64runf30f.c"
        "${BOX64_ROOT}/src/emu/x64runavx.c"
        "${BOX64_ROOT}/src/emu/x64runavx0f.c"
        "${BOX64_ROOT}/src/emu/x64runavx0f38.c"
        "${BOX64_ROOT}/src/emu/x64runavx660f.c"
        "${BOX64_ROOT}/src/emu/x64runavxf20f.c"
        "${BOX64_ROOT}/src/emu/x64runavxf30f.c"
        "${BOX64_ROOT}/src/emu/x64runavx660f38.c"
        "${BOX64_ROOT}/src/emu/x64runavx660f3a.c"
        "${BOX64_ROOT}/src/emu/x64runavxf20f38.c"
        "${BOX64_ROOT}/src/emu/x64runavxf30f38.c"
        "${BOX64_ROOT}/src/emu/x64runavxf20f3a.c"
        "${BOX64_ROOT}/src/emu/x64runavxf30f3a.c"
    )
    # x64syscall.c/x86int3.c/x64int3.c deliberately excluded, matching
    # wowbox64.c's own already-hardware-validated choice (its
    # WOW64_BOX64CPU_SRC list excludes all three too -- read in full,
    # confirmed, not assumed): x86int3.c is BOX32-gated in the real
    # CMakeLists.txt (line 509, inside `if(BOX32)`) and irrelevant to
    # either guest bitness this project hosts. x64syscall.c implements
    # box64's own Linux-style syscall-number dispatch (x64Syscall(), which
    # real Linux hosting's EmuX64Syscall calls into, per src/os/os_linux.c)
    # -- not used here, since this build's own EmuX64Syscall
    # (src/os/os_horizon.c, #ifdef WINE_NX_X64NATIVE) calls
    # wine_nx_do_syscall() directly instead, the same way wowbox64.c's
    # EmitInterruptionImpl bypasses it for the 32-bit case. x64int3.c is
    # part of the *full* ELFLOADER_SRC build's base list (CMakeLists.txt
    # line 455, unconditional there) but wowbox64.c doesn't need it either
    # -- EmuInt3() (used by emit_signal_wine.c, which IS in this build) is
    # invoked from the interpreter files already included above, not from
    # x64int3.c itself.

    set(X64NATIVE_DYNACACHE_HASH_HEADER "${BOX64_ROOT}/src/dynacache_hashes.h")
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_command(
        OUTPUT "${X64NATIVE_DYNACACHE_HASH_HEADER}"
        COMMAND "${Python3_EXECUTABLE}" "${BOX64_ROOT}/tools/gen_dynacache_hashes.py"
            --root "${BOX64_ROOT}"
            --output "${X64NATIVE_DYNACACHE_HASH_HEADER}"
        # gen_dynacache_hashes.py itself does an rglob("*") over the whole
        # BOX64_ROOT tree, so its true inputs are impractical to list here
        # exhaustively -- DEPENDS on the script itself is a floor, not a
        # complete dependency, but it's a real improvement over the
        # original version of this command, which had no DEPENDS at all
        # and therefore never re-ran once its OUTPUT file existed: found
        # the hard way mid compile-error campaign, when this command
        # turned out to have been silently reusing a stale
        # dynacache_hashes.h left over from an earlier wowbox64.dll build
        # session three days prior (dynacache_hashes.h is generated
        # in-tree, at a path both build configurations share).
        DEPENDS "${BOX64_ROOT}/tools/gen_dynacache_hashes.py"
        BYPRODUCTS "${X64NATIVE_DYNACACHE_HASH_HEADER}"
        VERBATIM
    )
    add_custom_target(x64native-generate-dynacache-hashes DEPENDS "${X64NATIVE_DYNACACHE_HASH_HEADER}")

    set(X64NATIVE_INCLUDES
        "${BOX64_ROOT}/src/include"
        "${BOX64_ROOT}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64/wine"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64/wine/include"
        ${WINE_SWITCH_NTDLL_INCLUDES}
    )
    # wine/include added on top of the original list here (found the hard
    # way mid compile-error campaign: "wine/compiler.h: No such file or
    # directory" building os_horizon.c/hostext_wine.c) -- it holds ONLY
    # wine/include/wine/{compiler,debug}.h, genuine Wine-extension headers
    # with no Windows-SDK equivalent at all, nothing resembling
    # windows.h/winternl.h at its own top level (confirmed: `find
    # third_party/box64/wine/include -maxdepth 1 -type f` returns
    # nothing), so adding it carries none of the shadowing risk described
    # below for a plain windows.h.
    #
    # WINE_SWITCH_NTDLL_INCLUDES is still what resolves <windows.h>/
    # <winternl.h> itself -- deliberately: box64's own wine/include/ is
    # shaped for the aarch64-w64-mingw32/llvm-mingw sysroot wowbox64.dll
    # uses, a DIFFERENT (if similar) windows.h from this project's own
    # real ../include/windows.h that every other native/devkitA64 object
    # library in this file already builds against (wine-horizon-real,
    # wine-ntdll-loader-real, ...). Mixing the two
    # risks real type/macro conflicts; reusing the already-proven one is
    # both simpler and consistent with everything else in this file.
    set(X64NATIVE_DEFS ${WINE_SWITCH_NTDLL_DEFS})
    # __WINESRC__ dropped, unlike every other consumer of
    # WINE_SWITCH_NTDLL_DEFS in this file: it does not mean "building
    # against Wine headers" (a reasonable-sounding but wrong guess this
    # list started with) -- it specifically means "this translation unit
    # IS Wine's own internal source," and winbase.h uses it to POISON raw
    # strncpy()/wcsncpy() calls (rewrites them to a deliberately-broken
    # macro, `error do_not_use_strncpy_...`), forcing genuine Wine source
    # to use lstrcpynA/memcpy instead -- confirmed by reading the actual
    # #if/#else structure in include/winbase.h (~line 2542-2630), not
    # guessed, after a real devkitA64 build reproduced the exact
    # corrupted line (my_cpuid.c's own strncpy call, poisoned this way).
    # Box64 is third-party code that treats <windows.h> as a normal
    # application header (see src/include/os.h's own `#ifndef _WIN32`
    # design) -- it was never "Wine's own source" and must not get this
    # define, the same way an ordinary Windows application wouldn't.
    list(REMOVE_ITEM X64NATIVE_DEFS __WINESRC__)
    # Same reasoning, same fix, for the same class of bug: WINE_UNIX_LIB
    # means "this translation unit IS wine-nx's own unix-side/native
    # implementation code" (dlls/ntdll/unix/*.c) -- under it,
    # include/winbase.h's own GetProcessHeap/GetLastError/SetLastError
    # aren't declared *at all* (that branch, ~winbase.h:2662, only
    # defines GetCurrentProcess/GetCurrentThread/GetCurrentProcessId/
    # GetCurrentThreadId as macros -- unix-side code reaches the process
    # heap directly, with no GetProcessHeap()-style indirection needed).
    # Confirmed via a real devkitA64 build failing exactly there
    # (os_horizon.c calling GetProcessHeap(), "implicit declaration").
    # Box64 is neither Wine's own source nor its unix-side implementation
    # -- it's third-party code that needs the same externally-facing
    # WINBASEAPI declarations any application would get.
    list(REMOVE_ITEM X64NATIVE_DEFS WINE_UNIX_LIB)
    list(APPEND X64NATIVE_DEFS
        WINE_NX_X64NATIVE
        DYNAREC
        ARM64
        _WIN32
        WIN32_LEAN_AND_MEAN
    )
    # WIN32_LEAN_AND_MEAN: standard Windows SDK convention, not something
    # new -- without __WINESRC__ (removed above), windows.h's own
    # `#ifndef __WINESRC__` / `#ifndef WIN32_LEAN_AND_MEAN` block (~line
    # 48-80) pulls in a large "not so essential" bundle box64 has no use
    # for (winscard.h, mmsystem.h, nb30.h, ddeml.h, ole2.h, winspool.h,
    # ...) -- found the hard way, via a real devkitA64 build failing on
    # winscard.h needing wtypes.h, which this include environment doesn't
    # have. Any ordinary third-party Windows application defines this for
    # the same reason; box64 (via os.h's own `#ifndef _WIN32` design)
    # already treats itself as exactly that kind of application.
    # _WIN32: box64's own src/include/os.h already branches on this --
    # `#ifndef _WIN32` pulls in <dlfcn.h>/<sys/mman.h> and expects a real
    # POSIX host; the #else side (no dlfcn.h/sys/mman.h) declares
    # mmap/munmap/mprotect and PROT_*/MAP_* itself, matching os_horizon.c's
    # existing definitions of those three functions exactly. This is not
    # a new mechanism -- box64 already uses it throughout (custommem.c,
    # dynarec_native.c, x64emu.c, and others all branch on _WIN32 too);
    # the wowbox64.dll build never had to think about this because
    # aarch64-w64-mingw32-clang predefines both _WIN32 and _WIN64
    # automatically as part of its target triple (confirmed via
    # `echo | aarch64-w64-mingw32-clang -dM -E -`), which devkitA64's bare
    # aarch64-none-elf-gcc does neither of. WINE_SWITCH_NTDLL_DEFS above
    # already carries _WIN64; this adds the missing _WIN32 explicitly.
    # First real compile error found by the actual Docker/devkitA64 build
    # (attempt 1: os.h:8:10, "fatal error: dlfcn.h: No such file or
    # directory") -- fixed by using box64's own existing switch, not by
    # adding a new stub header.

    add_library(x64native_dynarec_pass0 OBJECT ${X64NATIVE_DYNAREC_PASS})
    add_library(x64native_dynarec_pass1 OBJECT ${X64NATIVE_DYNAREC_PASS})
    add_library(x64native_dynarec_pass2 OBJECT ${X64NATIVE_DYNAREC_PASS})
    add_library(x64native_dynarec_pass3 OBJECT ${X64NATIVE_DYNAREC_PASS})
    add_library(x64native_interpreter   OBJECT ${X64NATIVE_INTERPRETER})
    set_target_properties(x64native_dynarec_pass0 PROPERTIES COMPILE_DEFINITIONS "STEP=0")
    set_target_properties(x64native_dynarec_pass1 PROPERTIES COMPILE_DEFINITIONS "STEP=1")
    set_target_properties(x64native_dynarec_pass2 PROPERTIES COMPILE_DEFINITIONS "STEP=2")
    set_target_properties(x64native_dynarec_pass3 PROPERTIES COMPILE_DEFINITIONS "STEP=3")

    add_library(x64native-core OBJECT
        ${X64NATIVE_BOX64CPU_SRC}
        ${X64NATIVE_DYNAREC_ASM}
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64/wine/x64native/x64native.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64/wine/x64native/x64native_globals.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64/wine/x64native/x64native_rtl.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64/wine/x64native/x64native_rtl.S"
    )
    # x64native_rtl.S: RtlCaptureContext, plain ARM64 register-capture
    # assembly with no LSE instructions at all -- unlike
    # X64NATIVE_DYNAREC_ASM, it does NOT need the armv8.1-a bump (that
    # section's own comment explains why arm64_lock.S alone does), so
    # it's kept out of that list and left on the toolchain's own default
    # -march=armv8-a+crc+crypto.
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64/wine/x64native/x64native_rtl.S"
        PROPERTIES LANGUAGE ASM)
    add_dependencies(x64native-core x64native-generate-dynacache-hashes)

    foreach(X64NATIVE_TGT IN ITEMS
            x64native_dynarec_pass0 x64native_dynarec_pass1
            x64native_dynarec_pass2 x64native_dynarec_pass3
            x64native_interpreter x64native-core)
        target_compile_definitions(${X64NATIVE_TGT} PRIVATE ${X64NATIVE_DEFS})
        target_include_directories(${X64NATIVE_TGT} PRIVATE ${X64NATIVE_INCLUDES})
        target_compile_options(${X64NATIVE_TGT} PRIVATE
            -std=gnu11 -w
            $<$<COMPILE_LANGUAGE:C>:-include;stdarg.h>)
        # $<COMPILE_LANGUAGE:C> guard: x64native-core also compiles real
        # .S assembly sources (X64NATIVE_DYNAREC_ASM) through this same
        # foreach -- a plain, ungated -include stdarg.h applies to every
        # source regardless of language, and GCC's assembler chokes
        # immediately on stdarg.h's C `typedef` syntax ("unknown mnemonic
        # `typedef'"), confirmed via a real devkitA64 build. -include
        # stdarg.h: some box64 files (src/os/hostext_wine.c,
        # confirmed via a real devkitA64 build) include
        # winternl.h/windef.h directly rather than the full windows.h --
        # windows.h itself includes <stdarg.h> before <windef.h> (see
        # include/windows.h:37-38), which is what makes va_list already
        # visible by the time minwindef.h's `#define __ms_va_list
        # va_list` (now reachable after removing WINE_UNIX_LIB above)
        # needs it elsewhere. Force-including it here matches this
        # file's own established pattern for exactly this situation
        # (wine-ntdll-loader-real's `-include ntdll_loader_compat.h`
        # above) and fixes it for every file in one place instead of
        # editing each box64 .c file's own include order.
        # -w (not -Wall -Wextra -Werror=implicit-function-declaration like
        # this file's own sources): box64 is a large, external codebase
        # with its own warning posture; enforcing this project's own
        # -Werror=implicit-function-declaration against it is how the
        # compile-error campaign starts, not something to silence, BUT
        # doing so file-by-file as errors are actually found is more
        # tractable than fighting box64's entire warning surface at once
        # sight-unseen. Tighten this once the first clean build exists.
        add_dependencies(${X64NATIVE_TGT} x64native-generate-dynacache-hashes)
    endforeach()
    set_source_files_properties(${X64NATIVE_DYNAREC_ASM} PROPERTIES LANGUAGE ASM)
    # arm64_lock.S uses ARMv8.1 LSE atomics (casal/swpal) for its fast
    # path -- the base -march=armv8-a this toolchain sets globally
    # (cmake/switch-devkitA64.cmake, matching the Switch's real Cortex-A57
    # CPU, which is ARMv8.0 and has no LSE in hardware) doesn't recognize
    # them at the assembler level at all (confirmed via a real devkitA64
    # build: "selected processor does not support `casal w2,w1,[x0]'").
    #
    # This is NOT a hardware-compatibility risk despite that, though --
    # verified by reading the actual .S source (not assumed): every LSE
    # instruction is reached only through a runtime check first
    # (`adrp/ldr cpuext; tbnz w3, #0, arm64_atomic_...`), falling back to
    # portable ldaxr/stlxr LL/SC loops otherwise, which work on any ARM64
    # CPU including real Cortex-A57. On real hardware cpuext's LSE bit
    # will read false (A57 genuinely can't do LSE), so this code is
    # encoded but never executed there -- the same reasoning that already
    # justifies wowbox64.dll's own build using -mcpu=cortex-a76 (which
    # also enables LSE) for this exact file, hardware-proven working.
    # Bumping to armv8.1-a (the minimum ISA level LSE was introduced in)
    # rather than copying cortex-a76 specifically, to change as little as
    # possible relative to the toolchain's own base -march=armv8-a+crc+crypto.
    set_source_files_properties(${X64NATIVE_DYNAREC_ASM} PROPERTIES
        COMPILE_OPTIONS "-march=armv8.1-a+crc+crypto")

    # wine-ntdll-loader-real (dlls/ntdll/loader.c, defined above) needs
    # WINE_NX_X64NATIVE to compile wine_nx_x64_handoff() at all, and needs
    # this submodule's wine/ dir on its include path to resolve
    # #include "wine/x64native.h". Both applied via generator expression so
    # the DEFAULT (WINE_NX_BUILD_X64NATIVE=OFF) build of this same object
    # library -- shared by wine-nx-runtime, wine-nx-runtime-deko3d, AND
    # every smoke-test executable -- is completely unchanged; only turning
    # this option on recompiles it with the new macro. Safe even for
    # consumers that don't link x64native-core because wine_nx_x64_handoff()
    # declares its four box64-side dependencies __attribute__((weak)) (see
    # loader.c's own comment on that) -- verified least risky given the
    # BOOL generator expression only takes effect for consumers of THIS
    # object library, not a global define.
    target_compile_definitions(wine-ntdll-loader-real PRIVATE
        $<$<BOOL:${WINE_NX_BUILD_X64NATIVE}>:WINE_NX_X64NATIVE>)
    target_include_directories(wine-ntdll-loader-real PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box64")
    # NOT .../third_party/box64/wine -- loader.c includes
    # "wine/x64native.h" (a quoted, wine/-prefixed path, matching how
    # every other file in the box64 tree refers to its own wine/
    # subdirectory), so the include path needs to be one level higher,
    # at box64's own root, for that path to resolve at all. Found via
    # the actual link attempt failing with "wine/x64native.h: No such
    # file or directory" -- the original version of this line pointed
    # one directory too deep.

    # Linked into wine-nx-runtime specifically (not wine-nx-runtime-deko3d,
    # the software-render variant -- out of scope for this pass, add
    # later if needed) via target_sources() rather than editing that
    # add_executable() call above, so the default build's source list is
    # untouched either way.
    target_sources(wine-nx-runtime PRIVATE
        $<TARGET_OBJECTS:x64native_dynarec_pass0>
        $<TARGET_OBJECTS:x64native_dynarec_pass1>
        $<TARGET_OBJECTS:x64native_dynarec_pass2>
        $<TARGET_OBJECTS:x64native_dynarec_pass3>
        $<TARGET_OBJECTS:x64native_interpreter>
        $<TARGET_OBJECTS:x64native-core>
    )
    # Same objects, into wine-nx-launcher too (defined further down this
    # file) -- the merged single-NRO build's whole point is running the
    # identical 64-bit x86 guest path this gives wine-nx-runtime, not a
    # second copy of it.
    target_sources(wine-nx-launcher PRIVATE
        $<TARGET_OBJECTS:x64native_dynarec_pass0>
        $<TARGET_OBJECTS:x64native_dynarec_pass1>
        $<TARGET_OBJECTS:x64native_dynarec_pass2>
        $<TARGET_OBJECTS:x64native_dynarec_pass3>
        $<TARGET_OBJECTS:x64native_interpreter>
        $<TARGET_OBJECTS:x64native-core>
    )
endif()
