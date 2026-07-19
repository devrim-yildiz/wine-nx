/*
 * Minimal Win32 direct-blit test for the Wine-on-Switch software window path.
 *
 * Companion to samples/gui-smoke/gui_smoke.c, built to answer a narrower
 * question: is the ~5-6fps ceiling gui_smoke.c hits specific to its
 * InvalidateRect()+UpdateWindow()+BeginPaint()/EndPaint() repaint chain
 * (redraw_window + two get_update_region calls + get_paint_regions/
 * get_visible_region, all real IPC round trips through horizon.c, see
 * wine-nx-probe/perf-lab-log.md, "The ~14ms Per-Call IPC Floor"), or a ceiling on Win32 rendering
 * on this port generally?
 *
 * This app never calls InvalidateRect(), UpdateWindow(), BeginPaint(), or
 * EndPaint() in its steady-state loop. It calls GetDC() exactly ONCE,
 * before the loop starts, and reuses that one HDC for every frame's
 * StretchDIBits() call -- GetDC() is itself backed by NtUserGetDCEx(), the
 * same function BeginPaint()'s send_erase() calls internally, so calling
 * it once and caching the HDC (instead of once per frame) is what actually
 * keeps this app's steady-state loop free of that machinery, not merely
 * avoiding InvalidateRect/BeginPaint/EndPaint by name. As long as nothing
 * moves, resizes, or reparents this window during the run, the DC's visible
 * region stays valid for the whole test -- confirmed by design, not
 * something this app needs to re-check per frame.
 *
 * With no BeginPaint/EndPaint ever running, this app has no explicit
 * "flush this surface to the screen now" call at all -- painting relies
 * entirely on the message loop's own idle-triggered flush_window_surfaces()
 * path (dlls/win32u/message.c, dlls/win32u/dce.c), the same 50ms-debounced
 * mechanism fixed and A/B-tested earlier this project. If that debounce
 * turns out to dominate this app's frame time instead of the StretchDIBits
 * call itself, that's a real, different finding worth reporting plainly,
 * not something to work around here.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define WINE_NX_W 1280
#define WINE_NX_H 720
#define BOX_SIZE 80
#define BOX_MAX_SPEED 9

static int g_box_x = 200, g_box_y = 200, g_box_vx = 7, g_box_vy = 5;

/* Full-frame pixel buffers, plain C arrays -- no GDI objects (no brushes,
 * pens, bitmaps) anywhere in the per-frame path, so the only GDI call this
 * app's steady-state loop ever makes is the one StretchDIBits() below. */
static DWORD g_bg[WINE_NX_W * WINE_NX_H];
static DWORD g_pixels[WINE_NX_W * WINE_NX_H];

static void build_background(void)
{
    int x, y;

    for (y = 0; y < WINE_NX_H; y++)
    {
        BYTE g = (BYTE)(20 + (y * 40) / WINE_NX_H);
        for (x = 0; x < WINE_NX_W; x++)
        {
            BYTE r = (BYTE)(16 + (x * 30) / WINE_NX_W);
            /* 32bpp BI_RGB: B,G,R,pad in memory -- same convention as
             * gui_smoke.c's draw_pixel_ramp(). */
            g_bg[(size_t)y * WINE_NX_W + x] = ((DWORD)r << 16) | ((DWORD)g << 8) | 40u;
        }
    }
}

static void bounce_step(void)
{
    g_box_x += g_box_vx;
    g_box_y += g_box_vy;
    if (g_box_x < 0) { g_box_x = 0; g_box_vx = -g_box_vx; }
    if (g_box_x + BOX_SIZE > WINE_NX_W) { g_box_x = WINE_NX_W - BOX_SIZE; g_box_vx = -g_box_vx; }
    if (g_box_y < 0) { g_box_y = 0; g_box_vy = -g_box_vy; }
    if (g_box_y + BOX_SIZE > WINE_NX_H) { g_box_y = WINE_NX_H - BOX_SIZE; g_box_vy = -g_box_vy; }
}

static void draw_frame_pixels(void)
{
    int x, y;

    memcpy( g_pixels, g_bg, sizeof(g_pixels) );

    for (y = g_box_y; y < g_box_y + BOX_SIZE; y++)
    {
        DWORD *row = g_pixels + (size_t)y * WINE_NX_W + g_box_x;
        for (x = 0; x < BOX_SIZE; x++) row[x] = 0x0066D1FFu; /* amber */
    }
}

/* Same phase-timing pattern as gui_smoke.c's TIMING_LOG_PATH/phase_stat_t/
 * now_ms()/timing_*() -- deliberately kept structurally identical so the
 * two apps' logs are directly comparable, not just superficially similar. */
#define TIMING_LOG_PATH L"C:\\blit\\blit_timing.log"

typedef struct { unsigned long long sum_ms; unsigned long long count; } phase_stat_t;

static phase_stat_t g_stat_dispatch, g_stat_blit, g_stat_sleep, g_stat_total;
static HANDLE g_timing_file = INVALID_HANDLE_VALUE;
static ULONGLONG g_timing_epoch_ms;
static ULONGLONG g_qpc_freq;

static ULONGLONG now_ms(void)
{
    LARGE_INTEGER counter;

    if (!g_qpc_freq)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency( &freq );
        g_qpc_freq = (ULONGLONG)freq.QuadPart;
    }
    QueryPerformanceCounter( &counter );
    return g_qpc_freq ? (ULONGLONG)counter.QuadPart * 1000ULL / g_qpc_freq : 0;
}

static void timing_open(void)
{
    g_timing_file = CreateFileW( TIMING_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
}

static void timing_stat_add( phase_stat_t *s, ULONGLONG ms )
{
    s->sum_ms += ms;
    s->count++;
}

static unsigned long long timing_avg( const phase_stat_t *s )
{
    return s->count ? s->sum_ms / s->count : 0;
}

static void timing_maybe_flush(void)
{
    ULONGLONG now = now_ms();
    char line[192];
    int len;
    DWORD written;

    if (!g_timing_epoch_ms) g_timing_epoch_ms = now;
    if (now - g_timing_epoch_ms < 1000) return;

    len = snprintf( line, sizeof(line),
                    "iters=%llu dispatch_avg=%llums blit_avg=%llums sleep_avg=%llums total_avg=%llums\r\n",
                    g_stat_total.count, timing_avg( &g_stat_dispatch ),
                    timing_avg( &g_stat_blit ), timing_avg( &g_stat_sleep ), timing_avg( &g_stat_total ) );
    if (len > 0 && g_timing_file != INVALID_HANDLE_VALUE)
    {
        WriteFile( g_timing_file, line, (DWORD)len, &written, NULL );
        FlushFileBuffers( g_timing_file );
    }

    g_stat_dispatch.sum_ms = g_stat_dispatch.count = 0;
    g_stat_blit.sum_ms = g_stat_blit.count = 0;
    g_stat_sleep.sum_ms = g_stat_sleep.count = 0;
    g_stat_total.sum_ms = g_stat_total.count = 0;
    g_timing_epoch_ms = now;
}

static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_PAINT:
    {
        /* No InvalidateRect/UpdateWindow calls anywhere in this app's
         * steady-state loop -- the only way WM_PAINT ever fires at all is
         * the one-time initial paint ShowWindow() triggers. Ack it with an
         * empty BeginPaint/EndPaint pair so Windows doesn't keep
         * re-posting it; real frame content is pushed entirely through the
         * cached HDC in wWinMain()'s loop via direct StretchDIBits calls,
         * deliberately bypassing BeginPaint/EndPaint/region computation
         * for every frame after this one-time startup ack. */
        PAINTSTRUCT ps;
        BeginPaint( hwnd, &ps );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show )
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    HDC hdc;
    MSG msg;
    BITMAPINFO bmi;

    (void)prev;
    (void)cmd;

    build_background();
    timing_open();

    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"WineNxDirectBlit";
    wc.hCursor       = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW( &wc )) return 1;

    hwnd = CreateWindowExW( 0, L"WineNxDirectBlit", L"Wine NX Direct Blit",
                            WS_POPUP | WS_VISIBLE,
                            0, 0, WINE_NX_W, WINE_NX_H,
                            NULL, NULL, inst, NULL );
    if (!hwnd) return 2;

    ShowWindow( hwnd, show ? show : SW_SHOW );

    memset( &bmi, 0, sizeof(bmi) );
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = WINE_NX_W;
    bmi.bmiHeader.biHeight = -WINE_NX_H;   /* negative = top-down, matches draw_frame_pixels()'s row order */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    /* The one and only NtUserGetDCEx/update_visible_region call this app
     * ever makes -- see the file header comment for why caching this HDC,
     * not merely skipping InvalidateRect/BeginPaint/EndPaint by name, is
     * what actually keeps the steady-state loop free of that machinery. */
    hdc = GetDC( hwnd );

    for (;;)
    {
        ULONGLONG t_iter = now_ms();
        ULONGLONG t0, t1;

        t0 = now_ms();
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT)
            {
                ReleaseDC( hwnd, hdc );
                return (int)msg.wParam;
            }
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        t1 = now_ms();
        timing_stat_add( &g_stat_dispatch, t1 - t0 );

        bounce_step();
        draw_frame_pixels();

        t0 = now_ms();
        StretchDIBits( hdc, 0, 0, WINE_NX_W, WINE_NX_H, 0, 0, WINE_NX_W, WINE_NX_H,
                       g_pixels, &bmi, DIB_RGB_COLORS, SRCCOPY );
        t1 = now_ms();
        timing_stat_add( &g_stat_blit, t1 - t0 );

        t0 = now_ms();
        Sleep( 10 );
        t1 = now_ms();
        timing_stat_add( &g_stat_sleep, t1 - t0 );

        timing_stat_add( &g_stat_total, now_ms() - t_iter );
        timing_maybe_flush();
    }
}
