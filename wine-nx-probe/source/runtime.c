#include <switch.h>

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/server.h"
#include "unix_private.h"
#include "horizon_private.h"

u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 256 * 1024 * 1024;
unsigned char __attribute__((aligned(16))) __nx_exception_stack[0x10000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
/* Run __libnx_exception_handler even when hbloader/Atmosphere has attached
 * as the debugger (which is always the case for NRO launches). Without this,
 * libnx's exception.s short-circuits to abort before calling our handler. */
u32 __nx_exception_ignoredebug = 1;

#define WINE_ROOT "sdmc:/switch/wine"
#define WINE_DRIVE_C WINE_ROOT "/drive_c"
#define WINE_SYSTEM_DIR WINE_DRIVE_C "/windows/system32"
#define RUNTIME_DIR WINE_ROOT
#define DEFAULT_TARGET WINE_DRIVE_C "/curl/curl.exe"
/* WINE_NX_DEKO3D_ONLY is a compile-time, per-target define (see
 * CMakeLists.txt's wine-nx-runtime-deko3d target) -- a completely separate
 * binary from wine-nx-runtime, not a runtime flag. It must never reference
 * nwindowGetDefault()/consoleInit()/Framebuffer, since libnx's own
 * __appInit() unconditionally runs __nx_win_init() before main() whenever
 * nwindowGetDefault is referenced anywhere in the binary (confirmed against
 * libnx's nx/source/runtime/init.c and nx/source/display/default_window.c),
 * which always wins the "first vi consumer" race against our own deko3d
 * init -- see wine-nx-probe/3d-accel-scoping.md for the full trace. This
 * build exists specifically to not contain that reference at all, so deko3d
 * really is the first and only vi consumer, matching every real reference
 * (Borealis, deko_basic) that actually works. */
#ifdef WINE_NX_DEKO3D_ONLY
#define WINE_NX_RUNTIME_BUILD "nx-deko3d-only-1"
#define WINE_NX_RUNTIME_LOG_NAME "/wine-nx-runtime-deko3d.log"
#else
#define WINE_NX_RUNTIME_BUILD "nx-present-throttle-1"
#define WINE_NX_RUNTIME_LOG_NAME "/wine-nx-runtime.log"
#endif
#define MAX_RUNTIME_MODULES 64
#define MAX_IMPORT_DEPTH 16

extern void wine_nx_runtime_platform_init(void);
extern void wine_nx_runtime_environment_init(void);
extern NTSTATUS wine_nx_loader_bootstrap( const UNICODE_STRING *main_nt_name );
extern NTSTATUS wine_nx_loader_fixup_main_imports(void);
extern NTSTATUS wine_nx_loader_attach_main(void);
extern const char *wine_nx_loader_last_import_dll(void);
extern NTSTATUS wine_nx_loader_last_import_status(void);
extern const char *wine_nx_loader_last_open_path(void);
extern NTSTATUS wine_nx_loader_last_open_status(void);
extern const char *wine_nx_loader_last_export_diag(void);

/* Deko3d compositor backend (runtime_deko3d.c) -- additive, opt-in via
 * WINE_NX_DEKO3D, see wine_nx_fb_backend_select() below. */
extern int wine_nx_deko3d_fb_init(void);
extern void *wine_nx_deko3d_fb_lock( int *width, int *height, int *stride_px );
extern void wine_nx_deko3d_fb_unlock(void);
extern void wine_nx_deko3d_fb_present(void);

static FILE *log_file;

struct runtime_module
{
    char path[512];
    char dir[512];
    char name[128];
    void *base;
    SIZE_T size;
    IMAGE_NT_HEADERS64 *nt;
    int is_main;
    int resolving_imports;
    int imports_scanned;
};

struct import_stats
{
    unsigned int dlls;
    unsigned int loaded_dlls;
    unsigned int missing_dlls;
    unsigned int imports;
    unsigned int bound;
    unsigned int unresolved;
    unsigned int forwarded;
};

static struct runtime_module modules[MAX_RUNTIME_MODULES];
static unsigned int module_count;

static pthread_t log_main_thread;
static int log_main_thread_set;

/* The text console and the Wine framebuffer both own the default nwindow, so
 * once a GUI app brings up the display driver we hand the screen over to the
 * framebuffer and stop driving the console (logs still go to the file). */
static int wine_nx_console_active = 1;
static Framebuffer wine_nx_fb;
static int wine_nx_fb_ready;
static int wine_nx_touch_ready;
static pthread_mutex_t wine_nx_fb_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *wine_nx_fb_pending_bits;
static int wine_nx_fb_pending_stride;
static int wine_nx_fb_pending_dirty;
static int wine_nx_fb_lock_depth;

static void log_line( const char *fmt, ... )
{
#ifndef WINE_NX_DEKO3D_ONLY
    /* The software console aborts the process (framebufferBegin →
     * diagAbortWithResult) when driven from any thread but the one that
     * called consoleInit, including exception handlers running on Wine
     * secondary threads.  Off the main thread, log to the file only. */
    int on_main = wine_nx_console_active &&
                  (!log_main_thread_set || pthread_equal( pthread_self(), log_main_thread ));
#endif
    va_list args;

#ifndef WINE_NX_DEKO3D_ONLY
    if (on_main)
    {
        va_start( args, fmt );
        vprintf( fmt, args );
        va_end( args );
        putchar( '\n' );
    }
#endif

    if (log_file)
    {
        va_start( args, fmt );
        vfprintf( log_file, fmt, args );
        va_end( args );
        fputc( '\n', log_file );
        fflush( log_file );
    }
#ifndef WINE_NX_DEKO3D_ONLY
    if (on_main) consoleUpdate( NULL );
#endif
}

void wine_nx_runtime_trace( const char *msg )
{
    log_line( "%s", msg );
}

/***********************************************************************
 * Framebuffer platform hooks used by the win32u Switch display driver
 * (dlls/win32u/winnx_drv.c).  The driver renders into ordinary DIB memory;
 * these present the dirty pixels to the libnx framebuffer.
 */
#define WINE_NX_FB_W 1280
#define WINE_NX_FB_H 720

/* Defined further down with the rest of the HUD/input state; started here so
 * button polling runs at its own fixed rate from the moment the framebuffer
 * comes up, instead of being tied to however often present() gets called. */
static void wine_nx_hud_ensure_input_thread(void);

/* Defined further down alongside the rest of the HUD state; not static --
 * runtime_deko3d.c calls these three directly so the deko3d present path
 * drives the exact same on-screen FPS overlay and rolling stats the libnx
 * path already does, instead of a second, drifted implementation. */
void wine_nx_hud_mark_attempt(void);
void wine_nx_hud_mark_executed( uint64_t present_ms );
void wine_nx_hud_draw( void *bits, int fb_w, int fb_h, int stride );

/* Defined further down (near target.txt/run-entry.txt handling); forward
 * declared here so backend selection can reuse the exact same file-based
 * config pattern instead of inventing a second one. */
static int read_bool_file( const char *path );
/* Defined alongside read_bool_file below; see the long comment there. */
static int wine_nx_resolve_bool_toggle( const char *env_name, const char *config_key,
                                        const char *legacy_path, const char **source_out );
static int wine_nx_config_get_bool( const char *key );

/* Backend selection: additive and opt-in, same spirit as WINE_NX_HOST_SIM
 * gating the win32u display driver choice in dlls/win32u/driver.c. Default
 * (WINE_NX_DEKO3D unset/false) is this file's existing libnx-framebuffer
 * path, completely untouched below -- the deko3d path lives entirely in
 * runtime_deko3d.c and is only reached via the dispatch checks at the top
 * of each of the four functions below.
 *
 * Checks an env var first, as asked -- but this runtime is launched as
 * Switch homebrew via hbmenu, not from a shell, and nothing else in this
 * project relies on getenv() ever seeing anything real in that launch path
 * (grep the tree: the only other getenv() call is inside the *hosted PE*
 * environment-variable emulation, a completely different, internal table,
 * not real process environment). Every other piece of "configure this
 * launch without a shell" in this project (target.txt, run-entry.txt) is a
 * file on the SD card instead, so WINE_ROOT/deko3d.txt is checked too --
 * without it, WINE_NX_DEKO3D would very likely be silently unreachable
 * from an actual hbmenu launch, defeating the entire point of an opt-in
 * switch. Whichever one (if either) actually engages, the choice made is
 * always logged explicitly (not just inferred from later log lines), so a
 * silent fallback to the default path never looks like deko3d engaged. */
static int wine_nx_fb_backend_checked;
static int wine_nx_fb_use_deko3d;

static void wine_nx_fb_backend_select(void)
{
    const char *source;
    if (wine_nx_fb_backend_checked) return;
    wine_nx_fb_backend_checked = 1;
    wine_nx_fb_use_deko3d = wine_nx_resolve_bool_toggle( "WINE_NX_DEKO3D", "deko3d",
                                                          RUNTIME_DIR "/deko3d.txt", &source );
    log_line( "[NXFB] backend: %s (source=%s)",
             wine_nx_fb_use_deko3d ? "deko3d" : "libnx framebuffer (default)", source );
}

/* Off by default. The raw per-syscall [SYSCALL] entry/exit trace in
 * dlls/ntdll/unix/signal_arm64.c is extremely high-frequency -- every NT
 * syscall logs two lines, and log_line() below fflush()es every line to
 * the SD card -- and was found to be the actual cause of the "2fps" gui
 * stalls: a single WM_PAINT dispatch containing 720 SetPixel calls alone
 * produced ~1440 synchronous SD-card writes. Non-static so signal_arm64.c
 * (a different translation unit, compiled into ntdll, not this runtime)
 * can read it directly with no per-syscall function-call overhead.
 * Same env-var-then-file-fallback pattern as wine_nx_fb_backend_select()
 * above, for the same hbmenu-has-no-real-environment reason. */
int wine_nx_syscall_trace_enabled;

static void wine_nx_syscall_trace_select(void)
{
    const char *source;
    wine_nx_syscall_trace_enabled = wine_nx_resolve_bool_toggle( "WINE_NX_SYSCALL_TRACE", "systrace",
                                                                  RUNTIME_DIR "/systrace.txt", &source );
    log_line( "[NXTRACE] raw syscall trace: %s (source=%s)",
             wine_nx_syscall_trace_enabled ? "ON" : "off (default)", source );
}

/* Off by default, same reasoning and same pattern as
 * wine_nx_syscall_trace_enabled above -- this one gates the
 * [NXPAINT][TIMING]/[NXPAINT][CALLER] tier added while chasing
 * paint_avg (dlls/win32u/dce.c's switch_paint_trace() call sites, plus
 * the trace_samples/fb_lock_call/fb_unlock_call additions in
 * dlls/win32u/winnx_drv.c). That investigation found the exact same
 * bug relocated one level down: every one of those trace calls' own
 * fflush() lands in the untimed gap between two phase timers, so the
 * tier meant to measure paint_avg's cost was itself becoming a
 * meaningful fraction of it -- up to ~14 fflushes per
 * window_surface_flush() call. See README, "Presentation Is Still Too
 * Slow". Non-static for the same cross-translation-unit reason as
 * wine_nx_syscall_trace_enabled. */
int wine_nx_paint_trace_enabled;

static void wine_nx_paint_trace_select(void)
{
    const char *source;
    wine_nx_paint_trace_enabled = wine_nx_resolve_bool_toggle( "WINE_NX_PAINT_TRACE", "painttrace",
                                                                RUNTIME_DIR "/painttrace.txt", &source );
    log_line( "[NXTRACE] paint sub-phase trace: %s (source=%s)",
             wine_nx_paint_trace_enabled ? "ON" : "off (default)", source );
}

/* Off by default -- default = current, real behavior; ON = the legacy
 * behavior flush_window_surfaces() (dlls/win32u/dce.c) had for its
 * entire life until the NtGetTickCount fix landed tonight, which made
 * this A/B-able for the first time.
 *
 * flush_window_surfaces()'s idle=TRUE path was fixed earlier tonight to
 * share the same 50ms debounce the idle=FALSE path always had, instead
 * of skipping the debounce and always flushing unconditionally. That fix
 * was correct on its own terms, but its actual behavior on hardware
 * depended entirely on NtGetTickCount() -- while that clock was frozen,
 * "now - last_flush" was always 0 < 50, so the debounce always skipped,
 * meaning this function's flush loop was *structurally incapable* of
 * running, ever. Fixing NtGetTickCount() (also tonight) made "now" a
 * real, advancing value for the first time, and since gui_smoke's paint
 * cycles are ~250ms apart -- well over 50ms -- the debounce now almost
 * never blocks, so the flush loop runs on nearly every frame. Neither
 * change caused this by itself; the two combined did, and only the
 * second one revealed it.
 *
 * This toggle exists to answer whether that's a net win or a net loss
 * without the ~240-470ms run-to-run noise this whole session has shown
 * between separate test launches: it forces flush_window_surfaces() to
 * unconditionally take the skip path every time, regardless of what the
 * (correct, real) clock says -- reproducing tonight's old, accidental
 * behavior exactly, in the same binary, so it can be A/B tested by
 * flipping one file and relaunching, not by comparing across builds. */
int wine_nx_flush_legacy_enabled;

static void wine_nx_flush_legacy_select(void)
{
    const char *source;
    wine_nx_flush_legacy_enabled = wine_nx_resolve_bool_toggle( "WINE_NX_FLUSH_LEGACY", "flushlegacy",
                                                                 RUNTIME_DIR "/flushlegacy.txt", &source );
    log_line( "[NXTRACE] flush_window_surfaces legacy always-skip mode: %s (source=%s)",
             wine_nx_flush_legacy_enabled ? "ON (idle=TRUE debounce forced to always skip, pre-clock-fix behavior)"
                                          : "off (default: real 50ms debounce, current behavior)", source );
}

/* Off by default -- this is a real protocol change (a new combined
 * get_paint_regions server request, dlls/ntdll/unix/horizon.c), not a
 * diagnostic, so wrong here means silent visual corruption, not a crash.
 *
 * NtUserBeginPaint always issues get_update_region followed by
 * get_visible_region, each its own ~14-15ms IPC round trip (see README,
 * "The ~14ms Per-Call IPC Floor"). horizon_server_handle_get_paint_regions
 * computes both under one lock pass with no cross-dependency between them,
 * so ON fetches both in a single round trip via
 * switch_prefetch_paint_regions() (dlls/win32u/dce.c) instead of two
 * separate calls -- saving one full round trip per paint cycle in the
 * common case (no WM_NCPAINT churn between the two fetches). When ON,
 * dce.c still validates the prefetch (same hwnd, matching DCX_WINDOW bit)
 * before using it, and falls back to the exact old sequential calls
 * whenever that validation fails -- so even with this toggle on, a mismatch
 * costs no more than OFF does; it just doesn't save the round trip that
 * particular time. This toggle exists to A/B the combined path against the
 * old sequential one in the same binary/session, the same way
 * WINE_NX_FLUSH_LEGACY does for flush_window_surfaces(). */
int wine_nx_batch_paint_regions_enabled;

static void wine_nx_batch_paint_regions_select(void)
{
    const char *source;
    wine_nx_batch_paint_regions_enabled = wine_nx_resolve_bool_toggle( "WINE_NX_BATCH_PAINT_REGIONS", "batchpaint",
                                                                        RUNTIME_DIR "/batchpaint.txt", &source );
    log_line( "[NXTRACE] combined get_paint_regions mode: %s (source=%s)",
             wine_nx_batch_paint_regions_enabled ? "ON (one combined IPC round trip for BeginPaint)"
                                                 : "off (default: separate get_update_region + get_visible_region calls)",
             source );
}

/* Off by default -- same correctness-risk-change reasoning as
 * wine_nx_batch_paint_regions_enabled above, applied to a different IPC
 * pair. UpdateWindow() -> update_now() (dlls/win32u/dce.c) always issues
 * get_update_region twice (once to find+dispatch WM_PAINT, once more to
 * check whether more painting is needed) even for a window with zero
 * children, where the second check is provably guaranteed to find nothing
 * -- confirmed via hardware logs (exactly 2 get_update_region calls per
 * get_paint_regions call, 129 frames, no variance) and via reading
 * horizon_server_find_window_update_locked's exact recursion (nothing to
 * iterate over with no children). ON routes RDW_UPDATENOW through
 * switch_update_now() instead of update_now(), which skips that second
 * round trip only when BOTH the window had zero children before the
 * WM_PAINT dispatch AND switch_window_tree_generation (window.c) proves
 * nothing created or reparented a window during it -- see the long design
 * comment on switch_update_now() for the full correctness argument. Any
 * window with real children, or any window-tree change during dispatch,
 * falls through to the exact same calls update_now() would have made --
 * so even with this toggle on, those cases cost no more than OFF does. */
int wine_nx_skip_redundant_update_check_enabled;

static void wine_nx_skip_redundant_update_check_select(void)
{
    const char *source;
    wine_nx_skip_redundant_update_check_enabled = wine_nx_resolve_bool_toggle(
        "WINE_NX_SKIP_REDUNDANT_UPDATE_CHECK", "skipupdatecheck", RUNTIME_DIR "/skipupdatecheck.txt", &source );
    log_line( "[NXTRACE] update_now redundant-check skip: %s (source=%s)",
             wine_nx_skip_redundant_update_check_enabled ? "ON (skips proven-empty follow-up get_update_region calls)"
                                                         : "off (default: update_now()'s original always-double-check behavior)",
             source );
}

/* Off by default -- see the long comment on FLUSH_PERIOD in
 * dlls/win32u/dibdrv/dc.c. Real Wine's dibdrv forces a surface flush
 * whenever a window has been continuously dirty for longer than
 * FLUSH_PERIOD; this port's actual flush cost measured far cheaper
 * (~7ms steady-state) than the original 50ms value suggests it should
 * need to be, once the fflush-contamination bugs found elsewhere this
 * session were fixed. ON tries a ~60Hz-matched 16ms period instead,
 * letting frames present sooner without (in theory) meaningfully raising
 * total CPU cost, since each flush is now cheap. Genuinely untested on
 * hardware -- this is a bet that presenting more often doesn't have
 * costs this session hasn't measured (GPU-side back-pressure, mutex
 * contention from more frequent present() calls). A/B this against the
 * default 50ms in one binary/session, same as every other toggle here. */
int wine_nx_fast_flush_period_enabled;

static void wine_nx_fast_flush_period_select(void)
{
    const char *source;
    wine_nx_fast_flush_period_enabled = wine_nx_resolve_bool_toggle( "WINE_NX_FAST_FLUSH_PERIOD", "fastflushperiod",
                                                                      RUNTIME_DIR "/fastflushperiod.txt", &source );
    log_line( "[NXTRACE] dibdrv FLUSH_PERIOD: %s (source=%s)",
             wine_nx_fast_flush_period_enabled ? "16ms (~60Hz-matched, EXPERIMENTAL, untested on hardware)"
                                               : "50ms (default: real Wine's original value)", source );
}

/* Off by default -- same correctness-risk-change reasoning as
 * wine_nx_batch_paint_regions_enabled above, applied to
 * switch_redraw_window_updatenow() (dlls/win32u/dce.c): merges
 * NtUserRedrawWindow's own redraw_window call with switch_update_now()'s
 * first get_update_flags_ex search into one round trip, for the specific
 * no-rect/RDW_UPDATENOW call shape UpdateWindow() actually uses. Only takes
 * effect when wine_nx_skip_redundant_update_check_enabled is also on, since
 * it depends on switch_update_now() existing. Genuinely untested on
 * hardware -- unlike the other toggles here, this one touches
 * NtUserRedrawWindow's own control flow (gated off by default, additive,
 * falls back to the exact original two-call path when off), not just a
 * diagnostic or a purely-additive protocol used from one call site. No
 * dedicated .txt file for this one -- config.txt only, per the "stop adding
 * one file per toggle" cleanup above. */
int wine_nx_batch_redraw_updatenow_enabled;

static void wine_nx_batch_redraw_updatenow_select(void)
{
    const char *env = getenv( "WINE_NX_BATCH_REDRAW_UPDATENOW" );
    if (env && env[0]) wine_nx_batch_redraw_updatenow_enabled = 1;
    else wine_nx_batch_redraw_updatenow_enabled = wine_nx_config_get_bool( "batchredrawupdatenow" );
    log_line( "[NXTRACE] combined redraw_window+get_update_flags_ex mode: %s (source=%s)",
             wine_nx_batch_redraw_updatenow_enabled ? "ON (one combined IPC round trip for UpdateWindow's first search)"
                                                    : "off (default: separate redraw_window + get_update_flags_ex calls)",
             (env && env[0]) ? "env" : (wine_nx_batch_redraw_updatenow_enabled ? "config.txt" : "default") );
}

#ifdef WINE_NX_DEKO3D_ONLY

/* This build's entire purpose is to never make this call -- deko3d must be
 * the first and only vi consumer, so there is no console to hand off from
 * and no libnx-framebuffer fallback to fall back to. A deko3d init failure
 * here is fatal to presentation for this binary; it fails cleanly and logs
 * why, rather than trying to limp along on a path this build deliberately
 * doesn't have. See the WINE_NX_DEKO3D_ONLY comment near the top of this
 * file for why that's the right tradeoff, not a regression from
 * wine-nx-runtime's safety-net fallback. */
int wine_nx_fb_init(void)
{
    if (wine_nx_deko3d_fb_init() == 0)
    {
        wine_nx_hud_ensure_input_thread();
        return 0;
    }
    log_line( "[NXFB] deko3d init FAILED -- this build has no fallback by design, presentation blocked" );
    return -1;
}

#else

/* Take the screen from the text console and bring up a linear framebuffer.
 * The console-exit step is shared by both backends -- deko3d's swapchain
 * needs the same nwindow handoff the libnx Framebuffer path already needed,
 * so it's hoisted above the backend dispatch rather than duplicated in
 * runtime_deko3d.c. wine_nx_console_active itself is the idempotency guard
 * (already 0 after the first call), so doing this check before the
 * existing wine_nx_fb_ready early-return doesn't change observable
 * behavior for the libnx path -- console-exit still only ever runs once. */
int wine_nx_fb_init(void)
{
    Result rc;
    wine_nx_fb_backend_select();
    if (wine_nx_console_active)
    {
        consoleExit( NULL );
        wine_nx_console_active = 0;
        /* consoleInit()'s default (non-GPU) renderer registers its own
         * buffers against nwindowGetDefault() -- per native_window.h,
         * "all buffers registered with a NWindow must have the same
         * dimensions, format and usage", and nwindowReleaseBuffers() is
         * the documented API for clearing that registration so a new
         * producer can configure the window fresh. The libnx Framebuffer
         * path below has always worked without this call because
         * framebufferCreate() apparently tolerates/renegotiates a stale
         * registration; dkSwapchainCreate() is not proven to, and this is
         * the first codepath to hand the post-console nwindow to deko3d,
         * so release explicitly rather than relying on that assumption. */
        nwindowReleaseBuffers( nwindowGetDefault() );
        log_line( "[NXFB] nwindow buffers released (console handoff)" );
    }
    if (wine_nx_fb_use_deko3d)
    {
        if (wine_nx_deko3d_fb_init() == 0) return 0;
        /* Safety net: any deko3d init failure, at any step, degrades to the
         * proven libnx path instead of leaving the app with a permanently
         * zeroed framebuffer -- confirmed on hardware to otherwise spin the
         * message loop uncontrolled with nothing to pace itself against
         * (no crash, no fallback, just a dead compositor). Flipping the
         * shared wine_nx_fb_use_deko3d flag here means every other dispatch
         * point (wine_nx_fb_lock/unlock/present) automatically follows suit
         * from now on -- they already just read this same flag, so nothing
         * else needs to change for the fallback to hold for the rest of the
         * run. This is unconditional: whatever the future root cause of a
         * deko3d failure turns out to be, this path must still degrade
         * gracefully rather than hang. */
        log_line( "[NXFB] deko3d init failed; falling back to libnx framebuffer path" );
        wine_nx_fb_use_deko3d = 0;
    }
    if (wine_nx_fb_ready) return 0;
    log_line( "[NXFB] fb_init: taking screen from console" );
    rc = framebufferCreate( &wine_nx_fb, nwindowGetDefault(),
                            WINE_NX_FB_W, WINE_NX_FB_H, PIXEL_FORMAT_RGBA_8888, 3 );
    if (R_FAILED( rc ))
    {
        log_line( "[NXFB] framebufferCreate FAILED rc=0x%x", rc );
        return -1;
    }
    framebufferMakeLinear( &wine_nx_fb );
    wine_nx_fb_ready = 1;
    wine_nx_hud_ensure_input_thread();
    log_line( "[NXFB] framebuffer ready %dx%d", WINE_NX_FB_W, WINE_NX_FB_H );
    return 0;
}

#endif /* WINE_NX_DEKO3D_ONLY */

#ifdef WINE_NX_DEKO3D_ONLY

/* wine_nx_deko3d_fb_lock() already does its own lazy wine_nx_deko3d_fb_init()
 * internally (see runtime_deko3d.c) and is the actual first call site that
 * reaches it in practice, so this just delegates straight through -- no
 * dual-backend decision to make in this build. wine_nx_hud_ensure_input_thread()
 * is idempotent (see its own guard), so calling it here on every successful
 * lock guarantees Minus-to-exit works regardless of whether wine_nx_fb_init()
 * ever actually runs on its own. */
void *wine_nx_fb_lock( int *width, int *height, int *stride_px )
{
    void *bits = wine_nx_deko3d_fb_lock( width, height, stride_px );
    if (bits) wine_nx_hud_ensure_input_thread();
    return bits;
}

void wine_nx_fb_unlock(void)
{
    wine_nx_deko3d_fb_unlock();
}

#else

/* Acquire the back buffer for writing; returns linear RGBA8888 pixels. */
void *wine_nx_fb_lock( int *width, int *height, int *stride_px )
{
    u32 stride = 0;
    void *bits = NULL;

    wine_nx_fb_backend_select();
    if (wine_nx_fb_use_deko3d)
    {
        void *deko3d_bits = wine_nx_deko3d_fb_lock( width, height, stride_px );
        if (deko3d_bits) return deko3d_bits;
        /* wine_nx_deko3d_fb_lock() only ever returns NULL when its own lazy
         * wine_nx_deko3d_fb_init() failed (confirmed by reading its body --
         * every other return path hands back a real, non-NULL staging
         * pointer). This is the actual first call site that triggers
         * deko3d init in practice (confirmed on hardware: the runtime never
         * reaches wine_nx_fb_init() before this), so the same safety-net
         * fallback lives here too, not just in wine_nx_fb_init(). Falling
         * through to wine_nx_fb_init() below now runs it with
         * wine_nx_fb_use_deko3d already cleared, so it takes the libnx
         * path directly instead of re-attempting and re-failing deko3d. */
        log_line( "[NXFB] deko3d fb_lock failed (init never succeeded); falling back to libnx framebuffer path" );
        wine_nx_fb_use_deko3d = 0;
    }

    if (!wine_nx_fb_ready && wine_nx_fb_init()) return NULL;
    pthread_mutex_lock( &wine_nx_fb_mutex );
    if (!wine_nx_fb_pending_bits)
    {
        wine_nx_fb_pending_bits = framebufferBegin( &wine_nx_fb, &stride );
        wine_nx_fb_pending_stride = (int)(stride / 4);
    }
    bits = wine_nx_fb_pending_bits;
    if (!bits)
    {
        pthread_mutex_unlock( &wine_nx_fb_mutex );
        return NULL;
    }
    wine_nx_fb_lock_depth++;
    if (width)     *width     = WINE_NX_FB_W;
    if (height)    *height    = WINE_NX_FB_H;
    if (stride_px) *stride_px = wine_nx_fb_pending_stride;
    return bits;
}

void wine_nx_fb_unlock(void)
{
    if (wine_nx_fb_use_deko3d) { wine_nx_deko3d_fb_unlock(); return; }

    wine_nx_fb_pending_dirty = 1;
    if (wine_nx_fb_lock_depth > 0) wine_nx_fb_lock_depth--;
    pthread_mutex_unlock( &wine_nx_fb_mutex );
}

#endif /* WINE_NX_DEKO3D_ONLY */

/***********************************************************************
 * Debug HUD: a tiny native-side overlay proving the present-rate throttle
 * (below) is actually doing something, without needing any bridge back to
 * PE/Wine code. It draws directly into the same linear framebuffer the app
 * already rendered into, right before framebufferEnd() submits it -- no
 * new syscalls, no shared memory tricks, just raw pixels.
 *
 * "attempted" counts every present() call that had real dirty content
 * ready to show; "executed" counts only the ones the throttle actually let
 * through. Both are latched once per second into *_display alongside a
 * live FPS reading (== executed/sec, since that's the real on-screen
 * update rate). If the app hammers present() far faster than 60/sec,
 * attempted should visibly run away from executed -- that gap *is* the
 * throttle working.
 *
 * Toggled with the controller's Plus button; Minus exits to the homebrew
 * menu. First hardware test found two real bugs here, both fixed below:
 * Plus/Minus needed several presses (polling lived inline in present(),
 * so a fast tap could land entirely between two poll calls on a slow
 * render loop and never be seen as an edge -- see the dedicated input
 * thread), and Minus crashed instead of exiting cleanly (it could call
 * framebufferClose() while a framebufferBegin() was still outstanding,
 * which is undefined/fatal -- the exit path now takes wine_nx_fb_mutex and
 * balances any outstanding Begin with an End first). Not yet re-verified
 * on hardware after this fix. If Minus still doesn't return to hbmenu, the
 * physical HOME button always will.
 */
#define WINE_NX_HUD_SCALE 4  /* each 3x5 glyph cell -> 3*4 x 5*4 screen px */

/* 3x5 bitmap font, 5 rows per glyph, low 3 bits per row (bit2=left pixel).
 * Deliberately minimal: only the characters the HUD strings below use. */
static unsigned char wine_nx_hud_glyph( char c, int row )
{
    static const unsigned char rows[][5] = {
        /*0*/ {0x7,0x5,0x5,0x5,0x7}, /*1*/ {0x2,0x6,0x2,0x2,0x7},
        /*2*/ {0x7,0x1,0x7,0x4,0x7}, /*3*/ {0x7,0x1,0x7,0x1,0x7},
        /*4*/ {0x5,0x5,0x7,0x1,0x1}, /*5*/ {0x7,0x4,0x7,0x1,0x7},
        /*6*/ {0x7,0x4,0x7,0x5,0x7}, /*7*/ {0x7,0x1,0x1,0x1,0x1},
        /*8*/ {0x7,0x5,0x7,0x5,0x7}, /*9*/ {0x7,0x5,0x7,0x1,0x7},
        /*F*/ {0x7,0x4,0x7,0x4,0x4}, /*P*/ {0x7,0x5,0x7,0x4,0x4},
        /*S*/ {0x7,0x4,0x7,0x1,0x7}, /*R*/ {0x7,0x5,0x7,0x6,0x5},
        /*E*/ {0x7,0x4,0x7,0x4,0x7}, /*A*/ {0x2,0x5,0x7,0x5,0x5},
        /*T*/ {0x7,0x2,0x2,0x2,0x2}, /*X*/ {0x5,0x5,0x2,0x5,0x5},
        /*I*/ {0x7,0x2,0x2,0x2,0x7}, /*:*/ {0x0,0x2,0x0,0x2,0x0},
        /*[*/ {0x6,0x4,0x4,0x4,0x6}, /*]*/ {0x3,0x1,0x1,0x1,0x3},
        /*+*/ {0x0,0x2,0x7,0x2,0x0}, /*-*/ {0x0,0x0,0x7,0x0,0x0},
        /*V*/ {0x5,0x5,0x5,0x5,0x2}, /*G*/ {0x7,0x4,0x5,0x5,0x7},
        /*M*/ {0x5,0x7,0x5,0x5,0x5}, /*N*/ {0x5,0x5,0x7,0x5,0x5},
        /* */ {0x0,0x0,0x0,0x0,0x0},
    };
    static const char chars[] = "0123456789FPSREATXI:[]+-VGMN ";
    const char *p = strchr( chars, c );
    if (!p || row < 0 || row > 4) return 0;
    return rows[p - chars][row];
}

static void wine_nx_hud_draw_text( uint32_t *fb, int fb_w, int fb_h, int stride,
                                   int x0, int y0, const char *text, uint32_t color )
{
    int x = x0;

    for (; *text; text++, x += 4 * WINE_NX_HUD_SCALE)
    {
        int row, col;
        for (row = 0; row < 5; row++)
        {
            unsigned char bits = wine_nx_hud_glyph( *text, row );
            for (col = 0; col < 3; col++)
            {
                int px, py;
                if (!(bits & (1 << (2 - col)))) continue;
                for (py = 0; py < WINE_NX_HUD_SCALE; py++)
                for (px = 0; px < WINE_NX_HUD_SCALE; px++)
                {
                    int dx = x + col * WINE_NX_HUD_SCALE + px;
                    int dy = y0 + row * WINE_NX_HUD_SCALE + py;
                    if (dx >= 0 && dx < fb_w && dy >= 0 && dy < fb_h) fb[dy * stride + dx] = color;
                }
            }
        }
    }
}

#define WINE_NX_HUD_FPS_WINDOW 8  /* ~8s of 1s buckets for the rolling avg/min/max */

static unsigned int wine_nx_hud_attempt_count, wine_nx_hud_executed_count;
static unsigned int wine_nx_hud_attempt_display, wine_nx_hud_executed_display, wine_nx_hud_fps_display;
static unsigned int wine_nx_hud_fps_history[WINE_NX_HUD_FPS_WINDOW];
static unsigned int wine_nx_hud_fps_history_count, wine_nx_hud_fps_history_pos;
static unsigned int wine_nx_hud_fps_avg_display, wine_nx_hud_fps_min_display, wine_nx_hud_fps_max_display;
/* Wall-clock cost of the framebufferEnd() call itself (the actual
 * linear->block-linear conversion + queue), separate from GDI paint cost or
 * loop overhead -- added specifically to answer "is the ~60fps floor from
 * the throttle below, or is each present() call itself taking far longer
 * than that?" without guessing. */
static uint64_t wine_nx_hud_present_ms_min = UINT64_MAX, wine_nx_hud_present_ms_max;
static uint64_t wine_nx_hud_present_ms_min_display, wine_nx_hud_present_ms_max_display;
static uint64_t wine_nx_hud_epoch_ms;
static int wine_nx_hud_visible = 1;
static PadState wine_nx_pad;

/* Dedicated polling thread: Plus toggles the HUD, Minus exits to hbmenu.
 * Runs at its own fixed ~30Hz rate independent of the present/render loop --
 * button polling used to happen inline in present(), so a slow render loop
 * (as turned out to be the case, see the present()-timing comment below)
 * meant a fast tap could land entirely between two poll calls and never be
 * seen as an edge at all. This thread can't miss a press for that reason. */
static void *wine_nx_hud_input_thread_fn( void *arg )
{
    (void)arg;
    padConfigureInput( 1, HidNpadStyleSet_NpadStandard );
    padInitializeDefault( &wine_nx_pad );

    for (;;)
    {
        u64 down;

        padUpdate( &wine_nx_pad );
        down = padGetButtonsDown( &wine_nx_pad );

        if (down & HidNpadButton_Plus)
        {
            wine_nx_hud_visible = !wine_nx_hud_visible;
            wine_nx_runtime_trace( wine_nx_hud_visible ? "[HUD] overlay ON" : "[HUD] overlay OFF" );
        }
        if (down & HidNpadButton_Minus)
        {
            /* Documented bug tonight: this is supposed to exit cleanly to
             * hbmenu and instead crashes to the system Home Menu. Every step
             * below now logs individually (log_line() fflush()es every
             * line, so whatever's written here survives even if the very
             * next line is what actually kills the process) so the next
             * hardware test shows exactly which step is last reached,
             * without needing to reproduce it interactively first.
             *
             * Also trying a real, but unverified-without-hardware, change:
             * calling svcExitProcess() directly instead of libc's exit(0).
             * This runs on wine_nx_hud_input_thread_fn, a background
             * pthread, not the main thread -- if libc's exit() (which runs
             * atexit handlers and static destructors) touches deko3d/GPU or
             * socket state concurrently with whatever the main thread is
             * doing at that exact moment, that's a plausible source of a
             * hard crash rather than a clean exit. svcExitProcess() is the
             * raw Horizon kernel primitive other homebrew reference
             * examples (deko_basic included, per the existing comment
             * below) rely on for process teardown -- it skips libc's
             * atexit/cleanup path entirely rather than racing it. If the
             * log's last line is still "calling svcExitProcess" and it
             * still crashes, that rules this theory out cleanly; if it
             * exits clean, that's the fix. */
            wine_nx_runtime_trace( "[HUD] Minus pressed; exiting to hbmenu" );
#ifndef WINE_NX_DEKO3D_ONLY
            /* Must hold the same mutex present()/lock() use: framebufferClose()
             * while a framebufferBegin() is still outstanding (no matching End)
             * is undefined/fatal, and this thread has no other way to know
             * whether the render thread is mid-frame right now. Balance any
             * outstanding Begin with an End first, then Close while still
             * holding the lock so no new Begin can start underneath us. */
            wine_nx_runtime_trace( "[EXIT] libnx build: locking wine_nx_fb_mutex to balance framebuffer" );
            pthread_mutex_lock( &wine_nx_fb_mutex );
            if (wine_nx_fb_ready)
            {
                if (wine_nx_fb_pending_bits)
                {
                    wine_nx_runtime_trace( "[EXIT] calling framebufferEnd() to balance outstanding Begin" );
                    framebufferEnd( &wine_nx_fb );
                    wine_nx_runtime_trace( "[EXIT] framebufferEnd() returned" );
                }
                wine_nx_runtime_trace( "[EXIT] calling framebufferClose()" );
                framebufferClose( &wine_nx_fb );
                wine_nx_runtime_trace( "[EXIT] framebufferClose() returned" );
                wine_nx_fb_ready = 0;
            }
            pthread_mutex_unlock( &wine_nx_fb_mutex );
            wine_nx_runtime_trace( "[EXIT] libnx framebuffer balanced, mutex released" );
#endif
            /* deko3d-only build: no libnx Framebuffer to balance/close, and no
             * reference example (deko_basic included) bothers with explicit
             * deko3d teardown before a plain process exit either -- Horizon
             * reclaims the device/queue/memory on process exit regardless. */
            wine_nx_runtime_trace( "[EXIT] calling socketExit()" );
            socketExit();
            wine_nx_runtime_trace( "[EXIT] socketExit() returned" );
            wine_nx_runtime_trace( "[EXIT] calling svcExitProcess() (was: libc exit(0)) -- if this is the last "
                                   "line you see, the crash is inside kernel-level process teardown itself, "
                                   "not libc's atexit/cleanup path" );
            svcExitProcess();
            /* Unreachable -- svcExitProcess() does not return. Logged only
             * in case it somehow does, since that would itself be worth
             * knowing about. */
            wine_nx_runtime_trace( "[EXIT] svcExitProcess() returned (unexpected!) -- falling back to exit(0)" );
            exit( 0 );
        }

        svcSleepThread( 33000000LL );   /* ~33ms, ~30Hz -- plenty for a button edge */
    }
    return NULL;
}

static pthread_t wine_nx_hud_input_thread;
static int wine_nx_hud_input_thread_started;

static void wine_nx_hud_ensure_input_thread(void)
{
    if (wine_nx_hud_input_thread_started) return;
    wine_nx_hud_input_thread_started = 1;
    pthread_create( &wine_nx_hud_input_thread, NULL, wine_nx_hud_input_thread_fn, NULL );
}

/* Shared by both backends -- extracted out of wine_nx_fb_present()'s libnx
 * body so the deko3d present path can drive the exact same rolling-window
 * FPS/min/max stats wine_nx_hud_draw() displays, instead of a second,
 * drifted copy of this bucketing logic. Split into two calls (not one) to
 * preserve the original semantics exactly: the libnx path calls _attempt()
 * on every present() call with dirty bits pending (even ones the present-
 * rate throttle below skips), and only calls _executed() for the ones that
 * actually ran framebufferEnd() -- that attempted/executed gap is what the
 * on-screen "PRES A:/E:" line and the README's throttle investigation are
 * about. The deko3d path has no such throttle, so it calls both for every
 * present that actually submits a frame -- attempted == executed there,
 * which is an honest reflection of its behavior, not a workaround. */
void wine_nx_hud_mark_attempt(void)
{
    uint64_t now_ms = armTicksToNs( armGetSystemTick() ) / 1000000ULL;

    wine_nx_hud_attempt_count++;
    if (!wine_nx_hud_epoch_ms) wine_nx_hud_epoch_ms = now_ms;
    if (now_ms - wine_nx_hud_epoch_ms >= 1000)
    {
        unsigned int i, sum, mn, mx;

        wine_nx_hud_attempt_display = wine_nx_hud_attempt_count;
        wine_nx_hud_executed_display = wine_nx_hud_executed_count;
        wine_nx_hud_fps_display = wine_nx_hud_executed_count;

        wine_nx_hud_fps_history[wine_nx_hud_fps_history_pos] = wine_nx_hud_executed_count;
        wine_nx_hud_fps_history_pos = (wine_nx_hud_fps_history_pos + 1) % WINE_NX_HUD_FPS_WINDOW;
        if (wine_nx_hud_fps_history_count < WINE_NX_HUD_FPS_WINDOW) wine_nx_hud_fps_history_count++;

        sum = 0;
        mn = mx = wine_nx_hud_fps_history[0];
        for (i = 0; i < wine_nx_hud_fps_history_count; i++)
        {
            unsigned int v = wine_nx_hud_fps_history[i];
            sum += v;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        wine_nx_hud_fps_avg_display = sum / wine_nx_hud_fps_history_count;
        wine_nx_hud_fps_min_display = mn;
        wine_nx_hud_fps_max_display = mx;

        wine_nx_hud_present_ms_min_display = wine_nx_hud_executed_count ? wine_nx_hud_present_ms_min : 0;
        wine_nx_hud_present_ms_max_display = wine_nx_hud_present_ms_max;

        wine_nx_hud_attempt_count = 0;
        wine_nx_hud_executed_count = 0;
        wine_nx_hud_present_ms_min = UINT64_MAX;
        wine_nx_hud_present_ms_max = 0;
        wine_nx_hud_epoch_ms = now_ms;
    }
}

void wine_nx_hud_mark_executed( uint64_t present_ms )
{
    wine_nx_hud_executed_count++;
    if (present_ms < wine_nx_hud_present_ms_min) wine_nx_hud_present_ms_min = present_ms;
    if (present_ms > wine_nx_hud_present_ms_max) wine_nx_hud_present_ms_max = present_ms;
}

void wine_nx_hud_draw( void *bits, int fb_w, int fb_h, int stride )
{
    char line[64];

    if (!wine_nx_hud_visible) return;
    /* Framebuffer byte order is R,G,B,A in memory (see wine_nx_surface_flush()
     * in dlls/win32u/winnx_drv.c), so as a native uint32_t these constants are
     * 0xAABBGGRR, not the more intuitive 0xAARRGGBB. */
    snprintf( line, sizeof(line), "FPS:%u", wine_nx_hud_fps_display );
    wine_nx_hud_draw_text( bits, fb_w, fb_h, stride, 24, 24, line, 0xffd4ea5eu ); /* RGB(94,234,212) */

    snprintf( line, sizeof(line), "AVG:%u MIN:%u MAX:%u", wine_nx_hud_fps_avg_display,
              wine_nx_hud_fps_min_display, wine_nx_hud_fps_max_display );
    wine_nx_hud_draw_text( bits, fb_w, fb_h, stride, 24, 24 + 7 * WINE_NX_HUD_SCALE, line, 0xffd4ea5eu );

    snprintf( line, sizeof(line), "PRES A:%u E:%u", wine_nx_hud_attempt_display, wine_nx_hud_executed_display );
    wine_nx_hud_draw_text( bits, fb_w, fb_h, stride, 24, 24 + 14 * WINE_NX_HUD_SCALE, line, 0xffffffffu ); /* white */

    snprintf( line, sizeof(line), "PMS MIN:%u MAX:%u", (unsigned)wine_nx_hud_present_ms_min_display,
              (unsigned)wine_nx_hud_present_ms_max_display );
    wine_nx_hud_draw_text( bits, fb_w, fb_h, stride, 24, 24 + 21 * WINE_NX_HUD_SCALE, line, 0xffffffffu );

    wine_nx_hud_draw_text( bits, fb_w, fb_h, stride, 24, 24 + 28 * WINE_NX_HUD_SCALE,
                           "[+]STATS [-]EXIT", 0xffffdbc4u ); /* RGB(196,219,255) */
}

/* framebufferEnd() converts the *entire* 1280x720 shadow buffer to
 * block-linear before queueing it, regardless of how small the actual dirty
 * region was (see wine-nx-probe/README.md, "Presentation Is Still Too Slow").
 * wine_nx_drv_ProcessEvents() already calls present() on every message-loop
 * iteration, which can be far more often than any display can show distinct
 * frames -- each of those extra calls pays the full conversion cost for
 * zero visible benefit once you're already at the display's refresh rate.
 * Capping how often the real conversion actually runs is a strict win, not a
 * quality/latency trade: nothing above the display's refresh rate is ever
 * visible, so skipped presents here aren't lost -- they just get folded into
 * the next one that's actually due (pending_dirty stays set), the same way
 * the existing batching already coalesces multiple flush() calls into one
 * present.
 *
 * First hardware numbers (2fps, identical before/after a CPU overclock) rule
 * out this 16ms/~60fps floor as the cause by themselves: armGetSystemTick()
 * is the ARM generic timer, a fixed-frequency reference clock that is
 * deliberately independent of core clock speed (required so wall-clock time
 * stays correct under CPU overclocking), so this comparison would behave
 * identically at any CPU clock regardless of whether the 16ms threshold
 * itself were the bottleneck. That rules out "the threshold math reads a
 * clock the overclock affects" but not "something takes longer than 16ms
 * every time, so the threshold never actually binds" -- CPU-clock-invariant
 * results are equally consistent with framebufferEnd()'s own conversion
 * cost being the real, non-CPU-bound floor (e.g. a fixed-function/DMA path,
 * matching this project's own pre-existing "Presentation Is Still Too Slow"
 * note above) as with a bug elsewhere in the loop. wine_nx_hud_present_ms_*
 * below measures the actual wall time inside framebufferEnd() itself, so the
 * next hardware run gives a real answer instead of another guess: if PMS
 * MIN/MAX is itself close to 500ms, framebufferEnd() is the bottleneck; if
 * it's small (close to the 16ms floor) while FPS is still ~2, the loop is
 * stalling somewhere else (GDI paint cost, Sleep(), message dispatch) and
 * that's where to look next. */
#define WINE_NX_FB_MIN_PRESENT_INTERVAL_MS 16  /* ~60 Hz cap; adjust to taste */

static uint64_t wine_nx_fb_last_present_ms;

#ifdef WINE_NX_DEKO3D_ONLY

void wine_nx_fb_present(void)
{
    wine_nx_deko3d_fb_present();
}

#else

void wine_nx_fb_present(void)
{
    uint64_t now_ms;

    if (wine_nx_fb_use_deko3d) { wine_nx_deko3d_fb_present(); return; }

    pthread_mutex_lock( &wine_nx_fb_mutex );
    if (wine_nx_fb_ready && wine_nx_fb_pending_bits && wine_nx_fb_pending_dirty && !wine_nx_fb_lock_depth)
    {
        wine_nx_hud_mark_attempt();
        now_ms = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
        if (now_ms - wine_nx_fb_last_present_ms >= WINE_NX_FB_MIN_PRESENT_INTERVAL_MS)
        {
            uint64_t t0, t1;

            wine_nx_hud_draw( wine_nx_fb_pending_bits, WINE_NX_FB_W, WINE_NX_FB_H, wine_nx_fb_pending_stride );
            t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
            framebufferEnd( &wine_nx_fb );
            t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
            wine_nx_hud_mark_executed( t1 - t0 );
            wine_nx_fb_pending_bits = NULL;
            wine_nx_fb_pending_stride = 0;
            wine_nx_fb_pending_dirty = 0;
            wine_nx_fb_last_present_ms = now_ms;
        }
        /* else: leave pending_dirty set. The next call -- and there will be
         * one, since ProcessEvents() calls present() on every message-loop
         * iteration -- picks up these writes once the interval has passed. */
    }
    pthread_mutex_unlock( &wine_nx_fb_mutex );
}

#endif /* WINE_NX_DEKO3D_ONLY */

/* Return the primary touchscreen contact in native 1280x720 display
 * coordinates.  Win32u turns it into the conventional mouse stream expected
 * by desktop applications; native WM_TOUCH can be layered on later. */
int wine_nx_touch_poll( int *x, int *y )
{
    HidTouchScreenState state = {0};

    if (!wine_nx_touch_ready)
    {
        hidInitializeTouchScreen();
        wine_nx_touch_ready = 1;
        log_line( "[NXINPUT] touchscreen ready" );
    }

    if (!hidGetTouchScreenStates( &state, 1 ) || state.count <= 0) return 0;
    if (x) *x = state.touches[0].x;
    if (y) *y = state.touches[0].y;
    return 1;
}

static int call_pe_entry_point( void *entry )
{
    extern void wine_nx_set_active_pe_teb( TEB *teb );
    uintptr_t ret;
    uintptr_t teb = (uintptr_t)NtCurrentTeb();

    wine_nx_set_active_pe_teb( (TEB *)teb );
    __asm__ volatile(
        "mov x16, %[entry]\n\t"
        "mov x17, %[teb]\n\t"
        "mov x20, x18\n\t"
        "mov x18, x17\n\t"
        "blr x16\n\t"
        "mov x18, x20\n\t"
        "mov %[ret], x0\n\t"
        : [ret] "=r"(ret)
        : [entry] "r"(entry), [teb] "r"(teb)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
          "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x20",
          "x30", "memory", "cc" );

    return (int)ret;
}

static void park_forever(void)
{
    log_line( "[EXIT] parked after runtime handoff; close from HOME" );
    for (;;) svcSleepThread( 1000000000LL );
}

static void trim_line( char *line )
{
    size_t len = strlen( line );

    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                   line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = 0;
}

static int read_first_line( const char *path, char *line, size_t size )
{
    FILE *file = fopen( path, "r" );

    if (!file) return 0;
    if (!fgets( line, size, file ))
    {
        fclose( file );
        return 0;
    }
    fclose( file );
    trim_line( line );
    return line[0] != 0;
}

static int read_bool_file( const char *path )
{
    char line[32];

    if (!read_first_line( path, line, sizeof(line) )) return 0;
    return !strcmp( line, "1" ) || !strcasecmp( line, "true" ) ||
           !strcasecmp( line, "yes" ) || !strcasecmp( line, "run" );
}

/* One consolidated config file (sdmc:/switch/wine/config.txt) instead of a
 * separate standalone .txt per toggle -- every new A/B flag this project
 * has added has meant one more file on the SD card to remember, sync, and
 * keep straight; this puts all of them (and args.txt/target.txt/
 * run-entry.txt's data) in one place going forward. Format: one KEY=VALUE
 * per line, or a bare KEY (implies true); '#' starts a comment; blank lines
 * ignored. Case-insensitive keys. Still falls back to the original
 * standalone-file convention per key (lowest priority, after env var and
 * this file) so nothing already on anyone's SD card breaks -- but every
 * *new* toggle from here on should only ever gain a config.txt key, never
 * another dedicated file. */
#define WINE_NX_CONFIG_MAX_ENTRIES 32
static struct
{
    char key[32];
    char value[64];
} wine_nx_config_entries[WINE_NX_CONFIG_MAX_ENTRIES];
static int wine_nx_config_entry_count;
static int wine_nx_config_loaded;

static void wine_nx_config_load(void)
{
    FILE *file;
    char line[128];

    if (wine_nx_config_loaded) return;
    wine_nx_config_loaded = 1;

    if (!(file = fopen( RUNTIME_DIR "/config.txt", "r" ))) return;

    while (fgets( line, sizeof(line), file ) && wine_nx_config_entry_count < WINE_NX_CONFIG_MAX_ENTRIES)
    {
        char *key, *value, *eq, *end;

        trim_line( line );
        key = line;
        while (*key == ' ' || *key == '\t') key++;
        if (!key[0] || key[0] == '#') continue;

        if ((eq = strchr( key, '=' )))
        {
            *eq = 0;
            value = eq + 1;
            while (*value == ' ' || *value == '\t') value++;
        }
        else value = key + strlen( key );  /* no '=' -> bare key, empty value (implies true) */

        end = key + strlen( key );
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        if (!key[0]) continue;

        snprintf( wine_nx_config_entries[wine_nx_config_entry_count].key,
                 sizeof(wine_nx_config_entries[0].key), "%s", key );
        snprintf( wine_nx_config_entries[wine_nx_config_entry_count].value,
                 sizeof(wine_nx_config_entries[0].value), "%s", value );
        wine_nx_config_entry_count++;
    }
    fclose( file );
}

static const char *wine_nx_config_get( const char *key )
{
    int i;

    wine_nx_config_load();
    for (i = 0; i < wine_nx_config_entry_count; i++)
        if (!strcasecmp( wine_nx_config_entries[i].key, key )) return wine_nx_config_entries[i].value;
    return NULL;
}

static int wine_nx_config_get_bool( const char *key )
{
    const char *value = wine_nx_config_get( key );

    if (!value) return 0;
    if (!value[0]) return 1;  /* bare key, no '=' -> true */
    return !strcmp( value, "1" ) || !strcasecmp( value, "true" ) ||
           !strcasecmp( value, "yes" ) || !strcasecmp( value, "run" );
}

/* Shared 3-tier resolution every boolean runtime toggle in this file uses:
 * env var (highest priority, useful for host-sim/quick dev testing) ->
 * config.txt key -> legacy standalone .txt file (lowest priority, kept only
 * for backward compatibility) -> default off. *source_out is set to a short
 * label for the log line, so it's always clear which of the three actually
 * engaged (or that none did). */
static int wine_nx_resolve_bool_toggle( const char *env_name, const char *config_key,
                                        const char *legacy_path, const char **source_out )
{
    const char *env = getenv( env_name );

    if (env && env[0]) { *source_out = "env"; return 1; }
    if (wine_nx_config_get( config_key ))
    {
        *source_out = "config.txt";
        return wine_nx_config_get_bool( config_key );
    }
    if (read_bool_file( legacy_path )) { *source_out = "legacy .txt file"; return 1; }
    *source_out = "default";
    return 0;
}

static unsigned int close_handle_object( HANDLE handle )
{
    unsigned int status;

    SERVER_START_REQ( close_handle )
    {
        req->handle = wine_server_obj_handle( handle );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return status;
}

static unsigned int runtime_init_process_done(void)
{
    unsigned int status;

    SERVER_START_REQ( init_process_done )
    {
        req->teb = wine_server_client_ptr( NtCurrentTeb() );
        req->peb = wine_server_client_ptr( NtCurrentTeb()->Peb );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return status;
}

static unsigned int runtime_open_exe( const char *path, HANDLE *handle )
{
    OBJECT_ATTRIBUTES attr;

    memset( &attr, 0, sizeof(attr) );
    attr.Length = sizeof(attr);
    return open_unix_file( handle, path, FILE_READ_DATA | SYNCHRONIZE, &attr,
                           FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
}

static int file_exists( const char *path )
{
    struct stat st;

    return !stat( path, &st ) && S_ISREG( st.st_mode );
}

static const char *path_basename( const char *path )
{
    const char *slash = strrchr( path, '/' );
    const char *backslash = strrchr( path, '\\' );

    if (!slash || backslash > slash) slash = backslash;
    return slash ? slash + 1 : path;
}

static void path_dirname( const char *path, char *dir, size_t size )
{
    const char *base = path_basename( path );
    size_t len = base > path ? (size_t)(base - path - 1) : 0;

    if (!len)
    {
        snprintf( dir, size, "%s", WINE_DRIVE_C );
        return;
    }
    if (len >= size) len = size - 1;
    memcpy( dir, path, len );
    dir[len] = 0;
}

static int join_path( char *out, size_t size, const char *dir, const char *name )
{
    int ret = snprintf( out, size, "%s/%s", dir, name );

    return ret > 0 && (size_t)ret < size;
}

static int path_starts_with( const char *path, const char *prefix )
{
    size_t len = strlen( prefix );

    return !strncmp( path, prefix, len );
}

static void slash_to_backslash( char *path )
{
    for (; *path; path++) if (*path == '/') *path = '\\';
}

static int target_to_dos_path( const char *target, char *dos_path, size_t size )
{
    const char *drive_c = WINE_DRIVE_C "/";
    const char *relative;
    int ret;

    if (strlen( target ) > 2 && target[1] == ':')
    {
        ret = snprintf( dos_path, size, "%s", target );
        if (ret <= 0 || (size_t)ret >= size) return 0;
        slash_to_backslash( dos_path );
        return 1;
    }

    if (path_starts_with( target, drive_c ))
    {
        relative = target + strlen( drive_c );
        ret = snprintf( dos_path, size, "C:\\%s", relative );
    }
    else ret = snprintf( dos_path, size, "C:\\%s", path_basename( target ) );

    if (ret <= 0 || (size_t)ret >= size) return 0;
    slash_to_backslash( dos_path );
    return 1;
}

static void dos_dirname( const char *path, char *dir, size_t size )
{
    const char *slash = strrchr( path, '\\' );
    size_t len;

    if (!slash)
    {
        snprintf( dir, size, "C:\\" );
        return;
    }
    len = slash - path;
    if (len < 3) len = 3;
    if (len >= size) len = size - 1;
    memcpy( dir, path, len );
    dir[len] = 0;
}

static void put_process_string( WCHAR **cursor, UNICODE_STRING *string, const char *value )
{
    size_t i, len = value ? strlen( value ) : 0;

    string->Buffer = *cursor;
    string->Length = len * sizeof(WCHAR);
    string->MaximumLength = (len + 1) * sizeof(WCHAR);
    for (i = 0; i < len; i++) (*cursor)[i] = (unsigned char)value[i];
    (*cursor)[len] = 0;
    *cursor += len + 1;
}

static RTL_USER_PROCESS_PARAMETERS *runtime_create_process_params( const char *target,
                                                                   UNICODE_STRING *main_nt_name,
                                                                   char *dos_path, size_t dos_path_size )
{
    RTL_USER_PROCESS_PARAMETERS *params;
    char nt_path[640], dll_path[1024], current_dir[512];
    char cmdline[1024], args_buf[896];
    size_t chars, size;
    WCHAR *cursor;
    const char *cmdline_str;

    if (!target_to_dos_path( target, dos_path, dos_path_size )) return NULL;
    dos_dirname( dos_path, current_dir, sizeof(current_dir) );
    snprintf( nt_path, sizeof(nt_path), "\\??\\%s", dos_path );
    snprintf( dll_path, sizeof(dll_path), "%s;C:\\windows\\system32;C:\\windows;C:\\",
              current_dir );

    /* Args come from config.txt's "args" key first, falling back to the
     * standalone args.txt next to the target NRO (sdmc:/switch/wine/args.txt)
     * for backward compatibility. Format expected either way: "<argv[0]>
     * <args...>" — a full Win32 command line. If present, use it verbatim
     * as CommandLine so curl etc. see args via GetCommandLineA/W. Otherwise
     * fall back to the dos_path alone. */
    cmdline_str = dos_path;
    args_buf[0] = 0;
    {
        const char *config_args = wine_nx_config_get( "args" );
        if (config_args && config_args[0]) snprintf( args_buf, sizeof(args_buf), "%s", config_args );
        else read_first_line( RUNTIME_DIR "/args.txt", args_buf, sizeof(args_buf) );
    }
    if (args_buf[0])
    {
        snprintf( cmdline, sizeof(cmdline), "%s", args_buf );
        cmdline_str = cmdline;
        log_line( "[ARGS] CommandLine='%s'", cmdline_str );
    }
    else
    {
        log_line( "[ARGS] no args configured; CommandLine='%s'", cmdline_str );
    }

    chars = strlen( current_dir ) + 1;
    chars += strlen( dll_path ) + 1;
    chars += strlen( dos_path ) + 1;
    chars += strlen( cmdline_str ) + 1;
    chars += strlen( dos_path ) + 1;
    chars += strlen( nt_path ) + 1;
    chars += 2; /* empty environment */
    size = sizeof(*params) + chars * sizeof(WCHAR);

    if (!(params = calloc( 1, size ))) return NULL;
    params->AllocationSize = size;
    params->Size = size;
    params->Flags = PROCESS_PARAMS_FLAG_NORMALIZED;
    /* The Switch runtime presents one foreground desktop application.  Use
     * the standard Win32 startup hint so applications maximize their own
     * top-level window while dialogs and child windows keep normal sizing. */
    params->dwFlags = STARTF_USESHOWWINDOW;
    params->wShowWindow = SW_SHOWMAXIMIZED;
    params->ProcessGroupId = GetCurrentProcessId();

    cursor = (WCHAR *)(params + 1);
    put_process_string( &cursor, &params->CurrentDirectory.DosPath, current_dir );
    put_process_string( &cursor, &params->DllPath, dll_path );
    put_process_string( &cursor, &params->ImagePathName, dos_path );
    put_process_string( &cursor, &params->CommandLine, cmdline_str );
    put_process_string( &cursor, &params->WindowTitle, dos_path );
    put_process_string( &cursor, main_nt_name, nt_path );
    params->Environment = cursor;
    *cursor++ = 0;
    *cursor++ = 0;
    params->EnvironmentSize = 2 * sizeof(WCHAR);
    return params;
}

static void runtime_init_peb_process( TEB *teb, void *module,
                                      RTL_USER_PROCESS_PARAMETERS *params )
{
    PEB *peb = teb->Peb;

    peb->ImageBaseAddress           = module;
    peb->ProcessParameters          = params;
    peb->OSMajorVersion             = 10;
    peb->OSMinorVersion             = 0;
    peb->OSBuildNumber              = 19045;
    peb->OSPlatformId               = VER_PLATFORM_WIN32_NT;
    peb->ImageSubSystem             = main_image_info.SubSystemType;
    peb->ImageSubSystemMajorVersion = main_image_info.MajorSubsystemVersion;
    peb->ImageSubSystemMinorVersion = main_image_info.MinorSubsystemVersion;
}

static int dll_name_matches( const char *loaded, const char *wanted )
{
    return !strcasecmp( loaded, wanted );
}

static struct runtime_module *find_module_by_name( const char *name )
{
    unsigned int i;

    for (i = 0; i < module_count; i++)
        if (dll_name_matches( modules[i].name, name )) return &modules[i];
    return NULL;
}

static void *rva_ptr( const struct runtime_module *module, DWORD rva, SIZE_T bytes )
{
    if (!rva || rva >= module->size) return NULL;
    if (bytes > module->size - rva) return NULL;
    return (char *)module->base + rva;
}

static IMAGE_NT_HEADERS64 *runtime_nt_headers( void *module )
{
    IMAGE_DOS_HEADER *dos = module;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    return (IMAGE_NT_HEADERS64 *)((char *)module + dos->e_lfanew);
}

static unsigned int map_pe_image( const char *path, void **module, SIZE_T *view_size )
{
    HANDLE file = 0, section = 0;
    unsigned int status;

    *module = NULL;
    *view_size = 0;

    status = runtime_open_exe( path, &file );
    if (status) return status;

    status = NtCreateSection( &section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY,
                              NULL, NULL, PAGE_EXECUTE_READ, SEC_IMAGE, file );
    if (!status)
    {
        status = NtMapViewOfSection( section, NtCurrentProcess(), module, 0, 0, NULL,
                                     view_size, ViewShare, 0, PAGE_EXECUTE_READ );
        close_handle_object( section );
    }
    close_handle_object( file );
    return status;
}

static struct runtime_module *register_module( const char *path, void *base, SIZE_T size, int is_main )
{
    struct runtime_module *module;
    IMAGE_NT_HEADERS64 *nt;

    if (module_count >= MAX_RUNTIME_MODULES)
    {
        log_line( "[FAIL] module table full" );
        return NULL;
    }

    nt = runtime_nt_headers( base );
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        log_line( "[FAIL] %s mapped image does not look like PE32+", path );
        return NULL;
    }

    module = &modules[module_count++];
    memset( module, 0, sizeof(*module) );
    snprintf( module->path, sizeof(module->path), "%s", path );
    snprintf( module->name, sizeof(module->name), "%s", path_basename( path ) );
    path_dirname( path, module->dir, sizeof(module->dir) );
    module->base = base;
    module->size = size;
    module->nt = nt;
    module->is_main = is_main;

    log_line( "[LOAD] %s base=%p size=0x%lx entry=0x%x",
              module->name, module->base, (unsigned long)module->size,
              module->nt->OptionalHeader.AddressOfEntryPoint );
    return module;
}

static int find_dll_path( const struct runtime_module *parent, const char *dll, char *path, size_t size )
{
    if (parent && join_path( path, size, parent->dir, dll ) && file_exists( path )) return 1;
    if (join_path( path, size, WINE_SYSTEM_DIR, dll ) && file_exists( path )) return 1;
    if (join_path( path, size, WINE_DRIVE_C, dll ) && file_exists( path )) return 1;
    if (join_path( path, size, RUNTIME_DIR, dll ) && file_exists( path )) return 1;
    return 0;
}

static struct runtime_module *load_dll_module( const struct runtime_module *parent, const char *dll,
                                               struct import_stats *stats )
{
    char path[512];
    void *base;
    SIZE_T size;
    unsigned int status;
    struct runtime_module *module;

    if ((module = find_module_by_name( dll ))) return module;
    if (!find_dll_path( parent, dll, path, sizeof(path) ))
    {
        log_line( "[MISS] DLL %s not found in local runtime paths", dll );
        stats->missing_dlls++;
        return NULL;
    }

    status = map_pe_image( path, &base, &size );
    if (status)
    {
        log_line( "[FAIL] load DLL %s status=%08x", path, status );
        stats->missing_dlls++;
        return NULL;
    }

    module = register_module( path, base, size, 0 );
    if (module) stats->loaded_dlls++;
    return module;
}

static void *resolve_forwarder( const struct runtime_module *parent, const char *forwarder,
                                struct import_stats *stats, int depth );

static void *resolve_export( const struct runtime_module *module, const char *name, WORD ordinal,
                             const struct runtime_module *parent, struct import_stats *stats, int depth )
{
    const IMAGE_DATA_DIRECTORY *dir = &module->nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY *exports;
    DWORD *functions, *names;
    WORD *ordinals;
    DWORD function_rva = 0;
    DWORD index;
    unsigned int i;

    if (!dir->VirtualAddress || !dir->Size) return NULL;
    exports = rva_ptr( module, dir->VirtualAddress, sizeof(*exports) );
    if (!exports) return NULL;

    functions = rva_ptr( module, exports->AddressOfFunctions, exports->NumberOfFunctions * sizeof(*functions) );
    names = rva_ptr( module, exports->AddressOfNames, exports->NumberOfNames * sizeof(*names) );
    ordinals = rva_ptr( module, exports->AddressOfNameOrdinals, exports->NumberOfNames * sizeof(*ordinals) );
    if (!functions || (!names && exports->NumberOfNames) || (!ordinals && exports->NumberOfNames)) return NULL;

    if (name)
    {
        for (i = 0; i < exports->NumberOfNames; i++)
        {
            const char *export_name = rva_ptr( module, names[i], 1 );

            if (!export_name || strcmp( export_name, name )) continue;
            index = ordinals[i];
            if (index >= exports->NumberOfFunctions) return NULL;
            function_rva = functions[index];
            break;
        }
        if (!function_rva) return NULL;
    }
    else
    {
        if (ordinal < exports->Base) return NULL;
        index = ordinal - exports->Base;
        if (index >= exports->NumberOfFunctions) return NULL;
        function_rva = functions[index];
    }

    if (function_rva >= dir->VirtualAddress && function_rva < dir->VirtualAddress + dir->Size)
    {
        const char *forwarder = rva_ptr( module, function_rva, 1 );

        if (!forwarder) return NULL;
        stats->forwarded++;
        return resolve_forwarder( parent, forwarder, stats, depth + 1 );
    }

    return rva_ptr( module, function_rva, 1 );
}

static void *resolve_forwarder( const struct runtime_module *parent, const char *forwarder,
                                struct import_stats *stats, int depth )
{
    char dll[128], name[128];
    const char *dot = strrchr( forwarder, '.' );
    struct runtime_module *module;

    if (!dot || dot == forwarder || depth > MAX_IMPORT_DEPTH) return NULL;
    if ((size_t)(dot - forwarder) >= sizeof(dll)) return NULL;
    memcpy( dll, forwarder, dot - forwarder );
    dll[dot - forwarder] = 0;
    if (!strchr( dll, '.' )) strncat( dll, ".dll", sizeof(dll) - strlen(dll) - 1 );
    snprintf( name, sizeof(name), "%s", dot + 1 );

    module = load_dll_module( parent, dll, stats );
    if (!module) return NULL;
    if (name[0] == '#') return resolve_export( module, NULL, (WORD)strtoul( name + 1, NULL, 10 ),
                                               parent, stats, depth + 1 );
    return resolve_export( module, name, 0, parent, stats, depth + 1 );
}

static int write_iat_entry( ULONGLONG *slot, void *value )
{
    void *protect_base = (void *)((uintptr_t)slot & ~(uintptr_t)0xfff);
    SIZE_T protect_size = ((uintptr_t)slot - (uintptr_t)protect_base) + sizeof(*slot);
    ULONG old_protect = 0;
    unsigned int status;

    status = NtProtectVirtualMemory( NtCurrentProcess(), &protect_base, &protect_size,
                                     PAGE_READWRITE, &old_protect );
    if (status)
    {
        log_line( "[FAIL] NtProtectVirtualMemory(IAT) status=%08x", status );
        return 0;
    }

    *slot = (ULONGLONG)(uintptr_t)value;

    status = NtProtectVirtualMemory( NtCurrentProcess(), &protect_base, &protect_size,
                                     old_protect, &old_protect );
    if (status) log_line( "[WARN] restore IAT protection status=%08x", status );
    return 1;
}

static void __attribute__((unused)) resolve_module_imports( struct runtime_module *module,
                                                            struct import_stats *stats, int depth )
{
    const IMAGE_DATA_DIRECTORY *dir = &module->nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *desc;
    unsigned int desc_count = 0;

    if (module->imports_scanned || module->resolving_imports) return;
    if (depth > MAX_IMPORT_DEPTH)
    {
        log_line( "[MISS] import recursion limit at %s", module->name );
        stats->unresolved++;
        return;
    }

    module->resolving_imports = 1;
    if (!dir->VirtualAddress || !dir->Size)
    {
        module->imports_scanned = 1;
        module->resolving_imports = 0;
        return;
    }

    desc = rva_ptr( module, dir->VirtualAddress, sizeof(*desc) );
    if (!desc)
    {
        log_line( "[FAIL] invalid import directory in %s", module->name );
        stats->unresolved++;
        module->resolving_imports = 0;
        return;
    }

    for (; desc->Name || desc->FirstThunk || desc->OriginalFirstThunk; desc++, desc_count++)
    {
        const char *dll_name;
        IMAGE_THUNK_DATA64 *lookup, *iat;
        DWORD lookup_rva;
        struct runtime_module *dll_module;
        unsigned int thunk_count = 0;

        if (desc_count > 512)
        {
            log_line( "[FAIL] too many import descriptors in %s", module->name );
            stats->unresolved++;
            break;
        }

        dll_name = rva_ptr( module, desc->Name, 1 );
        if (!dll_name)
        {
            log_line( "[FAIL] invalid import DLL name in %s", module->name );
            stats->unresolved++;
            continue;
        }

        stats->dlls++;
        log_line( "[IMPORT] %s -> %s", module->name, dll_name );
        dll_module = load_dll_module( module, dll_name, stats );
        if (!dll_module)
        {
            stats->unresolved++;
            continue;
        }

        resolve_module_imports( dll_module, stats, depth + 1 );

        lookup_rva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        lookup = rva_ptr( module, lookup_rva, sizeof(*lookup) );
        iat = rva_ptr( module, desc->FirstThunk, sizeof(*iat) );
        if (!lookup || !iat)
        {
            log_line( "[FAIL] invalid thunk table for %s in %s", dll_name, module->name );
            stats->unresolved++;
            continue;
        }

        for (; lookup->u1.AddressOfData; lookup++, iat++, thunk_count++)
        {
            const char *import_name = NULL;
            WORD ordinal = 0;
            void *target;

            if (thunk_count > 8192)
            {
                log_line( "[FAIL] too many thunks for %s in %s", dll_name, module->name );
                stats->unresolved++;
                break;
            }

            stats->imports++;
            if (IMAGE_SNAP_BY_ORDINAL64( lookup->u1.Ordinal ))
            {
                ordinal = IMAGE_ORDINAL64( lookup->u1.Ordinal );
                target = resolve_export( dll_module, NULL, ordinal, module, stats, depth + 1 );
            }
            else
            {
                IMAGE_IMPORT_BY_NAME *by_name = rva_ptr( module, (DWORD)lookup->u1.AddressOfData,
                                                          sizeof(*by_name) );

                if (!by_name)
                {
                    log_line( "[MISS] invalid import name rva=0x%llx in %s",
                              (unsigned long long)lookup->u1.AddressOfData, module->name );
                    stats->unresolved++;
                    continue;
                }
                import_name = by_name->Name;
                target = resolve_export( dll_module, import_name, 0, module, stats, depth + 1 );
            }

            if (!target)
            {
                if (import_name) log_line( "[MISS] %s!%s", dll_name, import_name );
                else log_line( "[MISS] %s ordinal %u", dll_name, ordinal );
                stats->unresolved++;
                continue;
            }

            if (write_iat_entry( &iat->u1.Function, target ))
            {
                stats->bound++;
                if (import_name) log_line( "[BIND] %s!%s -> %p", dll_name, import_name, target );
                else log_line( "[BIND] %s ordinal %u -> %p", dll_name, ordinal, target );
            }
            else stats->unresolved++;
        }
    }

    module->imports_scanned = 1;
    module->resolving_imports = 0;
}

static int runtime_describe_image( void *module, SIZE_T size, void **entry )
{
    IMAGE_NT_HEADERS64 *nt = runtime_nt_headers( module );
    IMAGE_DATA_DIRECTORY *imports;

    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        log_line( "[FAIL] mapped image does not look like PE32+" );
        return 0;
    }

    imports = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    *entry = (char *)module + nt->OptionalHeader.AddressOfEntryPoint;

    main_image_info.TransferAddress = *entry;
    main_image_info.MaximumStackSize = nt->OptionalHeader.SizeOfStackReserve;
    main_image_info.CommittedStackSize = nt->OptionalHeader.SizeOfStackCommit;
    main_image_info.SubSystemType = nt->OptionalHeader.Subsystem;
    main_image_info.MajorSubsystemVersion = nt->OptionalHeader.MajorSubsystemVersion;
    main_image_info.MinorSubsystemVersion = nt->OptionalHeader.MinorSubsystemVersion;
    main_image_info.MajorOperatingSystemVersion = nt->OptionalHeader.MajorOperatingSystemVersion;
    main_image_info.MinorOperatingSystemVersion = nt->OptionalHeader.MinorOperatingSystemVersion;
    main_image_info.ImageCharacteristics = nt->FileHeader.Characteristics;
    main_image_info.DllCharacteristics = nt->OptionalHeader.DllCharacteristics;
    main_image_info.Machine = nt->FileHeader.Machine;
    main_image_info.ImageContainsCode = TRUE;
    main_image_info.ImageFlags = 0;
    if (nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
        main_image_info.ImageDynamicallyRelocated = 1;
    main_image_info.LoaderFlags = nt->OptionalHeader.LoaderFlags;
    main_image_info.ImageFileSize = nt->OptionalHeader.SizeOfImage;
    main_image_info.CheckSum = nt->OptionalHeader.CheckSum;

    log_line( "[IMAGE] base=%p size=0x%lx preferred=0x%llx entry_rva=0x%x machine=0x%x",
              module, (unsigned long)size,
              (unsigned long long)nt->OptionalHeader.ImageBase,
              nt->OptionalHeader.AddressOfEntryPoint, nt->FileHeader.Machine );
    log_line( "[IMAGE] subsystem=%u dll_char=0x%x imports=0x%x/0x%x sections=%u",
              nt->OptionalHeader.Subsystem, nt->OptionalHeader.DllCharacteristics,
              imports->VirtualAddress, imports->Size, nt->FileHeader.NumberOfSections );
    return 1;
}

int main( int argc, char **argv )
{
    char target[512] = DEFAULT_TARGET;
    TEB *teb;
    void *module = NULL;
    void *entry = NULL;
    SIZE_T view_size = 0;
    struct runtime_module *main_module;
    RTL_USER_PROCESS_PARAMETERS *params;
    UNICODE_STRING main_nt_name;
    char dos_path[512];
    unsigned int status;
    unsigned int ldr_status = STATUS_INVALID_IMAGE_FORMAT;
    unsigned int attach_status = STATUS_INVALID_IMAGE_FORMAT;
    int autorun;

    log_main_thread = pthread_self();
    log_main_thread_set = 1;
#ifndef WINE_NX_DEKO3D_ONLY
    consoleInit( NULL );
#endif
    /* deko3d-only build never calls consoleInit() at all -- confirmed via
     * libnx's console_sw.c (the default renderer) that it calls
     * nwindowGetDefault() internally, so even calling consoleInit() just to
     * show boot-trace text would pull in the same __nx_win_init() conflict
     * this whole binary exists to avoid. No on-screen boot trace in this
     * build by design (matches deko_basic/Borealis, neither of which show
     * one either) -- the log file is the only source of truth, same as it's
     * been for every hardware diagnosis this session. */
    mkdir( "sdmc:/switch", 0777 );
    mkdir( RUNTIME_DIR, 0777 );
    mkdir( WINE_DRIVE_C, 0777 );
    mkdir( WINE_DRIVE_C "/windows", 0777 );
    mkdir( WINE_SYSTEM_DIR, 0777 );
    log_file = fopen( RUNTIME_DIR WINE_NX_RUNTIME_LOG_NAME, "w" );
    wine_nx_syscall_trace_select();
    wine_nx_paint_trace_select();
    wine_nx_flush_legacy_select();
    wine_nx_batch_paint_regions_select();
    wine_nx_skip_redundant_update_check_select();
    wine_nx_fast_flush_period_select();
    wine_nx_batch_redraw_updatenow_select();

    if (argc > 1 && argv[1] && argv[1][0]) snprintf( target, sizeof(target), "%s", argv[1] );
    else
    {
        const char *config_target = wine_nx_config_get( "target" );
        if (config_target && config_target[0]) snprintf( target, sizeof(target), "%s", config_target );
        else read_first_line( RUNTIME_DIR "/target.txt", target, sizeof(target) );
    }
    autorun = wine_nx_config_get( "run-entry" ) ? wine_nx_config_get_bool( "run-entry" )
                                                : read_bool_file( RUNTIME_DIR "/run-entry.txt" );

    log_line( "wine-nx-runtime: generic Wine ntdll PE loader path" );
    log_line( "[BUILD] %s", WINE_NX_RUNTIME_BUILD );
    log_line( "[TARGET] %s", target );

    wine_nx_runtime_platform_init();
    log_line( "[INIT] Wine paths/unix bridge ready" );
    virtual_init();
    log_line( "[INIT] virtual memory ready" );

    /* TEMPORARY: verify __libnx_exception_handler wiring. Set to 0 to disable. */
#define WINE_NX_TEST_FAULT 0
#if WINE_NX_TEST_FAULT
    log_line( "[TEST] about to deliberately deref NULL to verify exception handler" );
    fflush( log_file );
    {
        volatile int *null_ptr = (volatile int *)(uintptr_t)0;
        volatile int observed = *null_ptr;
        log_line( "[TEST] NULL deref did NOT fault, value=%d (handler not wired correctly)", observed );
    }
#endif
    wine_nx_runtime_environment_init();
    log_line( "[INIT] Wine NLS/environment ready" );
    teb = virtual_alloc_first_teb();
    if (!teb || NtCurrentTeb() != teb || !teb->Peb)
    {
        log_line( "[FAIL] virtual_alloc_first_teb" );
        park_forever();
    }

    server_init_process();
    status = runtime_init_process_done();
    if (status)
    {
        log_line( "[FAIL] init_process_done status=%08x", status );
        park_forever();
    }

    status = map_pe_image( target, &module, &view_size );
    if (status)
    {
        log_line( "[FAIL] map target status=%08x", status );
        park_forever();
    }

    if (runtime_describe_image( module, view_size, &entry ))
    {
        params = runtime_create_process_params( target, &main_nt_name, dos_path, sizeof(dos_path) );
        if (!params)
        {
            log_line( "[FAIL] process parameter allocation" );
            park_forever();
        }
        runtime_init_peb_process( teb, module, params );
        log_line( "[PEB] image=%s nt=\\??\\%s", dos_path, dos_path );

        main_module = register_module( target, module, view_size, 1 );
        if (main_module)
        {
            status = wine_nx_loader_bootstrap( &main_nt_name );
            log_line( "[LDR] bootstrap status=%08x", status );
            if (!status)
            {
                ldr_status = wine_nx_loader_fixup_main_imports();
                log_line( "[LDR] fixup_imports status=%08x", ldr_status );
                if (ldr_status && wine_nx_loader_last_import_dll()[0])
                    log_line( "[LDR] last failed import=%s status=%08x",
                              wine_nx_loader_last_import_dll(),
                              wine_nx_loader_last_import_status() );
                if (ldr_status && wine_nx_loader_last_open_path()[0])
                    log_line( "[LDR] last dll open=%s status=%08x",
                              wine_nx_loader_last_open_path(),
                              wine_nx_loader_last_open_status() );
                if (ldr_status && wine_nx_loader_last_export_diag()[0])
                    log_line( "[LDR] export diag=%s", wine_nx_loader_last_export_diag() );
                if (!ldr_status)
                {
                    attach_status = wine_nx_loader_attach_main();
                    log_line( "[LDR] process_attach status=%08x", attach_status );
                }
            }
        }
        log_line( "[READY] PE image is mapped by Wine ntdll; entry=%p", entry );
        if (autorun && !ldr_status && !attach_status)
        {
            log_line( "[RUN] run-entry.txt enabled; jumping to PE entry after Wine loader attach" );
            log_line( "[RUN] entry returned %d", call_pe_entry_point( entry ) );
        }
        else if (autorun)
            log_line( "[BLOCK] run-entry.txt enabled, but loader status import=%08x attach=%08x",
                      ldr_status, attach_status );
    }

    park_forever();
    return 0;
}
