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
