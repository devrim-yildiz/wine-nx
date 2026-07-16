/*
 * Minimal Win32 GUI smoke test for the Wine-on-Switch software window path.
 *
 * Exercises the win32u syscall surface end-to-end: RegisterClass, CreateWindow,
 * ShowWindow, UpdateWindow and a real GDI WM_PAINT.  The runtime keeps the
 * final framebuffer visible after the PE terminates.
 */
#include <windows.h>
#include <stdio.h>

#define WINE_NX_W 1280
#define WINE_NX_H 720
#define BADGE_W 360
#define BADGE_H 132

/* Bouncing-shapes demo: a ball and a few boxes moving inside the panel
 * drawn by draw_scene(), redrawn every loop iteration so the present path
 * (and its throttle, see wine-nx-probe/source/runtime.c) has continuous
 * real work to do instead of a single static frame. */
#define NUM_BOXES 3
#define BALL_RADIUS 28
#define SHAPE_MAX_SPEED 9
#define SCENE_LEFT 90
#define SCENE_TOP 90
#define SCENE_RIGHT (WINE_NX_W - 90)
#define SCENE_BOTTOM (WINE_NX_H - 90)

typedef struct { int x, y, vx, vy; } ball_t;
typedef struct { int x, y, w, h, vx, vy; COLORREF color; } box_t;

static ball_t g_ball;
static box_t g_boxes[NUM_BOXES];

/* Touch is bridged to the conventional WM_LBUTTONDOWN/MOUSEMOVE/UP stream
 * (see wine_nx_touch_poll() in runtime.c), so ordinary mouse messages are
 * all this needs to react to a finger on the screen. */
static int g_touch_active;
static int g_touch_x, g_touch_y;

/* Cached full-screen background: draw_scene() builds a lot of GDI state
 * (fonts, the badge's own memory DC, the pixel ramp) that doesn't change
 * frame to frame, so it's rendered once into an offscreen bitmap and
 * BitBlt'd back every frame instead of rebuilt from scratch each time. */
static HDC g_bg_dc;
static HBITMAP g_bg_bitmap;

/* draw_scene()/draw_badge() only ever run once in practice (guarded by
 * ensure_background()'s cache below), so these three were already only
 * created/destroyed once, not per-frame -- but caching them here removes
 * the footgun for good (a future change to ensure_background()'s cache
 * guard would otherwise silently turn this back into a per-frame cost)
 * and matches the same create-once-reuse treatment as the background
 * bitmap and draw_pixel_ramp's DIB blit. Deleted only at actual exit,
 * in wWinMain()'s WM_QUIT path. */
static HFONT g_title_font, g_body_font, g_badge_font;

static HFONT create_font( int height, int weight )
{
    return CreateFontW( height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Tahoma" );
}

static void ensure_fonts(void)
{
    if (g_title_font) return;

    g_title_font = create_font( -58, FW_BOLD );
    g_body_font = create_font( -30, FW_SEMIBOLD );
    g_badge_font = create_font( -24, FW_BOLD );
}

static void select_font( HDC hdc, HFONT font, HGDIOBJ *old_font )
{
    if (font) *old_font = SelectObject( hdc, font );
}

static void text_out( HDC hdc, int x, int y, const WCHAR *text )
{
    TextOutW( hdc, x, y, text, lstrlenW( text ) );
}

static int count_directory_matches( const WCHAR *pattern )
{
    WIN32_FIND_DATAW data;
    HANDLE find;
    int count = 0;

    find = FindFirstFileW( pattern, &data );
    if (find == INVALID_HANDLE_VALUE) return -1;

    do
    {
        if (data.cFileName[0] != L'.') count++;
    }
    while (count < 32 && FindNextFileW( find, &data ));

    FindClose( find );
    return count;
}

static void probe_directory_enumeration(void)
{
    count_directory_matches( L"C:\\windows\\fonts\\*.ttf" );
    count_directory_matches( L"Z:\\switch\\wine\\share\\wine\\fonts\\*.ttf" );
}

static void draw_pixel_ramp( HDC hdc )
{
    BITMAPINFO bmi;
    DWORD pixels[240 * 3];
    int i, row;

    for (i = 0; i < 240; i++)
    {
        BYTE r = (BYTE)(80 + i / 3);
        BYTE g = (BYTE)(220 - i / 4);
        BYTE b = (BYTE)(130 + i / 5);
        DWORD color = ((DWORD)r << 16) | ((DWORD)g << 8) | b; /* 32bpp BI_RGB: B,G,R,pad in memory */
        for (row = 0; row < 3; row++) pixels[row * 240 + i] = color;
    }

    memset( &bmi, 0, sizeof(bmi) );
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 240;
    bmi.bmiHeader.biHeight = 3;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits( hdc, 520, 615, 240, 3, 0, 0, 240, 3, pixels, &bmi, DIB_RGB_COLORS, SRCCOPY );
}

static void draw_badge( HDC hdc )
{
    HDC memdc = CreateCompatibleDC( hdc );
    HBITMAP bitmap = NULL;
    HGDIOBJ old_bitmap = NULL;
    HGDIOBJ old_brush = NULL;
    HGDIOBJ old_pen = NULL;
    HGDIOBJ old_font = NULL;
    HBRUSH bg = NULL;
    HBRUSH fill = NULL;
    HPEN pen = NULL;
    RECT rect;

    if (!memdc) return;

    bitmap = CreateCompatibleBitmap( hdc, BADGE_W, BADGE_H );
    if (!bitmap)
    {
        DeleteDC( memdc );
        return;
    }

    old_bitmap = SelectObject( memdc, bitmap );
    bg = CreateSolidBrush( RGB( 10, 15, 28 ) );
    fill = CreateSolidBrush( RGB( 48, 64, 98 ) );
    pen = CreatePen( PS_SOLID, 3, RGB( 94, 234, 212 ) );

    rect.left = 0;
    rect.top = 0;
    rect.right = BADGE_W;
    rect.bottom = BADGE_H;
    FillRect( memdc, &rect, bg );

    if (fill) old_brush = SelectObject( memdc, fill );
    if (pen) old_pen = SelectObject( memdc, pen );
    Rectangle( memdc, 14, 14, BADGE_W - 14, BADGE_H - 14 );
    Ellipse( memdc, 238, 34, 318, 104 );

    SetBkMode( memdc, TRANSPARENT );
    SetTextColor( memdc, RGB( 255, 255, 255 ) );
    select_font( memdc, g_badge_font, &old_font );
    text_out( memdc, 34, 42, L"GDI SURFACE" );
    text_out( memdc, 34, 72, L"BITBLT PATH" );

    BitBlt( hdc, 838, 470, BADGE_W, BADGE_H, memdc, 0, 0, SRCCOPY );
    StretchBlt( hdc, 86, 482, BADGE_W / 2, BADGE_H / 2, memdc, 0, 0, BADGE_W, BADGE_H, SRCCOPY );

    if (old_font) SelectObject( memdc, old_font );
    if (old_pen) SelectObject( memdc, old_pen );
    if (old_brush) SelectObject( memdc, old_brush );
    if (old_bitmap) SelectObject( memdc, old_bitmap );
    if (pen) DeleteObject( pen );
    if (fill) DeleteObject( fill );
    if (bg) DeleteObject( bg );
    DeleteObject( bitmap );
    DeleteDC( memdc );
}

static void draw_scene( HDC hdc )
{
    static const WCHAR title[] = L"WINE-NX IS BETWEEN US";
    HBRUSH bg = CreateSolidBrush( RGB( 18, 24, 46 ) );
    HBRUSH panel = CreateSolidBrush( RGB( 28, 38, 67 ) );
    HBRUSH accent = CreateSolidBrush( RGB( 94, 234, 212 ) );
    HPEN cyan = CreatePen( PS_SOLID, 5, RGB( 94, 234, 212 ) );
    HPEN pink = CreatePen( PS_SOLID, 3, RGB( 255, 92, 141 ) );
    HGDIOBJ old_brush = NULL;
    HGDIOBJ old_pen = NULL;
    HGDIOBJ old_font = NULL;
    RECT rect;

    ensure_fonts();

    if (bg)
    {
        old_brush = SelectObject( hdc, bg );
        PatBlt( hdc, 0, 0, WINE_NX_W, WINE_NX_H, PATCOPY );
    }

    if (panel) SelectObject( hdc, panel );
    if (cyan) old_pen = SelectObject( hdc, cyan );
    Rectangle( hdc, 72, 72, WINE_NX_W - 72, WINE_NX_H - 72 );

    if (accent) SelectObject( hdc, accent );
    Ellipse( hdc, 126, 122, 286, 282 );
    Ellipse( hdc, WINE_NX_W - 286, 122, WINE_NX_W - 126, 282 );

    if (pink) SelectObject( hdc, pink );
    MoveToEx( hdc, 180, 410, NULL );
    LineTo( hdc, WINE_NX_W - 180, 410 );
    MoveToEx( hdc, 260, 438, NULL );
    LineTo( hdc, WINE_NX_W - 260, 438 );

    SetBkMode( hdc, TRANSPARENT );
    SetTextColor( hdc, RGB( 255, 255, 255 ) );
    select_font( hdc, g_title_font, &old_font );
    rect.left = 0;
    rect.top = 284;
    rect.right = WINE_NX_W;
    rect.bottom = 370;
    DrawTextW( hdc, title, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE );

    if (old_font) SelectObject( hdc, old_font );
    old_font = NULL;
    SetTextColor( hdc, RGB( 196, 219, 255 ) );
    select_font( hdc, g_body_font, &old_font );
    text_out( hdc, 474, 382, L"REAL WIN32 GDI PAINT" );

    draw_badge( hdc );
    draw_pixel_ramp( hdc );

    if (old_font) SelectObject( hdc, old_font );
    if (old_pen) SelectObject( hdc, old_pen );
    if (old_brush) SelectObject( hdc, old_brush );
    if (pink) DeleteObject( pink );
    if (cyan) DeleteObject( cyan );
    if (accent) DeleteObject( accent );
    if (panel) DeleteObject( panel );
    if (bg) DeleteObject( bg );
}

static void init_scene(void)
{
    static const COLORREF box_colors[NUM_BOXES] =
    {
        RGB( 94, 234, 212 ), RGB( 255, 92, 141 ), RGB( 130, 170, 255 )
    };
    int i;

    g_ball.x = 200;
    g_ball.y = 200;
    g_ball.vx = 6;
    g_ball.vy = 4;

    for (i = 0; i < NUM_BOXES; i++)
    {
        box_t *b = &g_boxes[i];

        b->w = 70 + i * 20;
        b->h = 50 + i * 15;
        b->x = SCENE_LEFT + 150 + i * 220;
        b->y = SCENE_TOP + 60 + i * 90;
        b->vx = 5 - i * 2;
        if (!b->vx) b->vx = 3;
        b->vy = 4 + i;
        b->color = box_colors[i];
    }
}

static int clampi( int v, int lo, int hi )
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Nudge velocity one unit per tick toward the touch point, clamped to a
 * fixed top speed -- deliberately simple (no mass/easing) so the reaction
 * is obviously alive without overshooting into wild orbits. */
static void attract_towards_touch( int cx, int cy, int *vx, int *vy )
{
    int dx = g_touch_x - cx;
    int dy = g_touch_y - cy;

    *vx += (dx > 3) - (dx < -3);
    *vy += (dy > 3) - (dy < -3);
    *vx = clampi( *vx, -SHAPE_MAX_SPEED, SHAPE_MAX_SPEED );
    *vy = clampi( *vy, -SHAPE_MAX_SPEED, SHAPE_MAX_SPEED );
}

static void bounce_step( int *x, int *y, int *vx, int *vy, int w, int h )
{
    *x += *vx;
    *y += *vy;
    if (*x < SCENE_LEFT) { *x = SCENE_LEFT; *vx = -*vx; }
    if (*x + w > SCENE_RIGHT) { *x = SCENE_RIGHT - w; *vx = -*vx; }
    if (*y < SCENE_TOP) { *y = SCENE_TOP; *vy = -*vy; }
    if (*y + h > SCENE_BOTTOM) { *y = SCENE_BOTTOM - h; *vy = -*vy; }
}

static void update_scene(void)
{
    int i;

    if (g_touch_active)
        attract_towards_touch( g_ball.x + BALL_RADIUS, g_ball.y + BALL_RADIUS, &g_ball.vx, &g_ball.vy );
    bounce_step( &g_ball.x, &g_ball.y, &g_ball.vx, &g_ball.vy, BALL_RADIUS * 2, BALL_RADIUS * 2 );

    for (i = 0; i < NUM_BOXES; i++)
    {
        box_t *b = &g_boxes[i];

        if (g_touch_active) attract_towards_touch( b->x + b->w / 2, b->y + b->h / 2, &b->vx, &b->vy );
        bounce_step( &b->x, &b->y, &b->vx, &b->vy, b->w, b->h );
    }
}

static void draw_moving_shapes( HDC hdc )
{
    HBRUSH ball_brush = CreateSolidBrush( RGB( 255, 209, 102 ) );
    HPEN ball_pen = CreatePen( PS_SOLID, 3, RGB( 255, 92, 141 ) );
    HGDIOBJ old_brush = NULL, old_pen = NULL;
    int i;

    if (ball_brush) old_brush = SelectObject( hdc, ball_brush );
    if (ball_pen) old_pen = SelectObject( hdc, ball_pen );
    Ellipse( hdc, g_ball.x, g_ball.y, g_ball.x + BALL_RADIUS * 2, g_ball.y + BALL_RADIUS * 2 );
    if (old_pen) SelectObject( hdc, old_pen );
    if (old_brush) SelectObject( hdc, old_brush );
    if (ball_pen) DeleteObject( ball_pen );
    if (ball_brush) DeleteObject( ball_brush );

    for (i = 0; i < NUM_BOXES; i++)
    {
        box_t *b = &g_boxes[i];
        HBRUSH brush = CreateSolidBrush( b->color );
        HGDIOBJ old = brush ? SelectObject( hdc, brush ) : NULL;

        Rectangle( hdc, b->x, b->y, b->x + b->w, b->y + b->h );
        if (old) SelectObject( hdc, old );
        if (brush) DeleteObject( brush );
    }
}

static void ensure_background( HDC hdc )
{
    if (g_bg_dc) return;

    g_bg_dc = CreateCompatibleDC( hdc );
    if (!g_bg_dc) return;

    g_bg_bitmap = CreateCompatibleBitmap( hdc, WINE_NX_W, WINE_NX_H );
    if (!g_bg_bitmap)
    {
        DeleteDC( g_bg_dc );
        g_bg_dc = NULL;
        return;
    }

    SelectObject( g_bg_dc, g_bg_bitmap );
    draw_scene( g_bg_dc );
}

/* Loop-phase timing: framebufferEnd() itself measured 3-5ms on hardware and
 * attempted==executed at the throttle (see wine_nx_hud_present_ms_* in
 * runtime.c and the README's "Presentation Is Still Too Slow" update) --
 * that rules out presentation/conversion cost as the ~2fps bottleneck, so
 * the remaining suspects are all on this side of the boundary: message
 * dispatch, the scene-physics update, the GDI paint itself (UpdateWindow ->
 * WM_PAINT -> draw_frame's background BitBlt + shape draws), and whether
 * Sleep(10) actually sleeps ~10ms or something far longer. Each phase is
 * averaged over 1-second windows and written to a log file instead of drawn
 * on screen so the extra GDI text some other on-screen readout would need
 * can't itself skew the paint-cost number being measured.
 *
 * Deliberately NOT GetTickCount64(): the first version of this used it and
 * produced an empty log every time. Root cause (see README, "GetTickCount/
 * GetTickCount64 Are Frozen") -- it reads a shared-memory field that's only
 * ever kept ticking by the wineserver's poll loop, which this Switch port
 * never runs, so the value is frozen for the life of the process and every
 * delta reads as 0ms. QueryPerformanceCounter() routes through a different,
 * working clock_gettime()-backed path instead. */
#define TIMING_LOG_PATH L"C:\\gui\\gui_timing.log"

typedef struct { unsigned long long sum_ms; unsigned long long count; } phase_stat_t;

static phase_stat_t g_stat_dispatch, g_stat_update, g_stat_paint, g_stat_sleep, g_stat_total;
/* blit/shapes are sub-phases inside paint_avg (see draw_frame() below),
 * split out to find out how much of paint_avg is the full 1280x720
 * cached-background BitBlt vs. the moving-shape GDI churn (5
 * CreateSolidBrush/CreatePen + select + delete cycles per frame), rather
 * than guessing -- both were previously ruled out as "fast, local,
 * non-IPC" from source alone, which is true per-call but says nothing
 * about their cost at this specific size/frequency. */
static phase_stat_t g_stat_blit, g_stat_shapes;
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
    char line[256];
    int len;
    DWORD written;

    if (!g_timing_epoch_ms) g_timing_epoch_ms = now;
    if (now - g_timing_epoch_ms < 1000) return;

    len = snprintf( line, sizeof(line),
                    "iters=%llu dispatch_avg=%llums update_avg=%llums paint_avg=%llums "
                    "blit_avg=%llums shapes_avg=%llums sleep_avg=%llums total_avg=%llums\r\n",
                    g_stat_total.count, timing_avg( &g_stat_dispatch ), timing_avg( &g_stat_update ),
                    timing_avg( &g_stat_paint ), timing_avg( &g_stat_blit ), timing_avg( &g_stat_shapes ),
                    timing_avg( &g_stat_sleep ), timing_avg( &g_stat_total ) );
    if (len > 0 && g_timing_file != INVALID_HANDLE_VALUE)
    {
        WriteFile( g_timing_file, line, (DWORD)len, &written, NULL );
        FlushFileBuffers( g_timing_file );
    }

    g_stat_dispatch.sum_ms = g_stat_dispatch.count = 0;
    g_stat_update.sum_ms = g_stat_update.count = 0;
    g_stat_paint.sum_ms = g_stat_paint.count = 0;
    g_stat_blit.sum_ms = g_stat_blit.count = 0;
    g_stat_shapes.sum_ms = g_stat_shapes.count = 0;
    g_stat_sleep.sum_ms = g_stat_sleep.count = 0;
    g_stat_total.sum_ms = g_stat_total.count = 0;
    g_timing_epoch_ms = now;
}

static void draw_frame( HDC hdc )
{
    ULONGLONG t0, t1;

    ensure_background( hdc );

    t0 = now_ms();
    if (g_bg_dc) BitBlt( hdc, 0, 0, WINE_NX_W, WINE_NX_H, g_bg_dc, 0, 0, SRCCOPY );
    else draw_scene( hdc );   /* cache failed to allocate: draw straight through */
    t1 = now_ms();
    timing_stat_add( &g_stat_blit, t1 - t0 );

    t0 = now_ms();
    draw_moving_shapes( hdc );
    t1 = now_ms();
    timing_stat_add( &g_stat_shapes, t1 - t0 );
}

static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    (void)wp;

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );

        draw_frame( hdc );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_touch_active = 1;
        g_touch_x = (short)LOWORD( lp );
        g_touch_y = (short)HIWORD( lp );
        return 0;
    case WM_MOUSEMOVE:
        if (g_touch_active)
        {
            g_touch_x = (short)LOWORD( lp );
            g_touch_y = (short)HIWORD( lp );
        }
        return 0;
    case WM_LBUTTONUP:
        g_touch_active = 0;
        return 0;
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
    MSG msg;

    (void)prev;
    (void)cmd;

    probe_directory_enumeration();
    init_scene();
    timing_open();

    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"WineNxSmoke";
    wc.hCursor       = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW( &wc )) return 1;

    /* Full Switch framebuffer size. */
    hwnd = CreateWindowExW( 0, L"WineNxSmoke", L"Wine NX Smoke",
                            WS_POPUP | WS_VISIBLE,
                            0, 0, WINE_NX_W, WINE_NX_H,
                            NULL, NULL, inst, NULL );
    if (!hwnd) return 2;

    ShowWindow( hwnd, show ? show : SW_SHOW );
    UpdateWindow( hwnd );   /* forces a synchronous WM_PAINT through the GDI path */

    /* Real-time loop: advance the animation, repaint, pump input, repeat.
     * Runs until Minus is pressed (native-side exit(0), see runtime.c) or
     * WM_QUIT is posted -- there's no in-app close button by design. */
    for (;;)
    {
        ULONGLONG t_iter = now_ms();
        ULONGLONG t0, t1;

        t0 = now_ms();
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT)
            {
                if (g_title_font) DeleteObject( g_title_font );
                if (g_body_font) DeleteObject( g_body_font );
                if (g_badge_font) DeleteObject( g_badge_font );
                return (int)msg.wParam;
            }
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        t1 = now_ms();
        timing_stat_add( &g_stat_dispatch, t1 - t0 );

        t0 = now_ms();
        update_scene();
        t1 = now_ms();
        timing_stat_add( &g_stat_update, t1 - t0 );

        t0 = now_ms();
        InvalidateRect( hwnd, NULL, FALSE );
        UpdateWindow( hwnd );   /* synchronous WM_PAINT -> draw_frame() happens in here */
        t1 = now_ms();
        timing_stat_add( &g_stat_paint, t1 - t0 );

        /* Was Sleep(10) -- an unconditional pacing delay added on top of
         * whatever painting already took, regardless of load. That's not a
         * frame-rate cap tied to vsync or any real constraint, just an
         * arbitrary constant from early in this demo's development, and at
         * ~100ms/frame it was costing ~10% of every frame for no reason.
         * Cut to Sleep(1) -- still yields the thread every iteration
         * instead of busy-spinning, but doesn't manufacture an artificial
         * floor. This is a test-harness tuning change, not a Wine-NX port
         * fix -- it doesn't reflect any change in the port's own paint
         * pipeline cost, just how much this specific demo was leaving on
         * the table by choice. */
        t0 = now_ms();
        Sleep( 1 );
        t1 = now_ms();
        timing_stat_add( &g_stat_sleep, t1 - t0 );

        timing_stat_add( &g_stat_total, now_ms() - t_iter );
        timing_maybe_flush();
    }
}
