/*
 * Window painting functions
 *
 * Copyright 1993, 1994, 1995, 2001, 2004, 2005, 2008 Alexandre Julliard
 * Copyright 1996, 1997, 1999 Alex Korobka
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "ntgdi_private.h"
#include "ntuser_private.h"
#include "wine/opengl_driver.h"
#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(win);

#ifdef __SWITCH__
/* Sub-phase timing for NtUserBeginPaint/NtUserEndPaint, added to find out
 * which specific step inside that span accounts for the gap between
 * paint_avg (measured in wine-nx-probe/samples/gui-smoke/gui_smoke.c) and
 * the sum of everything already individually timed (draw_frame's
 * blit_avg/shapes_avg, the [NXIPC][TIMING] calls, the [NXDRV][TIMING]
 * pixel loop) -- see README, "Presentation Is Still Too Slow".
 *
 * Update: as this got split into finer and finer sub-phases (down to
 * window_surface_flush()'s individual lock/rect-math/get_color/shape/
 * trace-macro/funcs_flush/unlock steps), every actual piece of work came
 * back at 0ms or single-digit ms -- the remaining "unaccounted" time
 * tracked the number of trace calls themselves, not any real operation.
 * Same root cause as the raw syscall trace fixed earlier (log_line()
 * fflush()es every line to the SD card), just relocated to this
 * lower-frequency-per-call-site-but-many-call-sites tier: up to ~14
 * fflushes per window_surface_flush() invocation. Gated off by default
 * behind wine_nx_paint_trace_enabled (WINE_NX_PAINT_TRACE env var or
 * sdmc:/switch/wine/painttrace.txt, same pattern as the syscall trace)
 * for exactly that reason -- flip it on when actively debugging this
 * span, leave it off for a clean fps measurement. */
extern void wine_nx_runtime_trace( const char *msg );
extern int wine_nx_paint_trace_enabled;
/* wine-nx-probe/source/runtime.c -- A/B toggle for flush_window_surfaces()'s
 * debounce, see the long comment on wine_nx_flush_legacy_select() there. */
extern int wine_nx_flush_legacy_enabled;
/* wine-nx-probe/source/runtime.c -- A/B toggle for the combined
 * get_paint_regions request, see switch_prefetch_paint_regions() below and
 * the long comment on wine_nx_batch_paint_regions_select() there. Off by
 * default: this collapses two of NtUserBeginPaint's IPC round trips into
 * one, which is a real protocol change (wrong here = silent visual
 * corruption, not a crash), not a diagnostic. */
extern int wine_nx_batch_paint_regions_enabled;
/* wine-nx-probe/source/runtime.c -- A/B toggle for switch_update_now(),
 * see the long comment there and on wine_nx_skip_redundant_update_check_select()
 * in runtime.c. Off by default, same correctness-risk-change reasoning as
 * wine_nx_batch_paint_regions_enabled above. */
extern int wine_nx_skip_redundant_update_check_enabled;
/* dlls/win32u/window.c -- bumped on every NtUserCreateWindowEx/
 * NtUserSetParent call (attempted, not just successful). See the long
 * comment on switch_update_now() below for why this exists. */
extern LONG switch_window_tree_generation;
/* wine-nx-probe/source/runtime.c -- A/B toggle for
 * switch_redraw_window_updatenow(), see the long comment there. Off by
 * default, same correctness-risk-change reasoning as
 * wine_nx_batch_paint_regions_enabled above -- this is a real protocol
 * change to NtUserRedrawWindow's own control flow, not a diagnostic. Only
 * takes effect when wine_nx_skip_redundant_update_check_enabled is also on,
 * since it depends on switch_update_now() existing. */
extern int wine_nx_batch_redraw_updatenow_enabled;

/* Bumped only by the specific calls that can change any window's paint/
 * update state (redraw_window_rects() and switch_redraw_window_updatenow()
 * below -- the only two that ever send HORIZON_RDW_INVALIDATE and so are
 * the only ones that reach horizon_server_mark_window_update_locked() on
 * the server, including its recursive effect on children). Deliberately
 * NOT every server call: an earlier version used wine_server_call_generation
 * (dlls/ntdll/unix/server.c, bumped on literally any completed server call)
 * for this and it never once hit on hardware -- check_for_events(QS_PAINT),
 * called unconditionally at the top of every RDW_UPDATENOW NtUserRedrawWindow
 * call (i.e. every UpdateWindow()), makes its own set_queue_mask server call
 * before switch_update_now() ever gets a chance to check the cache, which
 * has nothing to do with paint state but still bumped that counter every
 * single time -- confirmed via [NXIPC][TIMING] counts: get_update_flags_ex
 * fired once per frame same as redraw_window, zero [NXCACHE] hits, with the
 * generic counter. This one is still conservative in the same safe
 * direction (an unrelated redraw_window call on some other window still
 * correctly invalidates a stale cache), just scoped to what can actually
 * matter instead of everything. */
static unsigned int switch_paint_state_generation;

/* Populated by redraw_window_rects() below, opportunistically reused by
 * switch_update_now() when it immediately follows the redraw_window call
 * that produced it with no other paint-state-changing call in between --
 * exactly InvalidateRect()+UpdateWindow()'s own idiom (the common case
 * protocol 312 above can't help with, since RDW_INVALIDATE and
 * RDW_UPDATENOW never share one NtUserRedrawWindow call for that idiom).
 * See the comment on redraw_window_reply in server_protocol.h. One-shot:
 * cleared the moment it's consumed, so a later, unrelated UpdateWindow()
 * call never reuses stale data. __thread for the same reason
 * switch_paint_regions_prefetch below is -- no reason to risk a
 * cross-thread false-share even though Wine's per-window UI threading
 * model makes this effectively single-threaded per window in practice. */
struct switch_redraw_window_cache
{
    int          valid;
    HWND         hwnd;
    HWND         child;
    UINT         flags;
    BOOL         has_children;
    unsigned int gen_after;
};
static __thread struct switch_redraw_window_cache switch_redraw_window_cache;

/* Update 2: switch_paint_trace() itself used to fflush() one line per
 * call (log_line(), unconditional fflush) -- exactly the same mistake
 * as the raw syscall trace, just at this tier's own call frequency.
 * Aggregate in memory instead (name -> running sum/count/max) and flush
 * ONE combined line per second, the same pattern gui_smoke.c's own
 * gui_timing.log already uses for its phase averages (phase_stat_t /
 * timing_maybe_flush). This changes what wine_nx_paint_trace_enabled=1
 * looks like in the log (one "[NXPAINT][AVG] name=avg/max/n ..." line
 * per second instead of one "[NXPAINT][TIMING] name took Nms" line per
 * call) but not its off-by-default behavior -- still zero cost when
 * disabled. Not thread-safety-hardened (no lock around the phase
 * table): window_surface_flush()/wine_nx_surface_flush() are only ever
 * called from the single render thread in every trace captured tonight,
 * so a benign race here would at worst skew one sample, never crash --
 * acceptable for a diagnostic-only structure that's off by default. */
/* Exactly 24 distinct phase names are already in use across dce.c/
 * winnx_drv.c as of this session (confirmed by grepping every
 * switch_paint_trace() call site, not assumed) -- meaning the array was
 * already at its exact limit, not merely close to it. switch_paint_trace()'s
 * own registration check (below) silently drops any phase past the cap:
 * no error, no truncation warning, the sample just never accumulates.
 * Same class of silent-data-loss bug as everything else chased this
 * session, just found by counting instead of by a hardware log showing
 * a phase missing. Raised with real headroom, not just enough to fit
 * today's count. */
#define SWITCH_PAINT_TRACE_MAX_PHASES 40

struct switch_paint_trace_phase
{
    const char *name;
    unsigned long long sum_ms;
    unsigned int count;
    unsigned int max_ms;
};

static struct switch_paint_trace_phase switch_paint_trace_phases[SWITCH_PAINT_TRACE_MAX_PHASES];
static unsigned int switch_paint_trace_phase_count;
static u64 switch_paint_trace_epoch_ms;

static void switch_paint_trace_flush(void)
{
    /* Sized for SWITCH_PAINT_TRACE_MAX_PHASES (40) entries at the longest
     * real phase name currently in use plus its "=NNNms(max=NNN,n=NN)"
     * suffix, with headroom -- the previous 900-byte size was tuned for
     * a smaller phase count and would silently truncate the printed line
     * (not the underlying data, just what actually reaches the log) once
     * enough phases were in use, same bug class as the array cap above. */
    char buf[2048];
    int len = 0;
    unsigned int i;

    len += snprintf( buf + len, sizeof(buf) - len, "[NXPAINT][AVG]" );
    for (i = 0; i < switch_paint_trace_phase_count && len < (int)sizeof(buf) - 64; i++)
    {
        struct switch_paint_trace_phase *p = &switch_paint_trace_phases[i];
        unsigned long long avg = p->count ? p->sum_ms / p->count : 0;

        len += snprintf( buf + len, sizeof(buf) - len, " %s=%llums(max=%u,n=%u)",
                         p->name, avg, p->max_ms, p->count );
        p->sum_ms = 0;
        p->count = 0;
        p->max_ms = 0;
    }
    wine_nx_runtime_trace( buf );
}

void switch_paint_trace( const char *phase, unsigned int ms )
{
    unsigned int i;
    u64 now_ms;

    if (!wine_nx_paint_trace_enabled) return;

    for (i = 0; i < switch_paint_trace_phase_count; i++)
        if (!strcmp( switch_paint_trace_phases[i].name, phase )) break;
    if (i == switch_paint_trace_phase_count && i < SWITCH_PAINT_TRACE_MAX_PHASES)
    {
        switch_paint_trace_phases[i].name = phase;
        switch_paint_trace_phase_count++;
    }
    if (i < SWITCH_PAINT_TRACE_MAX_PHASES)
    {
        switch_paint_trace_phases[i].sum_ms += ms;
        switch_paint_trace_phases[i].count++;
        if (ms > switch_paint_trace_phases[i].max_ms) switch_paint_trace_phases[i].max_ms = ms;
    }

    now_ms = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
    if (!switch_paint_trace_epoch_ms) switch_paint_trace_epoch_ms = now_ms;
    if (now_ms - switch_paint_trace_epoch_ms >= 1000)
    {
        switch_paint_trace_flush();
        switch_paint_trace_epoch_ms = now_ms;
    }
}
#endif

struct dce
{
    struct list entry;         /* entry in global DCE list */
    HDC         hdc;
    HWND        hwnd;
    HRGN        clip_rgn;
    UINT        flags;
    LONG        count;         /* usage count; 0 or 1 for cache DCEs, always 1 for window DCEs,
                                  always >= 1 for class DCEs */
};

static struct list dce_list = LIST_INIT(dce_list);

#define DCE_CACHE_SIZE 64

static struct list window_surfaces = LIST_INIT( window_surfaces );
static pthread_mutex_t surfaces_lock = PTHREAD_MUTEX_INITIALIZER;

/*******************************************************************
 * Dummy window surface for windows that shouldn't get painted.
 */

static void dummy_surface_set_clip( struct window_surface *window_surface, const RECT *rects, UINT count )
{
    /* nothing to do */
}

static BOOL dummy_surface_flush( struct window_surface *window_surface, const RECT *rect, const RECT *dirty,
                                 const BITMAPINFO *color_info, const void *color_bits, BOOL shape_changed,
                                 const BITMAPINFO *shape_info, const void *shape_bits )
{
    /* nothing to do */
    return TRUE;
}

static void dummy_surface_destroy( struct window_surface *window_surface )
{
    /* nothing to do */
}

static const struct window_surface_funcs dummy_surface_funcs =
{
    dummy_surface_set_clip,
    dummy_surface_flush,
    dummy_surface_destroy
};

struct window_surface dummy_surface =
{
    .funcs = &dummy_surface_funcs,
    .ref = 1,
    .rect = {.right = 1, .bottom = 1},
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

/*******************************************************************
 * Off-screen window surface.
 */

static void offscreen_window_surface_set_clip( struct window_surface *surface, const RECT *rects, UINT count )
{
}

static BOOL offscreen_window_surface_flush( struct window_surface *surface, const RECT *rect, const RECT *dirty,
                                            const BITMAPINFO *color_info, const void *color_bits, BOOL shape_changed,
                                            const BITMAPINFO *shape_info, const void *shape_bits )
{
    return TRUE;
}

static void offscreen_window_surface_destroy( struct window_surface *surface )
{
}

static const struct window_surface_funcs offscreen_window_surface_funcs =
{
    offscreen_window_surface_set_clip,
    offscreen_window_surface_flush,
    offscreen_window_surface_destroy
};

static void create_offscreen_window_surface( HWND hwnd, const RECT *surface_rect, struct window_surface **window_surface )
{
    char buffer[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    struct window_surface *surface, *previous;
    BITMAPINFO *info = (BITMAPINFO *)buffer;

    TRACE( "hwnd %p, surface_rect %s, window_surface %p.\n", hwnd, wine_dbgstr_rect( surface_rect ), window_surface );

    /* check that old surface is an offscreen_window_surface, or release it */
    if ((previous = *window_surface) && previous->funcs == &offscreen_window_surface_funcs) return;

    memset( info, 0, sizeof(*info) );
    info->bmiHeader.biSize        = sizeof(info->bmiHeader);
    info->bmiHeader.biWidth       = surface_rect->right;
    info->bmiHeader.biHeight      = -surface_rect->bottom; /* top-down */
    info->bmiHeader.biPlanes      = 1;
    info->bmiHeader.biBitCount    = 32;
    info->bmiHeader.biSizeImage   = get_dib_image_size( info );
    info->bmiHeader.biCompression = BI_RGB;

    *window_surface = window_surface_create( sizeof(*surface), &offscreen_window_surface_funcs, hwnd, surface_rect, info, 0 );

    if (previous) window_surface_release( previous );
}

struct scaled_surface
{
    struct window_surface header;
    struct window_surface *target_surface;
    UINT dpi_from;
    UINT dpi_to;
};

static struct scaled_surface *get_scaled_surface( struct window_surface *window_surface )
{
    return CONTAINING_RECORD( window_surface, struct scaled_surface, header );
}

static void scaled_surface_set_clip( struct window_surface *window_surface, const RECT *rects, UINT count )
{
    struct scaled_surface *surface = get_scaled_surface( window_surface );
    HRGN hrgn = map_dpi_region( window_surface->clip_region, surface->dpi_from, surface->dpi_to );
    window_surface_set_clip( surface->target_surface, hrgn );
    if (hrgn) NtGdiDeleteObjectApp( hrgn );
}

static BOOL scaled_surface_flush( struct window_surface *window_surface, const RECT *rect, const RECT *dirty,
                                  const BITMAPINFO *color_info, const void *color_bits, BOOL shape_changed,
                                  const BITMAPINFO *shape_info, const void *shape_bits )
{
    struct scaled_surface *surface = get_scaled_surface( window_surface );
    RECT src = *dirty, dst;
    HDC hdc_dst, hdc_src;

    src.left &= ~7;
    src.top &= ~7;
    src.right = (src.right + 7) & ~7;
    src.bottom = (src.bottom + 7) & ~7;

    dst = map_dpi_rect( src, surface->dpi_from, surface->dpi_to );

    hdc_dst = NtGdiCreateCompatibleDC( 0 );
    hdc_src = NtGdiCreateCompatibleDC( 0 );

    NtGdiSelectBitmap( hdc_src, window_surface->color_bitmap );
    NtGdiSelectBitmap( hdc_dst, surface->target_surface->color_bitmap );

    /* FIXME: implement HALFTONE with alpha for layered surfaces */
    if (!window_surface->alpha_mask) set_stretch_blt_mode( hdc_dst, STRETCH_HALFTONE );

    NtGdiStretchBlt( hdc_dst, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                     hdc_src, src.left, src.top, src.right - src.left, src.bottom - src.top,
                     SRCCOPY, 0 );

    NtGdiDeleteObjectApp( hdc_dst );
    NtGdiDeleteObjectApp( hdc_src );

    window_surface_lock( surface->target_surface );
    add_bounds_rect( &surface->target_surface->bounds, &dst );
    window_surface_unlock( surface->target_surface );

    if (shape_changed)
    {
        HRGN hrgn = map_dpi_region( window_surface->shape_region, surface->dpi_from, surface->dpi_to );
        window_surface_set_shape( surface->target_surface, hrgn );
        if (hrgn) NtGdiDeleteObjectApp( hrgn );

        window_surface_set_layered( surface->target_surface, window_surface->color_key,
                                    window_surface->alpha_bits, window_surface->alpha_mask );
    }

    window_surface_flush( surface->target_surface );
    return TRUE;
}

static void scaled_surface_destroy( struct window_surface *window_surface )
{
    struct scaled_surface *surface = get_scaled_surface( window_surface );
    window_surface_release( surface->target_surface );
}

static const struct window_surface_funcs scaled_surface_funcs =
{
    scaled_surface_set_clip,
    scaled_surface_flush,
    scaled_surface_destroy
};

static void scaled_surface_set_target( struct scaled_surface *surface, struct window_surface *target, UINT dpi_to )
{
    if (surface->target_surface) window_surface_release( surface->target_surface );
    window_surface_add_ref( (surface->target_surface = target) );
    surface->dpi_to = dpi_to;
}

static struct window_surface *scaled_surface_create( HWND hwnd, const RECT *surface_rect, UINT dpi_from, UINT dpi_to,
                                                     struct window_surface *target_surface )
{
    char buffer[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *info = (BITMAPINFO *)buffer;
    struct window_surface *window_surface;
    struct scaled_surface *surface;

    memset( info, 0, sizeof(*info) );
    info->bmiHeader.biSize        = sizeof(info->bmiHeader);
    info->bmiHeader.biWidth       = surface_rect->right;
    info->bmiHeader.biHeight      = -surface_rect->bottom; /* top-down */
    info->bmiHeader.biPlanes      = 1;
    info->bmiHeader.biBitCount    = 32;
    info->bmiHeader.biSizeImage   = get_dib_image_size( info );
    info->bmiHeader.biCompression = BI_RGB;

    if ((window_surface = window_surface_create( sizeof(*surface), &scaled_surface_funcs, hwnd, surface_rect, info, 0 )))
    {
        surface = get_scaled_surface( window_surface );
        surface->dpi_from = dpi_from;
        scaled_surface_set_target( surface, target_surface, dpi_to );
    }

    return window_surface;
}

static RECT get_surface_rect( RECT rect )
{
    OffsetRect( &rect, -rect.left, -rect.top );

    rect.left &= ~127;
    rect.top  &= ~127;
    rect.right  = max( rect.left + 128, (rect.right + 127) & ~127 );
    rect.bottom = max( rect.top + 128, (rect.bottom + 127) & ~127 );

    return rect;
}

void create_window_surface( HWND hwnd, BOOL create_layered, const RECT *surface_rect, UINT monitor_dpi,
                            struct window_surface **window_surface )
{
    struct window_surface *previous, *driver_surface;
    UINT dpi = get_dpi_for_window( hwnd );
    RECT monitor_rect;


    monitor_rect = get_surface_rect( map_dpi_rect( *surface_rect, dpi, monitor_dpi ) );
    if ((driver_surface = get_driver_window_surface( *window_surface, monitor_dpi )))
    {
        /* reuse the underlying driver surface only if it also matches the target monitor rect */
        if (EqualRect( &driver_surface->rect, &monitor_rect )) window_surface_add_ref( driver_surface );
        else window_surface_add_ref( (driver_surface = &dummy_surface) );
    }

    if (!user_driver->pCreateWindowSurface( hwnd, create_layered, &monitor_rect, &driver_surface ))
    {
        if (driver_surface) window_surface_release( driver_surface );
        if (*window_surface)
        {
            /* create an offscreen window surface if the driver doesn't implement CreateWindowSurface */
            create_offscreen_window_surface( hwnd, surface_rect, window_surface );
        }
        return;
    }

    if (!driver_surface || dpi == monitor_dpi)
    {
        if (*window_surface) window_surface_release( *window_surface );
        *window_surface = driver_surface;
        return;
    }

    /* reuse previous scaling surface, update its target to the driver surface */
    if ((previous = *window_surface) && previous->funcs == &scaled_surface_funcs)
    {
        struct scaled_surface *surface = get_scaled_surface( previous );
        scaled_surface_set_target( surface, driver_surface, monitor_dpi );
        window_surface_release( driver_surface );
        return;
    }
    if (previous) window_surface_release( previous );

    *window_surface = scaled_surface_create( hwnd, surface_rect, dpi, monitor_dpi, driver_surface );
    window_surface_release( driver_surface );
}

struct window_surface *get_driver_window_surface( struct window_surface *surface, UINT monitor_dpi )
{
    if (!surface || surface == &dummy_surface) return surface;
    if (surface->funcs != &scaled_surface_funcs) return surface;
    if (get_scaled_surface( surface )->dpi_to != monitor_dpi) return &dummy_surface;
    return get_scaled_surface( surface )->target_surface;
}

/* window surface common helpers */

static UINT get_color_component( UINT color, UINT mask )
{
    int shift;
    for (shift = 0; !(mask & 1); shift++) mask >>= 1;
    return (color * mask / 255) << shift;
}

static COLORREF get_color_key( const BITMAPINFO *info, COLORREF color_key )
{
    if (color_key == CLR_INVALID) return CLR_INVALID;
    if (info->bmiHeader.biBitCount <= 8) return CLR_INVALID;
    if (color_key & (1 << 24)) /* PALETTEINDEX */ return 0;
    if (color_key >> 16 == 0x10ff) /* DIBINDEX */ return 0;

    if (info->bmiHeader.biCompression == BI_BITFIELDS)
    {
        UINT *masks = (UINT *)info->bmiColors;
        return get_color_component( GetRValue( color_key ), masks[0] ) |
               get_color_component( GetGValue( color_key ), masks[1] ) |
               get_color_component( GetBValue( color_key ), masks[2] );
    }

    return (GetRValue( color_key ) << 16) | (GetGValue( color_key ) << 8) | GetBValue( color_key );
}

static void set_surface_shape_rect( BYTE *bits, UINT stride, const RECT *rect )
{
    BYTE *begin = bits + rect->top * stride, *end = bits + rect->bottom * stride;
    UINT l = rect->left / 8, l_mask, r = rect->right / 8, r_mask;

    /* 1bpp bitmaps use MSB for lowest X */
    l_mask = (1 << (8 - (rect->left & 7))) - 1;
    r_mask = (1 << (8 - (rect->right & 7))) - 1;
    if (r_mask == 0xff) { r--; r_mask = 0; } /* avoid writing to the next byte */

    if (rect->right - rect->left == 8 * stride) memset( begin, 0xff, end - begin );
    else if (l == r) for (bits = begin; bits < end; bits += stride) bits[l] |= l_mask & r_mask;
    else if (l < r)
    {
        for (bits = begin; bits < end; bits += stride)
        {
            bits[l] |= l_mask;
            memset( bits + l + 1, 0xff, r - l - 1 );
            bits[r] |= ~r_mask;
        }
    }
}

static void *window_surface_get_shape( struct window_surface *surface, BITMAPINFO *info )
{
    struct bitblt_coords coords = {0};
    struct gdi_image_bits gdi_bits;
    BITMAPOBJ *bmp;

    if (!(bmp = GDI_GetObjPtr( surface->shape_bitmap, NTGDI_OBJ_BITMAP ))) return NULL;
    get_image_from_bitmap( bmp, info, &gdi_bits, &coords );
    GDI_ReleaseObj( surface->shape_bitmap );

    return gdi_bits.ptr;
}

static BYTE shape_from_alpha_mask( UINT32 *bits, UINT32 alpha_mask, UINT32 alpha )
{
    BYTE i, bit, mask = 0;
    for (i = 0, bit = 7; i < 8; i++, bit--) mask |= ((bits[i] & alpha_mask) == alpha) << bit;
    return ~mask;
}

static BYTE shape_from_color_key_16( UINT16 *bits, UINT16 color_mask, UINT16 color_key )
{
    BYTE i, bit, mask = 0;
    for (i = 0, bit = 7; i < 8; i++, bit--) mask |= ((bits[i] & color_mask) == color_key) << bit;
    return ~mask;
}

static BYTE shape_from_color_key_32( UINT32 *bits, UINT32 color_mask, UINT32 color_key )
{
    BYTE i, bit, mask = 0;
    for (i = 0, bit = 7; i < 8; i++, bit--) mask |= ((bits[i] & color_mask) == color_key) << bit;
    return ~mask;
}

static BOOL set_surface_shape( struct window_surface *surface, const RECT *rect, const RECT *dirty,
                               const BITMAPINFO *color_info, void *color_bits )
{
    UINT width, height, x, y, shape_stride, color_stride, alpha_mask = surface->alpha_mask;
    char shape_buf[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *shape_info = (BITMAPINFO *)shape_buf;
    COLORREF color_key = surface->color_key;
    void *shape_bits, *old_shape;
    RECT *shape_rect, tmp_rect;
    WINEREGION *data;
    BOOL ret;

    width = color_info->bmiHeader.biWidth;
    height = abs( color_info->bmiHeader.biHeight );
    assert( !(width & 7) ); /* expect 1bpp bitmap to be aligned on bytes */

    if (!surface->shape_bitmap) surface->shape_bitmap = NtGdiCreateBitmap( width, height, 1, 1, NULL );
    if (!(shape_bits = window_surface_get_shape( surface, shape_info ))) return FALSE;

    old_shape = malloc( shape_info->bmiHeader.biSizeImage );
    memcpy( old_shape, shape_bits, shape_info->bmiHeader.biSizeImage );

    color_stride = color_info->bmiHeader.biSizeImage / height;
    shape_stride = shape_info->bmiHeader.biSizeImage / abs( shape_info->bmiHeader.biHeight );

    if (!surface->shape_region) set_surface_shape_rect( shape_bits, shape_stride, dirty );
    else if ((data = GDI_GetObjPtr( surface->shape_region, NTGDI_OBJ_REGION )))
    {
        if (EqualRect( rect, dirty )) memset( shape_bits, 0, shape_info->bmiHeader.biSizeImage );
        for (shape_rect = data->rects; shape_rect < data->rects + data->numRects; shape_rect++)
        {
            if (!intersect_rect( &tmp_rect, shape_rect, dirty )) continue;
            set_surface_shape_rect( shape_bits, shape_stride, &tmp_rect );
        }
        GDI_ReleaseObj( surface->shape_region );
    }

    switch (color_info->bmiHeader.biBitCount)
    {
    case 16:
    {
        UINT *masks = (UINT *)color_info->bmiColors, color_mask;
        BYTE *shape = shape_bits, *color = color_bits;

        if (color_key == CLR_INVALID) color_mask = 0;
        else color_mask = masks[0] | masks[1] | masks[2];
        if (!color_mask) break;

        color += dirty->top * color_stride;
        shape += dirty->top * shape_stride;

        for (y = dirty->top; y < dirty->bottom; y++, color += color_stride, shape += shape_stride)
        {
            for (x = dirty->left; x < dirty->right; x += 8)
            {
                shape[x / 8] &= shape_from_color_key_16( (UINT16 *)color + x, color_mask, color_key );
            }
        }
        break;
    }
    case 24: case 32:
    {
        BYTE *shape = shape_bits, *color = color_bits;
        UINT color_mask, alpha = 0;

        if (color_key == CLR_INVALID) color_mask = 0;
        else if (color_info->bmiHeader.biCompression == BI_RGB) color_mask = 0xffffff;
        else
        {
            UINT *masks = (UINT *)color_info->bmiColors;
            color_mask = masks[0] | masks[1] | masks[2];
        }
        if (!alpha_mask && !color_mask) break;
        if (!alpha_mask) alpha = -1;

        color += dirty->top * color_stride;
        shape += dirty->top * shape_stride;

        for (y = dirty->top; y < dirty->bottom; y++, color += color_stride, shape += shape_stride)
        {
            for (x = dirty->left; x < dirty->right; x += 8)
            {
                shape[x / 8] &= shape_from_alpha_mask( (UINT32 *)color + x, alpha_mask, alpha );
                shape[x / 8] &= shape_from_color_key_32( (UINT32 *)color + x, color_mask, color_key );
            }
        }
        break;
    }
    }

    ret = memcmp( old_shape, shape_bits, shape_info->bmiHeader.biSizeImage );
    free( old_shape );
    return ret;
}

static BOOL clear_surface_shape( struct window_surface *surface )
{
    if (!surface->shape_bitmap) return FALSE;
    NtGdiDeleteObjectApp( surface->shape_bitmap );
    surface->shape_bitmap = 0;
    return TRUE;
}

static BOOL update_surface_shape( struct window_surface *surface, const RECT *rect, const RECT *dirty,
                                  const BITMAPINFO *color_info, void *color_bits )
{
    if (surface == &dummy_surface) return FALSE;

    if (surface->shape_region || surface->alpha_mask || surface->color_key != CLR_INVALID)
        return set_surface_shape( surface, rect, dirty, color_info, color_bits );
    else
        return clear_surface_shape( surface );
}

W32KAPI struct window_surface *window_surface_create( UINT size, const struct window_surface_funcs *funcs, HWND hwnd,
                                                      const RECT *rect, BITMAPINFO *info, HBITMAP bitmap )
{
    struct window_surface *surface;

    if (!(surface = calloc( 1, size ))) return NULL;
    surface->funcs = funcs;
    surface->ref = 1;
    surface->hwnd = hwnd;
    surface->rect = *rect;
    surface->color_key = CLR_INVALID;
    surface->alpha_bits = -1;
    surface->alpha_mask = 0;
    reset_bounds( &surface->bounds );

    if (!bitmap) bitmap = NtGdiCreateDIBSection( 0, NULL, 0, info, DIB_RGB_COLORS, 0, 0, 0, NULL );
    if (!(surface->color_bitmap = bitmap))
    {
        free( surface );
        return NULL;
    }

    pthread_mutex_init( &surface->mutex, NULL );

    memset( window_surface_get_color( surface, info ), 0xff, info->bmiHeader.biSizeImage );

    TRACE( "created surface %p for hwnd %p rect %s\n", surface, hwnd, wine_dbgstr_rect( &surface->rect ) );
    return surface;
}

W32KAPI void window_surface_add_ref( struct window_surface *surface )
{
    InterlockedIncrement( &surface->ref );
}

W32KAPI void window_surface_release( struct window_surface *surface )
{
    ULONG ret = InterlockedDecrement( &surface->ref );
    if (!ret)
    {
        if (surface != &dummy_surface) pthread_mutex_destroy( &surface->mutex );
        if (surface->clip_region) NtGdiDeleteObjectApp( surface->clip_region );
        if (surface->color_bitmap) NtGdiDeleteObjectApp( surface->color_bitmap );
        if (surface->shape_bitmap) NtGdiDeleteObjectApp( surface->shape_bitmap );
        surface->funcs->destroy( surface );
        if (surface != &dummy_surface) free( surface );
    }
}

W32KAPI struct window_surface *window_surface_get( HWND hwnd )
{
    struct window_surface *surface = NULL;
    WND *win = get_win_ptr( hwnd );

    if (win && win != WND_DESKTOP && win != WND_OTHER_PROCESS)
    {
        if ((surface = win->surface))
            window_surface_add_ref( surface );
        release_win_ptr( win );
    }
    return surface;
}

W32KAPI void window_surface_lock( struct window_surface *surface )
{
    if (surface == &dummy_surface) return;
    pthread_mutex_lock( &surface->mutex );
}

W32KAPI void window_surface_unlock( struct window_surface *surface )
{
    if (surface == &dummy_surface) return;
    pthread_mutex_unlock( &surface->mutex );
}

void *window_surface_get_color( struct window_surface *surface, BITMAPINFO *info )
{
    struct bitblt_coords coords = {0};
    struct gdi_image_bits gdi_bits;
    BITMAPOBJ *bmp;

    if (surface == &dummy_surface)
    {
        static BITMAPINFOHEADER header = {.biSize = sizeof(header), .biWidth = 1, .biHeight = 1,
                                          .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB};
        static DWORD dummy_data;

        info->bmiHeader = header;
        return &dummy_data;
    }

    if (!(bmp = GDI_GetObjPtr( surface->color_bitmap, NTGDI_OBJ_BITMAP ))) return NULL;
    get_image_from_bitmap( bmp, info, &gdi_bits, &coords );
    GDI_ReleaseObj( surface->color_bitmap );

    return gdi_bits.ptr;
}

W32KAPI void window_surface_flush( struct window_surface *surface )
{
    char color_buf[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    char shape_buf[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *color_info = (BITMAPINFO *)color_buf;
    BITMAPINFO *shape_info = (BITMAPINFO *)shape_buf;
    RECT dirty = surface->rect, bounds;
    void *color_bits;
#ifdef __SWITCH__
    u64 t0, t1;
    if (wine_nx_paint_trace_enabled)
    {
        /* A source-only scan (checked update_now()'s dispatch loop, the
         * default WM_NCPAINT/WM_ERASEBKGND handlers, release_dc(), and
         * NtUserReleaseDC()) didn't turn up a confirmed second caller for
         * why this fires ~2x/paint-cycle while BeginPaint/EndPaint's own
         * sub-phases don't -- see README, "Presentation Is Still Too Slow".
         * Logging the actual return address settled it with certainty
         * instead of more guessing (resolved with addr2line/nm against
         * the matching build's .elf); both callers found that way are
         * now fixed (system_dpi init-order, idle-flush debounce), so this
         * should be a single, constant address every call in a clean
         * build. Rate-limited to the first few calls instead of the
         * aggregator treatment above -- an address isn't a timing to
         * average, but logging it every single frame forever is exactly
         * the same per-call fflush() cost this whole tier exists to
         * avoid, for a value that (if the fix held) never changes after
         * the first few frames anyway. */
        static unsigned int logged;
        if (logged < 5)
        {
            char buf[64];
            logged++;
            snprintf( buf, sizeof(buf), "[NXPAINT][CALLER] window_surface_flush ret=%p (sample %u/5)",
                     __builtin_return_address( 0 ), logged );
            wine_nx_runtime_trace( buf );
        }
    }
#endif

#ifdef __SWITCH__
    t0 = armGetSystemTick();
#endif
    window_surface_lock( surface );
#ifdef __SWITCH__
    t1 = armGetSystemTick();
    /* Time to *acquire* the lock -- if this is where present_dirty's
     * ~110ms unexplained-vs-its-own-children gap turns out to live, that's
     * lock contention or a slow lock primitive on this port, a different
     * class of bug than anything found so far (see README, "Presentation
     * Is Still Too Slow"), not just more bookkeeping to shrug off. */
    switch_paint_trace( "surface_lock", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif

#ifdef __SWITCH__
    t0 = armGetSystemTick();
#endif
    /* align bounds / dirty rect to help with 1bpp shape bitmap updates */
    bounds.left = surface->bounds.left & ~7;
    bounds.top = surface->bounds.top;
    bounds.right = (surface->bounds.right + 7) & ~7;
    bounds.bottom = surface->bounds.bottom;

    OffsetRect( &dirty, -dirty.left, -dirty.top );
#ifdef __SWITCH__
    t1 = armGetSystemTick();
    switch_paint_trace( "rect_math", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif

#ifdef __SWITCH__
    t0 = armGetSystemTick();
#endif
    if (intersect_rect( &dirty, &dirty, &bounds ) && (color_bits = window_surface_get_color( surface, color_info )))
    {
        BOOL shape_changed;
#ifdef __SWITCH__
        t1 = armGetSystemTick();
        switch_paint_trace( "surface_get_color", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );

        t0 = armGetSystemTick();
#endif
        shape_changed = update_surface_shape( surface, &surface->rect, &dirty, color_info, color_bits );
        void *shape_bits = window_surface_get_shape( surface, shape_info );
#ifdef __SWITCH__
        t1 = armGetSystemTick();
        switch_paint_trace( "surface_shape", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif

#ifdef __SWITCH__
        t0 = armGetSystemTick();
#endif
        TRACE( "Flushing hwnd %p, surface %p %s, bounds %s, dirty %s\n", surface->hwnd, surface,
               wine_dbgstr_rect( &surface->rect ), wine_dbgstr_rect( &surface->bounds ), wine_dbgstr_rect( &dirty ) );
#ifdef __SWITCH__
        t1 = armGetSystemTick();
        switch_paint_trace( "trace_macro", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif

#ifdef __SWITCH__
        t0 = armGetSystemTick();
#endif
        if (surface->funcs->flush( surface, &surface->rect, &dirty, color_info, color_bits,
                                   shape_changed, shape_info, shape_bits ))
            reset_bounds( &surface->bounds );
#ifdef __SWITCH__
        t1 = armGetSystemTick();
        switch_paint_trace( "surface_funcs_flush", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif
    }

#ifdef __SWITCH__
    t0 = armGetSystemTick();
#endif
    window_surface_unlock( surface );
#ifdef __SWITCH__
    t1 = armGetSystemTick();
    switch_paint_trace( "surface_unlock", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif
}

W32KAPI void window_surface_set_layered( struct window_surface *surface, COLORREF color_key, UINT alpha_bits, UINT alpha_mask )
{
    char color_buf[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *color_info = (BITMAPINFO *)color_buf;
    void *color_bits;

    window_surface_lock( surface );
    if ((color_bits = window_surface_get_color( surface, color_info )))
    {
        color_key = get_color_key( color_info, color_key );
        if (color_key != surface->color_key)
        {
            surface->color_key = color_key;
            surface->bounds = surface->rect;
        }
        if (alpha_bits != surface->alpha_bits)
        {
            surface->alpha_bits = alpha_bits;
            surface->bounds = surface->rect;
        }
        if (alpha_mask != surface->alpha_mask)
        {
            surface->alpha_mask = alpha_mask;
            surface->bounds = surface->rect;
        }
    }
    window_surface_unlock( surface );
}

W32KAPI void window_surface_set_clip( struct window_surface *surface, HRGN clip_region )
{
    window_surface_lock( surface );

    if (!clip_region && surface->clip_region)
    {
        TRACE( "hwnd %p, surface %p %s, clearing clip region\n", surface->hwnd, surface,
               wine_dbgstr_rect( &surface->rect ) );

        NtGdiDeleteObjectApp( surface->clip_region );
        surface->clip_region = 0;
        surface->funcs->set_clip( surface, NULL, 0 );
    }
    else if (clip_region && !NtGdiEqualRgn( clip_region, surface->clip_region ))
    {
        WINEREGION *data;

        TRACE( "hwnd %p, surface %p %s, setting clip region %p\n", surface->hwnd, surface,
               wine_dbgstr_rect( &surface->rect ), clip_region );

        if (!surface->clip_region) surface->clip_region = NtGdiCreateRectRgn( 0, 0, 0, 0 );
        NtGdiCombineRgn( surface->clip_region, clip_region, 0, RGN_COPY );

        if ((data = GDI_GetObjPtr( clip_region, NTGDI_OBJ_REGION )))
        {
            surface->funcs->set_clip( surface, data->rects, data->numRects );
            GDI_ReleaseObj( clip_region );
        }
    }

    window_surface_unlock( surface );
}

W32KAPI void window_surface_set_shape( struct window_surface *surface, HRGN shape_region )
{
    window_surface_lock( surface );

    if (!shape_region && surface->shape_region)
    {
        NtGdiDeleteObjectApp( surface->shape_region );
        surface->shape_region = 0;
        surface->bounds = surface->rect;
    }
    else if (shape_region && !NtGdiEqualRgn( shape_region, surface->shape_region ))
    {
        if (!surface->shape_region) surface->shape_region = NtGdiCreateRectRgn( 0, 0, 0, 0 );
        NtGdiCombineRgn( surface->shape_region, shape_region, 0, RGN_COPY );
        surface->bounds = surface->rect;
    }

    window_surface_unlock( surface );

    window_surface_flush( surface );
}

/*******************************************************************
 *           register_window_surface
 *
 * Register a window surface in the global list, possibly replacing another one.
 */
void register_window_surface( struct window_surface *old, struct window_surface *new )
{
    if (old == &dummy_surface) old = NULL;
    if (new == &dummy_surface) new = NULL;
    if (old == new) return;
    pthread_mutex_lock( &surfaces_lock );
    if (old) list_remove( &old->entry );
    if (new) list_add_tail( &window_surfaces, &new->entry );
    pthread_mutex_unlock( &surfaces_lock );
}

/*******************************************************************
 *           flush_window_surfaces
 *
 * Flush pending output from all window surfaces.
 */
void flush_window_surfaces( BOOL idle )
{
    static DWORD last_flush;
    DWORD now;
    struct window_surface *surface;
#ifdef __SWITCH__
    static int logged_mode;
    if (!logged_mode)
    {
        logged_mode = 1;
        wine_nx_runtime_trace( wine_nx_flush_legacy_enabled
                               ? "[NXFLUSH] flush_window_surfaces: legacy always-skip mode ACTIVE"
                               : "[NXFLUSH] flush_window_surfaces: real 50ms debounce ACTIVE (current default)" );
    }
#endif

    pthread_mutex_lock( &surfaces_lock );
#ifdef __SWITCH__
    /* A/B toggle, off by default: forces the always-skip behavior this
     * function had for its entire life until tonight's NtGetTickCount
     * fix made the debounce below start actually evaluating for the
     * first time -- see wine_nx_flush_legacy_select()'s comment in
     * wine-nx-probe/source/runtime.c. Checked after the lock so the
     * later unlock is always paired with a lock regardless of which
     * path is taken. */
    if (wine_nx_flush_legacy_enabled) goto done;
#endif
    now = NtGetTickCount();
    /* idle used to skip this debounce entirely and unconditionally flush
     * every registered surface every time -- fine on a real desktop where
     * "queue just went idle" is a rare transition, but NtUserPeekMessage()'s
     * empty-queue path (message.c) calls this with idle=TRUE on every
     * single non-blocking poll that finds nothing pending. For a tight
     * PeekMessageW loop like gui_smoke.c's, that's once per frame -- an
     * extra full window_surface_flush() on top of the legitimate one
     * EndPaint already does, every iteration, regardless of whether
     * anything was actually dirty. Apply the same 50ms debounce this
     * function already had for the idle=FALSE path to both paths instead,
     * keyed off the last time a flush actually ran rather than off
     * idle-ness specifically. */
    if ((int)(now - last_flush) < 50) goto done;
    last_flush = now;

    LIST_FOR_EACH_ENTRY( surface, &window_surfaces, struct window_surface, entry )
        window_surface_flush( surface );
done:
    pthread_mutex_unlock( &surfaces_lock );
}

/***********************************************************************
 *           dump_rdw_flags
 */
static void dump_rdw_flags(UINT flags)
{
    TRACE("flags:");
    if (flags & RDW_INVALIDATE) TRACE(" RDW_INVALIDATE");
    if (flags & RDW_INTERNALPAINT) TRACE(" RDW_INTERNALPAINT");
    if (flags & RDW_ERASE) TRACE(" RDW_ERASE");
    if (flags & RDW_VALIDATE) TRACE(" RDW_VALIDATE");
    if (flags & RDW_NOINTERNALPAINT) TRACE(" RDW_NOINTERNALPAINT");
    if (flags & RDW_NOERASE) TRACE(" RDW_NOERASE");
    if (flags & RDW_NOCHILDREN) TRACE(" RDW_NOCHILDREN");
    if (flags & RDW_ALLCHILDREN) TRACE(" RDW_ALLCHILDREN");
    if (flags & RDW_UPDATENOW) TRACE(" RDW_UPDATENOW");
    if (flags & RDW_ERASENOW) TRACE(" RDW_ERASENOW");
    if (flags & RDW_FRAME) TRACE(" RDW_FRAME");
    if (flags & RDW_NOFRAME) TRACE(" RDW_NOFRAME");

#define RDW_FLAGS \
    (RDW_INVALIDATE | \
     RDW_INTERNALPAINT | \
     RDW_ERASE | \
     RDW_VALIDATE | \
     RDW_NOINTERNALPAINT | \
     RDW_NOERASE | \
     RDW_NOCHILDREN | \
     RDW_ALLCHILDREN | \
     RDW_UPDATENOW | \
     RDW_ERASENOW | \
     RDW_FRAME | \
     RDW_NOFRAME)

    if (flags & ~RDW_FLAGS) TRACE(" %04x", flags & ~RDW_FLAGS);
    TRACE("\n");
#undef RDW_FLAGS
}

int force_present_to_surface( const RECT *win_rect )
{
    static int cached = -1;

    if (cached == -1)
    {
        const char *sgi = getenv( "SteamGameId" );

        cached = sgi &&
                 (
                    !strcmp(sgi, "803600")
                 );
    }
    if (!cached) return 0;

    return win_rect->right < 0 && win_rect->bottom < 0;
}

#ifdef __SWITCH__
/* Thread-local stash used by switch_prefetch_paint_regions() (defined
 * further down, next to get_update_region()) to hand a prefetched
 * get_visible_region reply to update_visible_region() below. See the design
 * comment on switch_prefetch_paint_regions() for the full rationale and the
 * validation this is subject to before being trusted. */
struct switch_paint_regions_prefetch
{
    int      valid;
    HWND     hwnd;
    UINT     flags;        /* visible_flags this was fetched with; only DCX_WINDOW is ever compared */
    HRGN     vis_rgn;
    HWND     top_win;
    RECT     win_rect;
    RECT     top_rect;
    DWORD    paint_flags;
};
static __thread struct switch_paint_regions_prefetch switch_paint_regions_prefetch;

static void switch_paint_regions_prefetch_clear(void)
{
    /* A prefetched vis_rgn that never gets consumed (WM_NCPAINT invalidation,
     * update_vis_rgn ending up FALSE, or an early BeginPaint bail) is a real
     * GDI region handle that must be freed here -- otherwise every such
     * frame leaks one HRGN, same class of bug as never calling ReleaseDC. */
    if (switch_paint_regions_prefetch.valid && switch_paint_regions_prefetch.vis_rgn)
        NtGdiDeleteObjectApp( switch_paint_regions_prefetch.vis_rgn );
    switch_paint_regions_prefetch.valid = 0;
    switch_paint_regions_prefetch.vis_rgn = 0;
}
#endif

/***********************************************************************
 *           update_visible_region
 *
 * Set the visible region and X11 drawable for the DC associated to
 * a given window.
 */
static void update_visible_region( struct dce *dce )
{
    struct window_surface *surface = NULL;
    NTSTATUS status;
    HRGN vis_rgn = 0;
    HWND top_win = 0;
    DWORD flags = dce->flags;
    DWORD paint_flags = 0;
    size_t size = 256;
    RECT win_rect, top_rect;
    UINT raw_dpi;
    DWORD layered_flags;
    WND *win;

    /* don't clip siblings if using parent clip region */
    if (flags & DCX_PARENTCLIP) flags &= ~DCX_CLIPSIBLINGS;

#ifdef __SWITCH__
    /* See the design comment above switch_prefetch_paint_regions(). Only
     * consume the prefetch if it's for this exact hwnd and the one flag
     * bit the server's reply actually depends on (DCX_WINDOW) matches --
     * any mismatch means fall through to the normal IPC call below, which
     * is always correct regardless of why the prefetch didn't apply. */
    if (wine_nx_batch_paint_regions_enabled && switch_paint_regions_prefetch.valid &&
        switch_paint_regions_prefetch.hwnd == dce->hwnd &&
        !((switch_paint_regions_prefetch.flags ^ flags) & DCX_WINDOW))
    {
        vis_rgn     = switch_paint_regions_prefetch.vis_rgn;
        top_win     = switch_paint_regions_prefetch.top_win;
        win_rect    = switch_paint_regions_prefetch.win_rect;
        top_rect    = switch_paint_regions_prefetch.top_rect;
        paint_flags = switch_paint_regions_prefetch.paint_flags;
        status = STATUS_SUCCESS;
        switch_paint_regions_prefetch.valid = 0;

        if (wine_nx_paint_trace_enabled)
        {
            static unsigned int logged;
            if (logged < 5)
            {
                char buf[128];
                snprintf( buf, sizeof(buf), "[NXBATCH] consumed hwnd=%x", (int)(ULONG_PTR)dce->hwnd );
                wine_nx_runtime_trace( buf );
                logged++;
            }
        }
    }
    else
    {
        if (wine_nx_batch_paint_regions_enabled && switch_paint_regions_prefetch.valid &&
            wine_nx_paint_trace_enabled)
        {
            static unsigned int logged;
            if (logged < 5)
            {
                char buf[160];
                snprintf( buf, sizeof(buf), "[NXBATCH] miss hwnd=%x prefetch_hwnd=%x flags=%x prefetch_flags=%x",
                         (int)(ULONG_PTR)dce->hwnd, (int)(ULONG_PTR)switch_paint_regions_prefetch.hwnd,
                         (unsigned int)flags, (unsigned int)switch_paint_regions_prefetch.flags );
                wine_nx_runtime_trace( buf );
                logged++;
            }
        }
        switch_paint_regions_prefetch_clear();
#endif
    /* fetch the visible region from the server */
    do
    {
        RGNDATA *data = malloc( sizeof(*data) + size - 1 );
        if (!data) return;

        SERVER_START_REQ( get_visible_region )
        {
            req->window  = wine_server_user_handle( dce->hwnd );
            req->flags   = flags;
            wine_server_set_reply( req, data->Buffer, size );
            if (!(status = wine_server_call( req )))
            {
                size_t reply_size = wine_server_reply_size( reply );
                data->rdh.dwSize   = sizeof(data->rdh);
                data->rdh.iType    = RDH_RECTANGLES;
                data->rdh.nCount   = reply_size / sizeof(RECT);
                data->rdh.nRgnSize = reply_size;
                vis_rgn = NtGdiExtCreateRegion( NULL, data->rdh.dwSize + data->rdh.nRgnSize, data );
                top_win     = wine_server_ptr_handle( reply->top_win );
                win_rect    = wine_server_get_rect( reply->win_rect );
                top_rect    = wine_server_get_rect( reply->top_rect );
                paint_flags = reply->paint_flags;
            }
            else size = reply->total_size;
        }
        SERVER_END_REQ;
        free( data );
    } while (status == STATUS_BUFFER_OVERFLOW);
#ifdef __SWITCH__
    }
#endif

    if (status || !vis_rgn) return;

    if (dce->clip_rgn) NtGdiCombineRgn( vis_rgn, vis_rgn, dce->clip_rgn,
                                        (flags & DCX_INTERSECTRGN) ? RGN_AND : RGN_DIFF );

    /* don't use a surface to paint the client area of OpenGL windows */
    if (!(paint_flags & SET_WINPOS_PIXEL_FORMAT) || (flags & DCX_WINDOW)
        || (NtUserGetLayeredWindowAttributes( dce->hwnd, NULL, NULL, &layered_flags ) && layered_flags & LWA_COLORKEY)
        || force_present_to_surface( &win_rect ))
    {
        win = get_win_ptr( top_win );
        if (win && win != WND_DESKTOP && win != WND_OTHER_PROCESS)
        {
            surface = win->surface;
            if (surface) window_surface_add_ref( surface );
            release_win_ptr( win );
        }
    }

    if (surface)
    {
#ifdef __SWITCH__
        /* This trace used to fire completely unconditionally on every single
         * call -- no wine_nx_paint_trace_enabled gate, no rate limit at all --
         * making it the single hottest unrated-limited fflush() call site in
         * the whole port: update_visible_region() runs at least twice per
         * WM_PAINT cycle (erase's own GetDCEx plus BeginPaint's, even when the
         * IPC call itself is skipped via the get_paint_regions prefetch hit
         * above, since this trace sits downstream of both branches). This is
         * almost certainly the real explanation for "Track A"'s never-closed
         * ~30-46ms update_visible_region() gap in the README. Gated and
         * rate-limited to match every other one-off diagnostic trace in this
         * file. */
        if (wine_nx_paint_trace_enabled)
        {
            static unsigned int logged;
            if (logged < 5)
            {
                char buf[192];
                snprintf( buf, sizeof(buf), "[NXDCE] hwnd=%x top=%x flags=%x paint=%x surf=%x win=%d,%d %dx%d",
                          (int)(ULONG_PTR)dce->hwnd, (int)(ULONG_PTR)top_win, (int)flags, (int)paint_flags,
                          (int)(ULONG_PTR)surface, win_rect.left, win_rect.top,
                          win_rect.right - win_rect.left, win_rect.bottom - win_rect.top );
                wine_nx_runtime_trace( buf );
                logged++;
            }
        }
#endif
        user_driver->pGetDC( dce->hdc, dce->hwnd, top_win, &win_rect, &top_rect, flags );
        set_visible_region( dce->hdc, vis_rgn, &win_rect, &top_rect, surface, 0, 0 );
        window_surface_release( surface );
    }
    else
    {
        RECT window_rect, toplevel_rect;
        UINT dpi;

#ifdef __SWITCH__
        /* Same unconditional-fflush bug as the surface branch above, same fix. */
        if (wine_nx_paint_trace_enabled)
        {
            static unsigned int logged;
            if (logged < 5)
            {
                char buf[192];
                snprintf( buf, sizeof(buf), "[NXDCE] hwnd=%x top=%x flags=%x paint=%x surf=0 win=%d,%d %dx%d",
                          (int)(ULONG_PTR)dce->hwnd, (int)(ULONG_PTR)top_win, (int)flags, (int)paint_flags,
                          win_rect.left, win_rect.top, win_rect.right - win_rect.left, win_rect.bottom - win_rect.top );
                wine_nx_runtime_trace( buf );
                logged++;
            }
        }
#endif

        get_win_monitor_dpi( top_win, &raw_dpi );
        dpi = get_dpi_for_window( top_win );

        window_rect = map_rect_virt_to_raw( win_rect, dpi );
        toplevel_rect = map_rect_virt_to_raw( top_rect, dpi );
        user_driver->pGetDC( dce->hdc, dce->hwnd, top_win, &window_rect, &toplevel_rect, flags );

        SetRectEmpty( &top_rect );
        set_visible_region( dce->hdc, vis_rgn, &win_rect, &top_rect, NULL, dpi, raw_dpi );
    }
}

/***********************************************************************
 *           release_dce
 */
static void release_dce( struct dce *dce )
{
    if (!dce->hwnd) return;  /* already released */

    set_visible_region( dce->hdc, 0, &dummy_surface.rect, &dummy_surface.rect, &dummy_surface, 0, 0 );
    user_driver->pReleaseDC( dce->hwnd, dce->hdc );

    if (dce->clip_rgn) NtGdiDeleteObjectApp( dce->clip_rgn );
    dce->clip_rgn = 0;
    dce->hwnd     = 0;
    dce->flags   &= DCX_CACHE;
}

/***********************************************************************
 *           delete_clip_rgn
 */
static void delete_clip_rgn( struct dce *dce )
{
    if (!dce->clip_rgn) return;  /* nothing to do */

    dce->flags &= ~(DCX_EXCLUDERGN | DCX_INTERSECTRGN);
    NtGdiDeleteObjectApp( dce->clip_rgn );
    dce->clip_rgn = 0;

    /* make it dirty so that the vis rgn gets recomputed next time */
    set_dce_flags( dce->hdc, DCHF_INVALIDATEVISRGN );
}

/***********************************************************************
 *           delete_dce
 */
BOOL delete_dce( struct dce *dce )
{
    BOOL ret = TRUE;

    TRACE( "hdc = %p\n", dce->hdc );

    user_lock();
    if (!(dce->flags & DCX_CACHE))
    {
        WARN("Application trying to delete an owned DC %p\n", dce->hdc);
        ret = FALSE;
    }
    else
    {
        list_remove( &dce->entry );
        if (dce->clip_rgn) NtGdiDeleteObjectApp( dce->clip_rgn );
        free( dce );
    }
    user_unlock();
    return ret;
}

/***********************************************************************
 *           update_dc
 *
 * Make sure the DC vis region is up to date.
 * This function may need user lock so the GDI lock should _not_
 * be held when calling it.
 */
void update_dc( DC *dc )
{
    if (!dc->dirty) return;
    dc->dirty = 0;
    if (dc->dce)
    {
        if (dc->dce->count) update_visible_region( dc->dce );
        else /* non-fatal but shouldn't happen */
            WARN("DC is not in use!\n");
    }
}

/***********************************************************************
 *           alloc_dce
 *
 * Allocate a new DCE.
 */
static struct dce *alloc_dce(void)
{
    struct dce *dce;

    if (!(dce = malloc( sizeof(*dce) ))) return NULL;
    if (!(dce->hdc = NtGdiOpenDCW( NULL, NULL, NULL, 0, TRUE, 0, NULL, NULL )))
    {
        free( dce );
        return 0;
    }
    dce->hwnd      = 0;
    dce->clip_rgn  = 0;
    dce->flags     = 0;
    dce->count     = 1;

    set_dc_dce( dce->hdc, dce );
    return dce;
}

/***********************************************************************
 *           get_window_dce
 */
static struct dce *get_window_dce( HWND hwnd )
{
    struct dce *dce;
    WND *win = get_win_ptr( hwnd );

    if (!win || win == WND_OTHER_PROCESS || win == WND_DESKTOP) return NULL;

    dce = win->dce;
    if (!dce && (dce = get_class_dce( win->class )))
    {
        win->dce = dce;
        dce->count++;
    }
    release_win_ptr( win );

    if (!dce)  /* try to allocate one */
    {
        struct dce *dce_to_free = NULL;
        LONG class_style = get_class_long( hwnd, GCL_STYLE, FALSE );

        if (class_style & CS_CLASSDC)
        {
            if (!(dce = alloc_dce())) return NULL;

            win = get_win_ptr( hwnd );
            if (win && win != WND_OTHER_PROCESS && win != WND_DESKTOP)
            {
                if (win->dce)  /* another thread beat us to it */
                {
                    dce_to_free = dce;
                    dce = win->dce;
                }
                else if ((win->dce = set_class_dce( win->class, dce )) != dce)
                {
                    dce_to_free = dce;
                    dce = win->dce;
                    dce->count++;
                }
                else
                {
                    dce->count++;
                    list_add_tail( &dce_list, &dce->entry );
                }
                release_win_ptr( win );
            }
            else dce_to_free = dce;
        }
        else if (class_style & CS_OWNDC)
        {
            if (!(dce = alloc_dce())) return NULL;

            win = get_win_ptr( hwnd );
            if (win && win != WND_OTHER_PROCESS && win != WND_DESKTOP)
            {
                if (win->dwStyle & WS_CLIPCHILDREN) dce->flags |= DCX_CLIPCHILDREN;
                if (win->dwStyle & WS_CLIPSIBLINGS) dce->flags |= DCX_CLIPSIBLINGS;
                if (win->dce)  /* another thread beat us to it */
                {
                    dce_to_free = dce;
                    dce = win->dce;
                }
                else
                {
                    win->dce = dce;
                    dce->hwnd = hwnd;
                    list_add_tail( &dce_list, &dce->entry );
                }
                release_win_ptr( win );
            }
            else dce_to_free = dce;
        }

        if (dce_to_free)
        {
            set_dc_dce( dce_to_free->hdc, NULL );
            NtGdiDeleteObjectApp( dce_to_free->hdc );
            free( dce_to_free );
            if (dce_to_free == dce)
                dce = NULL;
        }
    }
    return dce;
}

/***********************************************************************
 *           free_dce
 *
 * Free a class or window DCE.
 */
void free_dce( struct dce *dce, HWND hwnd, struct list *drawables )
{
    struct dce *dce_to_free = NULL;

    user_lock();

    if (dce)
    {
        if (!--dce->count)
        {
            release_dce( dce );
            list_remove( &dce->entry );
            dce_to_free = dce;
        }
        else if (dce->hwnd == hwnd)
        {
            release_dce( dce );
        }
    }

    /* now check for cache DCEs */

    if (hwnd)
    {
        LIST_FOR_EACH_ENTRY( dce, &dce_list, struct dce, entry )
        {
            if (dce->hwnd != hwnd) continue;
            if (!(dce->flags & DCX_CACHE)) break;

            release_dce( dce );
            if (dce->count)
            {
                WARN( "GetDC() without ReleaseDC() for window %p\n", hwnd );
                dce->count = 0;
                set_dc_pixel_format_internal( dce->hdc, 0, drawables );
                set_dce_flags( dce->hdc, DCHF_DISABLEDC );
            }
        }
    }

    user_unlock();

    if (dce_to_free)
    {
        set_dc_dce( dce_to_free->hdc, NULL );
        NtGdiDeleteObjectApp( dce_to_free->hdc );
        free( dce_to_free );
    }
}

BOOL is_cache_dc( HDC hdc )
{
    BOOL ret = FALSE;
    struct dce *dce;

    user_lock();
    if ((dce = get_dc_dce( hdc ))) ret = !!(dce->flags & DCX_CACHE);
    user_unlock();

    return ret;
}

/***********************************************************************
 *           make_dc_dirty
 *
 * Mark the associated DC as dirty to force a refresh of the visible region
 */
static void make_dc_dirty( struct dce *dce )
{
    if (!dce->count)
    {
        /* Don't bother with visible regions of unused DCEs */
        TRACE("purged %p hwnd %p\n", dce->hdc, dce->hwnd);
        release_dce( dce );
    }
    else
    {
        /* Set dirty bits in the hDC and DCE structs */
        TRACE("fixed up %p hwnd %p\n", dce->hdc, dce->hwnd);
        set_dce_flags( dce->hdc, DCHF_INVALIDATEVISRGN );
    }
}

/***********************************************************************
 *           invalidate_dce
 *
 * It is called from SetWindowPos() - we have to
 * mark as dirty all busy DCEs for windows that have pWnd->parent as
 * an ancestor and whose client rect intersects with specified update
 * rectangle. In addition, pWnd->parent DCEs may need to be updated if
 * DCX_CLIPCHILDREN flag is set.
 */
void invalidate_dce( WND *win, const RECT *old_rect )
{
    UINT context;
    struct dce *dce;

    if (!win->parent) return;

    context = set_thread_dpi_awareness_context( get_window_dpi_awareness_context( win->handle ) );

    TRACE( "%p parent %p, old_rect %s\n", win->handle, win->parent, wine_dbgstr_rect( old_rect ) );

    /* walk all DCEs and fixup non-empty entries */

    LIST_FOR_EACH_ENTRY( dce, &dce_list, struct dce, entry )
    {
        if (!dce->hwnd) continue;

        TRACE( "%p: hwnd %p dcx %08x %s %s\n", dce->hdc, dce->hwnd, dce->flags,
               (dce->flags & DCX_CACHE) ? "Cache" : "Owned", dce->count ? "InUse" : "" );

        if ((dce->hwnd == win->parent) && !(dce->flags & DCX_CLIPCHILDREN))
            continue;  /* child window positions don't bother us */

        /* if DCE window is a child of hwnd, it has to be invalidated */
        if (dce->hwnd == win->handle || is_child( win->handle, dce->hwnd ))
        {
            make_dc_dirty( dce );
            continue;
        }

        /* otherwise check if the window rectangle intersects this DCE window */
        if (win->parent == dce->hwnd || is_child( win->parent, dce->hwnd ))
        {
            RECT tmp, new_window_rect, old_window_rect;
            struct window_rects rects;

            /* get the parent client-relative old/new window rects */
            get_window_rects( win->handle, COORDS_PARENT, &rects, get_thread_dpi() );
            old_window_rect = old_rect ? *old_rect : rects.window;
            new_window_rect = rects.window;

            /* get the DCE window rect in client-relative coordinates */
            get_window_rects( dce->hwnd, COORDS_CLIENT, &rects, get_thread_dpi() );
            if (win->parent != dce->hwnd)
            {
                /* map the window rects from parent client-relative to DCE window client-relative coordinates */
                map_window_points( win->parent, dce->hwnd, (POINT *)&new_window_rect, 2, get_thread_dpi() );
                map_window_points( win->parent, dce->hwnd, (POINT *)&old_window_rect, 2, get_thread_dpi() );
            }

            /* check if any of the window rects intersects with the DCE window rect */
            if (intersect_rect( &tmp, &rects.window, &old_window_rect )) make_dc_dirty( dce );
            else if (intersect_rect( &tmp, &rects.window, &new_window_rect )) make_dc_dirty( dce );
        }
    }
    set_thread_dpi_awareness_context( context );
}

/***********************************************************************
 *           release_dc
 */
static INT release_dc( HWND hwnd, HDC hdc, BOOL end_paint )
{
    struct list drawables = LIST_INIT( drawables );
    struct dce *dce;
    BOOL ret = FALSE;

    TRACE( "%p %p\n", hwnd, hdc );

    user_lock();
    dce = get_dc_dce( hdc );
    if (dce && dce->count && dce->hwnd)
    {
        if (!(dce->flags & DCX_NORESETATTRS)) set_dce_flags( dce->hdc, DCHF_RESETDC );
        if (end_paint || (dce->flags & DCX_CACHE)) delete_clip_rgn( dce );
        if (dce->flags & DCX_CACHE)
        {
            dce->count = 0;
            set_dc_pixel_format_internal( hdc, 0, &drawables );
            set_dce_flags( dce->hdc, DCHF_DISABLEDC );
        }
        ret = TRUE;
    }
    user_unlock();

    release_opengl_drawables( &drawables );
    return ret;
}

/***********************************************************************
 *           NtUserGetDCEx (win32u.@)
 */
HDC WINAPI NtUserGetDCEx( HWND hwnd, HRGN clip_rgn, DWORD flags )
{
    const DWORD clip_flags = DCX_PARENTCLIP | DCX_CLIPSIBLINGS | DCX_CLIPCHILDREN | DCX_WINDOW;
    const DWORD user_flags = clip_flags | DCX_NORESETATTRS; /* flags that can be set by user */
    BOOL update_vis_rgn = TRUE;
    struct dce *dce;
    HWND parent;
    DWORD window_style = get_window_long( hwnd, GWL_STYLE );
#ifdef __SWITCH__
    u64 t0, t1;
#endif

    if (!hwnd) hwnd = get_desktop_window();
    else hwnd = get_full_window_handle( hwnd );

    TRACE( "hwnd %p, clip_rgn %p, flags %08x\n", hwnd, clip_rgn, flags );

    if (!is_window(hwnd)) return 0;

#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled) t0 = armGetSystemTick();
#endif
    /* fixup flags */

    if (flags & (DCX_WINDOW | DCX_PARENTCLIP)) flags |= DCX_CACHE;

    if (flags & DCX_USESTYLE)
    {
        flags &= ~(DCX_CLIPCHILDREN | DCX_CLIPSIBLINGS | DCX_PARENTCLIP);

        if (window_style & WS_CLIPSIBLINGS) flags |= DCX_CLIPSIBLINGS;

        if (!(flags & DCX_WINDOW))
        {
            if (get_class_long( hwnd, GCL_STYLE, FALSE ) & CS_PARENTDC) flags |= DCX_PARENTCLIP;

            if (window_style & WS_CLIPCHILDREN && !(window_style & WS_MINIMIZE))
                flags |= DCX_CLIPCHILDREN;
        }
    }

    if (flags & DCX_WINDOW) flags &= ~DCX_CLIPCHILDREN;

    parent = NtUserGetAncestor( hwnd, GA_PARENT );
    if (!parent || (parent == get_desktop_window()))
        flags = (flags & ~DCX_PARENTCLIP) | DCX_CLIPSIBLINGS;

    /* it seems parent clip is ignored when clipping siblings or children */
    if (flags & (DCX_CLIPSIBLINGS | DCX_CLIPCHILDREN)) flags &= ~DCX_PARENTCLIP;

    if( flags & DCX_PARENTCLIP )
    {
        DWORD parent_style = get_window_long( parent, GWL_STYLE );
        if( (window_style & WS_VISIBLE) && (parent_style & WS_VISIBLE) )
        {
            flags &= ~DCX_CLIPCHILDREN;
            if (parent_style & WS_CLIPSIBLINGS) flags |= DCX_CLIPSIBLINGS;
        }
    }
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled)
    {
        t1 = armGetSystemTick();
        switch_paint_trace( "getdcex_flags_fixup", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
        t0 = armGetSystemTick();
    }
#endif

    /* find a suitable DCE */

    if ((flags & DCX_CACHE) || !(dce = get_window_dce( hwnd )))
    {
        struct dce *dceEmpty = NULL, *dceUnused = NULL, *found = NULL;
        unsigned int count = 0;

        /* Strategy: First, we attempt to find a non-empty but unused DCE with
         * compatible flags. Next, we look for an empty entry. If the cache is
         * full we have to purge one of the unused entries.
         */
        user_lock();
        LIST_FOR_EACH_ENTRY( dce, &dce_list, struct dce, entry )
        {
            if (!(dce->flags & DCX_CACHE)) break;
            count++;
            if (dce->count) continue;
            dceUnused = dce;
            if (!dce->hwnd) dceEmpty = dce;
            else if ((dce->hwnd == hwnd) && !((dce->flags ^ flags) & clip_flags))
            {
                TRACE( "found valid %p hwnd %p, flags %08x\n", dce->hdc, hwnd, dce->flags );
                found = dce;
                update_vis_rgn = FALSE;
                break;
            }
        }
        if (!found) found = dceEmpty;
        if (!found && count >= DCE_CACHE_SIZE) found = dceUnused;

        dce = found;
        if (dce)
        {
            dce->count = 1;
            set_dce_flags( dce->hdc, DCHF_ENABLEDC );
        }
        user_unlock();

        /* if there's no dce empty or unused, allocate a new one */
        if (!dce)
        {
            if (!(dce = alloc_dce())) return 0;
            dce->flags = DCX_CACHE;
            user_lock();
            list_add_head( &dce_list, &dce->entry );
            user_unlock();
        }
    }
    else
    {
        flags |= DCX_NORESETATTRS;
        if (dce->hwnd != hwnd)
        {
            /* we should free dce->clip_rgn here, but Windows apparently doesn't */
            dce->flags &= ~(DCX_EXCLUDERGN | DCX_INTERSECTRGN);
            dce->clip_rgn = 0;
        }
        else update_vis_rgn = FALSE; /* updated automatically, via DCHook() */
    }
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled)
    {
        t1 = armGetSystemTick();
        switch_paint_trace( "getdcex_dce_lookup", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
        t0 = armGetSystemTick();
    }
#endif

    if (flags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN))
    {
        /* if the extra clip region has changed, get rid of the old one */
        if (dce->clip_rgn != clip_rgn || ((flags ^ dce->flags) & (DCX_INTERSECTRGN | DCX_EXCLUDERGN)))
            delete_clip_rgn( dce );
        dce->clip_rgn = clip_rgn;
        if (!dce->clip_rgn) dce->clip_rgn = NtGdiCreateRectRgn( 0, 0, 0, 0 );
        dce->flags |= flags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN);
        update_vis_rgn = TRUE;
    }

    if (get_window_long( hwnd, GWL_EXSTYLE ) & WS_EX_LAYOUTRTL)
        NtGdiSetLayout( dce->hdc, -1, LAYOUT_RTL );

    dce->hwnd = hwnd;
    dce->flags = (dce->flags & ~user_flags) | (flags & user_flags);

    /* cross-process invalidation is not supported yet, so always update the vis rgn */
    if (!is_current_process_window( hwnd )) update_vis_rgn = TRUE;

    if (set_dce_flags( dce->hdc, DCHF_VALIDATEVISRGN )) update_vis_rgn = TRUE;  /* DC was dirty */
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled)
    {
        t1 = armGetSystemTick();
        switch_paint_trace( "getdcex_clip_setup", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
        t0 = armGetSystemTick();
    }
#endif

    if (update_vis_rgn) update_visible_region( dce );
#ifdef __SWITCH__
    if (wine_nx_paint_trace_enabled)
    {
        t1 = armGetSystemTick();
        /* Should roughly match the already-known get_visible_region IPC
         * cost (~14-15ms) when update_vis_rgn is TRUE -- this is the one
         * piece of NtUserGetDCEx's ~30ms-beyond-the-IPC-call gap that was
         * already accounted for. getdcex_flags_fixup/dce_lookup/clip_setup
         * above are the three candidates for the rest. See README, "The
         * ~14ms Per-Call IPC Floor". */
        switch_paint_trace( "getdcex_vis_rgn", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
    }
#endif

    TRACE( "(%p,%p,0x%x): returning %p%s\n", hwnd, clip_rgn, flags, dce->hdc,
           update_vis_rgn ? " (updated)" : "" );
    return dce->hdc;
}

/***********************************************************************
 *           NtUserReleaseDC (win32u.@)
 */
INT WINAPI NtUserReleaseDC( HWND hwnd, HDC hdc )
{
    if (hwnd && !is_current_process_window( hwnd ))
        user_driver->pProcessEvents( 0 );

    return release_dc( hwnd, hdc, FALSE );
}

/***********************************************************************
 *           NtUserGetDC (win32u.@)
 */
HDC WINAPI NtUserGetDC( HWND hwnd )
{
    if (!hwnd) return NtUserGetDCEx( 0, 0, DCX_CACHE | DCX_WINDOW );
    return NtUserGetDCEx( hwnd, 0, DCX_USESTYLE );
}

/***********************************************************************
 *           NtUserGetWindowDC (win32u.@)
 */
HDC WINAPI NtUserGetWindowDC( HWND hwnd )
{
    return NtUserGetDCEx( hwnd, 0, DCX_USESTYLE | DCX_WINDOW );
}

/**********************************************************************
 *           NtUserWindowFromDC (win32u.@)
 */
HWND WINAPI NtUserWindowFromDC( HDC hdc )
{
    struct dce *dce;
    HWND hwnd = 0;

    user_lock();
    dce = get_dc_dce( hdc );
    if (dce) hwnd = dce->hwnd;
    user_unlock();
    return hwnd;
}

#ifdef __SWITCH__
/* Batched get_update_region + get_visible_region for NtUserBeginPaint's
 * specific call sequence. Background: BeginPaint always issues
 * get_update_region (via send_ncpaint) immediately followed by
 * get_visible_region (via send_erase -> NtUserGetDCEx -> update_visible_region),
 * and each is its own ~14-15ms IPC round trip (see README, "The ~14ms
 * Per-Call IPC Floor") -- confirmed real, unavoidable Horizon OS thread-wake
 * latency, not a deletable inefficiency. horizon_server_handle_get_paint_regions
 * (dlls/ntdll/unix/horizon.c) computes both under one lock pass with no
 * cross-dependency, so both can be fetched in a single round trip.
 *
 * The catch: send_ncpaint() can dispatch WM_NCPAINT to the app *between*
 * the two fetches (when UPDATE_NONCLIENT is set), and an app's WM_NCPAINT
 * handler can change window geometry -- so a visible-region reply fetched
 * before that dispatch could be stale by the time NtUserGetDCEx would
 * normally have fetched it. This is handled by fetching optimistically and
 * validating before use, never by assuming the optimistic fetch is safe:
 *   1. switch_prefetch_paint_regions() replaces get_update_region()'s own
 *      IPC call inside send_ncpaint(), fetching both halves in one request
 *      and stashing the visible-region half in switch_paint_regions_prefetch
 *      (thread-local -- BeginPaint's window is only ever touched by its
 *      owning thread, so no cross-thread locking is needed for this).
 *   2. If send_ncpaint() goes on to actually dispatch WM_NCPAINT, it clears
 *      the stash first (switch_paint_regions_prefetch_clear()) -- app code
 *      is about to run, so the prefetched visible-region data must not be
 *      trusted anymore.
 *   3. update_visible_region() (below get_update_region in this file) checks
 *      the stash before issuing its own get_visible_region call. It only
 *      consumes the stash if the hwnd matches AND the one flag bit the
 *      server's reply actually depends on (DCX_WINDOW -- confirmed by
 *      reading horizon_server_handle_get_visible_region and the combined
 *      handler: paint_flags/top_win/top_rect never depend on flags at all,
 *      win_rect depends only on DCX_WINDOW) still matches what was
 *      prefetched. Any mismatch, or an empty stash, falls through to the
 *      normal, always-correct IPC call -- so every fallback path costs
 *      exactly what today's unbatched code costs, never more, and the
 *      match is a positive proof of freshness, not an assumption.
 * Net effect: the common steady-state case (no WM_NCPAINT churn, which is
 * what continuous redraw looks like) saves one full round trip per paint
 * cycle; any case that doesn't hold falls back to today's exact behavior.
 * Gated off by default behind wine_nx_batch_paint_regions_enabled
 * (WINE_NX_BATCH_PAINT_REGIONS env var / sdmc:/switch/wine/batchpaint.txt).
 */
static HRGN switch_build_region_from_bytes( const void *bytes, size_t byte_size )
{
    RGNDATA *data;
    HRGN hrgn;

    if (!(data = malloc( sizeof(*data) + byte_size ))) return 0;
    data->rdh.dwSize   = sizeof(data->rdh);
    data->rdh.iType    = RDH_RECTANGLES;
    data->rdh.nCount   = byte_size / sizeof(RECT);
    data->rdh.nRgnSize = byte_size;
    if (byte_size) memcpy( data->Buffer, bytes, byte_size );
    hrgn = NtGdiExtCreateRegion( NULL, data->rdh.dwSize + data->rdh.nRgnSize, data );
    free( data );
    return hrgn;
}

static HRGN switch_prefetch_paint_regions( HWND hwnd, UINT *flags, HWND *child )
{
    HRGN hrgn = 0;
    NTSTATUS status;
    RGNDATA *combined;
    size_t size = 512;
    UINT visible_flags = DCX_INTERSECTRGN | DCX_USESTYLE;

    if (is_iconic( hwnd )) visible_flags |= DCX_WINDOW;
    switch_paint_regions_prefetch_clear();

    do
    {
        if (!(combined = malloc( sizeof(*combined) + size - 1 )))
        {
            RtlSetLastWin32Error( ERROR_OUTOFMEMORY );
            return 0;
        }

        SERVER_START_REQ( get_paint_regions )
        {
#ifdef __SWITCH__
            u64 ipc_t0 = 0, ipc_t1;
            if (wine_nx_paint_trace_enabled) ipc_t0 = armGetSystemTick();
#endif
            req->window        = wine_server_user_handle( hwnd );
            req->from_child    = wine_server_user_handle( child ? *child : 0 );
            req->update_flags  = *flags;
            req->visible_flags = visible_flags;
            wine_server_set_reply( req, combined->Buffer, size );
            status = wine_server_call( req );
#ifdef __SWITCH__
            if (wine_nx_paint_trace_enabled)
            {
                ipc_t1 = armGetSystemTick();
                switch_paint_trace( "ncpaint_ipc", (unsigned int)(armTicksToNs( ipc_t1 - ipc_t0 ) / 1000000ULL) );
            }
#endif
            if (!status)
            {
                size_t update_size = reply->update_total_size;
                size_t visible_size = reply->visible_total_size;

                hrgn = switch_build_region_from_bytes( combined->Buffer, update_size );
                if (child) *child = wine_server_ptr_handle( reply->child );
                *flags = reply->update_flags;

                /* Always build a region object here, even for visible_size==0
                 * (window fully offscreen/occluded) -- update_visible_region()'s
                 * "if (status || !vis_rgn) return;" treats a NULL vis_rgn as a
                 * fetch failure, not "valid empty region", and the unbatched
                 * get_visible_region path always builds one regardless of size
                 * too (see its own NtGdiExtCreateRegion call below). */
                switch_paint_regions_prefetch.vis_rgn =
                    switch_build_region_from_bytes( combined->Buffer + update_size, visible_size );
                switch_paint_regions_prefetch.valid       = 1;
                switch_paint_regions_prefetch.hwnd        = hwnd;
                switch_paint_regions_prefetch.flags       = visible_flags;
                switch_paint_regions_prefetch.top_win     = wine_server_ptr_handle( reply->top_win );
                switch_paint_regions_prefetch.win_rect    = wine_server_get_rect( reply->win_rect );
                switch_paint_regions_prefetch.top_rect    = wine_server_get_rect( reply->top_rect );
                switch_paint_regions_prefetch.paint_flags = reply->paint_flags;

                if (wine_nx_paint_trace_enabled)
                {
                    static unsigned int logged;
                    if (logged < 5)
                    {
                        char buf[192];
                        snprintf( buf, sizeof(buf),
                                 "[NXBATCH] prefetch hwnd=%x usize=%u vsize=%u top=%x win=%d,%d-%d,%d",
                                 (int)(ULONG_PTR)hwnd, (unsigned int)update_size, (unsigned int)visible_size,
                                 (int)(ULONG_PTR)switch_paint_regions_prefetch.top_win,
                                 switch_paint_regions_prefetch.win_rect.left, switch_paint_regions_prefetch.win_rect.top,
                                 switch_paint_regions_prefetch.win_rect.right, switch_paint_regions_prefetch.win_rect.bottom );
                        wine_nx_runtime_trace( buf );
                        logged++;
                    }
                }
            }
            else size = reply->update_total_size + reply->visible_total_size;
        }
        SERVER_END_REQ;
        free( combined );
    } while (status == STATUS_BUFFER_OVERFLOW);

    if (status) RtlSetLastWin32Error( RtlNtStatusToDosError(status) );
    return hrgn;
}
#endif

/***********************************************************************
 *           get_update_region
 *
 * Return update region (in screen coordinates) for a window.
 */
static HRGN get_update_region( HWND hwnd, UINT *flags, HWND *child )
{
    HRGN hrgn = 0;
    NTSTATUS status;
    RGNDATA *data;
    size_t size = 256;

    do
    {
        if (!(data = malloc( sizeof(*data) + size - 1 )))
        {
            RtlSetLastWin32Error( ERROR_OUTOFMEMORY );
            return 0;
        }

        SERVER_START_REQ( get_update_region )
        {
            req->window     = wine_server_user_handle( hwnd );
            req->from_child = wine_server_user_handle( child ? *child : 0 );
            req->flags      = *flags;
            wine_server_set_reply( req, data->Buffer, size );
            if (!(status = wine_server_call( req )))
            {
                size_t reply_size = wine_server_reply_size( reply );
                data->rdh.dwSize   = sizeof(data->rdh);
                data->rdh.iType    = RDH_RECTANGLES;
                data->rdh.nCount   = reply_size / sizeof(RECT);
                data->rdh.nRgnSize = reply_size;
                hrgn = NtGdiExtCreateRegion( NULL, data->rdh.dwSize + data->rdh.nRgnSize, data );
                if (child) *child = wine_server_ptr_handle( reply->child );
                *flags = reply->flags;
            }
            else size = reply->total_size;
        }
        SERVER_END_REQ;
        free( data );
    } while (status == STATUS_BUFFER_OVERFLOW);

    if (status) RtlSetLastWin32Error( RtlNtStatusToDosError(status) );
    return hrgn;
}

/***********************************************************************
 *           redraw_window_rects
 *
 * Redraw part of a window.
 */
static BOOL redraw_window_rects( HWND hwnd, UINT flags, const RECT *rects, UINT count )
{
    BOOL ret;

    if (!(flags & (RDW_INVALIDATE|RDW_VALIDATE|RDW_INTERNALPAINT|RDW_NOINTERNALPAINT)))
        return TRUE;  /* nothing to do */

#ifdef __SWITCH__
    switch_redraw_window_cache.valid = 0;
#endif

    SERVER_START_REQ( redraw_window )
    {
        req->window = wine_server_user_handle( hwnd );
        req->flags  = flags;
        wine_server_add_data( req, rects, count * sizeof(RECT) );
        ret = !wine_server_call_err( req );
#ifdef __SWITCH__
        switch_paint_state_generation++;
        if (ret && wine_nx_batch_redraw_updatenow_enabled)
        {
            switch_redraw_window_cache.valid       = 1;
            switch_redraw_window_cache.hwnd         = hwnd;
            switch_redraw_window_cache.child        = wine_server_ptr_handle( reply->child );
            switch_redraw_window_cache.flags        = reply->flags;
            switch_redraw_window_cache.has_children = reply->has_children != 0;
            switch_redraw_window_cache.gen_after    = switch_paint_state_generation;
        }
#endif
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           get_update_flags
 *
 * Get only the update flags, not the update region.
 */
static BOOL get_update_flags( HWND hwnd, HWND *child, UINT *flags )
{
    BOOL ret;

    SERVER_START_REQ( get_update_region )
    {
        req->window     = wine_server_user_handle( hwnd );
        req->from_child = wine_server_user_handle( child ? *child : 0 );
        req->flags      = *flags | UPDATE_NOREGION;
        if ((ret = !wine_server_call_err( req )))
        {
            if (child) *child = wine_server_ptr_handle( reply->child );
            *flags = reply->flags;
        }
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           send_ncpaint
 *
 * Send a WM_NCPAINT message if needed, and return the resulting update region (in screen coords).
 * Helper for erase_now and BeginPaint.
 */
static HRGN send_ncpaint( HWND hwnd, HWND *child, UINT *flags )
{
    HRGN whole_rgn;
    HRGN client_rgn = 0;
    DWORD style;

#ifdef __SWITCH__
    if (wine_nx_batch_paint_regions_enabled)
        whole_rgn = switch_prefetch_paint_regions( hwnd, flags, child );
    else
        whole_rgn = get_update_region( hwnd, flags, child );
#else
    whole_rgn = get_update_region( hwnd, flags, child );
#endif

    if (child) hwnd = *child;

    if (hwnd == get_desktop_window()) return whole_rgn;

    if (whole_rgn)
    {
        struct window_rects rects;
        UINT context;
        RECT update;
        INT type;

        context = set_thread_dpi_awareness_context( get_window_dpi_awareness_context( hwnd ));

        /* check if update rgn overlaps with nonclient area */
        type = NtGdiGetRgnBox( whole_rgn, &update );
        get_window_rects( hwnd, COORDS_SCREEN, &rects, get_thread_dpi() );

        if ((*flags & UPDATE_NONCLIENT) ||
            update.left < rects.client.left || update.top < rects.client.top ||
            update.right > rects.client.right || update.bottom > rects.client.bottom)
        {
            client_rgn = NtGdiCreateRectRgn( rects.client.left, rects.client.top, rects.client.right, rects.client.bottom );
            NtGdiCombineRgn( client_rgn, client_rgn, whole_rgn, RGN_AND );

            /* check if update rgn contains complete nonclient area */
            if (type == SIMPLEREGION && EqualRect( &rects.window, &update ))
            {
                NtGdiDeleteObjectApp( whole_rgn );
                whole_rgn = (HRGN)1;
            }
        }
        else
        {
            client_rgn = whole_rgn;
            whole_rgn = 0;
        }

        if (whole_rgn) /* NOTE: WM_NCPAINT allows wParam to be 1 */
        {
            if (*flags & UPDATE_NONCLIENT)
            {
                /* Mark standard scroll bars as not painted before sending WM_NCPAINT */
                style = get_window_long( hwnd, GWL_STYLE );
                if (style & WS_HSCROLL)
                    set_standard_scroll_painted( hwnd, SB_HORZ, FALSE );
                if (style & WS_VSCROLL)
                    set_standard_scroll_painted( hwnd, SB_VERT, FALSE );

#ifdef __SWITCH__
                /* App code is about to run and may change window geometry --
                 * any visible-region data already prefetched for this
                 * BeginPaint call can no longer be trusted. See the design
                 * comment above switch_prefetch_paint_regions(). */
                if (wine_nx_batch_paint_regions_enabled && switch_paint_regions_prefetch.valid &&
                    wine_nx_paint_trace_enabled)
                {
                    static unsigned int logged;
                    if (logged < 5)
                    {
                        char buf[128];
                        snprintf( buf, sizeof(buf), "[NXBATCH] invalidated hwnd=%x reason=WM_NCPAINT",
                                 (int)(ULONG_PTR)hwnd );
                        wine_nx_runtime_trace( buf );
                        logged++;
                    }
                }
                switch_paint_regions_prefetch_clear();
#endif
                send_notify_message( hwnd, WM_NCPAINT, (WPARAM)whole_rgn, 0, FALSE );
            }
            if (whole_rgn > (HRGN)1) NtGdiDeleteObjectApp( whole_rgn );
        }
        set_thread_dpi_awareness_context( context );
    }
    return client_rgn;
}

/***********************************************************************
 *           send_erase
 *
 * Send a WM_ERASEBKGND message if needed, and optionally return the DC for painting.
 * If a DC is requested, the region is selected into it. In all cases the region is deleted.
 * Helper for erase_now and BeginPaint.
 */
static BOOL send_erase( HWND hwnd, UINT flags, HRGN client_rgn,
                        RECT *clip_rect, HDC *hdc_ret )
{
    BOOL need_erase = (flags & UPDATE_DELAYED_ERASE) != 0;
    HDC hdc = 0;
    RECT dummy;
#ifdef __SWITCH__
    u64 t0, t1;
#endif

    if (!clip_rect) clip_rect = &dummy;
    if (hdc_ret || (flags & UPDATE_ERASE))
    {
        UINT dcx_flags = DCX_INTERSECTRGN | DCX_USESTYLE;
        if (is_iconic(hwnd)) dcx_flags |= DCX_WINDOW;

#ifdef __SWITCH__
        t0 = armGetSystemTick();
#endif
        hdc = NtUserGetDCEx( hwnd, client_rgn, dcx_flags );
#ifdef __SWITCH__
        t1 = armGetSystemTick();
        /* NtUserGetDCEx's DCX_INTERSECTRGN forces update_visible_region(),
         * which is the get_visible_region IPC call -- isolating this from
         * the WM_ERASEBKGND dispatch below tells us how much of erase's
         * ~47ms is "getting a DC" vs. "actually erasing". See README,
         * "The ~14ms Per-Call IPC Floor". */
        switch_paint_trace( "erase_get_dcex", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif
        if (hdc)
        {
#ifdef __SWITCH__
            t0 = armGetSystemTick();
#endif
            INT type = NtGdiGetAppClipBox( hdc, clip_rect );
#ifdef __SWITCH__
            t1 = armGetSystemTick();
            switch_paint_trace( "erase_clipbox", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif

            if (flags & UPDATE_ERASE)
            {
                /* don't erase if the clip box is empty */
                if (type != NULLREGION)
                {
#ifdef __SWITCH__
                    t0 = armGetSystemTick();
#endif
                    need_erase = !send_message_timeout( hwnd, WM_ERASEBKGND, (WPARAM)hdc, 0, SMTO_ABORTIFHUNG, 1000, FALSE );
#ifdef __SWITCH__
                    t1 = armGetSystemTick();
                    /* This is the actual WM_ERASEBKGND dispatch: process_message()'s
                     * get_window_thread() IPC lookup (another ~14ms-floor call, see
                     * README) followed by call_window_proc() running whatever this
                     * window's WM_ERASEBKGND handling actually does -- for
                     * gui_smoke.c that's DefWindowProcW's default FillRect with the
                     * class background brush, since the app doesn't handle
                     * WM_ERASEBKGND itself. */
                    switch_paint_trace( "erase_wm_erasebkgnd", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif
                    if (need_erase && RtlGetLastWin32Error() == ERROR_TIMEOUT) ERR( "timeout.\n" );
                }
            }
            if (!hdc_ret)
            {
#ifdef __SWITCH__
                t0 = armGetSystemTick();
#endif
                release_dc( hwnd, hdc, TRUE );
#ifdef __SWITCH__
                t1 = armGetSystemTick();
                switch_paint_trace( "erase_release_dc", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif
            }
        }

        if (hdc_ret) *hdc_ret = hdc;
    }
    if (!hdc) NtGdiDeleteObjectApp( client_rgn );
    return need_erase;
}

/***********************************************************************
 *           move_window_bits
 *
 * Move the window bits when a window is resized, or moved within a parent window.
 */
void move_window_bits( HWND hwnd, const struct window_rects *rects, const RECT *valid_rects )
{
    RECT dst = valid_rects[0], src = valid_rects[1];

    if (src.left - rects->visible.left != dst.left - rects->visible.left ||
        src.top - rects->visible.top != dst.top - rects->visible.top)
    {
        UINT flags = UPDATE_NOCHILDREN | UPDATE_CLIPCHILDREN;
        HRGN rgn = get_update_region( hwnd, &flags, NULL );
        HDC hdc = NtUserGetDCEx( hwnd, rgn, DCX_CACHE | DCX_WINDOW | DCX_EXCLUDERGN );

        TRACE( "copying %s -> %s\n", wine_dbgstr_rect( &src ), wine_dbgstr_rect( &dst ));
        OffsetRect( &src, -rects->window.left, -rects->window.top );
        OffsetRect( &dst, -rects->window.left, -rects->window.top );

        NtGdiStretchBlt( hdc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                         hdc, src.left, src.top, src.right - src.left, src.bottom - src.top, SRCCOPY, 0 );
        NtUserReleaseDC( hwnd, hdc );
    }
}


/***********************************************************************
 *           move_window_bits_surface
 *
 * Move the window bits from a previous window surface when its surface is recreated.
 */
void move_window_bits_surface( HWND hwnd, const RECT *window_rect, struct window_surface *old_surface,
                               const RECT *old_visible_rect, const RECT *valid_rects )
{
    char buffer[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
    BITMAPINFO *info = (BITMAPINFO *)buffer;
    UINT flags = UPDATE_NOCHILDREN | UPDATE_CLIPCHILDREN;
    HRGN rgn = get_update_region( hwnd, &flags, NULL );
    HDC hdc = NtUserGetDCEx( hwnd, rgn, DCX_CACHE | DCX_WINDOW | DCX_EXCLUDERGN );
    void *bits;

    RECT dst = valid_rects[0];
    RECT src = valid_rects[1];

    TRACE( "copying %s -> %s\n", wine_dbgstr_rect( &src ), wine_dbgstr_rect( &dst ));
    OffsetRect( &src, -old_visible_rect->left, -old_visible_rect->top );
    OffsetRect( &dst, -window_rect->left, -window_rect->top );

    window_surface_lock( old_surface );
    bits = window_surface_get_color( old_surface, info );
    NtGdiSetDIBitsToDeviceInternal( hdc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                                    src.left - old_surface->rect.left, old_surface->rect.bottom - src.bottom,
                                    0, old_surface->rect.bottom - old_surface->rect.top,
                                    bits, info, DIB_RGB_COLORS, 0, 0, FALSE, NULL );
    window_surface_unlock( old_surface );
    NtUserReleaseDC( hwnd, hdc );
}


/***********************************************************************
 *           NtUserBeginPaint (win32u.@)
 */
HDC WINAPI NtUserBeginPaint( HWND hwnd, PAINTSTRUCT *ps )
{
    HRGN hrgn;
    HDC hdc;
    BOOL erase;
    RECT rect;
    UINT flags = UPDATE_NONCLIENT | UPDATE_ERASE | UPDATE_PAINT | UPDATE_INTERNALPAINT | UPDATE_NOCHILDREN;
#ifdef __SWITCH__
    u64 t0, t1;
#endif

    NtUserHideCaret( hwnd );

#ifdef __SWITCH__
    {
        static int logged_mode;
        if (!logged_mode)
        {
            logged_mode = 1;
            wine_nx_runtime_trace( wine_nx_batch_paint_regions_enabled
                                   ? "[NXBATCH] NtUserBeginPaint: combined get_paint_regions mode ACTIVE"
                                   : "[NXBATCH] NtUserBeginPaint: legacy sequential get_update_region+get_visible_region mode ACTIVE (current default)" );
        }
    }
    t0 = armGetSystemTick();
#endif
    if (!(hrgn = send_ncpaint( hwnd, NULL, &flags )))
    {
#ifdef __SWITCH__
        switch_paint_regions_prefetch_clear();
#endif
        return 0;
    }
#ifdef __SWITCH__
    t1 = armGetSystemTick();
    switch_paint_trace( "ncpaint", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif

#ifdef __SWITCH__
    t0 = armGetSystemTick();
#endif
    erase = send_erase( hwnd, flags, hrgn, &rect, &hdc );
#ifdef __SWITCH__
    t1 = armGetSystemTick();
    switch_paint_trace( "erase", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
    /* Defensive: if update_vis_rgn ended up FALSE inside NtUserGetDCEx (e.g.
     * a cached DCE was reused without needing a visible-region refresh), the
     * prefetch above was never consumed. Clear it here so it can never be
     * picked up by some later, unrelated call for the same hwnd. */
    switch_paint_regions_prefetch_clear();
#endif

    TRACE( "hdc = %p box = (%s), fErase = %d\n", hdc, wine_dbgstr_rect(&rect), erase );

    if (!ps)
    {
        release_dc( hwnd, hdc, TRUE );
        return 0;
    }
    ps->fErase = erase;
    ps->rcPaint = rect;
    ps->hdc = hdc;
    return hdc;
}

/***********************************************************************
 *           NtUserEndPaint (win32u.@)
 */
#ifdef __SWITCH__
static void switch_present_surface_dirty( struct window_surface *surface )
{
    extern void wine_nx_fb_present( void ) __attribute__((weak));
    u64 t0, t1;

    /* present_dirty (NtUserEndPaint's own timer around this whole function)
     * measured ~99.9ms while window_surface_flush()'s own known children
     * (lock/get_color/shape/funcs_flush/unlock) and wine_nx_fb_present()'s
     * known children (dkQueueAcquireImage/submit+signal+present) only
     * summed to ~29.5ms -- a ~70ms gap not attributable to either
     * function's already-timed internals from source alone (checked
     * wine_nx_hud_draw(), wine_nx_hud_mark_attempt(), and g_dk_mutex
     * contention -- all too small or not applicable). Splitting the timer
     * at this exact boundary answers which *side* the gap is actually on
     * before tracing either one further -- see README, "Presentation Is
     * Still Too Slow". */
    t0 = armGetSystemTick();
    window_surface_flush( surface );
    t1 = armGetSystemTick();
    switch_paint_trace( "surface_flush_call", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );

    if (&wine_nx_fb_present)
    {
        t0 = armGetSystemTick();
        wine_nx_fb_present();
        t1 = armGetSystemTick();
        switch_paint_trace( "fb_present_call", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
    }
}
#endif

BOOL WINAPI NtUserEndPaint( HWND hwnd, const PAINTSTRUCT *ps )
{
    struct window_surface *surface;
#ifdef __SWITCH__
    u64 t0, t1;
#endif

    NtUserShowCaret( hwnd );
#ifdef __SWITCH__
    t0 = armGetSystemTick();
#endif
    flush_window_surfaces( FALSE );
#ifdef __SWITCH__
    t1 = armGetSystemTick();
    switch_paint_trace( "flush_surfaces", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
#endif
    if (!ps)
    {
        if ((surface = window_surface_get( hwnd ))) window_surface_release( surface );
        else                                        user_driver->pProcessEvents( 0 );
        return FALSE;
    }
    release_dc( hwnd, ps->hdc, TRUE );
#ifdef __SWITCH__
    if ((surface = window_surface_get( hwnd )))
    {
        t0 = armGetSystemTick();
        switch_present_surface_dirty( surface );
        t1 = armGetSystemTick();
        switch_paint_trace( "present_dirty", (unsigned int)(armTicksToNs( t1 - t0 ) / 1000000ULL) );
        window_surface_release( surface );
    }
    else user_driver->pProcessEvents( 0 );
#else
    if ((surface = window_surface_get( hwnd ))) window_surface_release( surface );
    else                                        user_driver->pProcessEvents( 0 );
#endif
    return TRUE;
}

/***********************************************************************
 *           erase_now
 *
 * Implementation of RDW_ERASENOW behavior.
 */
void erase_now( HWND hwnd, UINT rdw_flags )
{
    HWND child = 0;
    HRGN hrgn;
    BOOL need_erase = FALSE;

    /* loop while we find a child to repaint */
    for (;;)
    {
        UINT flags = UPDATE_NONCLIENT | UPDATE_ERASE;

        if (rdw_flags & RDW_NOCHILDREN) flags |= UPDATE_NOCHILDREN;
        else if (rdw_flags & RDW_ALLCHILDREN) flags |= UPDATE_ALLCHILDREN;
        if (need_erase) flags |= UPDATE_DELAYED_ERASE;

        if (!(hrgn = send_ncpaint( hwnd, &child, &flags ))) break;
        need_erase = send_erase( child, flags, hrgn, NULL, NULL );

        if (!flags) break;  /* nothing more to do */
        if ((rdw_flags & RDW_NOCHILDREN) && !need_erase) break;
    }
}

#ifdef __SWITCH__
static BOOL switch_get_update_flags_ex( HWND hwnd, HWND *child, UINT *flags, BOOL *has_children )
{
    BOOL ret;

    SERVER_START_REQ( get_update_flags_ex )
    {
        req->window     = wine_server_user_handle( hwnd );
        req->from_child = wine_server_user_handle( child ? *child : 0 );
        req->flags      = *flags | UPDATE_NOREGION;
        if ((ret = !wine_server_call_err( req )))
        {
            if (child) *child = wine_server_ptr_handle( reply->child );
            *flags = reply->flags;
            *has_children = reply->has_children != 0;
        }
    }
    SERVER_END_REQ;
    return ret;
}

/* Switch-specific replacement for update_now(), gated behind
 * wine_nx_skip_redundant_update_check_enabled. Re-derived from source, not
 * assumed: update_now()'s for(;;) loop calls get_update_flags() BEFORE
 * dispatching WM_PAINT (finds whatever window in hwnd's subtree currently
 * has a pending update, starting the search over from hwnd each time) and
 * calls it AGAIN after (with from_child set to the window just dispatched,
 * so the search resumes past it) to see if there's more to do -- looping
 * until a call finds nothing. The AFTER check's real job is walking the
 * REST of the subtree: siblings that already had their own pending update
 * before this call started, OR a window that the just-processed WM_PAINT
 * handler itself invalidated (a child painting itself, or a child newly
 * created and invalidated by the parent's own handler -- both real,
 * supported Win32 patterns this loop exists specifically to catch across
 * multiple iterations, not just one extra safety check).
 *
 * For UpdateWindow() specifically (dlls/user32/win.c: RDW_UPDATENOW |
 * RDW_ALLCHILDREN, no RDW_NOCHILDREN), this loop runs unconditionally
 * twice for hwnd's own top-level paint even when hwnd has zero children --
 * once to find+dispatch, once more that can only ever confirm nothing else
 * is pending, because the search has nothing to recurse into. Confirmed via
 * hardware logs: get_update_region fired exactly 2x per get_paint_regions
 * (2:1:1 with redraw_window) across 129 frames of gui_smoke.c, a childless
 * window, with no variance.
 *
 * The one case that makes the second check NOT redundant even for a
 * childless window: the just-dispatched WM_PAINT handler creates a new
 * child window and invalidates it, all before returning -- unusual, but a
 * real, legal Win32 pattern, and exactly the kind of "silently breaks
 * repaint correctness for more complex real apps" regression this session
 * has been careful to avoid everywhere else. Handled by a proof, not a
 * guess: has_children (from the first call's reply, always computed for
 * hwnd itself, independent of whatever the recursive search finds) tells
 * us hwnd had zero children before the dispatch; switch_window_tree_generation
 * (bumped by every NtUserCreateWindowEx/NtUserSetParent call in this
 * process, attempted or not, see window.c) tells us whether anything could
 * have added one during the dispatch. Skip the follow-up round trip only
 * when BOTH hold -- zero children before AND no window-tree-changing call
 * happened during -- which is exactly the condition under which the
 * server's own search algorithm (horizon_server_find_window_update_locked,
 * dlls/ntdll/unix/horizon.c) is provably guaranteed to find nothing: with
 * no children to recurse into, and past_from already true for hwnd itself
 * (it was just dispatched), the for-loop over horizon_windows has nothing
 * matching child->parent == hwnd to ever iterate over.
 *
 * Known, deliberately out-of-scope gap: a DIFFERENT thread in the same
 * process creating or reparenting a window into hwnd's subtree while this
 * thread is blocked inside the synchronous WM_PAINT dispatch would still
 * bump the generation counter (correctly triggering the fallback), so this
 * isn't actually a correctness gap -- just noting it as the one scenario
 * that makes the counter earn its keep beyond the single-threaded case,
 * since Wine's per-window UI threading model makes it exotic in practice. */
/* Shared loop body for switch_update_now(), factored out so
 * switch_redraw_window_updatenow() (below) can feed it an already-fetched
 * first iteration (from the combined redraw_window+get_update_flags_ex
 * call) instead of making switch_update_now()'s usual first fetch itself.
 * have_first == FALSE reproduces switch_update_now()'s original for(;;)
 * loop byte-for-byte (the "if (!have_first)" branch runs every iteration,
 * exactly like the loop always did); have_first == TRUE skips only the one
 * fetch that the caller already did, then rejoins the identical logic from
 * "if (!flags) break" onward. */
static void switch_update_now_loop( HWND hwnd, UINT rdw_flags, HWND child, UINT flags,
                                    BOOL has_children, LONG gen_before, BOOL have_first )
{
    for (;;)
    {
        if (!have_first)
        {
            flags = UPDATE_PAINT | UPDATE_INTERNALPAINT;
            has_children = TRUE;   /* fail-safe: never skip unless proven empty below */

            if (rdw_flags & RDW_NOCHILDREN) flags |= UPDATE_NOCHILDREN;
            else if (rdw_flags & RDW_ALLCHILDREN) flags |= UPDATE_ALLCHILDREN;

            gen_before = switch_window_tree_generation;
            if (!switch_get_update_flags_ex( hwnd, &child, &flags, &has_children )) break;
        }
        have_first = FALSE;

        if (!flags) break;

#ifdef __SWITCH__
        /* Wraps the *entire* synchronous WM_PAINT dispatch -- BeginPaint's
         * IPC/prefetch consumption, the app's own drawing calls in between,
         * and EndPaint's flush, all at once. Compare against the sum of
         * ncpaint+erase+surface_funcs_flush+present_dirty (+ the app's own
         * blit_avg/shapes_avg, measured separately in gui_smoke.c) to find
         * out how much of paint_avg is genuinely untraced: SendMessage/
         * DispatchMessage's own dispatch machinery, vs. the app's drawing
         * code, vs. something this session hasn't instrumented yet. */
        {
            u64 wm_t0 = 0, wm_t1;
            if (wine_nx_paint_trace_enabled) wm_t0 = armGetSystemTick();
            send_message( child, WM_PAINT, 0, 0 );
            if (wine_nx_paint_trace_enabled)
            {
                wm_t1 = armGetSystemTick();
                switch_paint_trace( "wm_paint_dispatch", (unsigned int)(armTicksToNs( wm_t1 - wm_t0 ) / 1000000ULL) );
            }
        }
#else
        send_message( child, WM_PAINT, 0, 0 );
#endif
        if (rdw_flags & RDW_NOCHILDREN) break;

        if (!has_children && gen_before == switch_window_tree_generation)
        {
            if (wine_nx_paint_trace_enabled)
            {
                static unsigned int logged;
                if (logged < 5)
                {
                    char buf[128];
                    snprintf( buf, sizeof(buf), "[NXUPDATE] skip hwnd=%x (no children, tree unchanged)",
                             (int)(ULONG_PTR)hwnd );
                    wine_nx_runtime_trace( buf );
                    logged++;
                }
            }
            break;
        }

        if (wine_nx_paint_trace_enabled)
        {
            static unsigned int logged;
            if (logged < 5)
            {
                char buf[160];
                snprintf( buf, sizeof(buf),
                         "[NXUPDATE] fallback hwnd=%x has_children=%d gen_before=%d gen_after=%d",
                         (int)(ULONG_PTR)hwnd, has_children, (int)gen_before, (int)switch_window_tree_generation );
                wine_nx_runtime_trace( buf );
                logged++;
            }
        }
    }
}

static void switch_update_now( HWND hwnd, UINT rdw_flags )
{
    if (hwnd == get_desktop_window())
    {
        erase_now( hwnd, rdw_flags | RDW_NOCHILDREN );
        return;
    }

#ifdef __SWITCH__
    /* Opportunistic reuse of the immediately-preceding redraw_window call's
     * reply -- see the long comment on switch_redraw_window_cache above.
     * gen_after == wine_server_call_generation proves nothing else called
     * into the server between that redraw_window call and right now, so
     * the search it already computed (from_child=0, same flags this
     * function's own first fetch would use) is still exactly correct --
     * not stale, not guessed. One-shot: cleared immediately so a second,
     * unrelated UpdateWindow() later never reuses it. */
    if (wine_nx_batch_redraw_updatenow_enabled &&
        switch_redraw_window_cache.valid &&
        switch_redraw_window_cache.hwnd == hwnd &&
        switch_redraw_window_cache.gen_after == switch_paint_state_generation)
    {
        HWND child = switch_redraw_window_cache.child;
        UINT flags = switch_redraw_window_cache.flags;
        BOOL has_children = switch_redraw_window_cache.has_children;
        LONG gen_before = switch_window_tree_generation;

        switch_redraw_window_cache.valid = 0;

        if (wine_nx_paint_trace_enabled)
        {
            static unsigned int logged;
            if (logged < 5)
            {
                char buf[96];
                snprintf( buf, sizeof(buf), "[NXCACHE] redraw_window reply reused, hwnd=%x",
                         (int)(ULONG_PTR)hwnd );
                wine_nx_runtime_trace( buf );
                logged++;
            }
        }

        switch_update_now_loop( hwnd, rdw_flags, child, flags, has_children, gen_before, TRUE );
        return;
    }
#endif

    switch_update_now_loop( hwnd, rdw_flags, 0, 0, FALSE, 0, FALSE );
}

/* Combined redraw_window(count==0) + switch_update_now()'s first search,
 * gated behind wine_nx_batch_redraw_updatenow_enabled (off by default).
 * Only called from NtUserRedrawWindow's no-rect/no-region, RDW_UPDATENOW
 * branch -- exactly UpdateWindow()'s real call shape, and the one case
 * where redraw_window_rects()'s call and switch_update_now()'s first
 * switch_get_update_flags_ex() call are adjacent with no intervening
 * app-code dispatch (confirmed by reading NtUserRedrawWindow directly, not
 * assumed). Handles the entire redraw+search+dispatch job itself; the
 * caller should skip both its own redraw_window_rects() call and its later
 * switch_update_now() call when this returns. */
static void switch_redraw_window_updatenow( HWND hwnd, UINT rdw_flags )
{
    HWND child = 0;
    UINT search_flags = UPDATE_PAINT | UPDATE_INTERNALPAINT;
    BOOL has_children = TRUE;
    LONG gen_before;
    BOOL ok;

    if (rdw_flags & RDW_NOCHILDREN) search_flags |= UPDATE_NOCHILDREN;
    else if (rdw_flags & RDW_ALLCHILDREN) search_flags |= UPDATE_ALLCHILDREN;

    gen_before = switch_window_tree_generation;

    SERVER_START_REQ( redraw_window_updatenow )
    {
        req->window       = wine_server_user_handle( hwnd );
        req->redraw_flags = rdw_flags;
        req->search_flags = search_flags | UPDATE_NOREGION;
        if ((ok = !wine_server_call_err( req )))
        {
            child        = wine_server_ptr_handle( reply->child );
            search_flags = reply->flags;
            has_children = reply->has_children != 0;
        }
        switch_paint_state_generation++;
    }
    SERVER_END_REQ;

    if (!ok) return;  /* matches redraw_window_rects()/switch_update_now() both silently no-op'ing on IPC failure */

    switch_update_now_loop( hwnd, rdw_flags, child, search_flags, has_children, gen_before, TRUE );
}
#endif

/***********************************************************************
 *           update_now
 *
 * Implementation of RDW_UPDATENOW behavior.
 */
static void update_now( HWND hwnd, UINT rdw_flags )
{
    HWND child = 0;

    /* desktop window never gets WM_PAINT, only WM_ERASEBKGND */
    if (hwnd == get_desktop_window()) erase_now( hwnd, rdw_flags | RDW_NOCHILDREN );

    /* loop while we find a child to repaint */
    for (;;)
    {
        UINT flags = UPDATE_PAINT | UPDATE_INTERNALPAINT;

        if (rdw_flags & RDW_NOCHILDREN) flags |= UPDATE_NOCHILDREN;
        else if (rdw_flags & RDW_ALLCHILDREN) flags |= UPDATE_ALLCHILDREN;

        if (!get_update_flags( hwnd, &child, &flags )) break;
        if (!flags) break;  /* nothing more to do */

        send_message( child, WM_PAINT, 0, 0 );
        if (rdw_flags & RDW_NOCHILDREN) break;
    }
}

/***********************************************************************
 *           NtUserRedrawWindow (win32u.@)
 */
BOOL WINAPI NtUserRedrawWindow( HWND hwnd, const RECT *rect, HRGN hrgn, UINT flags )
{
    static const RECT empty;
    BOOL ret;
#ifdef __SWITCH__
    BOOL combined_done = FALSE;
#endif

    if (TRACE_ON(win))
    {
        if (hrgn)
        {
            RECT r;
            NtGdiGetRgnBox( hrgn, &r );
            TRACE( "%p region %p box %s ", hwnd, hrgn, wine_dbgstr_rect(&r) );
        }
        else if (rect)
            TRACE( "%p rect %s ", hwnd, wine_dbgstr_rect(rect) );
        else
            TRACE( "%p whole window ", hwnd );

        dump_rdw_flags(flags);
    }

    /* process pending expose events before painting */
#ifdef __SWITCH__
    /* Not yet known whether this costs anything real on this port --
     * process_driver_events() (message.c) can make its own set_queue_mask
     * server call here depending on shared-queue state (queue_shm's
     * wake_mask/changed_mask and access_time), and that call isn't wrapped
     * by any existing phase timer (redraw_window/get_paint_regions cover
     * the two calls this session already knows about; this one was never
     * measured). Timed here to find out on the next hardware run instead
     * of guessing from source alone. */
    if (flags & RDW_UPDATENOW)
    {
        u64 cfe_t0 = 0, cfe_t1;
        if (wine_nx_paint_trace_enabled) cfe_t0 = armGetSystemTick();
        check_for_events( QS_PAINT );
        if (wine_nx_paint_trace_enabled)
        {
            cfe_t1 = armGetSystemTick();
            switch_paint_trace( "check_for_events", (unsigned int)(armTicksToNs( cfe_t1 - cfe_t0 ) / 1000000ULL) );
        }
    }
#else
    if (flags & RDW_UPDATENOW) check_for_events( QS_PAINT );
#endif

    if (rect && !hrgn)
    {
        RECT ordered = *rect;

        order_rect( &ordered );
        if (IsRectEmpty( &ordered )) ordered = empty;
        ret = redraw_window_rects( hwnd, flags, &ordered, 1 );
    }
    else if (!hrgn)
    {
#ifdef __SWITCH__
        /* Genuine same-call combine: RDW_INVALIDATE and RDW_UPDATENOW both
         * set on ONE NtUserRedrawWindow call (e.g. an app calling
         * RedrawWindow() directly instead of InvalidateRect()+UpdateWindow()).
         * Bug fixed here: this used to check RDW_UPDATENOW alone, which
         * ALSO matches UpdateWindow()'s own call (RDW_UPDATENOW only, no
         * RDW_INVALIDATE) -- meaning it unconditionally intercepted every
         * UpdateWindow() call, set combined_done, and skipped
         * switch_update_now() entirely, so the redraw_window-reply cache
         * below (switch_update_now(), populated by InvalidateRect()'s own
         * earlier redraw_window_rects() call) never got a chance to run.
         * Confirmed on hardware: redraw_window and redraw_window_updatenow
         * both fired once per frame, same 2-round-trip cost as before this
         * "fix" existed. Requiring RDW_INVALIDATE here restores the
         * fall-through below for UpdateWindow()'s own call, which is where
         * the real saving happens now. hwnd != 0 && hwnd != desktop matches
         * the exact condition under which switch_update_now() itself would
         * not take its early-return desktop branch. */
        if (wine_nx_batch_redraw_updatenow_enabled && (flags & RDW_UPDATENOW) && (flags & RDW_INVALIDATE) &&
            wine_nx_skip_redundant_update_check_enabled &&
            hwnd && hwnd != get_desktop_window())
        {
            switch_redraw_window_updatenow( hwnd, flags );
            combined_done = TRUE;
            ret = TRUE;
        }
        else
#endif
        /* UpdateWindow()'s own call (RDW_UPDATENOW, no RDW_INVALIDATE) hits
         * this too -- redraw_window_rects()'s own early-return guard
         * ("nothing to do" when none of RDW_INVALIDATE/VALIDATE/
         * INTERNALPAINT/NOINTERNALPAINT are set) makes that a cheap no-op,
         * not a wasted IPC call, leaving switch_update_now() below free to
         * check its cache. */
        ret = redraw_window_rects( hwnd, flags, NULL, 0 );
    }
    else  /* need to build a list of the region rectangles */
    {
        DWORD size;
        RGNDATA *data;

        if (!(size = NtGdiGetRegionData( hrgn, 0, NULL ))) return FALSE;
        if (!(data = malloc( size ))) return FALSE;
        NtGdiGetRegionData( hrgn, size, data );
        if (!data->rdh.nCount)  /* empty region -> use a single all-zero rectangle */
            ret = redraw_window_rects( hwnd, flags, &empty, 1 );
        else
            ret = redraw_window_rects( hwnd, flags, (const RECT *)data->Buffer, data->rdh.nCount );
        free( data );
    }

    if (!hwnd) hwnd = get_desktop_window();

    if (flags & RDW_UPDATENOW)
    {
#ifdef __SWITCH__
        if (combined_done) {}  /* switch_redraw_window_updatenow() already did the full job above */
        else if (wine_nx_skip_redundant_update_check_enabled) switch_update_now( hwnd, flags );
        else update_now( hwnd, flags );
#else
        update_now( hwnd, flags );
#endif
    }
    else if (flags & RDW_ERASENOW) erase_now( hwnd, flags );

    return ret;
}

/***********************************************************************
 *           NtUserValidateRect (win32u.@)
 */
BOOL WINAPI NtUserValidateRect( HWND hwnd, const RECT *rect )
{
    UINT flags = RDW_VALIDATE;

    if (!hwnd)
    {
        flags = RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ERASENOW;
        rect = NULL;
    }

    return NtUserRedrawWindow( hwnd, rect, 0, flags );
}

/***********************************************************************
 *           NtUserValidateRgn (win32u.@)
 */
BOOL WINAPI NtUserValidateRgn( HWND hwnd, HRGN hrgn )
{
    if (!hwnd)
    {
        RtlSetLastWin32Error( ERROR_INVALID_WINDOW_HANDLE );
        return FALSE;
    }

    return NtUserRedrawWindow( hwnd, NULL, hrgn, RDW_VALIDATE );
}

/***********************************************************************
 *           NtUserGetUpdateRgn (win32u.@)
 */
INT WINAPI NtUserGetUpdateRgn( HWND hwnd, HRGN hrgn, BOOL erase )
{
    INT retval = ERROR;
    UINT flags = UPDATE_NOCHILDREN, context;
    HRGN update_rgn;

    context = set_thread_dpi_awareness_context( get_window_dpi_awareness_context( hwnd ));

    if (erase) flags |= UPDATE_NONCLIENT | UPDATE_ERASE;

    if ((update_rgn = send_ncpaint( hwnd, NULL, &flags )))
    {
        retval = NtGdiCombineRgn( hrgn, update_rgn, 0, RGN_COPY );
        if (send_erase( hwnd, flags, update_rgn, NULL, NULL ))
        {
            flags = UPDATE_DELAYED_ERASE;
            get_update_flags( hwnd, NULL, &flags );
        }
        /* map region to client coordinates */
        map_window_region( 0, hwnd, hrgn );
    }
    set_thread_dpi_awareness_context( context );
    return retval;
}

/***********************************************************************
 *           NtUserGetUpdateRect (win32u.@)
 */
BOOL WINAPI NtUserGetUpdateRect( HWND hwnd, RECT *rect, BOOL erase )
{
    UINT flags = UPDATE_NOCHILDREN;
    HRGN update_rgn;
    BOOL need_erase;

    if (erase) flags |= UPDATE_NONCLIENT | UPDATE_ERASE;

    if (!(update_rgn = send_ncpaint( hwnd, NULL, &flags ))) return FALSE;

    if (rect && NtGdiGetRgnBox( update_rgn, rect ) != NULLREGION)
    {
        HDC hdc = NtUserGetDCEx( hwnd, 0, DCX_USESTYLE );
        DWORD layout = NtGdiSetLayout( hdc, -1, 0 );  /* map_window_points mirrors already */
        UINT win_dpi = get_dpi_for_window( hwnd );
        map_window_points( 0, hwnd, (POINT *)rect, 2, win_dpi );
        *rect = map_dpi_rect( *rect, win_dpi, get_thread_dpi() );
        NtGdiTransformPoints( hdc, (POINT *)rect, (POINT *)rect, 2, NtGdiDPtoLP );
        NtGdiSetLayout( hdc, -1, layout );
        NtUserReleaseDC( hwnd, hdc );
    }
    need_erase = send_erase( hwnd, flags, update_rgn, NULL, NULL );

    /* check if we still have an update region */
    flags = UPDATE_PAINT | UPDATE_NOCHILDREN;
    if (need_erase) flags |= UPDATE_DELAYED_ERASE;
    return get_update_flags( hwnd, NULL, &flags ) && (flags & UPDATE_PAINT);
}

/***********************************************************************
 *           NtUserExcludeUpdateRgn (win32u.@)
 */
INT WINAPI NtUserExcludeUpdateRgn( HDC hdc, HWND hwnd )
{
    HRGN update_rgn = NtGdiCreateRectRgn( 0, 0, 0, 0 );
    INT ret = NtUserGetUpdateRgn( hwnd, update_rgn, FALSE );

    if (ret != ERROR)
    {
        UINT context;
        POINT pt;

        context = set_thread_dpi_awareness_context( get_window_dpi_awareness_context( hwnd ));
        NtGdiGetDCPoint( hdc, NtGdiGetDCOrg, &pt );
        map_window_points( 0, hwnd, &pt, 1, get_thread_dpi() );
        NtGdiOffsetRgn( update_rgn, -pt.x, -pt.y );
        ret = NtGdiExtSelectClipRgn( hdc, update_rgn, RGN_DIFF );
        set_thread_dpi_awareness_context( context );
    }
    NtGdiDeleteObjectApp( update_rgn );
    return ret;
}

/***********************************************************************
 *           NtUserInvalidateRgn (win32u.@)
 */
BOOL WINAPI NtUserInvalidateRgn( HWND hwnd, HRGN hrgn, BOOL erase )
{
    if (!hwnd)
    {
        RtlSetLastWin32Error( ERROR_INVALID_WINDOW_HANDLE );
        return FALSE;
    }

    return NtUserRedrawWindow( hwnd, NULL, hrgn, RDW_INVALIDATE | (erase ? RDW_ERASE : 0) );
}

/***********************************************************************
 *           NtUserInvalidateRect (win32u.@)
 */
BOOL WINAPI NtUserInvalidateRect( HWND hwnd, const RECT *rect, BOOL erase )
{
    UINT flags = RDW_INVALIDATE | (erase ? RDW_ERASE : 0);

    if (!hwnd)
    {
        flags = RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ERASENOW;
        rect = NULL;
    }

    return NtUserRedrawWindow( hwnd, rect, 0, flags );
}

/***********************************************************************
 *           NtUserLockWindowUpdate (win32u.@)
 */
BOOL WINAPI NtUserLockWindowUpdate( HWND hwnd )
{
    static HWND locked_hwnd;

    FIXME( "(%p), partial stub!\n", hwnd );

    if (!hwnd)
    {
        locked_hwnd = NULL;
        return TRUE;
    }
    return !InterlockedCompareExchangePointer( (void **)&locked_hwnd, hwnd, 0 );
}

/*************************************************************************
 *             fix_caret
 *
 * Helper for NtUserScrollWindowEx:
 * If the return value is 0, no special caret handling is necessary.
 * Otherwise the return value is the handle of the window that owns the
 * caret. Its caret needs to be hidden during the scroll operation and
 * moved to new_caret_pos if move_caret is TRUE.
 */
static HWND fix_caret( HWND hwnd, const RECT *scroll_rect, INT dx, INT dy,
                       UINT flags, BOOL *move_caret, POINT *new_caret_pos )
{
    RECT rect, mapped_caret;
    GUITHREADINFO info;

    info.cbSize = sizeof(info);
    if (!NtUserGetGUIThreadInfo( GetCurrentThreadId(), &info )) return 0;
    if (!info.hwndCaret) return 0;

    mapped_caret = info.rcCaret;
    if (info.hwndCaret == hwnd)
    {
        /* The caret needs to be moved along with scrolling even if it's
         * outside the visible area. Otherwise, when the caret is scrolled
         * out from the view, the position won't get updated anymore and
         * the caret will never scroll back again. */
        *move_caret = TRUE;
        new_caret_pos->x = info.rcCaret.left + dx;
        new_caret_pos->y = info.rcCaret.top + dy;
    }
    else
    {
        *move_caret = FALSE;
        if (!(flags & SW_SCROLLCHILDREN) || !is_child( hwnd, info.hwndCaret ))
            return 0;
        map_window_points( info.hwndCaret, hwnd, (POINT *)&mapped_caret, 2, get_thread_dpi() );
    }

    /* If the caret is not in the src/dest rects, all is fine done. */
    if (!intersect_rect( &rect, scroll_rect, &mapped_caret ))
    {
        rect = *scroll_rect;
        OffsetRect( &rect, dx, dy );
        if (!intersect_rect( &rect, &rect, &mapped_caret ))
            return 0;
    }

    /* Indicate that the caret needs to be updated during the scrolling. */
    return info.hwndCaret;
}

/*************************************************************************
 *           NtUserScrollWindowEx (win32u.@)
 *
 * Note: contrary to what the doc says, pixels that are scrolled from the
 *      outside of clipRect to the inside are NOT painted.
 */
INT WINAPI NtUserScrollWindowEx( HWND hwnd, INT dx, INT dy, const RECT *rect,
                                 const RECT *clip_rect, HRGN update_rgn,
                                 RECT *update_rect, UINT flags )
{
    BOOL update = update_rect || update_rgn || flags & (SW_INVALIDATE | SW_ERASE);
    BOOL own_rgn = TRUE, move_caret = FALSE;
    HRGN temp_rgn, winupd_rgn = 0;
    INT retval = NULLREGION;
    HWND caret_hwnd = NULL;
    POINT new_caret_pos;
    RECT rc, cliprc;
    int rdw_flags;
    HDC hdc;

    TRACE( "%p, %d,%d update_rgn=%p update_rect = %p %s %04x\n",
           hwnd, dx, dy, update_rgn, update_rect, wine_dbgstr_rect(rect), flags );
    TRACE( "clip_rect = %s\n", wine_dbgstr_rect(clip_rect) );
    if (flags & ~(SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE | SW_NODCCACHE))
        FIXME( "some flags (%04x) are unhandled\n", flags );

    rdw_flags = (flags & SW_ERASE) && (flags & SW_INVALIDATE) ?
        RDW_INVALIDATE | RDW_ERASE  : RDW_INVALIDATE;

    hwnd = get_full_window_handle( hwnd );

    if (!is_window_drawable( hwnd, TRUE ))
        SetRectEmpty( &rc );
    else
        get_client_rect( hwnd, &rc, get_thread_dpi() );

    if (clip_rect) intersect_rect( &cliprc, &rc, clip_rect );
    else cliprc = rc;

    if (rect) intersect_rect( &rc, &rc, rect );
    if (update_rgn) own_rgn = FALSE;
    else if (update) update_rgn = NtGdiCreateRectRgn( 0, 0, 0, 0 );

    new_caret_pos.x = new_caret_pos.y = 0;

    if (!IsRectEmpty( &cliprc ) && (dx || dy))
    {
        DWORD style = get_window_long( hwnd, GWL_STYLE );
        DWORD dcxflags = 0;

        caret_hwnd = fix_caret( hwnd, &rc, dx, dy, flags, &move_caret, &new_caret_pos );
        if (caret_hwnd) NtUserHideCaret( caret_hwnd );

        if (!(flags & SW_NODCCACHE)) dcxflags |= DCX_CACHE;
        if (style & WS_CLIPSIBLINGS) dcxflags |= DCX_CLIPSIBLINGS;
        if (get_class_long( hwnd, GCL_STYLE, FALSE ) & CS_PARENTDC) dcxflags |= DCX_PARENTCLIP;
        if (!(flags & SW_SCROLLCHILDREN) && (style & WS_CLIPCHILDREN))
            dcxflags |= DCX_CLIPCHILDREN;
        hdc = NtUserGetDCEx( hwnd, 0, dcxflags);
        if (hdc)
        {
            NtUserScrollDC( hdc, dx, dy, &rc, &cliprc, update_rgn, update_rect );
            NtUserReleaseDC( hwnd, hdc );
            if (!update) NtUserRedrawWindow( hwnd, NULL, update_rgn, rdw_flags );
        }

        /* If the windows has an update region, this must be scrolled as well.
         * Keep a copy in winupd_rgn to be added to hrngUpdate at the end. */
        temp_rgn = NtGdiCreateRectRgn( 0, 0, 0, 0 );
        retval = NtUserGetUpdateRgn( hwnd, temp_rgn, FALSE );
        if (retval != NULLREGION)
        {
            HRGN clip_rgn = NtGdiCreateRectRgn( cliprc.left, cliprc.top,
                                                cliprc.right, cliprc.bottom );
            if (!own_rgn)
            {
                winupd_rgn = NtGdiCreateRectRgn( 0, 0, 0, 0);
                NtGdiCombineRgn( winupd_rgn, temp_rgn, 0, RGN_COPY);
            }
            NtGdiOffsetRgn( temp_rgn, dx, dy );
            NtGdiCombineRgn( temp_rgn, temp_rgn, clip_rgn, RGN_AND );
            if (!own_rgn) NtGdiCombineRgn( winupd_rgn, winupd_rgn, temp_rgn, RGN_OR );
            NtUserRedrawWindow( hwnd, NULL, temp_rgn, rdw_flags );

           /*
            * Catch the case where the scrolling amount exceeds the size of the
            * original window. This generated a second update area that is the
            * location where the original scrolled content would end up.
            * This second region is not returned by the ScrollDC and sets
            * ScrollWindowEx apart from just a ScrollDC.
            *
            * This has been verified with testing on windows.
            */
            if (abs( dx ) > abs( rc.right - rc.left ) || abs( dy ) > abs( rc.bottom - rc.top ))
            {
                NtGdiSetRectRgn( temp_rgn, rc.left + dx, rc.top + dy, rc.right+dx, rc.bottom + dy );
                NtGdiCombineRgn( temp_rgn, temp_rgn, clip_rgn, RGN_AND );
                NtGdiCombineRgn( update_rgn, update_rgn, temp_rgn, RGN_OR );

                if (update_rect)
                {
                    RECT temp_rect;
                    NtGdiGetRgnBox( temp_rgn, &temp_rect );
                    union_rect( update_rect, update_rect, &temp_rect );
                }

                if (!own_rgn) NtGdiCombineRgn( winupd_rgn, winupd_rgn, temp_rgn, RGN_OR );
            }
            NtGdiDeleteObjectApp( clip_rgn );
        }
        NtGdiDeleteObjectApp( temp_rgn );
    }
    else
    {
        /* nothing was scrolled */
        if (!own_rgn) NtGdiSetRectRgn( update_rgn, 0, 0, 0, 0 );
        SetRectEmpty( update_rect );
    }

    if (flags & SW_SCROLLCHILDREN)
    {
        HWND *list = list_window_children( hwnd );
        if (list)
        {
            RECT r, dummy;
            int i;

            for (i = 0; list[i]; i++)
            {
                get_window_rect_rel( list[i], COORDS_PARENT, &r, get_thread_dpi() );
                if (!rect || intersect_rect( &dummy, &r, rect ))
                    NtUserSetWindowPos( list[i], 0, r.left + dx, r.top  + dy, 0, 0,
                                        SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE |
                                        SWP_NOREDRAW | SWP_DEFERERASE );
            }
            free( list );
        }
    }

    if (flags & (SW_INVALIDATE | SW_ERASE))
        NtUserRedrawWindow( hwnd, NULL, update_rgn, rdw_flags |
                            ((flags & SW_SCROLLCHILDREN) ? RDW_ALLCHILDREN : 0 ) );

    if (winupd_rgn)
    {
        NtGdiCombineRgn( update_rgn, update_rgn, winupd_rgn, RGN_OR );
        NtGdiDeleteObjectApp( winupd_rgn );
    }

    if (move_caret) NtUserSetCaretPos( new_caret_pos.x, new_caret_pos.y );
    if (caret_hwnd) NtUserShowCaret( caret_hwnd );
    if (own_rgn && update_rgn) NtGdiDeleteObjectApp( update_rgn );

    return retval;
}

/************************************************************************
 *           NtUserPrintWindow (win32u.@)
 */
BOOL WINAPI NtUserPrintWindow( HWND hwnd, HDC hdc, UINT flags )
{
    UINT prf_flags = PRF_CHILDREN | PRF_ERASEBKGND | PRF_OWNED | PRF_CLIENT;
    if (!(flags & PW_CLIENTONLY)) prf_flags |= PRF_NONCLIENT;
    send_message( hwnd, WM_PRINT, (WPARAM)hdc, prf_flags );
    return TRUE;
}
