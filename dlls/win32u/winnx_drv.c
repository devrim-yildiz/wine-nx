/*
 * Minimal Nintendo Switch (Horizon) display driver for win32u.
 *
 * Software-only: windows render into ordinary DIB memory (handled by the GDI
 * engine), and the window surface's flush() blits the dirty pixels to the
 * libnx framebuffer via the runtime hooks wine_nx_fb_*().  No GPU.
 *
 * The driver reuses the null_user_driver for everything else; only window
 * creation and surface presentation are Switch-specific (see driver.c, which
 * plugs these in under __SWITCH__).
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
/* winnx_drv.c also compiles unmodified into the host-sim build (see
 * switch-shims/README.md) which could in principle target a non-ARM host,
 * so this is guarded on the actual target architecture, not __SWITCH__ --
 * armv8-a (this port's -march on real hardware, see
 * cmake/switch-devkitA64.cmake) always has NEON, it's mandatory in the
 * base ISA, not an optional extension. */
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#define WINE_NX_HAVE_NEON 1
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "ntgdi_private.h"
#include "ntuser_private.h"
#include "win32u_private.h"
#include "wine/gdi_driver.h"

/* Framebuffer hooks implemented in the runtime (wine-nx-probe/source/runtime.c). */
extern void *wine_nx_fb_lock( int *width, int *height, int *stride_px );
extern void  wine_nx_fb_unlock( void );
extern void  wine_nx_fb_present( void );
extern int   wine_nx_touch_poll( int *x, int *y );
extern void  wine_nx_runtime_trace( const char *msg ) __attribute__((weak));
extern int   wine_nx_paint_trace_enabled;
#ifdef WINE_NX_HAVE_NEON
/* wine-nx-probe/source/runtime.c -- off by default. The single riskiest
 * toggle in this whole session: a hand-vectorized NEON BGRX->RGBA blit,
 * verified bit-exact against the scalar reference across ~800,000 test
 * pixels on a native arm64 build (known values, every tail width 0-63,
 * every exact multiple of 16, 200 rounds of full-range random fuzzing --
 * see the comment on the NEON path in wine_nx_surface_flush() below for
 * the actual verification methodology), but never run on real Switch
 * hardware. Exists as an explicit A/B switch specifically because a wrong
 * shuffle would silently produce wrong colors with no crash, and this
 * session has no way to see the screen to catch that -- if colors look
 * wrong (channels swapped differently, tinted, garbled) with this on,
 * turn it back off. */
extern int wine_nx_neon_blit_enabled;
#endif
/* dlls/win32u/dce.c -- now an in-memory, once-per-second aggregator
 * rather than a per-call fflush(), same reasoning as everything else
 * gated behind wine_nx_paint_trace_enabled. */
extern void  switch_paint_trace( const char *phase, unsigned int ms );

struct wine_nx_surface
{
    struct window_surface surface;
    BOOL initial_redraw_done;
    RECT present_rect;
    POINT screen_origin;
    RECT clip_rect;
    BOOL has_clip;
};

struct wine_nx_surface_entry
{
    HWND hwnd;
    RECT screen_rect;
    BOOL visible;
    struct wine_nx_surface_entry *next;
};

static struct wine_nx_surface_entry *wine_nx_surface_entries;

static struct wine_nx_surface *wine_nx_surface_from_base( struct window_surface *surface )
{
    return CONTAINING_RECORD( surface, struct wine_nx_surface, surface );
}

static void nxdrv_trace( const char *fmt, int a, int b, int c, int d )
{
    char buf[160];
    if (!&wine_nx_runtime_trace) return;
    snprintf( buf, sizeof(buf), fmt, a, b, c, d );
    wine_nx_runtime_trace( buf );
}

static void nxdrv_trace_hot( const char *fmt, int a, int b, int c, int d )
{
    static unsigned int count;

    /* This was rate-limited to 120 calls but never gated behind
     * wine_nx_paint_trace_enabled -- present_full/flush dirty/fb_lock/touch
     * all paid real unconditional fflush() cost for the first 120 frames of
     * every single run regardless of trace settings, same bug class as
     * everything else in this file. Check the flag first (not just the
     * count) so a run with tracing left off never burns any of the budget,
     * and turning tracing on later still gets a full 120 real samples. */
    if (!wine_nx_paint_trace_enabled) return;
    if (count++ >= 120) return;
    nxdrv_trace( fmt, a, b, c, d );
}

/* get_dib_stride / get_dib_image_size come from the win32u private headers. */

static void trace_surface_samples( const BITMAPINFO *color_info, const void *color_bits )
{
    static int sample_count;
    const DWORD *bits = color_bits;
    int width = color_info->bmiHeader.biWidth;
    int height = abs( color_info->bmiHeader.biHeight );
    DWORD p0, pc;
    int nonwhite = 0;
    int x, y;

    /* nxdrv_trace() -> wine_nx_runtime_trace() fflush()es unconditionally --
     * this call was contaminating the first ~40 frames of every run with
     * real fflush cost regardless of WINE_NX_PAINT_TRACE. Same bug class
     * being chased across this whole file tonight (Thread A, 2.7s stall
     * investigation); ported here from the already-verified fix on
     * fps-investigation, since this branch was cut from a clean main that
     * predates it. */
    if (!wine_nx_paint_trace_enabled) return;
    if (sample_count >= 40 || width <= 0 || height <= 0 || !bits) return;

    p0 = bits[0];
    pc = bits[(size_t)(height / 2) * width + width / 2];
    for (y = 0; y < height; y += 64)
    {
        for (x = 0; x < width; x += 64)
        {
            if ((bits[(size_t)y * width + x] & 0x00ffffff) != 0x00ffffff) nonwhite++;
        }
    }

    nxdrv_trace( "[NXDRV] samples p0=%08x pc=%08x nonwhite=%d",
                 (int)p0, (int)pc, nonwhite, 0 );
    sample_count++;
}

static BOOL wine_nx_surface_flush( struct window_surface *surface, const RECT *rect, const RECT *dirty,
                                   const BITMAPINFO *color_info, const void *color_bits, BOOL shape_changed,
                                   const BITMAPINFO *shape_info, const void *shape_bits );
static const struct window_surface_funcs wine_nx_surface_funcs;

static void wine_nx_surface_mark_full_dirty( struct window_surface *surface )
{
    struct wine_nx_surface *nx_surface = wine_nx_surface_from_base( surface );
    RECT dirty = nx_surface->present_rect;

    if (dirty.right <= 0 || dirty.bottom <= 0) return;

    window_surface_lock( surface );
    surface->bounds = dirty;
    window_surface_unlock( surface );
}

static BOOL wine_nx_surface_present_full( struct window_surface *surface )
{
    struct wine_nx_surface *nx_surface = wine_nx_surface_from_base( surface );
    char color_buf[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *color_info = (BITMAPINFO *)color_buf;
    RECT dirty = nx_surface->present_rect;
    void *color_bits;
    BOOL ret = FALSE;

    if (dirty.right <= 0 || dirty.bottom <= 0) return FALSE;

    window_surface_lock( surface );
    color_bits = window_surface_get_color( surface, color_info );
    nxdrv_trace_hot( "[NXDRV] present_full bits=%d %dx%d",
                     color_bits ? 1 : 0, dirty.right, dirty.bottom, 0 );
    if (color_bits)
        ret = wine_nx_surface_flush( surface, &surface->rect, &dirty, color_info, color_bits,
                                     FALSE, NULL, NULL );
    if (ret) SetRectEmpty( &surface->bounds );
    window_surface_unlock( surface );
    return ret;
}

static BOOL wine_nx_surface_present_screen_rect( struct window_surface *surface, const RECT *screen_rect )
{
    struct wine_nx_surface *nx_surface = wine_nx_surface_from_base( surface );
    char color_buf[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *color_info = (BITMAPINFO *)color_buf;
    RECT dirty = *screen_rect;
    void *color_bits;
    BOOL ret = FALSE;

    OffsetRect( &dirty, -nx_surface->screen_origin.x, -nx_surface->screen_origin.y );
    if (!intersect_rect( &dirty, &dirty, &nx_surface->present_rect )) return FALSE;

    window_surface_lock( surface );
    color_bits = window_surface_get_color( surface, color_info );
    if (color_bits)
        ret = wine_nx_surface_flush( surface, &surface->rect, &dirty, color_info, color_bits,
                                     FALSE, NULL, NULL );
    window_surface_unlock( surface );
    return ret;
}

static struct wine_nx_surface_entry *wine_nx_find_surface_entry( HWND hwnd, BOOL create )
{
    struct wine_nx_surface_entry *entry;

    for (entry = wine_nx_surface_entries; entry; entry = entry->next)
        if (entry->hwnd == hwnd) return entry;

    if (!create) return NULL;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return NULL;
    entry->hwnd = hwnd;
    entry->next = wine_nx_surface_entries;
    wine_nx_surface_entries = entry;
    return entry;
}

static BOOL wine_nx_get_cached_screen_rect( HWND hwnd, RECT *rect )
{
    struct wine_nx_surface_entry *entry = wine_nx_find_surface_entry( hwnd, FALSE );

    if (!entry || IsRectEmpty( &entry->screen_rect )) return FALSE;
    *rect = entry->screen_rect;
    return TRUE;
}

static void wine_nx_note_surface_present( HWND hwnd, const POINT *origin, const RECT *present_rect )
{
    struct wine_nx_surface_entry *entry = wine_nx_find_surface_entry( hwnd, TRUE );

    if (!entry) return;
    entry->screen_rect = *present_rect;
    OffsetRect( &entry->screen_rect, origin->x, origin->y );
    entry->visible = !IsRectEmpty( &entry->screen_rect );
}

static void wine_nx_note_surface_hidden( HWND hwnd )
{
    struct wine_nx_surface_entry *entry = wine_nx_find_surface_entry( hwnd, FALSE );

    if (entry) entry->visible = FALSE;
}

static BOOL wine_nx_present_hwnd_surface( HWND hwnd, const RECT *screen_rect )
{
    struct window_surface *surface, *driver_surface;
    UINT raw_dpi = 0;
    BOOL ret = FALSE;

    if (!(surface = window_surface_get( hwnd ))) return FALSE;
    get_win_monitor_dpi( hwnd, &raw_dpi );
    driver_surface = get_driver_window_surface( surface, raw_dpi );
    if (driver_surface && driver_surface->funcs == &wine_nx_surface_funcs)
        ret = screen_rect ? wine_nx_surface_present_screen_rect( driver_surface, screen_rect ) :
                            wine_nx_surface_present_full( driver_surface );
    window_surface_release( surface );
    return ret;
}

static void wine_nx_present_visible_popups( HWND except )
{
    struct wine_nx_surface_entry *entry;

    for (entry = wine_nx_surface_entries; entry; entry = entry->next)
    {
        if (!entry->visible || entry->hwnd == except) continue;
        if (!(get_window_long( entry->hwnd, GWL_STYLE ) & WS_POPUP)) continue;
        wine_nx_present_hwnd_surface( entry->hwnd, NULL );
    }
}

static HWND wine_nx_find_popup_restore_owner( HWND hwnd, HWND owner_hint, const RECT *old_rect )
{
    HWND owner = owner_hint;

    if (!owner) owner = get_window_relative( hwnd, GW_OWNER );
    if (!owner && old_rect && !IsRectEmpty( old_rect ))
        owner = NtUserWindowFromPoint( (old_rect->left + old_rect->right) / 2,
                                      (old_rect->top + old_rect->bottom) / 2 );
    if (!owner) owner = get_active_window();
    if (!owner) owner = get_focus();
    if (owner) owner = NtUserGetAncestor( owner, GA_ROOT );
    if (owner == hwnd) owner = 0;
    return owner;
}

static void wine_nx_restore_popup_owner( HWND hwnd, HWND owner_hint, const RECT *old_rect )
{
    HWND owner;
    BOOL presented;

    if (!(get_window_long( hwnd, GWL_STYLE ) & WS_POPUP)) return;
    wine_nx_note_surface_hidden( hwnd );
    owner = wine_nx_find_popup_restore_owner( hwnd, owner_hint, old_rect );
    if (!owner || owner == hwnd) return;

    presented = wine_nx_present_hwnd_surface( owner, old_rect );
    wine_nx_present_visible_popups( hwnd );
    nxdrv_trace( "[NXDRV] restore popup=%x owner=%x presented=%d",
                 (int)(ULONG_PTR)hwnd, (int)(ULONG_PTR)owner, presented, 0 );
    if (!presented)
        NtUserRedrawWindow( owner, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                           RDW_ALLCHILDREN | RDW_UPDATENOW );
}

static void wine_nx_surface_set_clip( struct window_surface *surface, const RECT *rects, UINT count )
{
    struct wine_nx_surface *nx_surface = wine_nx_surface_from_base( surface );
    RECT clip = {0};
    UINT i;

    nx_surface->has_clip = FALSE;
    if (!rects || !count) return;

    for (i = 0; i < count; i++)
    {
        RECT rect;

        if (!intersect_rect( &rect, &rects[i], &nx_surface->present_rect )) continue;
        if (!nx_surface->has_clip) clip = rect;
        else union_rect( &clip, &clip, &rect );
        nx_surface->has_clip = TRUE;
    }
    nx_surface->clip_rect = clip;
}

/* Blit the dirty region of the (BGRX, top-down) DIB to the linear RGBA8888
 * framebuffer at the surface's screen position, clipped to the screen. */
static BOOL wine_nx_surface_flush( struct window_surface *surface, const RECT *rect, const RECT *dirty,
                                   const BITMAPINFO *color_info, const void *color_bits, BOOL shape_changed,
                                   const BITMAPINFO *shape_info, const void *shape_bits )
{
    struct wine_nx_surface *nx_surface = wine_nx_surface_from_base( surface );
    RECT blit = *dirty;
    int sw = color_info->bmiHeader.biWidth;
    int sh = abs( color_info->bmiHeader.biHeight );
    int fbw = 0, fbh = 0, fbstride = 0;
    DWORD *fb;
    int sy, sx;
#ifdef __SWITCH__
    u64 t0, t1;
#endif

    (void)rect;

    if (!intersect_rect( &blit, &blit, &nx_surface->present_rect )) return TRUE;
    if (nx_surface->has_clip && !intersect_rect( &blit, &blit, &nx_surface->clip_rect )) return TRUE;

    nxdrv_trace_hot( "[NXDRV] flush dirty=%d,%d-%d,%d", blit.left, blit.top, blit.right, blit.bottom );

    /* surface_funcs_flush (this whole function, timed from dce.c) was
     * ~39ms bigger than the pixel loop alone -- splitting the three other
     * things this function does (sample tracing, the fb_lock call --
     * itself already known to be ~0ms for its own internal dkFenceWait,
     * so this isolates whatever else fb_lock/mutex-acquire costs -- and
     * fb_unlock) into their own timers instead of guessing which one it
     * is. See README, "Presentation Is Still Too Slow".
     *
     * Update: that split found every real piece of work here was already
     * small -- the "missing" time tracked the trace calls' own fflush()
     * cost, not any actual operation. Gated behind wine_nx_paint_trace_enabled
     * (see dlls/win32u/dce.c's switch_paint_trace()) for the same reason,
     * off by default. */
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled) t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
#endif
    trace_surface_samples( color_info, color_bits );
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled)
    {
        t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
        switch_paint_trace( "trace_samples", (unsigned int)(t1 - t0) );
    }
#endif

#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled) t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
#endif
    fb = wine_nx_fb_lock( &fbw, &fbh, &fbstride );
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled)
    {
        t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
        switch_paint_trace( "fb_lock_call", (unsigned int)(t1 - t0) );
    }
#endif
    nxdrv_trace_hot( "[NXDRV] fb_lock -> fb=%d fbw=%d fbh=%d stride=%d", fb ? 1 : 0, fbw, fbh, fbstride );
    if (!fb) return TRUE;

    /* Diagnosing a ~795ms/frame GDI-paint stall (gui_smoke.c's own QPC-based
     * phase timing). This scalar, unvectorized per-pixel loop is confirmed
     * to run synchronously inside EndPaint() -- NtUserEndPaint ->
     * switch_present_surface_dirty -> window_surface_flush -> this
     * function's .flush vtable slot (dlls/win32u/dce.c:1819-1826) -- so it's
     * in scope for what paint_avg measures, not deferred elsewhere. For a
     * full-window InvalidateRect(hwnd, NULL, FALSE) it's up to width*height
     * iterations of bounds-checking and BGRX->RGBA bit-swizzling with no
     * batching -- a real candidate the deko3d GPU-sync timing (all
     * near-zero) and the redraw_window/get_update_region/get_visible_region
     * IPC timing (~10% of paint_avg) didn't explain.
     *
     * EXPERIMENTAL follow-up: the per-pixel "if (u<0||u>=sw||dx<0||dx>=fbw)
     * continue" bounds check above was being re-evaluated for every one of
     * up to 921,600 pixels (a full 1280x720 window), but its result is
     * fully determined by sw/fbw/screen_origin.x (and sh/fbh/screen_origin.y
     * for the row check) -- none of which change across the blit, or even
     * across frames for a window that never moves. Hoisting the clamp out
     * of both loops (computed once, not 921,600 times) produces the exact
     * same set of written pixels -- this is a pure arithmetic restructuring
     * of which (sx,sy) pairs get iterated, not a behavior change -- just
     * without the wasted per-pixel branches. Unverified how much of
     * surface_funcs_flush's ~7-8ms this actually accounts for; that's what
     * the next hardware run is for. */
#ifdef __SWITCH__
    t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
#endif
    {
        int sx_min = blit.left;
        int sx_max = blit.right;
        int sy_min = blit.top;
        int sy_max = blit.bottom;

        if (sx_min < 0) sx_min = 0;
        if (sx_min < -nx_surface->screen_origin.x) sx_min = -nx_surface->screen_origin.x;
        if (sx_max > sw) sx_max = sw;
        if (sx_max > fbw - nx_surface->screen_origin.x) sx_max = fbw - nx_surface->screen_origin.x;

        if (sy_min < 0) sy_min = 0;
        if (sy_min < -nx_surface->screen_origin.y) sy_min = -nx_surface->screen_origin.y;
        if (sy_max > sh) sy_max = sh;
        if (sy_max > fbh - nx_surface->screen_origin.y) sy_max = fbh - nx_surface->screen_origin.y;

        for (sy = sy_min; sy < sy_max; sy++)
        {
            int dy = nx_surface->screen_origin.y + sy;
            const DWORD *src = (const DWORD *)color_bits + (size_t)sy * sw;
            DWORD *dst = fb + (size_t)dy * fbstride;

            sx = sx_min;
#ifdef WINE_NX_HAVE_NEON
            if (wine_nx_neon_blit_enabled)
            {
                /* vld4q_u8/vst4q_u8 deinterleave/reinterleave 16 pixels (64
                 * bytes) at a time into 4 separate 16-byte channel vectors --
                 * val[0]=every 1st byte, val[1]=every 2nd, val[2]=every 3rd,
                 * val[3]=every 4th, which for this port's BGRX-in-memory
                 * layout (0x00RRGGBB stored little-endian: byte0=B, byte1=G,
                 * byte2=R, byte3=0) means val[0]=B channel, val[1]=G,
                 * val[2]=R, val[3]=unused. Output needs byte0=R, byte1=G,
                 * byte2=B, byte3=0xFF, so this only relabels which loaded
                 * channel goes to which stored position -- out[0]=in[2],
                 * out[1]=in[1] unchanged, out[2]=in[0], out[3]=constant
                 * 0xFF -- and lets vld4/vst4 handle all the actual byte
                 * interleaving, rather than hand-rolling a byte shuffle.
                 * Verified bit-exact against the scalar reference below
                 * across ~800,000 test pixels on a native arm64 build
                 * (known values, every remainder width 0-63, every exact
                 * multiple of 16, 200 rounds of full-range random fuzzing
                 * including the normally-unused top byte) -- not just
                 * reasoned about, actually run and compared. Falls through
                 * to the identical scalar loop below for anything under 16
                 * pixels. */
                uint8x16_t alpha_ff = vdupq_n_u8( 0xff );
                for (; sx + 16 <= sx_max; sx += 16)
                {
                    uint8x16x4_t px = vld4q_u8( (const uint8_t *)(src + sx) );
                    uint8x16x4_t out;
                    out.val[0] = px.val[2];
                    out.val[1] = px.val[1];
                    out.val[2] = px.val[0];
                    out.val[3] = alpha_ff;
                    vst4q_u8( (uint8_t *)(dst + nx_surface->screen_origin.x + sx), out );
                }
            }
#endif
            for (; sx < sx_max; sx++)
            {
                int dx = nx_surface->screen_origin.x + sx;
                DWORD p = src[sx];
                /* BGRX (0x00RRGGBB) -> RGBA8888 (R,G,B,A bytes), opaque */
                dst[dx] = 0xff000000u | (p & 0x0000ff00u) | ((p >> 16) & 0xffu) | ((p & 0xffu) << 16);
            }
        }
    }
#ifdef __SWITCH__
    t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
    /* This was wrongly reasoned as "unlimited but harmless" before tonight:
     * nxdrv_trace() (used here, unlike nxdrv_trace_hot()) has no rate limit
     * of its own at all -- every call unconditionally fflush()es to the SD
     * card, every frame, forever. Part of Thread A's 2.7s-stall
     * investigation; ported here from the already-verified fix on
     * fps-investigation. Rate-limited to 5 samples, matching every other
     * "just show me examples" trace in this file. */
    {
        static unsigned int pixel_loop_logged;
        if (pixel_loop_logged < 5)
        {
            nxdrv_trace( "[NXDRV][TIMING] pixel loop took %dms w=%d h=%d px=%d",
                        (int)(t1 - t0), blit.right - blit.left, blit.bottom - blit.top,
                        (blit.right - blit.left) * (blit.bottom - blit.top) );
            pixel_loop_logged++;
        }
    }
#endif

#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled) t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
#endif
    wine_nx_fb_unlock();
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled)
    {
        t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
        switch_paint_trace( "fb_unlock_call", (unsigned int)(t1 - t0) );
    }
#endif
    return TRUE;
}

static void wine_nx_surface_destroy( struct window_surface *surface )
{
    /* The generic window_surface_release() frees the header storage. */
    (void)surface;
}

static const struct window_surface_funcs wine_nx_surface_funcs =
{
    wine_nx_surface_set_clip,
    wine_nx_surface_flush,
    wine_nx_surface_destroy,
};

/**********************************************************************
 *           wine_nx_drv_UpdateDisplayDevices
 *
 * Report a single 1280x720 primary monitor so win32u has a real virtual
 * screen; without it the desktop is 0x0 and every window clips to nothing.
 */
#define WINE_NX_SCREEN_W 1280
#define WINE_NX_SCREEN_H 720

UINT wine_nx_drv_UpdateDisplayDevices( const struct gdi_device_manager *dm, void *param )
{
    static const DWORD source_flags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP |
                                      DISPLAY_DEVICE_PRIMARY_DEVICE | DISPLAY_DEVICE_VGA_COMPATIBLE;
    RECT rc = { 0, 0, WINE_NX_SCREEN_W, WINE_NX_SCREEN_H };
    struct pci_id pci_id = { 0 };
    struct gdi_monitor monitor = { .rc_monitor = rc, .rc_work = rc };
    DEVMODEW mode =
    {
        .dmSize   = sizeof(mode),
        .dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL |
                    DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY,
        .dmBitsPerPel = 32, .dmPelsWidth = WINE_NX_SCREEN_W, .dmPelsHeight = WINE_NX_SCREEN_H,
        .dmDisplayFrequency = 60,
    };
    UINT dpi = NtUserGetSystemDpiForProcess( NULL );
    DEVMODEW current = mode;

    dm->add_gpu( "Wine NX GPU", &pci_id, NULL, param );
    dm->add_source( "Default", source_flags, dpi, param );
    dm->add_monitor( &monitor, param );
    current.dmFields |= DM_POSITION;
    dm->add_modes( &current, 1, &mode, param );
    nxdrv_trace( "[NXDRV] UpdateDisplayDevices -> %dx%d", WINE_NX_SCREEN_W, WINE_NX_SCREEN_H, 0, 0 );
    return STATUS_SUCCESS;
}

/**********************************************************************
 *           wine_nx_drv_ProcessEvents
 *
 * Expose the primary Switch touchscreen contact as an absolute mouse.  This
 * gives classic Win32 applications useful input immediately, including
 * non-client hit testing, menus and controls.
 */
BOOL wine_nx_drv_ProcessEvents( DWORD mask )
{
    static BOOL was_down;
    static int last_x, last_y;
    INPUT input = {0};
    BOOL down;
    int x = last_x, y = last_y;

    (void)mask;
    wine_nx_fb_present();
    down = wine_nx_touch_poll( &x, &y );
    if (down)
    {
        if (x < 0) x = 0;
        else if (x >= WINE_NX_SCREEN_W) x = WINE_NX_SCREEN_W - 1;
        if (y < 0) y = 0;
        else if (y >= WINE_NX_SCREEN_H) y = WINE_NX_SCREEN_H - 1;
    }

    if (down && (!was_down || x != last_x || y != last_y))
    {
        input.type = INPUT_MOUSE;
        input.mi.dx = x;
        input.mi.dy = y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        if (!was_down) input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
        NtUserSendHardwareInput( 0, 0, &input, 0 );
        nxdrv_trace_hot( "[NXINPUT] touch state=%d x=%d y=%d", !was_down ? 1 : 2, x, y, 0 );
        last_x = x;
        last_y = y;
    }
    else if (!down && was_down)
    {
        input.type = INPUT_MOUSE;
        input.mi.dx = last_x;
        input.mi.dy = last_y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP;
        NtUserSendHardwareInput( 0, 0, &input, 0 );
        nxdrv_trace_hot( "[NXINPUT] touch up x=%d y=%d", last_x, last_y, 0, 0 );
    }

    was_down = down;
    wine_nx_fb_present();
    return down;
}

/**********************************************************************
 *           wine_nx_drv_CreateWindow
 */
BOOL wine_nx_drv_CreateWindow( HWND hwnd )
{
    nxdrv_trace( "[NXDRV] CreateWindow hwnd=%p", (int)(ULONG_PTR)hwnd, 0, 0, 0 );
    return TRUE;
}

/**********************************************************************
 *           wine_nx_drv_CreateWindowSurface
 */
BOOL wine_nx_drv_CreateWindowSurface( HWND hwnd, BOOL layered, const RECT *surface_rect,
                                      struct window_surface **surface )
{
    char buffer[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *info = (BITMAPINFO *)buffer;
    int width  = surface_rect->right - surface_rect->left;
    int height = surface_rect->bottom - surface_rect->top;

    if (*surface) window_surface_release( *surface );
    *surface = NULL;
    if (width <= 0 || height <= 0) return TRUE;

    memset( info, 0, sizeof(*info) );
    info->bmiHeader.biSize        = sizeof(info->bmiHeader);
    info->bmiHeader.biWidth       = width;
    info->bmiHeader.biHeight      = -height; /* top-down */
    info->bmiHeader.biPlanes      = 1;
    info->bmiHeader.biBitCount    = 32;
    info->bmiHeader.biSizeImage   = get_dib_image_size( info );
    info->bmiHeader.biCompression = BI_RGB;

    *surface = window_surface_create( sizeof(struct wine_nx_surface), &wine_nx_surface_funcs,
                                      hwnd, surface_rect, info, 0 );
    nxdrv_trace( "[NXDRV] CreateWindowSurface rect=%d,%d %dx%d", surface_rect->left, surface_rect->top,
                 width, height );
    nxdrv_trace( "[NXDRV] surface_create -> %d", *surface ? 1 : 0, 0, 0, 0 );
    return TRUE;
}

/**********************************************************************
 *           wine_nx_drv_WindowPosChanged
 *
 * Present the current surface contents after a geometry/visibility change.
 */
void wine_nx_drv_WindowPosChanged( HWND hwnd, HWND insert_after, HWND owner_hint, UINT swp_flags,
                                   const struct window_rects *new_rects, struct window_surface *surface )
{
    BOOL initial_redraw = FALSE;
    RECT old_screen_rect;
    BOOL has_old_screen_rect = wine_nx_get_cached_screen_rect( hwnd, &old_screen_rect );

    nxdrv_trace( "[NXDRV] WindowPosChanged surface=%d swp=%x", surface ? 1 : 0, swp_flags, 0, 0 );
    if (swp_flags & SWP_HIDEWINDOW)
    {
        wine_nx_restore_popup_owner( hwnd, owner_hint, has_old_screen_rect ? &old_screen_rect : NULL );
        return;
    }
    if (surface)
    {
        if (surface->funcs == &wine_nx_surface_funcs)
        {
            struct wine_nx_surface *nx_surface = wine_nx_surface_from_base( surface );
            RECT allocation = { 0, 0, surface->rect.right - surface->rect.left,
                                surface->rect.bottom - surface->rect.top };
            int new_width = new_rects->visible.right - new_rects->visible.left;
            int new_height = new_rects->visible.bottom - new_rects->visible.top;

            if (nx_surface->initial_redraw_done &&
                (nx_surface->screen_origin.x != new_rects->visible.left ||
                 nx_surface->screen_origin.y != new_rects->visible.top ||
                 nx_surface->present_rect.right != new_width ||
                 nx_surface->present_rect.bottom != new_height))
                wine_nx_restore_popup_owner( hwnd, owner_hint,
                                             has_old_screen_rect ? &old_screen_rect : NULL );

            initial_redraw = !nx_surface->initial_redraw_done;
            nx_surface->initial_redraw_done = TRUE;
            nx_surface->screen_origin.x = new_rects->visible.left;
            nx_surface->screen_origin.y = new_rects->visible.top;
            nx_surface->present_rect.left = nx_surface->present_rect.top = 0;
            nx_surface->present_rect.right = new_width;
            nx_surface->present_rect.bottom = new_height;
            intersect_rect( &nx_surface->present_rect, &nx_surface->present_rect, &allocation );
            wine_nx_note_surface_present( hwnd, &nx_surface->screen_origin, &nx_surface->present_rect );
            nxdrv_trace( "[NXDRV] present rect=%d,%d %dx%d",
                         nx_surface->screen_origin.x, nx_surface->screen_origin.y,
                         nx_surface->present_rect.right, nx_surface->present_rect.bottom );

            wine_nx_surface_mark_full_dirty( surface );
            if (!wine_nx_surface_present_full( surface ))
                window_surface_flush( surface );

            /* Child controls can paint while the top-level still owns the dummy
             * surface during ShowWindow.  Repaint the complete hierarchy once
             * the real surface has been installed so those discarded pixels are
             * produced again on the drawable surface. */
            if (initial_redraw)
            {
                nxdrv_trace( "[NXDRV] initial redraw hwnd=%p", (int)(ULONG_PTR)hwnd, 0, 0, 0 );
                NtUserRedrawWindow( hwnd, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                                   RDW_ALLCHILDREN | RDW_UPDATENOW );
                wine_nx_surface_mark_full_dirty( surface );
                wine_nx_surface_present_full( surface );
            }
        }
        else window_surface_flush( surface );
        wine_nx_fb_present();
    }
}
