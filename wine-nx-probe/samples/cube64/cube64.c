/*
 * 64-bit (x86_64) Win32 GUI smoke test: a wireframe cube rotating in
 * software-projected 3D, drawn entirely with GDI line/text calls.
 *
 * The rotation/projection/drawing logic below is a deliberate, near-verbatim
 * copy of samples/cube32/cube32.c's already hardware-proven-on-the-32-bit-
 * WOW64-path code -- this is the first time ANY real x86_64 guest code has
 * been attempted on this project's own wine_nx_x64_handoff() (dlls/ntdll/
 * loader.c), so keeping the actual GDI/message-loop pattern identical to a
 * KNOWN-GOOD one means a failure here points at the new 64-bit execution
 * path itself (box64 dynarec, EmuX64Syscall, the sysx64 guest DLL set),
 * not at some new rendering idiom nobody has exercised before.
 *
 * The one deliberate difference: fullscreen instead of a fixed 480x480
 * corner window, sized from GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN) --
 * real values (dlls/win32u/sysparams.c's own SM_CXSCREEN case is wired to
 * this platform's actual WINE_NX_FB_W/H, 1280x720 -- see source/runtime.c),
 * not hardcoded, so this stays correct if that resolution ever changes.
 * Falls back to 1280x720 only if GetSystemMetrics ever returns 0 (e.g. some
 * intermediate boot state where win32u isn't fully up yet).
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>

#define FALLBACK_W 1280
#define FALLBACK_H 720
#define CUBE_SIZE_FRAC 0.25   /* cube half-size as a fraction of the shorter screen dimension */
#define CAM_DIST_FRAC  0.875 /* camera distance as a fraction of the shorter screen dimension */

typedef struct { double x, y, z; } vec3_t;

static const vec3_t g_cube_verts[8] =
{
    { -1, -1, -1 }, {  1, -1, -1 }, {  1,  1, -1 }, { -1,  1, -1 },
    { -1, -1,  1 }, {  1, -1,  1 }, {  1,  1,  1 }, { -1,  1,  1 },
};

/* 12 edges as index pairs into g_cube_verts. */
static const int g_cube_edges[12][2] =
{
    {0,1},{1,2},{2,3},{3,0},   /* back face */
    {4,5},{5,6},{6,7},{7,4},   /* front face */
    {0,4},{1,5},{2,6},{3,7},   /* connecting edges */
};

static int g_win_w = FALLBACK_W, g_win_h = FALLBACK_H;
static double g_cube_size, g_cam_dist;
static double g_angle_x, g_angle_y;
static ULONGLONG g_last_tick_ms;
static double g_fps;
static int g_frame_count;
static HFONT g_hud_font;

/* Deliberately GetTickCount(), matching cube32.c's own choice -- see that
 * file's header comment for why (the exact API OpenTTD-style apps reach
 * for first, and the one this project hardware-confirmed working on the
 * 32-bit guest path). Confirming it also works on the 64-bit path here is
 * as real a smoke test as the rotation itself. */
static double now_ms(void)
{
    return (double)GetTickCount();
}

static void rotate_point( const vec3_t *in, double ax, double ay, vec3_t *out )
{
    double y1, z1, x2, z2;
    double cx = cos( ax ), sx = sin( ax );
    double cy = cos( ay ), sy = sin( ay );

    /* rotate around X */
    y1 = in->y * cx - in->z * sx;
    z1 = in->y * sx + in->z * cx;

    /* rotate around Y */
    x2 = in->x * cy + z1 * sy;
    z2 = -in->x * sy + z1 * cy;

    out->x = x2;
    out->y = y1;
    out->z = z2;
}

static void project_point( const vec3_t *p, int *sx, int *sy )
{
    double z = p->z * g_cube_size + g_cam_dist;
    double f = g_cam_dist / (z > 1.0 ? z : 1.0);

    *sx = (int)(g_win_w / 2 + p->x * g_cube_size * f);
    *sy = (int)(g_win_h / 2 + p->y * g_cube_size * f);
}

static void draw_cube( HDC hdc )
{
    vec3_t rotated[8];
    int sx[8], sy[8];
    int i;
    HPEN pen = CreatePen( PS_SOLID, 3, RGB( 94, 234, 212 ) );
    HGDIOBJ old_pen = pen ? SelectObject( hdc, pen ) : NULL;

    for (i = 0; i < 8; i++)
    {
        rotate_point( &g_cube_verts[i], g_angle_x, g_angle_y, &rotated[i] );
        project_point( &rotated[i], &sx[i], &sy[i] );
    }

    for (i = 0; i < 12; i++)
    {
        int a = g_cube_edges[i][0], b = g_cube_edges[i][1];
        MoveToEx( hdc, sx[a], sy[a], NULL );
        LineTo( hdc, sx[b], sy[b] );
    }

    if (old_pen) SelectObject( hdc, old_pen );
    if (pen) DeleteObject( pen );

    for (i = 0; i < 8; i++)
    {
        HBRUSH dot = CreateSolidBrush( RGB( 255, 92, 141 ) );
        HGDIOBJ old = dot ? SelectObject( hdc, dot ) : NULL;
        Ellipse( hdc, sx[i] - 4, sy[i] - 4, sx[i] + 4, sy[i] + 4 );
        if (old) SelectObject( hdc, old );
        if (dot) DeleteObject( dot );
    }
}

static void draw_hud( HDC hdc )
{
    WCHAR buf[128];
    HGDIOBJ old_font = NULL;
    int len;

    if (!g_hud_font)
        g_hud_font = CreateFontW( -20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Tahoma" );

    SetBkMode( hdc, TRANSPARENT );
    SetTextColor( hdc, RGB( 255, 255, 255 ) );
    if (g_hud_font) old_font = SelectObject( hdc, g_hud_font );

    len = _snwprintf( buf, 128, L"cube64 fps=%d.%01d frame=%d %dx%d",
                      (int)g_fps, (int)(g_fps * 10) % 10, g_frame_count, g_win_w, g_win_h );
    TextOutW( hdc, 12, 12, buf, len );

    if (old_font) SelectObject( hdc, old_font );
}

static void draw_frame( HDC hdc )
{
    RECT full = { 0, 0, g_win_w, g_win_h };
    HBRUSH bg = CreateSolidBrush( RGB( 18, 24, 46 ) );

    if (bg)
    {
        FillRect( hdc, &full, bg );
        DeleteObject( bg );
    }

    draw_cube( hdc );
    draw_hud( hdc );
}

static void update_animation(void)
{
    double t = now_ms();

    g_angle_x += 0.03;
    g_angle_y += 0.021;
    g_frame_count++;

    if (g_last_tick_ms == 0) g_last_tick_ms = (ULONGLONG)t;
    if (t - (double)g_last_tick_ms >= 500.0)
    {
        /* smoothed instantaneous fps over the last half-second */
        static int frames_since;
        double dt = t - (double)g_last_tick_ms;
        g_fps = (g_frame_count - frames_since) * 1000.0 / dt;
        frames_since = g_frame_count;
        g_last_tick_ms = (ULONGLONG)t;
    }
}

static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
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
        /* Nudge spin speed on tap, mirrors cube32.c's own touch handling. */
        g_angle_x += 0.4;
        g_angle_y += 0.4;
        return 0;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show )
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG msg;
    double shorter_dim;

    (void)prev;
    (void)cmd;

    g_win_w = GetSystemMetrics( SM_CXSCREEN );
    g_win_h = GetSystemMetrics( SM_CYSCREEN );
    if (g_win_w <= 0) g_win_w = FALLBACK_W;
    if (g_win_h <= 0) g_win_h = FALLBACK_H;

    shorter_dim = (g_win_w < g_win_h) ? g_win_w : g_win_h;
    g_cube_size = shorter_dim * CUBE_SIZE_FRAC;
    g_cam_dist = shorter_dim * CAM_DIST_FRAC;

    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"WineNxCube64";
    wc.hCursor       = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW( &wc )) return 1;

    hwnd = CreateWindowExW( 0, L"WineNxCube64", L"cube64 (64-bit)",
                            WS_POPUP | WS_VISIBLE,
                            0, 0, g_win_w, g_win_h,
                            NULL, NULL, inst, NULL );
    if (!hwnd) return 2;

    ShowWindow( hwnd, show ? show : SW_SHOW );
    UpdateWindow( hwnd );

    for (;;)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT)
            {
                if (g_hud_font) DeleteObject( g_hud_font );
                return (int)msg.wParam;
            }
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }

        update_animation();
        InvalidateRect( hwnd, NULL, FALSE );
        UpdateWindow( hwnd );

        Sleep( 1 );
    }
}
