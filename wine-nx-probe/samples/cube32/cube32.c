/*
 * Minimal 32-bit (i386) Win32 GUI smoke test: a wireframe cube rotating in
 * software-projected 3D, drawn entirely with GDI line/text calls.
 *
 * Unlike samples/gui-smoke/gui_smoke.c (compiled aarch64-windows, i.e. a
 * *native* ARM64 PE that never touches WOW64/box64 at all), this is built
 * i686-w64-mingw32 on purpose: it's the only sample that actually runs the
 * same 32-bit guest execution path (WOW64 context switching, box64 dynarec,
 * the win32u/GDI syscall surface as seen from real 32-bit code) that OpenTTD
 * itself uses, without OpenTTD's own several-MB codebase in the way. Follows
 * gui_smoke.c's own proven structure (WS_POPUP|WS_VISIBLE, PeekMessage loop,
 * InvalidateRect+UpdateWindow forcing synchronous WM_PAINT) since that's the
 * pattern already hardware-confirmed to pump messages and paint correctly.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>

#define WIN_W 480
#define WIN_H 480
#define CUBE_SIZE 120
#define CAM_DIST 420

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

static double g_angle_x, g_angle_y;
static ULONGLONG g_last_tick_ms;
static double g_fps;
static int g_frame_count;
static HFONT g_hud_font;

/* Deliberately GetTickCount(), not QueryPerformanceCounter(): this is the
 * exact API OpenTTD-style apps reach for first, and the one documented as
 * frozen platform-wide (README.md, "GetTickCount/GetTickCount64 Are Frozen")
 * before dlls/kernelbase/sync.c was wired up to NtGetTickCount()'s already-
 * working clock. If the rotation animates and the FPS counter advances,
 * that fix is confirmed live on the 32-bit guest path specifically. */
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
    double scale = CUBE_SIZE;
    double z = p->z * scale + CAM_DIST;
    double f = CAM_DIST / (z > 1.0 ? z : 1.0);

    *sx = (int)(WIN_W / 2 + p->x * scale * f);
    *sy = (int)(WIN_H / 2 + p->y * scale * f);
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

    len = _snwprintf( buf, 128, L"cube32 fps=%d.%01d frame=%d",
                      (int)g_fps, (int)(g_fps * 10) % 10, g_frame_count );
    TextOutW( hdc, 12, 12, buf, len );

    if (old_font) SelectObject( hdc, old_font );
}

static void draw_frame( HDC hdc )
{
    RECT full = { 0, 0, WIN_W, WIN_H };
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
        /* Nudge spin speed on tap, just to confirm touch reaches this
         * 32-bit guest path too (mirrors gui_smoke.c's touch handling). */
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

    (void)prev;
    (void)cmd;

    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"WineNxCube32";
    wc.hCursor       = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW( &wc )) return 1;

    hwnd = CreateWindowExW( 0, L"WineNxCube32", L"cube32 (32-bit)",
                            WS_POPUP | WS_VISIBLE,
                            0, 0, WIN_W, WIN_H,
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
