set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO "$ENV{DEVKITPRO}")
else()
    set(DEVKITPRO "/opt/devkitpro")
endif()

set(DEVKITA64 "${DEVKITPRO}/devkitA64")
set(LIBNX "${DEVKITPRO}/libnx")
set(PORTLIBS "${DEVKITPRO}/portlibs/switch")

set(CMAKE_C_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-gcc")
set(CMAKE_CXX_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-g++")
set(CMAKE_ASM_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-gcc")
set(CMAKE_AR "${DEVKITA64}/bin/aarch64-none-elf-ar")
set(CMAKE_RANLIB "${DEVKITA64}/bin/aarch64-none-elf-ranlib")
set(CMAKE_STRIP "${DEVKITA64}/bin/aarch64-none-elf-strip")

set(CMAKE_FIND_ROOT_PATH "${DEVKITA64}" "${DEVKITPRO}" "${LIBNX}" "${PORTLIBS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(SWITCH_ARCH_FLAGS "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -fPIE")
set(SWITCH_COMMON_FLAGS "${SWITCH_ARCH_FLAGS} -ffixed-x18 -ffunction-sections -fdata-sections -D__SWITCH__ -DSWITCH=1 -DNX -D_GNU_SOURCE")
set(SWITCH_COMMON_FLAGS "${SWITCH_COMMON_FLAGS} -isystem ${LIBNX}/include -isystem ${PORTLIBS}/include")

set(CMAKE_C_FLAGS_INIT "${SWITCH_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${SWITCH_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${SWITCH_COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${LIBNX}/switch.specs -L${LIBNX}/lib -L${PORTLIBS}/lib -Wl,--gc-sections")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_CROSSCOMPILING TRUE)
