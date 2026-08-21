/* nxgui: minimal, purpose-built immediate-mode-ish rendering toolkit for
 * the wine-nx launcher, CPU-rasterized directly onto the pixel buffer
 * wine_nx_fb_lock()/wine_nx_fb_unlock() already hand back (RGBA8888,
 * 1280x720, per source/runtime.c's WINE_NX_FB_W/H) -- deliberately NOT
 * ImGui/SDL2/any GPU-backed toolkit. See wine-nx-probe/3d-accel-scoping.md
 * and this project's own vulkan_integration memory: both Vulkan and
 * deko3d on this hardware only work as the sole `vi` consumer in a binary
 * with zero `nwindowGetDefault()` references anywhere in the link
 * closure -- so the launcher, which has to coexist with (and hand off to)
 * whichever backend the user picks, can only safely use the one
 * presentation path proven not to conflict with anything: the plain
 * libnx framebuffer, the same one Wine's own GDI/DIB rendering already
 * uses throughout this project.
 *
 * NOT YET HARDWARE-VALIDATED. Written the same way every other new
 * subsystem in this project starts: real code, syntax-checked where
 * possible, honest about what hasn't been run on real hardware yet.
 */
#ifndef WINE_NX_GUI_H
#define WINE_NX_GUI_H

#include <stdint.h>
#include <stddef.h>

typedef struct { int x, y, w, h; } nxgui_rect_t;

/* Packed RGBA8888, byte order matching source/runtime.c's existing
 * wine_nx_hud_draw_text() convention (a plain uint32_t written directly
 * into the RGBA8888 framebuffer -- confirmed by reading that function,
 * not guessed): 0xAABBGGRR as a little-endian uint32_t, i.e. red in the
 * lowest byte. NXGUI_RGBA both matches that and reads naturally at call
 * sites. */
#define NXGUI_RGBA(r,g,b,a) \
    ((uint32_t)(uint8_t)(r) | ((uint32_t)(uint8_t)(g) << 8) | \
     ((uint32_t)(uint8_t)(b) << 16) | ((uint32_t)(uint8_t)(a) << 24))
#define NXGUI_RGB(r,g,b) NXGUI_RGBA(r,g,b,0xff)

/* "Premium dark" theme -- deep near-black background, soft off-white
 * text (not pure white -- less eye strain, more modern-flat), one
 * violet-blue accent used consistently for focus/selection/primary
 * actions. Deliberately a small, fixed palette rather than per-screen
 * ad hoc colors, so the whole UI reads as one system. */
#define NXGUI_COLOR_BG           NXGUI_RGB(0x14,0x16,0x1c)
#define NXGUI_COLOR_PANEL        NXGUI_RGB(0x1c,0x1f,0x28)
#define NXGUI_COLOR_PANEL_RAISED NXGUI_RGB(0x24,0x28,0x34)
#define NXGUI_COLOR_BORDER       NXGUI_RGB(0x33,0x38,0x47)
#define NXGUI_COLOR_TEXT         NXGUI_RGB(0xe8,0xea,0xf0)
#define NXGUI_COLOR_TEXT_DIM     NXGUI_RGB(0x8b,0x90,0xa0)
#define NXGUI_COLOR_ACCENT       NXGUI_RGB(0x5b,0x67,0xea)
#define NXGUI_COLOR_ACCENT_DIM   NXGUI_RGB(0x3a,0x42,0x99)
#define NXGUI_COLOR_GOOD         NXGUI_RGB(0x4c,0xd9,0x7a)
#define NXGUI_COLOR_BAD          NXGUI_RGB(0xe4,0x5b,0x5b)
#define NXGUI_COLOR_WARN         NXGUI_RGB(0xe0,0xa8,0x30)

/* Bound to whatever wine_nx_fb_lock() handed back for this frame -- the
 * caller (the launcher's own frame loop) owns the lock/unlock pairing;
 * nxgui just draws into the buffer it's given. */
typedef struct
{
    uint32_t *pixels;
    int width, height, stride_px;
} nxgui_surface_t;

void nxgui_clear( nxgui_surface_t *surf, uint32_t color );
void nxgui_fill_rect( nxgui_surface_t *surf, nxgui_rect_t r, uint32_t color );
/* Border-only rect, `thickness` px -- used for focus rings and panel
 * outlines. */
void nxgui_stroke_rect( nxgui_surface_t *surf, nxgui_rect_t r, int thickness, uint32_t color );

/* Real rounded-rect fill/stroke (corner quarter-circle mask, tested
 * per-pixel -- no anti-aliasing, matching this whole toolkit's flat,
 * no-AA style elsewhere). `radius` is clamped to min(w,h)/2 so an
 * oversized radius degrades to a full capsule/circle instead of
 * misbehaving. Added specifically for the Wii theme's high corner
 * rounding (nxmenu_library.c) and reused by nxgui_draw_slider's pill
 * track/handle below -- a square rect with radius=side/2 doubles as a
 * "circle" everywhere this toolkit needs one, so there's no separate
 * circle primitive. */
void nxgui_fill_rounded_rect( nxgui_surface_t *surf, nxgui_rect_t r, int radius, uint32_t color );
void nxgui_stroke_rounded_rect( nxgui_surface_t *surf, nxgui_rect_t r, int radius, int thickness, uint32_t color );

/* Per-console-theme color sets for the library screen (nxmenu_library.c).
 * Distinct layout mechanics + each console's real color/shape language is
 * the honest bar here, not literal pixel-for-pixel chrome accuracy --
 * this pipeline has no reference screenshots to diff against. Every
 * other screen (settings/controller studio/overlays) stays on the fixed
 * NXGUI_COLOR_* palette above; only the theme-switchable library grid
 * uses these. */
typedef struct
{
    uint32_t bg, panel, panel_raised, border, text, text_dim, accent, accent_dim, good, bad, warn;
} nxgui_palette_t;

extern const nxgui_palette_t nxgui_palette_switch;
extern const nxgui_palette_t nxgui_palette_ps5;
extern const nxgui_palette_t nxgui_palette_xbox;
extern const nxgui_palette_t nxgui_palette_ps3;
extern const nxgui_palette_t nxgui_palette_wii;

/* theme_id: 0=Switch, 1=PS5, 2=Xbox, 3=PS3, 4=Wii -- matches
 * nxmenu.h's own nxmenu_theme_id ordinals exactly (this file takes a
 * bare int rather than that enum so nxgui.h doesn't need to depend on
 * nxmenu.h). Out-of-range falls back to the Switch palette rather than
 * reading past the table. */
const nxgui_palette_t *nxgui_palette_for_theme( int theme_id );

/* Small immediate-mode-ish widgets, drawn fresh every frame like
 * everything else in this toolkit -- no persistent widget state here,
 * callers (nxmenu_detail.c's mod toggles, nxmenu_controller.c's
 * deadzone/sensitivity dials) own the actual bool/float being edited
 * and do their own hit-testing via nxgui_rect_contains, same pattern
 * every other screen already uses. */
#define NXGUI_TOGGLE_SIZE 28
void nxgui_draw_toggle( nxgui_surface_t *surf, int x, int y, int checked, uint32_t on_color, uint32_t off_color );

#define NXGUI_SLIDER_TRACK_H  8
#define NXGUI_SLIDER_HANDLE   22
/* value01 clamped to [0,1]; draws a pill track, a filled progress
 * segment up to value01, and a round handle at that position. */
void nxgui_draw_slider( nxgui_surface_t *surf, int x, int y, int w, float value01,
                        uint32_t track_color, uint32_t fill_color, uint32_t handle_color );

/* scale=1 renders each glyph's native 5x7 cell 1:1; scale=3 (the
 * launcher's own default body-text size) renders 15x21px per glyph.
 * Uppercases automatically (see nxgui_font.c's own header comment for
 * why only uppercase+digits+a working punctuation subset exist at all). */
void nxgui_draw_text( nxgui_surface_t *surf, int x, int y, int scale, const char *text, uint32_t color );
int  nxgui_text_width( const char *text, int scale );   /* px, for centering/right-alignment */
int  nxgui_text_height( int scale );                     /* px, single line */

/* Nearest-neighbor RGBA8888 blit with alpha compositing (straight, not
 * premultiplied -- matches how nxgui_pe_icon.c decodes ICO/BMP/PNG frames).
 * dst_w/dst_h independent of src_w/src_h so a 32x32 game icon can be
 * drawn at whatever grid-cell size the library screen wants without a
 * second decode. */
void nxgui_draw_image( nxgui_surface_t *surf, int x, int y, int dst_w, int dst_h,
                       const uint32_t *src, int src_w, int src_h );

/* Point-in-rect, shared by every screen's own hit-testing so the touch
 * and joycon-cursor input paths (nxinput.h) always agree with what got
 * drawn where. */
static inline int nxgui_rect_contains( nxgui_rect_t r, int x, int y )
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

#endif /* WINE_NX_GUI_H */
