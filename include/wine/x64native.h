/* Shared contract between dlls/ntdll/loader.c's wine_nx_x64_handoff() and
 * wine/x64native/x64native.c's implementation. Deliberately minimal and
 * deliberately NOT dependent on any box64-internal header (x64emu.h,
 * box64context.h, ...) -- loader.c is compiled as part of wine-ntdll-
 * loader-real, a shared object library reused by executables that never
 * link box64 at all (smoke tests); it must not need box64's include path.
 *
 * struct wine_nx_amd64_context mirrors the real x86_64 AMD64_CONTEXT
 * (winnt.h, x86_64-windows target) field-for-field, but is defined here
 * as our own plain struct rather than #including that winnt.h, since this
 * project's *native* (aarch64-windows) build has its own CONTEXT already
 * meaning something else entirely (the ARM64 register set) under the same
 * name -- see the field-layout warning in wine_nx_x64_enter()'s own
 * comment. If this ever drifts from the real AMD64_CONTEXT layout, guest
 * register state silently comes out wrong instead of failing to compile;
 * double check against a real x86_64-windows winnt.h before trusting it.
 *
 * This is a plain copy of wine-nx-probe/third_party/box64/wine/x64native.h,
 * kept here too so Wine's own PE-only host build (this repo's own configure/
 * make, producing aarch64-windows/i386-windows/x86_64-windows DLLs) can
 * resolve dlls/ntdll/loader.c's `#include "wine/x64native.h"` -- makedep
 * scans #include lines textually, independent of the #ifdef WINE_NX_X64NATIVE
 * that actually gates using it, so the header must physically exist on
 * this build's own include path even though WINE_NX_X64NATIVE is never
 * defined here and this file's contents are never actually compiled in by
 * it. Not deduplicated with the third_party/box64 copy: that one feeds the
 * devkitA64/CMake x64native-core build (a completely separate toolchain
 * and include path), and touching its location risks that already-working
 * build; two identical copies is the lower-risk fix. */
#ifndef WINE_NX_X64NATIVE_H
#define WINE_NX_X64NATIVE_H

#include <windef.h>

struct wine_nx_amd64_context
{
    DWORD64 P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    DWORD   ContextFlags, MxCsr;
    WORD    SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    DWORD   EFlags;
    DWORD64 Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    DWORD64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;
    DWORD64 R8, R9, R10, R11, R12, R13, R14, R15;
    DWORD64 Rip;
    /* FltSave/VectorRegisters/... intentionally omitted -- nothing on
     * either side of this contract reads them yet (see the FP/SSE-state
     * gap noted in wine_nx_x64_enter()). Extend here (matching the real
     * struct's field order) the same day something actually needs them,
     * not before -- an unused field nobody has verified the offset of is
     * worse than no field. */
};

/* dlls/ntdll/loader.c's wine_nx_x64_handoff() calls these four; all are
 * declared __attribute__((weak)) at the *call site*, not here -- this
 * header only states the contract, it doesn't decide how a given
 * translation unit resolves it. See wine_nx_x64_handoff()'s own comment
 * for why weak (wine-ntdll-loader-real is a shared object library reused
 * by executables that don't link box64 at all). */
NTSTATUS wine_nx_x64_process_init(void);
NTSTATUS wine_nx_x64_thread_init(void);
void wine_nx_x64_enter( struct wine_nx_amd64_context *ctx );
void WINAPI wine_nx_x64_prepare_for_exception( EXCEPTION_RECORD *rec, CONTEXT *context );

#endif /* WINE_NX_X64NATIVE_H */
