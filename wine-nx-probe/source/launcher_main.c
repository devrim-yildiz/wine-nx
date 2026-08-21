/* wine-nx-launcher: the single entry point the user actually launches
 * from hbmenu -- a Cemu-style game library, now merged with this
 * project's actual PE loader/box64 engine into one binary (see
 * CMakeLists.txt's own comment on the wine-nx-launcher target and
 * nx_arch.h). Cold boot still initializes nothing beyond its own GUI
 * (no consoleInit(), no Vulkan/deko3d ever actually invoked, no Wine
 * environment bring-up) -- libnx's plain framebuffer drives nxgui/
 * nxinput/nxmenu/nxdebug/nxtest (source/gui/) exactly as before. What's
 * different now: picking a GDI-backend game runs IN-PROCESS via
 * nx_arch_launch_exe(), instead of handing off to a separate
 * wine-nx-runtime.nro. Vulkan-backend games still hand off to a
 * SEPARATE binary via envSetNextLoad -- see
 * wine-nx-probe/3d-accel-scoping.md and this project's
 * vulkan_integration notes for why THAT specific combination (this
 * process being both a GUI that already references nwindowGetDefault()
 * AND a Vulkan/deko3d presenter) still isn't safe to merge, unlike the
 * GDI path.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#include "gui/nxgui.h"
#include "gui/nxinput.h"
#include "gui/nxlib.h"
#include "gui/nxmenu.h"
#include "gui/nxdebug.h"
#include "gui/nxtest.h"
#include "gui/nxmenu_quickmenu.h"
#include "gui/nxtoast.h"
#include "nx_arch.h"
#include "nx_diagnostics.h"

#include <switch.h>
#include <stdio.h>
#include <string.h>

/* No dedicated header for either of these -- source/runtime.c declares
 * wine_nx_runtime_platform_init() the same way (a plain extern next to its
 * own call site), since runtime_platform.c exports it with no header of
 * its own. Idempotent (paths_initialized/sockets_initialized statics in
 * runtime_platform.c), so calling it here AND again later inside
 * wine_nx_lazy_init() (once a game is actually picked, via
 * nx_arch_launch_exe() -> wine_nx_boot_and_run_target()) is safe -- the
 * underlying socketInitializeDefault() only ever runs once per process
 * either way.
 *
 * wine_nx_nxlink_connect() (source/runtime.c) reads sdmc:/switch/wine/
 * nxlink-host.txt and, if present, sets libnx's __nxlink_host before
 * calling nxlinkStdio() -- see that function's own comment for why this
 * is needed at all (nxlinkStdio() connects OUT to a host IP that a plain
 * hbmenu launch, unlike a real nxlink push, never populates on its own). */
extern void wine_nx_runtime_platform_init(void);
extern void wine_nx_nxlink_connect(void);

#define WINE_NX_LAUNCHER_FB_W 1280
#define WINE_NX_LAUNCHER_FB_H 720

/* Every runtime binary this hands off to lives flat under
 * sdmc:/switch/wine alongside this launcher itself and every config
 * file nxlib.c/nxtest.c read and write (see those files' own WINE_ROOT
 * comments) -- matching build-switch.sh's own packaging layout
 * (PACKAGE_DIR="$BUILD_DIR/sd-card/switch/wine"). */
#define WINE_NX_LAUNCHER_ROOT "sdmc:/switch/wine"

/* ZINK and D3D aren't real backends yet -- no binary in this project
 * implements Mesa/zink-over-something or Direct3D/DirectX translation,
 * only the plain GDI/framebuffer path (wine-nx-runtime.nro) and real,
 * hardware-confirmed Vulkan (wine-nx-vulkan-runtime.nro, which is
 * actually wine-nx-runtime-deko3d.elf wrapped under a different NACP --
 * see this project's CMakeLists.txt comment on VULKAN_RUNTIME_NRO_FILE).
 * Picking either falls back to GDI rather than handing off to a binary
 * that doesn't exist, and logs that fact through the debug overlay so
 * it's visible, not silently wrong. */
static const char *resolve_runtime_nro( nxlib_backend_t backend, nxdebug_state_t *debug )
{
    switch (backend)
    {
    case NXLIB_BACKEND_VULKAN:
        return WINE_NX_LAUNCHER_ROOT "/wine-nx-vulkan-runtime.nro";
    case NXLIB_BACKEND_ZINK:
        nxdebug_log( debug, "ZINK/GL BACKEND NOT IMPLEMENTED YET -- USING GDI" );
        return WINE_NX_LAUNCHER_ROOT "/wine-nx-runtime.nro";
    case NXLIB_BACKEND_D3D:
        nxdebug_log( debug, "D3D BACKEND NOT IMPLEMENTED YET -- USING GDI" );
        return WINE_NX_LAUNCHER_ROOT "/wine-nx-runtime.nro";
    case NXLIB_BACKEND_GDI:
    case NXLIB_BACKEND_AUTO:
    default:
        return WINE_NX_LAUNCHER_ROOT "/wine-nx-runtime.nro";
    }
}

/* Same target.txt every WINE_NX_APP test and every real game launch has
 * always used (source/runtime.c reads this on its own startup via
 * read_first_line) -- the launcher's one and only handshake with
 * whichever runtime binary it hands off to. */
static void write_launch_target( const char *native_path )
{
    FILE *f = fopen( WINE_NX_LAUNCHER_ROOT "/target.txt", "w" );
    if (!f) return;
    fprintf( f, "%s\n", native_path );
    fclose( f );
}

int main( int argc, char **argv )
{
    nxtest_state_t test_state;
    char next_nro[512];
    nxmenu_state_t menu;
    nxdebug_state_t debug;
    nxquickmenu_state_t quickmenu;
    nxtoast_state_t toasts;
    Framebuffer fb;
    Result rc;

    (void)argc;
    (void)argv;

    /* nxlink live logging, active for this binary's own GUI phase too --
     * not just once a game is running. See source/runtime.c's own main()
     * for why wine_nx_runtime_platform_init() (idempotent bsd socket
     * bring-up) is called explicitly here rather than assuming
     * wine_nx_lazy_init() already did it -- this process's GUI phase never
     * reaches that function until the user actually picks a game.
     * wine_nx_nxlink_connect() itself is called exactly once for this
     * process' lifetime, here -- not repeated inside wine_nx_lazy_init(),
     * since calling nxlinkStdio() twice in one process is not a safe
     * pattern to rely on. */
    wine_nx_runtime_platform_init();
    wine_nx_nxlink_connect();

    /* Auto-test-mode resume check runs before any graphics init at all
     * -- if a test chain is continuing, hand off immediately without
     * ever presenting a single frame this process lifetime. See
     * nxtest.h's own protocol comment. */
    nxtest_init( &test_state );
    if (nxtest_resume( &test_state, next_nro, sizeof(next_nro) ))
    {
        envSetNextLoad( next_nro, "" );
        return 0;
    }

    rc = framebufferCreate( &fb, nwindowGetDefault(),
                            WINE_NX_LAUNCHER_FB_W, WINE_NX_LAUNCHER_FB_H, PIXEL_FORMAT_RGBA_8888, 3 );
    if (R_FAILED(rc)) return 1;   /* nothing sensible to do without a framebuffer -- matches runtime.c's own framebufferCreate failure handling */
    framebufferMakeLinear( &fb );

    nxmenu_init( &menu );
    nxdebug_init( &debug );
    nxinput_init( WINE_NX_LAUNCHER_FB_W, WINE_NX_LAUNCHER_FB_H );
    /* nxmenu_init() already loaded these from config.txt (see
     * nxmenu.h's own comment on ctrl_deadzone/ctrl_sensitivity) --
     * applying them here is what actually makes the Controller Studio
     * screen's sliders persist across a relaunch, not just within one
     * session. */
    nxinput_set_deadzone( menu.ctrl_deadzone );
    nxinput_set_cursor_speed( menu.ctrl_sensitivity );
    nxtoast_init( &toasts );
    nxquickmenu_init( &quickmenu );

    /* A test chain just finished on THIS boot (as opposed to "continuing",
     * handled above before the framebuffer even existed) -- surface the
     * results immediately instead of making the user dig for them. */
    if (test_state.run_count > 0)
    {
        nxtest_report_to_log( &test_state, &debug );
        debug.visible = 1;
    }

    for (;;)
    {
        nxinput_state_t input, neutral_input;
        u32 stride;
        nxgui_surface_t surf;
        int debug_consumed, quickmenu_consumed;
        int prev_game_count;
        nxmenu_action_t action;

        nxinput_poll( &input );

        surf.pixels = (uint32_t *)framebufferBegin( &fb, &stride );
        surf.width = WINE_NX_LAUNCHER_FB_W;
        surf.height = WINE_NX_LAUNCHER_FB_H;
        surf.stride_px = (int)(stride / sizeof(uint32_t));   /* framebuffer.h: out_stride is in bytes */

        /* Modal precedence, highest first: nxdebug's fixed ZR+Minus
         * combo, then nxmenu_quickmenu's configurable-shoulder+Minus
         * combo, then the ordinary menu. Whichever of the first two
         * reports having consumed this frame's input gets a neutral/
         * all-zero nxinput_state_t passed to nxmenu_frame instead of
         * the real one, so a single physical press can't also act on
         * whatever's underneath -- nxmenu_frame still gets called every
         * frame regardless, purely to keep redrawing into this frame's
         * buffer (framebufferBegin's buffer isn't guaranteed to retain
         * the previous frame's contents). See nxdebug.h's own contract
         * comment, which nxquickmenu_update deliberately matches.
         *
         * Only the INPUT side runs here, before nxmenu_frame -- the
         * actual drawing (nxdebug_draw/nxquickmenu_draw, below) is
         * deliberately deferred until after nxmenu_frame has redrawn the
         * whole surface, otherwise that redraw would immediately erase
         * whatever these drew, in the same frame. See nxdebug_draw()'s
         * own comment for the full story on why this split exists --
         * this used to be one combined call per overlay, which meant a
         * successfully-opened overlay was invisible while STILL correctly
         * eating all navigation input, indistinguishable from a freeze. */
        debug_consumed = nxdebug_update( &debug, &input );
        quickmenu_consumed = debug_consumed ? 0 : nxquickmenu_update( &quickmenu, &menu, &toasts, &input );

        /* Diagnostic suite trigger -- A while the debug overlay is open
         * (see nxdebug.c's own hint line). Reuses that overlay's existing
         * log/scroll display verbatim (nxdiag_report_to_log() is just
         * more nxdebug_log() calls, the same mechanism nxtest_report_to_log()
         * below already uses for auto-test results) rather than building
         * a second results screen -- nxdebug.c itself stays untouched
         * beyond its own static hint text, keeping its "knows nothing
         * about any other module" design intact (see that file's own
         * header comment). */
        if (debug_consumed && debug.visible && input.confirm)
        {
            nxdiag_report_t diag_report;
            nxdiag_run_all( &diag_report );
            nxdiag_report_to_log( &diag_report, &debug );
        }

        memset( &neutral_input, 0, sizeof(neutral_input) );
        prev_game_count = menu.game_count;
        action = nxmenu_frame( &menu, &surf, (debug_consumed || quickmenu_consumed) ? &neutral_input : &input );
        if (menu.game_count > prev_game_count) nxtoast_push( &toasts, "GAME ADDED TO LIBRARY" );

        /* Settings' own "DEBUG OVERLAY" row (see nxmenu.h's own comment
         * on NXMENU_ACTION_OPEN_DEBUG) -- the reliable, discoverable way
         * in, an ordinary confirmed menu item rather than a button combo
         * the player has to already know about. Takes effect starting
         * next frame (debug_consumed/nxdebug_draw above already ran for
         * THIS one against the old visible value) -- one frame of
         * latency, not worth restructuring the loop to avoid. */
        if (action == NXMENU_ACTION_OPEN_DEBUG) debug.visible = 1;

        /* Overlay drawing, on top of whatever nxmenu_frame just drew --
         * see the big comment above debug_consumed/quickmenu_consumed for
         * why this must come after nxmenu_frame, not before. Toasts draw
         * last of all, anchored top-left specifically so they never
         * collide with nxquickmenu_draw's own right-edge panel (see
         * nxtoast.c). */
        if (debug_consumed) nxdebug_draw( &debug, &surf );
        if (quickmenu_consumed) nxquickmenu_draw( &quickmenu, &menu, &surf );
        nxtoast_frame( &toasts, &surf );

        framebufferEnd( &fb );

        if (action == NXMENU_ACTION_QUIT) break;
        if (action == NXMENU_ACTION_LAUNCH)
        {
            if (menu.launch_settings.backend == NXLIB_BACKEND_VULKAN)
            {
                /* The one remaining case that still needs a separate
                 * process -- see this file's own header comment and
                 * nx_arch.h for exactly why Vulkan/deko3d couldn't be
                 * folded into this same merge. */
                const char *nro = resolve_runtime_nro( menu.launch_settings.backend, &debug );
                write_launch_target( menu.launch_path );
                nxquickmenu_shutdown( &quickmenu );
                framebufferClose( &fb );
                socketExit();
                envSetNextLoad( nro, "" );
                return 0;
            }
            else
            {
                /* GDI/AUTO/ZINK/D3D (the latter two fall back to GDI --
                 * see resolve_runtime_nro's own comment) all run
                 * IN-PROCESS now, the whole point of this merge. Release
                 * this binary's own GUI framebuffer first -- exactly
                 * the same handoff nx_arch_launch_exe()'s own header
                 * comment requires, mirroring source/runtime.c's own
                 * console-to-framebuffer release pattern -- then call
                 * straight into the real loader. Any actual return here
                 * only ever means a failure before the guest ran (see
                 * runtime_boot.h); there's no safe way back to a normal
                 * GUI screen once that's happened, so this process just
                 * ends, exit code carrying whichever wine_nx_boot_result_t
                 * was returned. */
                wine_nx_boot_result_t boot_result;

                nxquickmenu_shutdown( &quickmenu );
                framebufferClose( &fb );
                nwindowReleaseBuffers( nwindowGetDefault() );
                boot_result = nx_arch_launch_exe( menu.launch_path );
                /* Only reached on a failure before any guest code ran --
                 * see runtime_boot.h. A successful launch never returns
                 * here at all, the same way source/runtime.c's own main()
                 * never gets back from wine_nx_boot_and_run_target()
                 * either -- this socketExit() is unreachable on the
                 * common, successful-launch path by construction. */
                socketExit();
                return (int)boot_result;
            }
        }
    }

    nxquickmenu_shutdown( &quickmenu );

    /* No deko3d/Vulkan is ever linked into this binary (see this file's
     * own header comment), so the atexit/static-destructor GPU-state
     * hazard that made source/runtime.c's own clean-exit path need
     * svcExitProcess() instead of a plain return (see that file's
     * comment on its Minus-button handler) doesn't apply here -- a
     * plain return after framebufferClose() is the whole exit sequence. */
    framebufferClose( &fb );
    socketExit();
    return 0;
}
