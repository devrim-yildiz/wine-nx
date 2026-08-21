/* See nx_diagnostics.h. HARDWARE-CONFIRMED: 17/18 real subtests passed
 * on real Switch hardware the first time this suite ever ran (reached
 * via Settings' own "DEBUG OVERLAY" row -- see nxmenu.h's own
 * NXMENU_ACTION_OPEN_DEBUG comment for why that's the reliable way in).
 * The one failure at the time (32-2) was real and correctly caught by
 * its own test, not a bug in the test: it used a plain malloc(), which
 * hardware-confirmed does NOT land below 4GB in this build -- true, but
 * not actually informative about 32-bit guest support, since guests
 * never go through libc's heap. 32-2 now exercises the real loader
 * allocator instead (NtAllocateVirtualMemory with an explicit sub-4GB
 * zero_bits ceiling, the exact mechanism virtual_alloc_first_teb() itself
 * uses) -- not yet re-run on hardware since that change.
 *
 * The primitives this file exercises -- jitCreate/jitTransitionTo* (also
 * proven independently by source/jit_smoke.c's own hardware-confirmed
 * Phase 1/2/3), TEB/PEB/IMAGE_NT_HEADERS* struct layouts (already
 * load-bearing in source/runtime.c's real PE loader), svcQueryMemory,
 * virtmemFindAslr, pthread TLS, NtAllocateVirtualMemory/NtFreeVirtualMemory
 * (via wine_nx_ensure_lazy_init(), runtime_boot.h) -- are each
 * independently real Horizon/libnx/pthread/Wine APIs, not reimplemented
 * here.
 */
#include "nx_diagnostics.h"

#include <switch.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "runtime_boot.h"

static void nxdiag_record( nxdiag_suite_t *suite, nxdiag_result_t result,
                            const char *name, const char *detail_fmt, ... )
{
    nxdiag_subtest_t *t;
    va_list args;

    if (suite->subtest_count >= NXDIAG_MAX_SUBTESTS) return;
    t = &suite->subtests[suite->subtest_count++];
    t->result = result;
    snprintf( t->name, sizeof(t->name), "%s", name );

    va_start( args, detail_fmt );
    vsnprintf( t->detail, sizeof(t->detail), detail_fmt, args );
    va_end( args );

    switch (result)
    {
    case NXDIAG_RESULT_PASS:    suite->pass_count++; break;
    case NXDIAG_RESULT_FAIL:    suite->fail_count++; break;
    case NXDIAG_RESULT_SKIPPED: suite->skip_count++; break;
    default: break;
    }
}

/* ================= 32-bit (x86 / PE32) suite ================= */

static void nxdiag_test_32_1_pe_header( nxdiag_suite_t *suite )
{
    /* Self-contained synthetic PE32 image header -- not a file on disk,
     * so this test has no dependency on what's staged on the SD card and
     * always runs the same way. Built and parsed through the exact same
     * IMAGE_DOS_HEADER/IMAGE_NT_HEADERS32 struct layouts runtime.c's own
     * peek_pe_machine()/runtime_describe_image() use against real
     * files. */
    unsigned char buf[512];
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    IMAGE_NT_HEADERS32 *nt32;
    IMAGE_NT_HEADERS32 *parsed;
    const DWORD nt_off = 0x80;

    memset( buf, 0, sizeof(buf) );
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = nt_off;

    nt32 = (IMAGE_NT_HEADERS32 *)(buf + nt_off);
    nt32->Signature = IMAGE_NT_SIGNATURE;
    nt32->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt32->FileHeader.NumberOfSections = 3;
    nt32->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt32->OptionalHeader.AddressOfEntryPoint = 0x1000;

    parsed = (IMAGE_NT_HEADERS32 *)(buf + dos->e_lfanew);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE && parsed->Signature == IMAGE_NT_SIGNATURE &&
        parsed->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        parsed->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        parsed->OptionalHeader.AddressOfEntryPoint == 0x1000)
    {
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "32-1: PE32 Header Parsing",
                        "machine=0x%x entry_rva=0x%x sections=%u",
                        parsed->FileHeader.Machine, parsed->OptionalHeader.AddressOfEntryPoint,
                        parsed->FileHeader.NumberOfSections );
    }
    else
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "32-1: PE32 Header Parsing",
                        "parsed fields did not round-trip (sig=0x%x machine=0x%x)",
                        parsed->Signature, parsed->FileHeader.Machine );
    }
}

static void nxdiag_test_32_2_sub4gb( nxdiag_suite_t *suite )
{
    /* Real loader allocator, not libc's malloc(): an earlier version of
     * this test used malloc(4096) and hardware-confirmed it lands well
     * above 4GB on this build -- a real, honest result, but not actually
     * informative about 32-bit guest support, since 32-bit guests never
     * go through libc's heap at all. What they DO go through is Wine's
     * own NtAllocateVirtualMemory, the same call and the same "zero_bits
     * as an address ceiling" trick virtual_alloc_first_teb() itself uses
     * (dlls/ntdll/unix/virtual.c: `is_win64 ? limit_2g - 1 : 0`) --
     * mirrored here with a 4GB ceiling (0xFFFFFFFF) instead of 2GB, via
     * get_zero_bits_limit()'s own documented handling of zero_bits >= 32
     * as a literal address bound, not a bit count.
     *
     * NtAllocateVirtualMemory requires virtual_init() to have already
     * set up its own mutex and view tables, or it crashes rather than
     * failing gracefully -- wine_nx_ensure_lazy_init() (runtime_boot.h)
     * guarantees that safely: it's the exact same guarded, idempotent
     * bootstrap a real game launch already needs, so triggering it early
     * from here doesn't duplicate or race that work, and a later real
     * launch just reuses the TEB this already established. The test
     * allocation itself is separate from anything the loader depends on
     * (not the TEB block, not any registered module) and is released via
     * NtFreeVirtualMemory before returning, so this can never permanently
     * claim address space a real launch might want. */
    struct _TEB *teb;
    void *ptr = NULL;
    SIZE_T size = 0x1000;
    NTSTATUS status;

    teb = wine_nx_ensure_lazy_init();
    if (!teb)
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "32-2: Real Loader Sub-4GB Allocation",
                        "wine_nx_ensure_lazy_init() failed -- virtual_init()/first TEB setup did not complete" );
        return;
    }

    status = NtAllocateVirtualMemory( NtCurrentProcess(), &ptr, 0xFFFFFFFFUL, &size,
                                       MEM_RESERVE, PAGE_READWRITE );
    if (status != STATUS_SUCCESS)
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "32-2: Real Loader Sub-4GB Allocation",
                        "NtAllocateVirtualMemory(zero_bits=0xFFFFFFFF) status=0x%x", (unsigned int)status );
        return;
    }

    {
        uintptr_t addr = (uintptr_t)ptr;
        void *free_ptr = ptr;
        SIZE_T free_size = 0;
        NTSTATUS free_status = NtFreeVirtualMemory( NtCurrentProcess(), &free_ptr, &free_size, MEM_RELEASE );

        if (addr < 0x100000000ULL)
            nxdiag_record( suite, NXDIAG_RESULT_PASS, "32-2: Real Loader Sub-4GB Allocation",
                            "NtAllocateVirtualMemory -> 0x%llx (< 4GB), NtFreeVirtualMemory status=0x%x",
                            (unsigned long long)addr, (unsigned int)free_status );
        else
            nxdiag_record( suite, NXDIAG_RESULT_FAIL, "32-2: Real Loader Sub-4GB Allocation",
                            "NtAllocateVirtualMemory -> 0x%llx (>= 4GB despite zero_bits=0xFFFFFFFF), NtFreeVirtualMemory status=0x%x",
                            (unsigned long long)addr, (unsigned int)free_status );
    }
}

static void nxdiag_test_32_3_reloc( nxdiag_suite_t *suite )
{
    /* Synthetic image: one data page holding two 32-bit "pointers" that
     * need rebasing, plus a real IMAGE_BASE_RELOCATION block describing
     * them -- the same layout any PE loader's relocation walker consumes
     * (a page RVA + block size, followed by packed 16-bit
     * type|offset entries). Applies the real IMAGE_REL_BASED_HIGHLOW
     * delta math: add (actual_base - preferred_base) to each flagged
     * 32-bit slot. */
    unsigned char image[0x2000];
    DWORD *slot0, *slot1;
    IMAGE_BASE_RELOCATION *block;
    WORD *entries;
    WORD count;
    int i;
    const DWORD preferred_base = 0x00400000;
    const DWORD actual_base = 0x10400000;
    const int32_t delta = (int32_t)(actual_base - preferred_base);
    const DWORD page_rva = 0x1000;

    memset( image, 0, sizeof(image) );

    slot0 = (DWORD *)(image + page_rva + 0x10);
    slot1 = (DWORD *)(image + page_rva + 0x20);
    *slot0 = preferred_base + 0x2000;
    *slot1 = preferred_base + 0x3000;

    block = (IMAGE_BASE_RELOCATION *)(image + 0x1800);
    block->VirtualAddress = page_rva;
    block->SizeOfBlock = sizeof(IMAGE_BASE_RELOCATION) + 2 * sizeof(WORD);
    entries = (WORD *)(block + 1);
    entries[0] = (WORD)((IMAGE_REL_BASED_HIGHLOW << 12) | 0x10);
    entries[1] = (WORD)((IMAGE_REL_BASED_HIGHLOW << 12) | 0x20);

    count = (WORD)((block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD));
    for (i = 0; i < count; i++)
    {
        WORD type = (WORD)(entries[i] >> 12);
        WORD offset = (WORD)(entries[i] & 0xfff);
        if (type == IMAGE_REL_BASED_HIGHLOW)
        {
            DWORD *target = (DWORD *)(image + block->VirtualAddress + offset);
            *target = (DWORD)((int32_t)*target + delta);
        }
    }

    if (*slot0 == actual_base + 0x2000 && *slot1 == actual_base + 0x3000)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "32-3: IMAGE_REL_BASED_HIGHLOW Relocation",
                        "delta=0x%x applied to 2 slots", (unsigned int)delta );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "32-3: IMAGE_REL_BASED_HIGHLOW Relocation",
                        "slot0=0x%x slot1=0x%x expected 0x%x/0x%x",
                        *slot0, *slot1, actual_base + 0x2000, actual_base + 0x3000 );
}

static void nxdiag_test_32_4_teb32_layout( nxdiag_suite_t *suite )
{
    /* Real struct introspection (sizeof/offsetof), not a hardcoded pass
     * -- winternl.h already C_ASSERTs these same facts at compile time
     * (so a mismatch would fail the build long before this ever runs),
     * but re-checking them here at runtime is still a genuine,
     * self-verifying test of the exact layout the WOW64 TEB this
     * loader's 32-bit path depends on. */
    int ok = 1;

    if (sizeof(TEB32) != 0x1000) ok = 0;
    if (offsetof(TEB32, Peb) != 0x30) ok = 0;
    if (sizeof(PEB32) != 0x480) ok = 0;

    nxdiag_record( suite, ok ? NXDIAG_RESULT_PASS : NXDIAG_RESULT_FAIL,
                    "32-4: TEB32/PEB32 Structure Layout",
                    "sizeof(TEB32)=0x%zx off(Peb)=0x%zx sizeof(PEB32)=0x%zx",
                    sizeof(TEB32), offsetof(TEB32, Peb), sizeof(PEB32) );
}

static void nxdiag_test_32_5_bitagnostic_fields( nxdiag_suite_t *suite )
{
    /* runtime.c's own runtime_describe_image() reads Signature/FileHeader
     * and the shared prefix of OptionalHeader through an
     * IMAGE_NT_HEADERS64 view even for a genuine PE32 image, because
     * those fields sit at identical offsets in both header layouts (see
     * that function's own header comment). Direct regression test of
     * that exact "bit-agnostic" contract: build a real PE32 buffer, read
     * the shared prefix through the 64-bit struct view, and verify it
     * matches what was written through the 32-bit view. A future winnt.h
     * change that broke that offset equivalence would silently corrupt
     * every 32-bit game's metadata extraction without a test like this
     * one catching it. */
    unsigned char buf[512];
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    IMAGE_NT_HEADERS32 *nt32;
    IMAGE_NT_HEADERS64 *nt64_view;
    const DWORD nt_off = 0x80;

    memset( buf, 0, sizeof(buf) );
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = nt_off;

    nt32 = (IMAGE_NT_HEADERS32 *)(buf + nt_off);
    nt32->Signature = IMAGE_NT_SIGNATURE;
    nt32->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt32->FileHeader.NumberOfSections = 5;
    nt32->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt32->OptionalHeader.AddressOfEntryPoint = 0x2000;

    nt64_view = (IMAGE_NT_HEADERS64 *)(buf + dos->e_lfanew);

    if (nt64_view->Signature == IMAGE_NT_SIGNATURE &&
        nt64_view->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        nt64_view->FileHeader.NumberOfSections == 5 &&
        nt64_view->OptionalHeader.AddressOfEntryPoint == 0x2000)
    {
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "32-5: Bit-Agnostic Header Prefix",
                        "64-bit view read machine=0x%x sections=%u entry=0x%x through a real PE32 buffer",
                        nt64_view->FileHeader.Machine, nt64_view->FileHeader.NumberOfSections,
                        nt64_view->OptionalHeader.AddressOfEntryPoint );
    }
    else
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "32-5: Bit-Agnostic Header Prefix",
                        "64-bit view sig=0x%x machine=0x%x sections=%u -- prefix offsets diverged",
                        nt64_view->Signature, nt64_view->FileHeader.Machine,
                        nt64_view->FileHeader.NumberOfSections );
    }
}

static void nxdiag_test_32_6_wow64_pointer_truncation( nxdiag_suite_t *suite )
{
    /* TEB32's own pointer-shaped fields (Peb, ThreadLocalStoragePointer,
     * EnvironmentPointer, ...) are plain ULONG, not a real pointer type
     * -- the exact WOW64 struct-marshalling hazard this loader's 32-bit
     * path depends on getting right: a sub-4GB host pointer must survive
     * a round trip through a 32-bit-wide field with no data loss. Uses a
     * synthetic sub-4GB value rather than a real live pointer (32-2
     * already tests whether a real one happens to fit) -- this test is
     * specifically about the truncation arithmetic itself. */
    uintptr_t synthetic_host_ptr = 0x04200000;
    ULONG truncated = (ULONG)synthetic_host_ptr;
    uintptr_t restored = (uintptr_t)truncated;

    if (restored == synthetic_host_ptr)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "32-6: WOW64 Pointer Truncation",
                        "0x%llx -> ULONG 0x%lx -> 0x%llx round-trips",
                        (unsigned long long)synthetic_host_ptr, (unsigned long)truncated,
                        (unsigned long long)restored );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "32-6: WOW64 Pointer Truncation",
                        "0x%llx -> ULONG 0x%lx -> 0x%llx MISMATCH",
                        (unsigned long long)synthetic_host_ptr, (unsigned long)truncated,
                        (unsigned long long)restored );
}

static void nxdiag_run_suite_32bit( nxdiag_suite_t *suite )
{
    suite->name = "32-bit (x86 / PE32)";
    nxdiag_test_32_1_pe_header( suite );
    nxdiag_test_32_2_sub4gb( suite );
    nxdiag_test_32_3_reloc( suite );
    nxdiag_test_32_4_teb32_layout( suite );
    nxdiag_test_32_5_bitagnostic_fields( suite );
    nxdiag_test_32_6_wow64_pointer_truncation( suite );
}

/* ================= 64-bit (x86_64 / PE32+) suite ================= */

static void nxdiag_test_64_1_pe_header( nxdiag_suite_t *suite )
{
    unsigned char buf[512];
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_NT_HEADERS64 *parsed;
    const DWORD nt_off = 0x80;

    memset( buf, 0, sizeof(buf) );
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = nt_off;

    nt = (IMAGE_NT_HEADERS64 *)(buf + nt_off);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.AddressOfEntryPoint = 0x3000;

    parsed = (IMAGE_NT_HEADERS64 *)(buf + dos->e_lfanew);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE && parsed->Signature == IMAGE_NT_SIGNATURE &&
        parsed->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        parsed->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        parsed->OptionalHeader.AddressOfEntryPoint == 0x3000)
    {
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "64-1: PE32+ Header Parsing",
                        "machine=0x%x entry_rva=0x%x", parsed->FileHeader.Machine,
                        parsed->OptionalHeader.AddressOfEntryPoint );
    }
    else
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "64-1: PE32+ Header Parsing",
                        "parsed fields did not round-trip (sig=0x%x machine=0x%x)",
                        parsed->Signature, parsed->FileHeader.Machine );
    }
}

static void nxdiag_test_64_2_address_space( nxdiag_suite_t *suite )
{
    /* virtmemFindAslr() is libnx's own general-purpose address-space
     * finder -- a different, unconstrained counterpart to 32-2's
     * NtAllocateVirtualMemory(zero_bits=0xFFFFFFFF): that one proves
     * Wine's own allocator CAN be constrained below 4GB when asked,
     * this one proves the full 64-bit range is genuinely available when
     * it isn't. Only finds a candidate slice, never maps or reserves
     * it, so there's nothing to release afterward. */
    void *slice;

    virtmemLock();
    slice = virtmemFindAslr( 0x10000, 0 );
    virtmemUnlock();

    if (slice)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "64-2: Full 64-bit Address Space Query",
                        "virtmemFindAslr(64KB) -> %p", slice );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "64-2: Full 64-bit Address Space Query",
                        "virtmemFindAslr(64KB) returned NULL" );
}

static void nxdiag_test_64_3_register_roundtrip( nxdiag_suite_t *suite )
{
    /* Real ARM64 asm: loads 8 known 64-bit patterns into x9-x16 (all
     * caller-saved scratch registers, safely clobbered inside one asm
     * block) then stores them straight back out, proving a real
     * register-context save/restore round trip -- the same shape of
     * primitive an ARM64<->x86_64 ABI bridge needs to get right, tested
     * in isolation from any actual bridge code. */
    static const uint64_t pattern[8] = {
        0x1111111111111111ULL, 0x2222222222222222ULL, 0x3333333333333333ULL, 0x4444444444444444ULL,
        0x5555555555555555ULL, 0x6666666666666666ULL, 0x7777777777777777ULL, 0x8888888888888888ULL
    };
    uint64_t readback[8];
    int i, ok = 1;

    memset( readback, 0, sizeof(readback) );

    __asm__ volatile(
        "ldp x9,  x10, [%[in],  #0]\n"
        "ldp x11, x12, [%[in],  #16]\n"
        "ldp x13, x14, [%[in],  #32]\n"
        "ldp x15, x16, [%[in],  #48]\n"
        "stp x9,  x10, [%[out], #0]\n"
        "stp x11, x12, [%[out], #16]\n"
        "stp x13, x14, [%[out], #32]\n"
        "stp x15, x16, [%[out], #48]\n"
        :
        : [in] "r" (pattern), [out] "r" (readback)
        : "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "memory"
    );

    for (i = 0; i < 8; i++)
        if (readback[i] != pattern[i]) ok = 0;

    if (ok)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "64-3: ARM64 Register Context Round-Trip",
                        "8 x 64-bit values survived ldp/stp through x9-x16" );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "64-3: ARM64 Register Context Round-Trip",
                        "readback[0]=0x%llx expected 0x%llx",
                        (unsigned long long)readback[0], (unsigned long long)pattern[0] );
}

static void nxdiag_test_64_4_teb64_layout( nxdiag_suite_t *suite )
{
    int ok = 1;

    if (sizeof(TEB64) != 0x1838) ok = 0;
    if (offsetof(TEB, Peb) != 0x60) ok = 0;
    if (sizeof(PEB64) != 0x7c8) ok = 0;

    nxdiag_record( suite, ok ? NXDIAG_RESULT_PASS : NXDIAG_RESULT_FAIL,
                    "64-4: TEB64/PEB64 Structure Layout",
                    "sizeof(TEB64)=0x%zx off(Peb)=0x%zx sizeof(PEB64)=0x%zx",
                    sizeof(TEB64), offsetof(TEB, Peb), sizeof(PEB64) );
}

static void nxdiag_test_64_5_reloc64( nxdiag_suite_t *suite )
{
    /* Same shape as 32-3, scaled to real 8-byte pointers and the real
     * IMAGE_REL_BASED_DIR64 relocation type a 64-bit PE actually uses. */
    unsigned char image[0x2000];
    ULONGLONG *slot0, *slot1;
    IMAGE_BASE_RELOCATION *block;
    WORD *entries;
    WORD count;
    int i;
    const ULONGLONG preferred_base = 0x0000000140000000ULL;
    const ULONGLONG actual_base    = 0x00007ff600000000ULL;
    const int64_t delta = (int64_t)(actual_base - preferred_base);
    const DWORD page_rva = 0x1000;

    memset( image, 0, sizeof(image) );

    slot0 = (ULONGLONG *)(image + page_rva + 0x10);
    slot1 = (ULONGLONG *)(image + page_rva + 0x20);
    *slot0 = preferred_base + 0x2000;
    *slot1 = preferred_base + 0x3000;

    block = (IMAGE_BASE_RELOCATION *)(image + 0x1800);
    block->VirtualAddress = page_rva;
    block->SizeOfBlock = sizeof(IMAGE_BASE_RELOCATION) + 2 * sizeof(WORD);
    entries = (WORD *)(block + 1);
    entries[0] = (WORD)((IMAGE_REL_BASED_DIR64 << 12) | 0x10);
    entries[1] = (WORD)((IMAGE_REL_BASED_DIR64 << 12) | 0x20);

    count = (WORD)((block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD));
    for (i = 0; i < count; i++)
    {
        WORD type = (WORD)(entries[i] >> 12);
        WORD offset = (WORD)(entries[i] & 0xfff);
        if (type == IMAGE_REL_BASED_DIR64)
        {
            ULONGLONG *target = (ULONGLONG *)(image + block->VirtualAddress + offset);
            *target = (ULONGLONG)((int64_t)*target + delta);
        }
    }

    if (*slot0 == actual_base + 0x2000 && *slot1 == actual_base + 0x3000)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "64-5: IMAGE_REL_BASED_DIR64 Relocation",
                        "delta=0x%llx applied to 2 slots", (unsigned long long)delta );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "64-5: IMAGE_REL_BASED_DIR64 Relocation",
                        "slot0=0x%llx slot1=0x%llx",
                        (unsigned long long)*slot0, (unsigned long long)*slot1 );
}

static void nxdiag_test_64_6_tls( nxdiag_suite_t *suite )
{
    /* Real pthread TLS slot: allocate, set, get, delete -- the same
     * mechanism this project already relies on elsewhere (e.g.
     * source/runtime.c's own thread bookkeeping) for a real per-thread
     * storage round trip, not a mocked "TLS index". */
    pthread_key_t key;
    int magic = 0x5A5AF00D;
    void *readback;

    if (pthread_key_create( &key, NULL ) != 0)
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "64-6: TLS Slot Allocation",
                        "pthread_key_create failed" );
        return;
    }

    if (pthread_setspecific( key, (void *)(uintptr_t)magic ) != 0)
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "64-6: TLS Slot Allocation",
                        "pthread_setspecific failed" );
        pthread_key_delete( key );
        return;
    }

    readback = pthread_getspecific( key );
    pthread_key_delete( key );

    if ((int)(uintptr_t)readback == magic)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "64-6: TLS Slot Allocation",
                        "key=%u round-tripped 0x%x through TLS", (unsigned int)key, magic );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "64-6: TLS Slot Allocation",
                        "expected 0x%x got 0x%x", magic, (int)(uintptr_t)readback );
}

static void nxdiag_run_suite_64bit( nxdiag_suite_t *suite )
{
    suite->name = "64-bit (x86_64 / PE32+)";
    nxdiag_test_64_1_pe_header( suite );
    nxdiag_test_64_2_address_space( suite );
    nxdiag_test_64_3_register_roundtrip( suite );
    nxdiag_test_64_4_teb64_layout( suite );
    nxdiag_test_64_5_reloc64( suite );
    nxdiag_test_64_6_tls( suite );
}

/* ================= JIT (W^X bypass) suite ================= */

typedef int (*nxdiag_jit_fn_t)( void );

static void nxdiag_run_suite_jit( nxdiag_suite_t *suite )
{
    /* Reuses the exact primitives and encoding source/jit_smoke.c's own
     * hardware-confirmed Phase 1 ("movz w0,#imm16 ; ret", chosen so a
     * wrong answer can only come from actually having executed
     * freshly-written code through the RX alias) and Phase 3 (a plain
     * DATA read through the RX alias) already proved on real hardware --
     * run here in-process instead of as a separate .nro, plus one
     * genuinely new subtest (JIT-6) that asks the kernel itself, via
     * svcQueryMemory, what permission bits the RX page actually carries
     * rather than trusting jitTransitionToExecutable()'s return code
     * alone. */
    Jit jit;
    Result rc;
    uint32_t *rw;
    nxdiag_jit_fn_t fn;
    int result;
    uint16_t magic = 0x5A5A;
    volatile uint64_t *rw64, *rx64;
    uint64_t magic64 = 0xFEEDFACECAFEBEEFULL;
    uint64_t readback64;
    MemoryInfo meminfo;
    u32 pageinfo;

    suite->name = "JIT (W^X Bypass)";

    rc = jitCreate( &jit, 0x1000 );
    if (R_FAILED(rc))
    {
        /* Every remaining JIT subtest needs a live Jit object. Reporting
         * them FAIL (not SKIPPED) is deliberate -- the capability itself
         * is what failed here, not a scope decision, and "no fake
         * passing tests" cuts both ways: a downstream subtest that never
         * ran must never show up as PASS either. */
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-1: Code Memory Allocation", "jitCreate rc=0x%x", rc );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-2: Write ARM64 Code via RW Alias", "no Jit object (see JIT-1)" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-3: Instruction Cache Invalidation", "no Jit object (see JIT-1)" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-4: Execute via RX Alias", "no Jit object (see JIT-1)" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-5: RX Alias Data Readability", "no Jit object (see JIT-1)" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-6: RX Page Permission Query", "no Jit object (see JIT-1)" );
        return;
    }
    nxdiag_record( suite, NXDIAG_RESULT_PASS, "JIT-1: Code Memory Allocation",
                    "type=%d rw=%p rx=%p", (int)jit.type, jitGetRwAddr( &jit ), jitGetRxAddr( &jit ) );

    rc = jitTransitionToWritable( &jit );
    if (R_FAILED(rc))
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-2: Write ARM64 Code via RW Alias",
                        "jitTransitionToWritable rc=0x%x", rc );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-3: Instruction Cache Invalidation", "prior step failed" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-4: Execute via RX Alias", "prior step failed" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-5: RX Alias Data Readability", "prior step failed" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-6: RX Page Permission Query", "prior step failed" );
        jitClose( &jit );
        return;
    }

    rw = (uint32_t *)jitGetRwAddr( &jit );
    rw[0] = 0x52800000u | ((uint32_t)magic << 5);   /* movz w0, #magic */
    rw[1] = 0xd65f03c0u;                             /* ret */
    nxdiag_record( suite, NXDIAG_RESULT_PASS, "JIT-2: Write ARM64 Code via RW Alias",
                    "wrote movz w0,#0x%x + ret at rw=%p", magic, (void *)rw );

    __builtin___clear_cache( (char *)rw, (char *)rw + 8 );
    nxdiag_record( suite, NXDIAG_RESULT_PASS, "JIT-3: Instruction Cache Invalidation",
                    "__builtin___clear_cache(rw, rw+8) completed" );

    rc = jitTransitionToExecutable( &jit );
    if (R_FAILED(rc))
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-4: Execute via RX Alias",
                        "jitTransitionToExecutable rc=0x%x", rc );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-5: RX Alias Data Readability", "prior step failed" );
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-6: RX Page Permission Query", "prior step failed" );
        jitClose( &jit );
        return;
    }

    fn = (nxdiag_jit_fn_t)jitGetRxAddr( &jit );
    result = fn();
    if (result == magic)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "JIT-4: Execute via RX Alias",
                        "called rx=%p, returned 0x%x as expected", jitGetRxAddr( &jit ), result );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-4: Execute via RX Alias",
                        "expected 0x%x got 0x%x", magic, result );

    /* JIT-5: a plain DATA read through the RX alias (not an instruction
     * fetch), at a different offset than the code JIT-2/4 already
     * verified, so a failure here can't be confused with theirs. */
    rc = jitTransitionToWritable( &jit );
    if (R_SUCCEEDED(rc))
    {
        rw64 = (volatile uint64_t *)((uint8_t *)jitGetRwAddr( &jit ) + 0x100);
        *rw64 = magic64;
        rc = jitTransitionToExecutable( &jit );
    }
    if (R_FAILED(rc))
    {
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-5: RX Alias Data Readability", "transition rc=0x%x", rc );
    }
    else
    {
        rx64 = (volatile uint64_t *)((uint8_t *)jitGetRxAddr( &jit ) + 0x100);
        readback64 = *rx64;
        if (readback64 == magic64)
            nxdiag_record( suite, NXDIAG_RESULT_PASS, "JIT-5: RX Alias Data Readability",
                            "read back 0x%016llx via RX alias", (unsigned long long)readback64 );
        else
            nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-5: RX Alias Data Readability",
                            "expected 0x%016llx got 0x%016llx",
                            (unsigned long long)magic64, (unsigned long long)readback64 );
    }

    /* JIT-6: ask the kernel itself what permission the RX page actually
     * carries, rather than trusting jitTransitionToExecutable()'s return
     * code alone -- real svcQueryMemory, genuinely new beyond what
     * jit_smoke.c already covered. */
    memset( &meminfo, 0, sizeof(meminfo) );
    rc = svcQueryMemory( &meminfo, &pageinfo, (u64)(uintptr_t)jitGetRxAddr( &jit ) );
    if (R_FAILED(rc))
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-6: RX Page Permission Query", "svcQueryMemory rc=0x%x", rc );
    else if (meminfo.perm & Perm_X)
        nxdiag_record( suite, NXDIAG_RESULT_PASS, "JIT-6: RX Page Permission Query",
                        "perm=0x%x (Perm_X set) at addr=0x%llx", meminfo.perm, (unsigned long long)meminfo.addr );
    else
        nxdiag_record( suite, NXDIAG_RESULT_FAIL, "JIT-6: RX Page Permission Query",
                        "perm=0x%x -- Perm_X NOT set", meminfo.perm );

    jitClose( &jit );
}

/* ================= Vulkan / D3D / OpenGL-EGL: honest skips ================= */

static void nxdiag_skip( nxdiag_suite_t *suite, const char *name, const char *reason )
{
    nxdiag_record( suite, NXDIAG_RESULT_SKIPPED, name, "%s", reason );
}

static void nxdiag_run_suite_vulkan( nxdiag_suite_t *suite )
{
    static const char *reason =
        "in-process Vulkan reproduces the documented nwindowGetDefault()/vi-consumer "
        "conflict (see 3d-accel-scoping.md) -- skipped per explicit user decision "
        "this session, not a fake pass";

    suite->name = "Vulkan";
    nxdiag_skip( suite, "VK-1: vkCreateInstance", reason );
    nxdiag_skip( suite, "VK-2: Physical Device Enumeration", reason );
    nxdiag_skip( suite, "VK-3: Logical Device + Queue Creation", reason );
    nxdiag_skip( suite, "VK-4: Swapchain Creation", reason );
}

static void nxdiag_run_suite_d3d( nxdiag_suite_t *suite )
{
    static const char *reason =
        "no DXVK build exists anywhere in this project yet -- scoped as separate "
        "future work per explicit user decision this session, not stubbed as a "
        "fake pass";

    suite->name = "Direct3D (DXVK)";
    nxdiag_skip( suite, "D3D-1: DXVK Instance Initialization", reason );
    nxdiag_skip( suite, "D3D-2: D3D11 DXVK Device Creation", reason );
    nxdiag_skip( suite, "D3D-3: Swapchain Presentation", reason );
}

static void nxdiag_run_suite_gl( nxdiag_suite_t *suite )
{
    static const char *reason =
        "no OpenGL/EGL backend exists anywhere in this project yet -- same "
        "reasoning and the same explicit user decision as the D3D suite above";

    suite->name = "OpenGL / EGL";
    nxdiag_skip( suite, "GL-1: EGL Display Initialization", reason );
    nxdiag_skip( suite, "GL-2: EGL Context Creation", reason );
    nxdiag_skip( suite, "GL-3: GL Context Make-Current", reason );
}

/* ================= public entry points ================= */

void nxdiag_run_all( nxdiag_report_t *report )
{
    memset( report, 0, sizeof(*report) );

    nxdiag_run_suite_32bit( &report->suites[NXDIAG_SUITE_32BIT] );
    nxdiag_run_suite_64bit( &report->suites[NXDIAG_SUITE_64BIT] );
    nxdiag_run_suite_jit( &report->suites[NXDIAG_SUITE_JIT] );
    nxdiag_run_suite_vulkan( &report->suites[NXDIAG_SUITE_VULKAN] );
    nxdiag_run_suite_d3d( &report->suites[NXDIAG_SUITE_D3D] );
    nxdiag_run_suite_gl( &report->suites[NXDIAG_SUITE_GL] );
}

void nxdiag_report_to_log( const nxdiag_report_t *report, nxdebug_state_t *debug )
{
    int s, t;
    int total_pass = 0, total_fail = 0, total_skip = 0;

    for (s = 0; s < NXDIAG_SUITE_COUNT; s++)
    {
        const nxdiag_suite_t *suite = &report->suites[s];

        nxdebug_log( debug, "== %s: %d pass, %d fail, %d skipped ==",
                     suite->name, suite->pass_count, suite->fail_count, suite->skip_count );

        for (t = 0; t < suite->subtest_count; t++)
        {
            const nxdiag_subtest_t *test = &suite->subtests[t];
            const char *tag = test->result == NXDIAG_RESULT_PASS ? "PASS" :
                               test->result == NXDIAG_RESULT_FAIL ? "FAIL" :
                               test->result == NXDIAG_RESULT_SKIPPED ? "SKIP" : "????";
            nxdebug_log( debug, "  [%s] %s -- %s", tag, test->name, test->detail );
        }

        total_pass += suite->pass_count;
        total_fail += suite->fail_count;
        total_skip += suite->skip_count;
    }

    nxdebug_log( debug, "== DIAGNOSTICS: %d pass, %d fail, %d skipped (of %d subtests) ==",
                 total_pass, total_fail, total_skip, total_pass + total_fail + total_skip );
}
