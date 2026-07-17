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
 * code so a wrong answer can't be mistaken for a right one.
 *
 * Phase 1 (below) hardware-confirmed 8/8 pass. Phase 2 is the follow-up
 * that phase 1's own header comment flagged as still open: does rewriting
 * PART of a Jit buffer ever corrupt or crash execution of a DIFFERENT,
 * already-stable part of the SAME buffer running concurrently on another
 * thread -- the actual hazard a multi-threaded JIT like Box64's dynarec
 * would hit if it shared one cache region across threads. Phase 2 only
 * runs if phase 1 fully passed (testing concurrency first, before knowing
 * the basic mechanism even works, would just produce uninterpretable
 * results). Still not covered by either phase: sustained high-frequency
 * transitions under real memory pressure, and more than two threads.
 */
#include <switch.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

static FILE *g_log;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Mutex-guarded because phase 2 below calls this from two threads --
 * without the lock this would be a data race in the *test's own* logging
 * code, not a finding about the thing being tested. */
static void log_line( const char *fmt, ... )
{
    va_list args;
    pthread_mutex_lock( &g_log_mutex );
    if (g_log)
    {
        va_start( args, fmt );
        vfprintf( g_log, fmt, args );
        va_end( args );
        fputc( '\n', g_log );
        fflush( g_log );
    }
    pthread_mutex_unlock( &g_log_mutex );
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

/* ---------- Phase 2: concurrent same-buffer read-while-write stress test ----------
 *
 * Layout: one Jit buffer. STABLE_OFFSET holds a function written once
 * before either thread starts and never touched again. CHURN_OFFSET holds
 * a function the writer thread continuously overwrites with a fresh magic
 * value. The reader thread hammers the stable function's RX pointer (a
 * fixed, cached pointer, since rx_addr never changes after jitCreate --
 * confirmed from this project's own disassembly of libnx's jit.o) in a
 * tight loop for the whole test duration. The writer thread (main thread)
 * concurrently transitions the SAME Jit object writable, rewrites the
 * churn function, transitions back to executable, and verifies its own
 * write, in a tight loop for the same duration. If a rewrite of the churn
 * region ever corrupts or crashes the reader's execution of the stable
 * region, that's the exact hazard this phase exists to catch. */
#define PHASE2_STABLE_OFFSET 0
#define PHASE2_CHURN_OFFSET  0x800
#define PHASE2_DURATION_SEC  4

static Jit g_phase2_jit;
static volatile int g_phase2_stop;
static uint16_t g_phase2_stable_magic;

static pthread_mutex_t g_phase2_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static long g_phase2_reader_pass, g_phase2_reader_fail;
static long g_phase2_writer_pass, g_phase2_writer_fail;

static void *phase2_reader_thread( void *arg )
{
    jit_fn_t stable_fn = (jit_fn_t)((uint8_t *)jitGetRxAddr( &g_phase2_jit ) + PHASE2_STABLE_OFFSET);
    long local_pass = 0, local_fail = 0, iter = 0;

    (void)arg;

    while (!g_phase2_stop)
    {
        int result = stable_fn();
        iter++;
        if (result == g_phase2_stable_magic) local_pass++;
        else
        {
            local_fail++;
            log_line( "[JIT][P2][READER] iter=%ld MISMATCH expected=0x%04x got=0x%04x",
                     iter, g_phase2_stable_magic, result );
        }
        if ((iter % 50000) == 0)
            log_line( "[JIT][P2][READER] checkpoint iter=%ld pass=%ld fail=%ld", iter, local_pass, local_fail );
    }

    pthread_mutex_lock( &g_phase2_counter_mutex );
    g_phase2_reader_pass += local_pass;
    g_phase2_reader_fail += local_fail;
    pthread_mutex_unlock( &g_phase2_counter_mutex );

    log_line( "[JIT][P2][READER] done, total iter=%ld pass=%ld fail=%ld", iter, local_pass, local_fail );
    return NULL;
}

static void run_phase2( void )
{
    Result rc;
    pthread_t reader;
    uint32_t *rw;
    u64 start_ms, now_ms;
    long writer_iter = 0;

    log_line( "[JIT][P2] concurrent same-buffer stress test starting, duration=%ds", PHASE2_DURATION_SEC );
    printf( "\nPhase 2: concurrent access stress test (%ds)...\n", PHASE2_DURATION_SEC );
    consoleUpdate( NULL );

    rc = jitCreate( &g_phase2_jit, 0x1000 );
    if (R_FAILED(rc))
    {
        log_line( "[JIT][P2] jitCreate FAILED rc=0x%x -- SKIPPING phase 2", rc );
        printf( "Phase 2 jitCreate FAILED, skipping\n" );
        return;
    }
    log_line( "[JIT][P2] jitCreate OK type=%d rw_addr=%p rx_addr=%p",
             (int)g_phase2_jit.type, jitGetRwAddr( &g_phase2_jit ), jitGetRxAddr( &g_phase2_jit ) );

    /* Write the stable function once, before either thread starts, and
     * sanity-check it synchronously before introducing any concurrency at
     * all -- a failure here means phase 2's own setup is broken, not that
     * concurrent access is unsafe, and those need to stay distinguishable. */
    g_phase2_stable_magic = 0xABCD;
    rc = jitTransitionToWritable( &g_phase2_jit );
    if (R_FAILED(rc))
    {
        log_line( "[JIT][P2] initial jitTransitionToWritable FAILED rc=0x%x -- ABORTING phase 2", rc );
        jitClose( &g_phase2_jit );
        return;
    }
    rw = (uint32_t *)((uint8_t *)jitGetRwAddr( &g_phase2_jit ) + PHASE2_STABLE_OFFSET);
    encode_mov_w0_ret( rw, g_phase2_stable_magic );
    rc = jitTransitionToExecutable( &g_phase2_jit );
    if (R_FAILED(rc))
    {
        log_line( "[JIT][P2] initial jitTransitionToExecutable FAILED rc=0x%x -- ABORTING phase 2", rc );
        jitClose( &g_phase2_jit );
        return;
    }
    {
        jit_fn_t stable_fn = (jit_fn_t)((uint8_t *)jitGetRxAddr( &g_phase2_jit ) + PHASE2_STABLE_OFFSET);
        int r = stable_fn();
        if (r != g_phase2_stable_magic)
        {
            log_line( "[JIT][P2] pre-flight stable-function check FAILED expected=0x%04x got=0x%04x -- ABORTING phase 2",
                     g_phase2_stable_magic, r );
            jitClose( &g_phase2_jit );
            return;
        }
        log_line( "[JIT][P2] pre-flight stable-function check OK (0x%04x)", g_phase2_stable_magic );
    }

    g_phase2_stop = 0;
    if (pthread_create( &reader, NULL, phase2_reader_thread, NULL ) != 0)
    {
        log_line( "[JIT][P2] pthread_create FAILED -- ABORTING phase 2" );
        jitClose( &g_phase2_jit );
        return;
    }

    start_ms = armTicksToNs( armGetSystemTick() ) / 1000000ULL;

    for (;;)
    {
        uint16_t churn_magic;
        uint32_t *churn_rw;
        jit_fn_t churn_fn;
        int result;

        now_ms = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
        if (now_ms - start_ms >= (u64)PHASE2_DURATION_SEC * 1000) break;

        writer_iter++;
        churn_magic = (uint16_t)(0x5000 + (writer_iter & 0xfff));

        rc = jitTransitionToWritable( &g_phase2_jit );
        if (R_FAILED(rc))
        {
            pthread_mutex_lock( &g_phase2_counter_mutex );
            g_phase2_writer_fail++;
            pthread_mutex_unlock( &g_phase2_counter_mutex );
            log_line( "[JIT][P2][WRITER] iter=%ld jitTransitionToWritable FAILED rc=0x%x", writer_iter, rc );
            continue;
        }

        churn_rw = (uint32_t *)((uint8_t *)jitGetRwAddr( &g_phase2_jit ) + PHASE2_CHURN_OFFSET);
        encode_mov_w0_ret( churn_rw, churn_magic );

        rc = jitTransitionToExecutable( &g_phase2_jit );
        if (R_FAILED(rc))
        {
            pthread_mutex_lock( &g_phase2_counter_mutex );
            g_phase2_writer_fail++;
            pthread_mutex_unlock( &g_phase2_counter_mutex );
            log_line( "[JIT][P2][WRITER] iter=%ld jitTransitionToExecutable FAILED rc=0x%x", writer_iter, rc );
            continue;
        }

        churn_fn = (jit_fn_t)((uint8_t *)jitGetRxAddr( &g_phase2_jit ) + PHASE2_CHURN_OFFSET);
        result = churn_fn();

        pthread_mutex_lock( &g_phase2_counter_mutex );
        if (result == churn_magic) g_phase2_writer_pass++;
        else g_phase2_writer_fail++;
        pthread_mutex_unlock( &g_phase2_counter_mutex );

        if (result != churn_magic)
            log_line( "[JIT][P2][WRITER] iter=%ld MISMATCH expected=0x%04x got=0x%04x",
                     writer_iter, churn_magic, result );

        if ((writer_iter % 5000) == 0)
            log_line( "[JIT][P2][WRITER] checkpoint iter=%ld", writer_iter );
    }

    g_phase2_stop = 1;
    pthread_join( reader, NULL );

    jitClose( &g_phase2_jit );

    log_line( "[JIT][P2] summary: writer iter=%ld pass=%ld fail=%ld | reader pass=%ld fail=%ld -- %s",
             writer_iter, g_phase2_writer_pass, g_phase2_writer_fail,
             g_phase2_reader_pass, g_phase2_reader_fail,
             (g_phase2_writer_fail == 0 && g_phase2_reader_fail == 0) ? "ALL PASS" : "SEE FAILURES ABOVE" );
    printf( "Phase 2: writer %ld/%ld, reader %ld/%ld\n",
           g_phase2_writer_pass, g_phase2_writer_pass + g_phase2_writer_fail,
           g_phase2_reader_pass, g_phase2_reader_pass + g_phase2_reader_fail );
    printf( "%s\n", (g_phase2_writer_fail == 0 && g_phase2_reader_fail == 0) ? "ALL PASS" : "SEE jit_smoke.log FOR FAILURES" );
}

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

    if (fail == 0 && pass == rounds)
        run_phase2();
    else
        log_line( "[JIT] phase 1 did not fully pass -- SKIPPING phase 2 (would be uninterpretable)" );

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
