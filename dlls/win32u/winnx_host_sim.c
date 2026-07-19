/*
 * Host simulation of the Switch presentation path (dlls/win32u/winnx_drv.c).
 *
 * Implements the same wine_nx_fb_*()/wine_nx_touch_poll()/wine_nx_runtime_trace()
 * hooks that wine-nx-probe/source/runtime.c provides on real hardware, but
 * backed by SDL2 instead of libnx, so winnx_drv.c can run unmodified on a
 * host build (see driver.c, gated behind the WINE_NX_HOST_SIM env var).
 *
 * This is additive and does not touch the real Switch runtime or
 * dlls/ntdll/unix/horizon.c. It only fakes the presentation surface:
 * PE loading, the Horizon NTDLL backend, and JIT/code-memory mapping are
 * out of scope here and remain hardware/emulator-only.
 *
 * Known gaps vs. the real hardware path (see wine-nx-probe/switch-shims/README.md):
 *   - single-pointer "touch": only one contact is ever reported (mouse
 *     position + button), so multi-touch gestures are not exercised.
 *   - SDL2's present cost model is nothing like the Switch's block-linear
 *     framebuffer conversion, so this cannot reproduce (or "fix") the
 *     presentation performance problem the real README describes. It's for
 *     verifying rendering/input correctness, not perf.
 *   - SDL window/event handling must happen on the process's main thread on
 *     macOS (AppKit requirement). If the Wine message-processing thread that
 *     calls wine_nx_drv_ProcessEvents isn't the process main thread, this
 *     will misbehave -- that's a loud, known limitation, not silently papered
 *     over.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <SDL.h>

#define WINE_NX_SIM_W 1280
#define WINE_NX_SIM_H 720

static SDL_Window   *sim_window;
static SDL_Renderer *sim_renderer;
static SDL_Texture  *sim_texture;
static uint32_t      sim_backing[WINE_NX_SIM_W * WINE_NX_SIM_H];
static int            sim_ready;
static int            sim_init_failed;
static int            sim_dirty;
static pthread_mutex_t sim_mutex = PTHREAD_MUTEX_INITIALIZER;

static int sim_mouse_down;
static int sim_mouse_x = WINE_NX_SIM_W / 2;
static int sim_mouse_y = WINE_NX_SIM_H / 2;

void wine_nx_runtime_trace( const char *msg )
{
    fprintf( stderr, "%s\n", msg );
}

#ifndef __SWITCH__
/* Runtime toggle globals normally defined by wine-nx-probe/source/runtime.c,
 * which only links into the Switch binaries -- without host-side definitions
 * here, winnx_drv.c's references leave win32u.so unlinkable on host builds
 * (found the first time a full host build was actually attempted). Both stay
 * at their off defaults: the paint-trace tier and the NEON blit are
 * Switch-hardware concerns the sim deliberately doesn't model. */
int wine_nx_paint_trace_enabled;
int wine_nx_neon_blit_enabled;
#endif

/* Lazily bring up the SDL window in place of framebufferCreate()/MakeLinear().
 * Must run on the main thread on macOS -- see the file header. Caller must
 * hold sim_mutex: both wine_nx_fb_lock() and wine_nx_touch_poll() can reach
 * this on first use, and without the lock held across the whole check+init
 * sequence two threads racing in here would each create their own SDL window
 * (sim_ready is only set at the very end of a successful init). */
static int wine_nx_sim_init( void )
{
    if (sim_ready) return 0;
    if (sim_init_failed) return -1;

    if (SDL_Init( SDL_INIT_VIDEO ) != 0)
    {
        wine_nx_runtime_trace( "[NXSIM] SDL_Init FAILED -- giving up, no more retries" );
        sim_init_failed = 1;
        return -1;
    }

    sim_window = SDL_CreateWindow( "wine-nx host sim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   WINE_NX_SIM_W, WINE_NX_SIM_H, SDL_WINDOW_SHOWN );
    if (!sim_window)
    {
        wine_nx_runtime_trace( "[NXSIM] SDL_CreateWindow FAILED -- giving up, no more retries" );
        sim_init_failed = 1;
        return -1;
    }

    sim_renderer = SDL_CreateRenderer( sim_window, -1, SDL_RENDERER_ACCELERATED );
    if (!sim_renderer) sim_renderer = SDL_CreateRenderer( sim_window, -1, SDL_RENDERER_SOFTWARE );
    if (!sim_renderer)
    {
        wine_nx_runtime_trace( "[NXSIM] SDL_CreateRenderer FAILED -- giving up, no more retries" );
        sim_init_failed = 1;
        return -1;
    }

    /* SDL_PIXELFORMAT_RGBA32 always gives memory byte order R,G,B,A regardless
     * of host endianness, matching what wine_nx_surface_flush() writes
     * (see dlls/win32u/winnx_drv.c, "RGBA8888 (R,G,B,A bytes)"). */
    sim_texture = SDL_CreateTexture( sim_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                     WINE_NX_SIM_W, WINE_NX_SIM_H );
    if (!sim_texture)
    {
        wine_nx_runtime_trace( "[NXSIM] SDL_CreateTexture FAILED -- giving up, no more retries" );
        sim_init_failed = 1;
        return -1;
    }

    memset( sim_backing, 0xff, sizeof(sim_backing) );
    sim_ready = 1;
    wine_nx_runtime_trace( "[NXSIM] SDL window ready 1280x720" );
    return 0;
}

void *wine_nx_fb_lock( int *width, int *height, int *stride_px )
{
    pthread_mutex_lock( &sim_mutex );
    if (!sim_ready && wine_nx_sim_init())
    {
        pthread_mutex_unlock( &sim_mutex );
        return NULL;
    }
    if (width)     *width     = WINE_NX_SIM_W;
    if (height)    *height    = WINE_NX_SIM_H;
    if (stride_px) *stride_px = WINE_NX_SIM_W;
    return sim_backing;
}

void wine_nx_fb_unlock( void )
{
    sim_dirty = 1;
    pthread_mutex_unlock( &sim_mutex );
}

void wine_nx_fb_present( void )
{
    pthread_mutex_lock( &sim_mutex );
    if (sim_ready && sim_dirty)
    {
        if (SDL_UpdateTexture( sim_texture, NULL, sim_backing, WINE_NX_SIM_W * (int)sizeof(uint32_t) ) != 0)
            wine_nx_runtime_trace( "[NXSIM] SDL_UpdateTexture FAILED" );
        if (SDL_RenderClear( sim_renderer ) != 0)
            wine_nx_runtime_trace( "[NXSIM] SDL_RenderClear FAILED" );
        if (SDL_RenderCopy( sim_renderer, sim_texture, NULL, NULL ) != 0)
            wine_nx_runtime_trace( "[NXSIM] SDL_RenderCopy FAILED" );
        SDL_RenderPresent( sim_renderer );
        sim_dirty = 0;
    }
    pthread_mutex_unlock( &sim_mutex );
}

/* Fake the primary touchscreen contact with the host mouse pointer. Only one
 * contact is ever reported -- see the "known gaps" note at the top of this
 * file for why that's a loud limitation and not silently pretended away. */
int wine_nx_touch_poll( int *x, int *y )
{
    SDL_Event event;
    int failed;

    pthread_mutex_lock( &sim_mutex );
    failed = !sim_ready && wine_nx_sim_init();
    pthread_mutex_unlock( &sim_mutex );
    if (failed) return 0;

    while (SDL_PollEvent( &event ))
    {
        switch (event.type)
        {
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                sim_mouse_down = 1;
                sim_mouse_x = event.button.x;
                sim_mouse_y = event.button.y;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) sim_mouse_down = 0;
            break;
        case SDL_MOUSEMOTION:
            sim_mouse_x = event.motion.x;
            sim_mouse_y = event.motion.y;
            break;
        case SDL_QUIT:
            /* No host-side WM_CLOSE plumbing yet -- exiting here is a known
             * placeholder, not a faithful simulation of console HOME-menu exit. */
            wine_nx_runtime_trace( "[NXSIM] SDL_QUIT -- exiting" );
            exit( 0 );
        default:
            break;
        }
    }

    if (x) *x = sim_mouse_x;
    if (y) *y = sim_mouse_y;
    return sim_mouse_down;
}
