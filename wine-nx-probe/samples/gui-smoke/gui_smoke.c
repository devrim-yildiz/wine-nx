/*
 * Minimal Win32 GUI smoke test for the Wine-on-Switch software window path.
 *
 * Exercises the win32u syscall surface end-to-end: RegisterClass, CreateWindow,
 * ShowWindow, UpdateWindow and a real GDI WM_PAINT.  The runtime keeps the
 * final framebuffer visible after the PE terminates.
 */
#include <windows.h>

#define WINE_NX_W 1280
#define WINE_NX_H 720
#define BADGE_W 360
#define BADGE_H 132

static HFONT create_font( int height, int weight )
{
    return CreateFontW( height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Tahoma" );
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
    int i;

    for (i = 0; i < 240; i++)
    {
        BYTE r = (BYTE)(80 + i / 3);
        BYTE g = (BYTE)(220 - i / 4);
        BYTE b = (BYTE)(130 + i / 5);
        SetPixel( hdc, 520 + i, 615, RGB( r, g, b ) );
        SetPixel( hdc, 520 + i, 616, RGB( r, g, b ) );
        SetPixel( hdc, 520 + i, 617, RGB( r, g, b ) );
    }
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
    HFONT font = NULL;
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
    font = create_font( -24, FW_BOLD );

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
    select_font( memdc, font, &old_font );
    text_out( memdc, 34, 42, L"GDI SURFACE" );
    text_out( memdc, 34, 72, L"BITBLT PATH" );

    BitBlt( hdc, 838, 470, BADGE_W, BADGE_H, memdc, 0, 0, SRCCOPY );
    StretchBlt( hdc, 86, 482, BADGE_W / 2, BADGE_H / 2, memdc, 0, 0, BADGE_W, BADGE_H, SRCCOPY );

    if (old_font) SelectObject( memdc, old_font );
    if (old_pen) SelectObject( memdc, old_pen );
    if (old_brush) SelectObject( memdc, old_brush );
    if (old_bitmap) SelectObject( memdc, old_bitmap );
    if (font) DeleteObject( font );
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
    HFONT title_font = create_font( -58, FW_BOLD );
    HFONT body_font = create_font( -30, FW_SEMIBOLD );
    HGDIOBJ old_brush = NULL;
    HGDIOBJ old_pen = NULL;
    HGDIOBJ old_font = NULL;
    RECT rect;

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
    select_font( hdc, title_font, &old_font );
    rect.left = 0;
    rect.top = 284;
    rect.right = WINE_NX_W;
    rect.bottom = 370;
    DrawTextW( hdc, title, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE );

    if (old_font) SelectObject( hdc, old_font );
    old_font = NULL;
    SetTextColor( hdc, RGB( 196, 219, 255 ) );
    select_font( hdc, body_font, &old_font );
    text_out( hdc, 474, 382, L"REAL WIN32 GDI PAINT" );

    draw_badge( hdc );
    draw_pixel_ramp( hdc );

    if (old_font) SelectObject( hdc, old_font );
    if (old_pen) SelectObject( hdc, old_pen );
    if (old_brush) SelectObject( hdc, old_brush );
    if (body_font) DeleteObject( body_font );
    if (title_font) DeleteObject( title_font );
    if (pink) DeleteObject( pink );
    if (cyan) DeleteObject( cyan );
    if (accent) DeleteObject( accent );
    if (panel) DeleteObject( panel );
    if (bg) DeleteObject( bg );
}

static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    (void)wp;
    (void)lp;

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );

        draw_scene( hdc );
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

    (void)prev;
    (void)cmd;

    probe_directory_enumeration();

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

    return 0;
}
