#include "nxgui.h"
#include "nxgui_font.h"

/* All four primitives below deliberately clip against the surface
 * bounds themselves (not just trust the caller) -- every call site in
 * the launcher computes x/y/w/h from live layout math (list scroll
 * offsets, grid positions), and a single off-by-one there should read
 * as "a clipped pixel," never as a write outside the framebuffer. */

void nxgui_clear( nxgui_surface_t *surf, uint32_t color )
{
    nxgui_rect_t full = { 0, 0, surf->width, surf->height };
    nxgui_fill_rect( surf, full, color );
}

void nxgui_fill_rect( nxgui_surface_t *surf, nxgui_rect_t r, uint32_t color )
{
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w; if (x1 > surf->width) x1 = surf->width;
    int y1 = r.y + r.h; if (y1 > surf->height) y1 = surf->height;
    int x, y;

    for (y = y0; y < y1; y++)
    {
        uint32_t *row = surf->pixels + (size_t)y * surf->stride_px;
        for (x = x0; x < x1; x++) row[x] = color;
    }
}

void nxgui_stroke_rect( nxgui_surface_t *surf, nxgui_rect_t r, int thickness, uint32_t color )
{
    nxgui_rect_t top    = { r.x, r.y, r.w, thickness };
    nxgui_rect_t bottom = { r.x, r.y + r.h - thickness, r.w, thickness };
    nxgui_rect_t left   = { r.x, r.y, thickness, r.h };
    nxgui_rect_t right  = { r.x + r.w - thickness, r.y, thickness, r.h };

    nxgui_fill_rect( surf, top, color );
    nxgui_fill_rect( surf, bottom, color );
    nxgui_fill_rect( surf, left, color );
    nxgui_fill_rect( surf, right, color );
}

/* local_x/local_y are 0-based within a w x h box (NOT surface
 * coordinates) -- true for both nxgui_fill_rounded_rect's outer box and
 * nxgui_stroke_rounded_rect's inset inner box, so both can share this
 * one test. Corner circle centers sit at column/row `rad` (top/left)
 * or `w-rad-1`/`h-rad-1` (bottom/right) -- symmetric by construction. */
static int nxgui_point_in_rounded_box( int local_x, int local_y, int w, int h, int rad )
{
    int in_top, in_bottom, left, right;

    if (local_x < 0 || local_x >= w || local_y < 0 || local_y >= h) return 0;
    if (rad <= 0) return 1;

    in_top = local_y < rad;
    in_bottom = local_y >= h - rad;
    if (!in_top && !in_bottom) return 1;

    left = local_x < rad;
    right = local_x >= w - rad;
    if (!left && !right) return 1;

    {
        int cy = in_top ? rad : h - rad - 1;
        int cx = left ? rad : w - rad - 1;
        int dx = local_x - cx;
        int dy = local_y - cy;
        return (dx * dx + dy * dy) <= rad * rad;
    }
}

static int nxgui_clamp_radius( int radius, int w, int h )
{
    int max_rad = (w < h ? w : h) / 2;
    if (radius > max_rad) radius = max_rad;
    if (radius < 0) radius = 0;
    return radius;
}

void nxgui_fill_rounded_rect( nxgui_surface_t *surf, nxgui_rect_t r, int radius, uint32_t color )
{
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w; if (x1 > surf->width) x1 = surf->width;
    int y1 = r.y + r.h; if (y1 > surf->height) y1 = surf->height;
    int rad = nxgui_clamp_radius( radius, r.w, r.h );
    int x, y;

    for (y = y0; y < y1; y++)
    {
        uint32_t *row = surf->pixels + (size_t)y * surf->stride_px;
        int local_y = y - r.y;
        for (x = x0; x < x1; x++)
        {
            if (nxgui_point_in_rounded_box( x - r.x, local_y, r.w, r.h, rad )) row[x] = color;
        }
    }
}

void nxgui_stroke_rounded_rect( nxgui_surface_t *surf, nxgui_rect_t r, int radius, int thickness, uint32_t color )
{
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w; if (x1 > surf->width) x1 = surf->width;
    int y1 = r.y + r.h; if (y1 > surf->height) y1 = surf->height;
    int rad = nxgui_clamp_radius( radius, r.w, r.h );
    int inner_rad;
    int x, y;

    if (thickness < 1) thickness = 1;
    inner_rad = rad - thickness;
    if (inner_rad < 0) inner_rad = 0;

    for (y = y0; y < y1; y++)
    {
        uint32_t *row = surf->pixels + (size_t)y * surf->stride_px;
        int local_y = y - r.y;
        for (x = x0; x < x1; x++)
        {
            int local_x = x - r.x;
            int outer = nxgui_point_in_rounded_box( local_x, local_y, r.w, r.h, rad );
            int inner = nxgui_point_in_rounded_box( local_x - thickness, local_y - thickness,
                                                     r.w - 2 * thickness, r.h - 2 * thickness, inner_rad );
            if (outer && !inner) row[x] = color;
        }
    }
}

void nxgui_draw_toggle( nxgui_surface_t *surf, int x, int y, int checked, uint32_t on_color, uint32_t off_color )
{
    nxgui_rect_t box = { x, y, NXGUI_TOGGLE_SIZE, NXGUI_TOGGLE_SIZE };
    nxgui_stroke_rounded_rect( surf, box, 6, 2, checked ? on_color : off_color );
    if (checked)
    {
        nxgui_rect_t inner = { x + 6, y + 6, NXGUI_TOGGLE_SIZE - 12, NXGUI_TOGGLE_SIZE - 12 };
        nxgui_fill_rounded_rect( surf, inner, 3, on_color );
    }
}

void nxgui_draw_slider( nxgui_surface_t *surf, int x, int y, int w, float value01,
                        uint32_t track_color, uint32_t fill_color, uint32_t handle_color )
{
    nxgui_rect_t track, fill, handle;
    int handle_x;

    if (value01 < 0.0f) value01 = 0.0f;
    if (value01 > 1.0f) value01 = 1.0f;

    track.x = x; track.y = y - NXGUI_SLIDER_TRACK_H / 2; track.w = w; track.h = NXGUI_SLIDER_TRACK_H;
    nxgui_fill_rounded_rect( surf, track, NXGUI_SLIDER_TRACK_H / 2, track_color );

    fill = track;
    fill.w = (int)((float)w * value01);
    if (fill.w > 0) nxgui_fill_rounded_rect( surf, fill, NXGUI_SLIDER_TRACK_H / 2, fill_color );

    handle_x = x + (int)((float)w * value01) - NXGUI_SLIDER_HANDLE / 2;
    handle.x = handle_x; handle.y = y - NXGUI_SLIDER_HANDLE / 2;
    handle.w = NXGUI_SLIDER_HANDLE; handle.h = NXGUI_SLIDER_HANDLE;
    nxgui_fill_rounded_rect( surf, handle, NXGUI_SLIDER_HANDLE / 2, handle_color );
}

/* Hand-tuned per console's real color/shape language -- see nxgui.h's
 * own comment on what "accurate" means in a pipeline with no reference
 * screenshots to diff against.
 *
 * Switch: near-black UI chrome, the real console's own blue/red Joy-Con
 * accent swapped for a single bright cyan (matches the system software's
 * actual "eShop blue" family more than red/blue would).
 * PS5: cool blue-black with PlayStation's own signature ice-blue accent.
 * Xbox: pure black chrome, signature Xbox green, zero warmth anywhere.
 * PS3: XMB's iconic near-black with soft cyan-white text (the XMB never
 * really used panel fills, hence bg==panel==panel_raised here -- see
 * render_theme_ps3's own header comment on the transparent-panel look).
 * Wii: light grey/white channel-plaza look, pastel blue glow -- the one
 * theme that's fundamentally light instead of dark, on purpose. */
const nxgui_palette_t nxgui_palette_switch =
{
    NXGUI_RGB(0x0f,0x0f,0x12), NXGUI_RGB(0x1a,0x1a,0x1e), NXGUI_RGB(0x24,0x24,0x29),
    NXGUI_RGB(0x33,0x33,0x38), NXGUI_RGB(0xff,0xff,0xff), NXGUI_RGB(0x9a,0x9a,0xa0),
    NXGUI_RGB(0x00,0xd6,0xe8), NXGUI_RGB(0x00,0x6e,0x78),
    NXGUI_RGB(0x4c,0xd9,0x7a), NXGUI_RGB(0xe4,0x5b,0x5b), NXGUI_RGB(0xe0,0xa8,0x30)
};

const nxgui_palette_t nxgui_palette_ps5 =
{
    NXGUI_RGB(0x08,0x0c,0x18), NXGUI_RGB(0x10,0x16,0x28), NXGUI_RGB(0x18,0x20,0x38),
    NXGUI_RGB(0x2a,0x35,0x55), NXGUI_RGB(0xf0,0xf3,0xff), NXGUI_RGB(0x8f,0x9b,0xc0),
    NXGUI_RGB(0x00,0x9d,0xff), NXGUI_RGB(0x00,0x4a,0x80),
    NXGUI_RGB(0x4c,0xd9,0x7a), NXGUI_RGB(0xe4,0x5b,0x5b), NXGUI_RGB(0xe0,0xa8,0x30)
};

const nxgui_palette_t nxgui_palette_xbox =
{
    NXGUI_RGB(0x00,0x00,0x00), NXGUI_RGB(0x0e,0x0e,0x0e), NXGUI_RGB(0x18,0x18,0x18),
    NXGUI_RGB(0x2a,0x2a,0x2a), NXGUI_RGB(0xff,0xff,0xff), NXGUI_RGB(0x9a,0x9a,0x9a),
    NXGUI_RGB(0x10,0x7c,0x10), NXGUI_RGB(0x0b,0x4a,0x0b),
    NXGUI_RGB(0x4c,0xd9,0x7a), NXGUI_RGB(0xe4,0x5b,0x5b), NXGUI_RGB(0xe0,0xa8,0x30)
};

const nxgui_palette_t nxgui_palette_ps3 =
{
    NXGUI_RGB(0x00,0x00,0x00), NXGUI_RGB(0x00,0x00,0x00), NXGUI_RGB(0x0a,0x0a,0x12),
    NXGUI_RGB(0x2a,0x3a,0x4a), NXGUI_RGB(0xff,0xff,0xff), NXGUI_RGB(0x7a,0x9a,0xb0),
    NXGUI_RGB(0x8f,0xe0,0xff), NXGUI_RGB(0x2a,0x4a,0x5a),
    NXGUI_RGB(0x4c,0xd9,0x7a), NXGUI_RGB(0xe4,0x5b,0x5b), NXGUI_RGB(0xe0,0xa8,0x30)
};

const nxgui_palette_t nxgui_palette_wii =
{
    NXGUI_RGB(0xe4,0xe7,0xec), NXGUI_RGB(0xff,0xff,0xff), NXGUI_RGB(0xf2,0xf4,0xf7),
    NXGUI_RGB(0xc8,0xcd,0xd6), NXGUI_RGB(0x30,0x34,0x3c), NXGUI_RGB(0x70,0x76,0x82),
    NXGUI_RGB(0x4a,0xa8,0xe8), NXGUI_RGB(0xb8,0xdc,0xf4),
    NXGUI_RGB(0x3a,0xb8,0x6a), NXGUI_RGB(0xd8,0x4a,0x4a), NXGUI_RGB(0xd8,0x9a,0x20)
};

const nxgui_palette_t *nxgui_palette_for_theme( int theme_id )
{
    switch (theme_id)
    {
    case 1: return &nxgui_palette_ps5;
    case 2: return &nxgui_palette_xbox;
    case 3: return &nxgui_palette_ps3;
    case 4: return &nxgui_palette_wii;
    case 0:
    default: return &nxgui_palette_switch;
    }
}

#define NXGUI_GLYPH_ADVANCE(scale) ((NXGUI_FONT_GLYPH_W + 1) * (scale))

void nxgui_draw_text( nxgui_surface_t *surf, int x, int y, int scale, const char *text, uint32_t color )
{
    int pen_x = x;

    if (scale < 1) scale = 1;
    for (; *text; text++, pen_x += NXGUI_GLYPH_ADVANCE(scale))
    {
        int row;
        for (row = 0; row < NXGUI_FONT_GLYPH_H; row++)
        {
            int bits = nxgui_font_glyph_row( *text, row );
            int col;
            for (col = 0; col < NXGUI_FONT_GLYPH_W; col++)
            {
                nxgui_rect_t px;
                /* bit4 (0x10) is the leftmost column -- matches
                 * nxgui_font.c's own row-byte convention exactly. */
                if (!(bits & (1 << (NXGUI_FONT_GLYPH_W - 1 - col)))) continue;
                px.x = pen_x + col * scale;
                px.y = y + row * scale;
                px.w = scale;
                px.h = scale;
                nxgui_fill_rect( surf, px, color );
            }
        }
    }
}

int nxgui_text_width( const char *text, int scale )
{
    int len = 0;
    if (scale < 1) scale = 1;
    while (text[len]) len++;
    return len * NXGUI_GLYPH_ADVANCE(scale);
}

int nxgui_text_height( int scale )
{
    if (scale < 1) scale = 1;
    return NXGUI_FONT_GLYPH_H * scale;
}

void nxgui_draw_image( nxgui_surface_t *surf, int x, int y, int dst_w, int dst_h,
                       const uint32_t *src, int src_w, int src_h )
{
    int dx, dy;

    if (dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0 || !src) return;

    for (dy = 0; dy < dst_h; dy++)
    {
        int py = y + dy;
        int sy = dy * src_h / dst_h;
        if (py < 0 || py >= surf->height) continue;
        for (dx = 0; dx < dst_w; dx++)
        {
            int px = x + dx;
            int sx, sidx;
            uint32_t sc, a, r, g, b, dr, dg, db;
            uint32_t *dst_px;
            if (px < 0 || px >= surf->width) continue;
            sx = dx * src_w / dst_w;
            sidx = sy * src_w + sx;
            sc = src[sidx];
            a = (sc >> 24) & 0xff;
            if (a == 0) continue;   /* fully transparent -- skip the blend entirely */
            dst_px = &surf->pixels[(size_t)py * surf->stride_px + px];
            if (a == 0xff) { *dst_px = sc; continue; }   /* fully opaque -- skip the blend math */
            r = sc & 0xff; g = (sc >> 8) & 0xff; b = (sc >> 16) & 0xff;
            dr = *dst_px & 0xff; dg = (*dst_px >> 8) & 0xff; db = (*dst_px >> 16) & 0xff;
            dr = (r * a + dr * (255 - a)) / 255;
            dg = (g * a + dg * (255 - a)) / 255;
            db = (b * a + db * (255 - a)) / 255;
            *dst_px = NXGUI_RGBA( dr, dg, db, 0xff );
        }
    }
}
