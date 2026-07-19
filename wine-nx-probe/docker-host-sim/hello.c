/*
 * Minimal self-checking GUI smoke PE for the host-sim dev loop.
 *
 * Draws eight solid color bars and a text label, then idles in a normal
 * message loop until a 60s timer expires (run-host-sim.sh kills it long
 * before that). The bars exist so the screenshot self-check in
 * run-host-sim.sh has something objective to assert: a correctly rendered
 * frame contains at least 8 distinct colors, a blank/failed one doesn't.
 *
 * Deliberately exercises only the basics every GUI app needs -- RegisterClass,
 * CreateWindow, WM_PAINT/BeginPaint/FillRect/TextOut -- so a failure here is
 * a loader/substrate failure, not an app-complexity one.
 */

#include <windows.h>

static const COLORREF bar_colors[] =
{
    RGB(0xe6, 0x19, 0x4b), RGB(0x3c, 0xb4, 0x4b), RGB(0xff, 0xe1, 0x19),
    RGB(0x43, 0x63, 0xd8), RGB(0xf5, 0x82, 0x31), RGB(0x91, 0x1e, 0xb4),
    RGB(0x46, 0xf0, 0xf0), RGB(0xf0, 0x32, 0xe6),
};
#define NUM_BARS (sizeof(bar_colors) / sizeof(bar_colors[0]))

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint( hwnd, &ps );
        RECT client;
        unsigned int i;

        GetClientRect( hwnd, &client );
        for (i = 0; i < NUM_BARS; i++)
        {
            RECT bar = client;
            HBRUSH brush = CreateSolidBrush( bar_colors[i] );
            bar.left = client.right * i / NUM_BARS;
            bar.right = client.right * (i + 1) / NUM_BARS;
            FillRect( dc, &bar, brush );
            DeleteObject( brush );
        }
        SetBkMode( dc, TRANSPARENT );
        TextOutA( dc, 10, 10, "wine-nx host-sim smoke", 22 );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_TIMER:
        DestroyWindow( hwnd );
        return 0;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcA( hwnd, msg, wparam, lparam );
}

int WINAPI WinMain( HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show )
{
    WNDCLASSA wc = { 0 };
    HWND hwnd;
    MSG msg;

    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA( NULL, (const char *)IDC_ARROW );
    wc.lpszClassName = "WineNxHostSimSmoke";
    if (!RegisterClassA( &wc )) return 1;

    hwnd = CreateWindowA( "WineNxHostSimSmoke", "wine-nx host-sim smoke",
                          WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          40, 40, 640, 480, NULL, NULL, inst, NULL );
    if (!hwnd) return 2;

    SetTimer( hwnd, 1, 60000, NULL );
    UpdateWindow( hwnd );

    while (GetMessageA( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageA( &msg );
    }
    return 0;
}
