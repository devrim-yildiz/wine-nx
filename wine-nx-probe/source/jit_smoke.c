/*
 * Minimal, standalone smoke test for libnx's JIT (W^X-bypass) primitive --
 * jitCreate()/jitTransitionToWritable()/jitTransitionToExecutable()/
 * jitGetRwAddr()/jitGetRxAddr(). NOT a Box64 integration and doesn't
 * attempt one -- this answers one narrow, load-bearing question before any
 * Box64 scoping assumes the answer: does the write-via-RW-alias, execute-
 * via-RX-alias pattern actually work on real Horizon OS hardware.
 *
 * Standalone libnx homebrew, zero Wine/wineserver dependency -- deliberately
 * kept as small as possible to isolate this one primitive, same reasoning
 * as deko3d_smoke.c isolating the GPU compositor from the rest of the
 * runtime rather than testing it inside the full Wine stack.
 *
 * What this does and doesn't prove: confirms jitCreate() succeeds, reports
 * which JitType the kernel actually granted (JitType_SetProcessMemoryPermission
 * vs JitType_CodeMemory -- see libnx's jit.h; this project's own
 * disassembly of libnx's jit.o couldn't fully resolve from static analysis
 * alone whether transitions are cheap no-ops or real per-call remaps on
 * this specific mechanism, which is exactly why this needs to run on real
 * hardware rather than be reasoned about further from source), and runs
 * repeated write-then-execute cycles with a value baked into the generated
 * code so a wrong answer can't be mistaken for a right one. It does NOT
 * test concurrent access to the same Jit object from multiple threads
 * (a real question for a multi-threaded JIT like Box64's, left for a
 * follow-up test if this one passes) or sustained high-frequency
 * transitions under load.
 */
#include <switch.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

static FILE *g_log;

static void log_line( const char *fmt, ... )
{
    va_list args;
    if (!g_log) return;
    va_start( args, fmt );
    vfprintf( g_log, fmt, args );
    va_end( args );
    fputc( '\n', g_log );
    fflush( g_log );
}

/* AArch64: "movz w0, #imm16" (opc=10, sf=0, hw=00) followed by "ret". Chosen
 * over a NOP-sled or memcpy'd blob so the executed result can only match by
 * actually having executed freshly-written code through the RX alias --
 * not by luck, stale cache contents, or a partial write. */
static void encode_mov_w0_ret( uint32_t *buf, uint16_t imm16 )
{
    buf[0] = 0x52800000u | ((uint32_t)imm16 << 5);
    buf[1] = 0xd65f03c0u;
}

typedef int (*jit_fn_t)( void );

int main( int argc, char *argv[] )
{
    Jit jit;
    Result rc;
    PadState pad;
    int pass = 0, fail = 0, rounds = 8, i;

    (void)argc;
    (void)argv;

    consoleInit( NULL );
    g_log = fopen( "sdmc:/switch/jit_smoke.log", "w" );
    log_line( "[JIT] wine-nx JIT (W^X bypass) smoke test starting" );
    printf( "wine-nx JIT smoke test\n" );

    rc = jitCreate( &jit, 0x1000 );
    if (R_FAILED(rc))
    {
        log_line( "[JIT] jitCreate FAILED rc=0x%x", rc );
        printf( "jitCreate FAILED rc=0x%x\n", rc );
        goto wait_exit;
    }
    log_line( "[JIT] jitCreate OK type=%d size=0x%lx rw_addr=%p rx_addr=%p",
             (int)jit.type, (unsigned long)jit.size, jitGetRwAddr( &jit ), jitGetRxAddr( &jit ) );
    printf( "jitCreate OK\ntype=%d (0=SetProcessMemoryPermission, 1=CodeMemory)\n", (int)jit.type );
    consoleUpdate( NULL );

    for (i = 0; i < rounds; i++)
    {
        uint16_t magic = (uint16_t)(0x1234 + i);
        uint32_t *rw;
        jit_fn_t fn;
        int result;

        rc = jitTransitionToWritable( &jit );
        if (R_FAILED(rc))
        {
            log_line( "[JIT] round %d: jitTransitionToWritable FAILED rc=0x%x", i, rc );
            fail++;
            continue;
        }

        rw = (uint32_t *)jitGetRwAddr( &jit );
        encode_mov_w0_ret( rw, magic );

        rc = jitTransitionToExecutable( &jit );
        if (R_FAILED(rc))
        {
            log_line( "[JIT] round %d: jitTransitionToExecutable FAILED rc=0x%x", i, rc );
            fail++;
            continue;
        }

        fn = (jit_fn_t)jitGetRxAddr( &jit );
        result = fn();

        if (result == magic)
        {
            log_line( "[JIT] round %d: PASS wrote magic=0x%04x via RW, RX alias returned 0x%04x",
                     i, magic, result );
            pass++;
        }
        else
        {
            log_line( "[JIT] round %d: FAIL wrote magic=0x%04x via RW, RX alias returned 0x%04x (MISMATCH)",
                     i, magic, result );
            fail++;
        }
    }

    log_line( "[JIT] summary: %d pass, %d fail (of %d rounds) -- %s",
             pass, fail, rounds, (fail == 0 && pass == rounds) ? "ALL PASS" : "SEE FAILURES ABOVE" );
    printf( "\n%d/%d rounds passed\n", pass, rounds );
    printf( "%s\n", (fail == 0 && pass == rounds) ? "ALL PASS" : "SEE jit_smoke.log FOR FAILURES" );

    jitClose( &jit );
    log_line( "[JIT] jitClose done" );

wait_exit:
    printf( "\nPress + to exit\n" );
    consoleUpdate( NULL );

    padConfigureInput( 1, HidNpadStyleSet_NpadStandard );
    padInitializeDefault( &pad );

    while (appletMainLoop())
    {
        u64 kDown;

        padUpdate( &pad );
        kDown = padGetButtonsDown( &pad );
        if (kDown & HidNpadButton_Plus) break;

        consoleUpdate( NULL );
        svcSleepThread( 1000000000ULL / 30 );
    }

    if (g_log) fclose( g_log );
    consoleExit( NULL );
    return 0;
}
