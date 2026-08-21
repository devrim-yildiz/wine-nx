/* Five independently-coded theme engines for the library screen. Each
 * `nav_theme_*`/`render_theme_*` pair owns its OWN layout math, its own
 * cell/hit-test geometry, and its own drawing -- none of them call a
 * shared "generic grid" helper, per the explicit brief this file was
 * built against. What IS shared between them is genuinely not layout:
 * nxmlib_build_visible()/nxmlib_find_pos() (which games are in scope,
 * not how they're arranged), nxmlib_begin_launch() (the same three
 * lines every theme needs to populate state->launch_* before returning
 * NXMENU_ACTION_LAUNCH), and nxmlib_machine_label()/nxmlib_draw_text_fit()
 * (string formatting, not layout).
 *
 * Honesty note on "95-100% visual accuracy": this pipeline has no
 * reference screenshots of the real consoles' UIs to diff against, and
 * the only text renderer available is a hand-authored 5x7 bitmap font
 * (nxgui_font.c) -- there is no way to actually verify a percentage
 * here, and claiming one would be exactly the kind of unverified claim
 * this project has deliberately avoided elsewhere (see e.g. this
 * project's own skepticism of an unverified "all code linked
 * successfully" claim earlier in its history). What's real and
 * checkable: each theme has a genuinely different layout mechanic
 * (horizontal carousel / hero+strip / asymmetric metro grid / XMB
 * cross-bar / uniform rounded grid), a distinct hand-tuned color
 * palette (nxgui.c's nxgui_palette_*), and independently-implemented
 * navigation math suited to that specific layout.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#include "nxmenu_library.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* shared (non-layout) helpers                                         */
/* ------------------------------------------------------------------ */

static const char *nxmlib_machine_label( unsigned short machine )
{
    if (machine == IMAGE_FILE_MACHINE_AMD64) return "64-BIT";
    if (machine == IMAGE_FILE_MACHINE_I386) return "32-BIT";
    if (machine == IMAGE_FILE_MACHINE_ARM64) return "ARM64";
    return "?";
}

static void nxmlib_draw_text_fit( nxgui_surface_t *surf, int x, int y, int scale, const char *text, int max_w, uint32_t color )
{
    char buf[72];
    size_t n = strlen( text );
    int truncated = 0;

    if (n > 64) n = 64;
    memcpy( buf, text, n );
    buf[n] = 0;

    while (nxgui_text_width( buf, scale ) > max_w && n > 0)
    {
        n--;
        buf[n] = 0;
        truncated = 1;
    }
    if (truncated)
    {
        while (n > 0 && nxgui_text_width( buf, scale ) + nxgui_text_width( "...", scale ) > max_w) { n--; buf[n] = 0; }
        strcat( buf, "..." );
    }
    nxgui_draw_text( surf, x, y, scale, buf, color );
}

static void nxmlib_begin_launch( nxmenu_state_t *state, const nxlib_game_t *g )
{
    snprintf( state->launch_path, sizeof(state->launch_path), "%s", g->path );
    nxlib_effective_settings( g, &state->launch_settings );
    state->launch_machine = g->machine;
}

/* Which games are in scope right now -- the Y-button favorites filter
 * is the one thing every theme respects identically (PS3's own
 * category system layers its finer categories on top of this, not
 * instead of it -- see render_theme_ps3/nav_theme_ps3). */
static int nxmlib_build_visible( const nxmenu_state_t *state, int *out )
{
    int i, n = 0;
    for (i = 0; i < state->game_count; i++)
    {
        if (state->favorites_filter && !state->games[i].is_favorite) continue;
        out[n++] = i;
    }
    return n;
}

static int nxmlib_find_pos( const int *visible, int count, int selected )
{
    int i;
    for (i = 0; i < count; i++) if (visible[i] == selected) return i;
    return count > 0 ? 0 : -1;
}

/* ================================================================== */
/* THEME: SWITCH -- horizontal carousel of square tiles, focused tile   */
/* enlarged with a bright accent outline, bottom row of round quick-    */
/* action icons. Real Switch home menu layout mechanic, 1D nav only.    */
/* ================================================================== */

#define SWITCH_TILE_W       210
#define SWITCH_TILE_FOCUS_W 260
#define SWITCH_GAP           26
#define SWITCH_ROW_Y         190

/* Fills pos_x[i]/pos_w[i] for every visible tile, focused tile
 * horizontally centered on screen -- called by both nav (touch hit-
 * test) and render (drawing) so they can never disagree about where a
 * tile actually is. */
static void switch_compute_positions( const nxmenu_state_t *state, int surf_w, const int *visible, int visible_count,
                                      int *pos_x, int *pos_w )
{
    int i, x_cursor = 0, focus_pos, focus_center = 0, scroll;

    focus_pos = nxmlib_find_pos( visible, visible_count, state->selected );
    if (focus_pos < 0) focus_pos = 0;

    for (i = 0; i < visible_count; i++)
    {
        int w = (visible[i] == state->selected) ? SWITCH_TILE_FOCUS_W : SWITCH_TILE_W;
        pos_x[i] = x_cursor;
        pos_w[i] = w;
        if (i == focus_pos) focus_center = x_cursor + w / 2;
        x_cursor += w + SWITCH_GAP;
    }
    scroll = surf_w / 2 - focus_center;
    for (i = 0; i < visible_count; i++) pos_x[i] += scroll;
}

static nxmenu_action_t nav_theme_switch( nxmenu_state_t *state, nxgui_surface_t *surf, const int *visible, int visible_count, const nxinput_state_t *input )
{
    int pos, i;
    int pos_x[NXMENU_MAX_GAMES], pos_w[NXMENU_MAX_GAMES];

    if (visible_count == 0)
    {
        if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
        return NXMENU_ACTION_NONE;
    }

    pos = nxmlib_find_pos( visible, visible_count, state->selected );
    if (pos < 0) pos = 0;
    if (input->nav_left && pos > 0) { pos--; state->selected = visible[pos]; }
    if (input->nav_right && pos < visible_count - 1) { pos++; state->selected = visible[pos]; }

    switch_compute_positions( state, surf->width, visible, visible_count, pos_x, pos_w );
    for (i = 0; i < visible_count; i++)
    {
        nxgui_rect_t r;
        r.x = pos_x[i]; r.w = pos_w[i]; r.h = pos_w[i];
        r.y = SWITCH_ROW_Y - (visible[i] == state->selected ? (SWITCH_TILE_FOCUS_W - SWITCH_TILE_W) / 2 : 0);
        if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( r, input->pointer_x, input->pointer_y ))
        {
            if (visible[i] == state->selected) { nxmlib_begin_launch( state, &state->games[visible[i]] ); return NXMENU_ACTION_LAUNCH; }
            state->selected = visible[i];
        }
    }

    if (input->confirm) { nxmlib_begin_launch( state, &state->games[state->selected] ); return NXMENU_ACTION_LAUNCH; }
    if (input->override_game) { state->screen = NXMENU_SCREEN_GAME_OVERRIDE; state->settings_field = 0; }
    /* R (nxinput's dbg_r edge, reused -- see nxmenu_detail.h's own
     * comment on why this doesn't conflict with the debug chord). */
    if (input->dbg_r) { state->screen = NXMENU_SCREEN_GAME_DETAIL; state->settings_field = 0; }
    if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
    return NXMENU_ACTION_NONE;
}

static void render_theme_switch( nxgui_surface_t *surf, nxmenu_state_t *state, const int *visible, int visible_count )
{
    const nxgui_palette_t *pal = &nxgui_palette_switch;
    int pos_x[NXMENU_MAX_GAMES], pos_w[NXMENU_MAX_GAMES];
    int i;

    nxgui_clear( surf, pal->bg );

    {
        nxgui_rect_t bar = { 0, 0, surf->width, 90 };
        nxgui_fill_rect( surf, bar, pal->panel );
        nxgui_draw_text( surf, 40, 32, 2, "WINE-NX", pal->text );
        if (state->favorites_filter)
        {
            int tw = nxgui_text_width( "FAVORITES", 1 );
            nxgui_draw_text( surf, surf->width - tw - 40, 36, 1, "FAVORITES", pal->accent );
        }
    }

    if (visible_count == 0)
    {
        const char *msg = "NO GAMES YET - PRESS + TO ADD ONE";
        int tw = nxgui_text_width( msg, 2 );
        nxgui_draw_text( surf, (surf->width - tw) / 2, surf->height / 2, 2, msg, pal->text_dim );
        return;
    }

    switch_compute_positions( state, surf->width, visible, visible_count, pos_x, pos_w );

    for (i = 0; i < visible_count; i++)
    {
        const nxlib_game_t *g = &state->games[visible[i]];
        int focused = (visible[i] == state->selected);
        nxgui_rect_t cell, inner;

        cell.x = pos_x[i]; cell.w = pos_w[i]; cell.h = pos_w[i];
        cell.y = SWITCH_ROW_Y - (focused ? (SWITCH_TILE_FOCUS_W - SWITCH_TILE_W) / 2 : 0);
        if (cell.x + cell.w < 0 || cell.x > surf->width) continue;   /* cheap off-screen clip */

        nxgui_fill_rect( surf, cell, pal->panel_raised );
        if (focused) nxgui_stroke_rect( surf, cell, 6, pal->accent );

        inner.x = cell.x + 10; inner.y = cell.y + 10; inner.w = cell.w - 20; inner.h = cell.h - 20;
        nxlib_ensure_icon( (nxlib_game_t *)g );
        if (g->icon_valid)
            nxgui_draw_image( surf, inner.x, inner.y, inner.w, inner.h, g->icon.pixels, g->icon.width, g->icon.height );
        else
        {
            char letter[2];
            int tw;
            letter[0] = g->name[0] ? g->name[0] : '?'; letter[1] = 0;
            nxgui_fill_rect( surf, inner, g->primary_color );
            tw = nxgui_text_width( letter, 4 );
            nxgui_draw_text( surf, inner.x + (inner.w - tw) / 2, inner.y + (inner.h - nxgui_text_height(4)) / 2, 4, letter, pal->text );
        }

        if (g->is_favorite)
        {
            nxgui_rect_t star = { cell.x + cell.w - 26, cell.y + 6, 20, 20 };
            nxgui_fill_rounded_rect( surf, star, 10, pal->warn );
        }

        if (focused)
        {
            int tw2 = nxgui_text_width( g->name, 1 );
            int tx = cell.x + (cell.w - tw2) / 2;
            nxgui_draw_text( surf, tx, cell.y + cell.h + 14, 1, g->name, pal->text );
        }
    }

    {
        int cy = surf->height - 60, r = 24, gap2 = 84, x0 = 70, b;
        static const char *labels[4] = { "FAV", "MODS", "INFO", "SET" };
        const char *hint = "LEFT/RIGHT SELECT  A LAUNCH  - OVERRIDE  R DETAILS  X THEME  Y FAVORITES  + MENU";

        for (b = 0; b < 4; b++)
        {
            nxgui_rect_t circ = { x0 + b * gap2 - r, cy - r, r * 2, r * 2 };
            nxgui_fill_rounded_rect( surf, circ, r, pal->panel_raised );
            nxgui_stroke_rounded_rect( surf, circ, r, 3, pal->accent );
            nxgui_draw_text( surf, circ.x + 2, cy - 3, 1, labels[b], pal->text_dim );
        }
        nxgui_draw_text( surf, x0 + 4 * gap2 + 20, cy - 6, 1, hint, pal->text_dim );
    }
}

/* ================================================================== */
/* THEME: PS5 -- top tab bar, compact strip of small tiles upper-left,  */
/* the remaining ~70% of the screen is a hero detail view for the       */
/* focused game (large title, playtime, PLAY button, primary_color      */
/* accent bar). 1D strip nav; PLAY is activated the same way A always   */
/* activates the focused game, everywhere in this launcher.             */
/* ================================================================== */

#define PS5_TILE     84
#define PS5_GAP      14
#define PS5_STRIP_Y  100
#define PS5_HERO_Y   210

static void ps5_compute_positions( const nxmenu_state_t *state, int surf_w, const int *visible, int visible_count, int *pos_x )
{
    int i, x_cursor = 0, focus_pos, focus_center = 0, scroll;

    focus_pos = nxmlib_find_pos( visible, visible_count, state->selected );
    if (focus_pos < 0) focus_pos = 0;

    for (i = 0; i < visible_count; i++)
    {
        pos_x[i] = x_cursor;
        if (i == focus_pos) focus_center = x_cursor + PS5_TILE / 2;
        x_cursor += PS5_TILE + PS5_GAP;
    }
    scroll = surf_w / 2 - focus_center;
    for (i = 0; i < visible_count; i++) pos_x[i] += scroll;
}

static nxmenu_action_t nav_theme_ps5( nxmenu_state_t *state, nxgui_surface_t *surf, const int *visible, int visible_count, const nxinput_state_t *input )
{
    int pos, i, pos_x[NXMENU_MAX_GAMES];

    if (visible_count == 0)
    {
        if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
        return NXMENU_ACTION_NONE;
    }

    pos = nxmlib_find_pos( visible, visible_count, state->selected );
    if (pos < 0) pos = 0;
    if (input->nav_left && pos > 0) { pos--; state->selected = visible[pos]; }
    if (input->nav_right && pos < visible_count - 1) { pos++; state->selected = visible[pos]; }

    ps5_compute_positions( state, surf->width, visible, visible_count, pos_x );
    for (i = 0; i < visible_count; i++)
    {
        nxgui_rect_t r = { pos_x[i], PS5_STRIP_Y, PS5_TILE, PS5_TILE };
        if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( r, input->pointer_x, input->pointer_y ))
        {
            if (visible[i] == state->selected) { nxmlib_begin_launch( state, &state->games[visible[i]] ); return NXMENU_ACTION_LAUNCH; }
            state->selected = visible[i];
        }
    }

    /* The PLAY button drawn in the hero pane is a fixed rect
     * (render_theme_ps5 draws it at this exact spot) -- also tappable,
     * not just A. */
    {
        nxgui_rect_t play = { 60, PS5_HERO_Y + 210, 200, 56 };
        if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( play, input->pointer_x, input->pointer_y ) && visible_count > 0)
        { nxmlib_begin_launch( state, &state->games[state->selected] ); return NXMENU_ACTION_LAUNCH; }
    }

    if (input->confirm) { nxmlib_begin_launch( state, &state->games[state->selected] ); return NXMENU_ACTION_LAUNCH; }
    if (input->override_game) { state->screen = NXMENU_SCREEN_GAME_OVERRIDE; state->settings_field = 0; }
    /* R (nxinput's dbg_r edge, reused -- see nxmenu_detail.h's own
     * comment on why this doesn't conflict with the debug chord). */
    if (input->dbg_r) { state->screen = NXMENU_SCREEN_GAME_DETAIL; state->settings_field = 0; }
    if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
    return NXMENU_ACTION_NONE;
}

static void render_theme_ps5( nxgui_surface_t *surf, nxmenu_state_t *state, const int *visible, int visible_count )
{
    const nxgui_palette_t *pal = &nxgui_palette_ps5;
    int pos_x[NXMENU_MAX_GAMES];
    int i;

    nxgui_clear( surf, pal->bg );

    {
        nxgui_rect_t tabbar = { 0, 0, surf->width, 56 };
        static const char *tabs[3] = { "GAMES", "MEDIA", "STORE" };
        int tx = 40, t;
        nxgui_fill_rect( surf, tabbar, pal->panel );
        for (t = 0; t < 3; t++)
        {
            nxgui_draw_text( surf, tx, 18, 2, tabs[t], t == 0 ? pal->text : pal->text_dim );
            if (t == 0)
            {
                nxgui_rect_t underline = { tx, 46, nxgui_text_width( tabs[t], 2 ), 3 };
                nxgui_fill_rect( surf, underline, pal->accent );
            }
            tx += nxgui_text_width( tabs[t], 2 ) + 50;
        }
        if (state->favorites_filter) nxgui_draw_text( surf, surf->width - 180, 18, 1, "FAVORITES ONLY", pal->accent );
    }

    if (visible_count == 0)
    {
        const char *msg = "NO GAMES YET - PRESS + TO ADD ONE";
        int tw = nxgui_text_width( msg, 2 );
        nxgui_draw_text( surf, (surf->width - tw) / 2, surf->height / 2, 2, msg, pal->text_dim );
        return;
    }

    ps5_compute_positions( state, surf->width, visible, visible_count, pos_x );
    for (i = 0; i < visible_count; i++)
    {
        const nxlib_game_t *g = &state->games[visible[i]];
        int focused = (visible[i] == state->selected);
        nxgui_rect_t cell;

        cell.x = pos_x[i]; cell.y = PS5_STRIP_Y; cell.w = PS5_TILE; cell.h = PS5_TILE;
        if (cell.x + cell.w < 0 || cell.x > surf->width) continue;

        nxgui_fill_rect( surf, cell, pal->panel_raised );
        if (focused) nxgui_stroke_rect( surf, cell, 4, pal->accent );

        nxlib_ensure_icon( (nxlib_game_t *)g );
        if (g->icon_valid)
            nxgui_draw_image( surf, cell.x + 6, cell.y + 6, cell.w - 12, cell.h - 12, g->icon.pixels, g->icon.width, g->icon.height );
        else
        {
            nxgui_rect_t inner = { cell.x + 6, cell.y + 6, cell.w - 12, cell.h - 12 };
            nxgui_fill_rect( surf, inner, g->primary_color );
        }
    }

    /* hero detail view for the focused game -- the remaining ~70% of
     * the screen below the strip. */
    {
        const nxlib_game_t *g = &state->games[state->selected];
        nxgui_rect_t hero = { 0, PS5_HERO_Y, surf->width, surf->height - PS5_HERO_Y };
        nxgui_rect_t accent_bar = { 0, PS5_HERO_Y, 10, hero.h };
        nxgui_rect_t icon_rect = { 60, PS5_HERO_Y + 30, 200, 200 };
        nxgui_rect_t play = { 60, PS5_HERO_Y + 210, 200, 56 };
        char stat[96];
        int text_x = icon_rect.x + icon_rect.w + 40;

        nxgui_fill_rect( surf, hero, pal->panel );
        nxgui_fill_rect( surf, accent_bar, g->primary_color );

        nxlib_ensure_icon( (nxlib_game_t *)g );
        if (g->icon_valid)
            nxgui_draw_image( surf, icon_rect.x, icon_rect.y, icon_rect.w, icon_rect.h, g->icon.pixels, g->icon.width, g->icon.height );
        else
            nxgui_fill_rect( surf, icon_rect, g->primary_color );

        /* scale 5 -> 35px glyph height, the closest this bitmap font
         * gets to the spec'd "36px font" (integer scale steps only). */
        nxmlib_draw_text_fit( surf, text_x, PS5_HERO_Y + 30, 5, g->name, surf->width - text_x - 40, pal->text );
        nxgui_draw_text( surf, text_x, PS5_HERO_Y + 80, 1, g->author, pal->text_dim );

        snprintf( stat, sizeof(stat), "PLAYTIME: %.1f HOURS", (double)g->playtime_hours );
        nxgui_draw_text( surf, text_x, PS5_HERO_Y + 110, 1, stat, pal->text_dim );
        nxgui_draw_text( surf, text_x, PS5_HERO_Y + 132, 1, nxmlib_machine_label( g->machine ), pal->text_dim );
        if (g->has_active_mods) nxgui_draw_text( surf, text_x, PS5_HERO_Y + 154, 1, "MODS ACTIVE", pal->accent );

        nxgui_fill_rounded_rect( surf, play, 8, g->primary_color );
        {
            int tw = nxgui_text_width( "PLAY", 2 );
            nxgui_draw_text( surf, play.x + (play.w - tw) / 2, play.y + (play.h - nxgui_text_height(2)) / 2, 2, "PLAY", pal->text );
        }

        nxgui_draw_text( surf, 40, surf->height - 30, 1, "LEFT/RIGHT SELECT  A/PLAY LAUNCH  - OVERRIDE  R DETAILS  X THEME  Y FAVORITES  + MENU", pal->text_dim );
    }
}

/* ================================================================== */
/* THEME: XBOX -- asymmetric metro dashboard. Game 0 is a massive 2x2   */
/* hero tile; games 1-4 sit beside it as four 1x1 tiles; anything past  */
/* that continues in a plain 4-wide grid below. 0px rounding throughout */
/* (razor-sharp corners), thick solid neon-green focus border. Real 2D  */
/* grid navigation via an explicit logical-cell table (not the same     */
/* row-major math the other grid theme, Wii, uses -- Wii's grid is      */
/* uniform, this one deliberately isn't). */
/* ================================================================== */

#define XBOX_CELL      150
#define XBOX_GAP        14
#define XBOX_COLS        4
#define XBOX_TOP       100
#define XBOX_MARGIN     44
#define XBOX_MAX_ROWS   66   /* covers up to NXMENU_MAX_GAMES (256) games: 2 hero rows + ceil((256-5)/4) */

/* grid[row][col] holds a position-in-`visible` index, or -1 for an
 * empty cell. The hero (visible position 0, if any game exists) fills
 * all four cells of rows 0-1 / cols 0-1 -- deliberately duplicated
 * across 4 cells rather than "spanned" in any special-cased way, so
 * ordinary row-major neighbor lookups (xbox_find_cell / the nav
 * movement below) just work without needing span-aware logic. */
static void xbox_build_grid( int visible_count, int grid[XBOX_MAX_ROWS][XBOX_COLS] )
{
    int r, c, i;
    for (r = 0; r < XBOX_MAX_ROWS; r++) for (c = 0; c < XBOX_COLS; c++) grid[r][c] = -1;
    if (visible_count == 0) return;

    grid[0][0] = grid[0][1] = grid[1][0] = grid[1][1] = 0;
    if (visible_count > 1) grid[0][2] = 1;
    if (visible_count > 2) grid[0][3] = 2;
    if (visible_count > 3) grid[1][2] = 3;
    if (visible_count > 4) grid[1][3] = 4;

    for (i = 5; i < visible_count; i++)
    {
        int t = i - 5;
        int col = t % XBOX_COLS;
        int row = 2 + t / XBOX_COLS;
        if (row < XBOX_MAX_ROWS) grid[row][col] = i;
    }
}

static int xbox_grid_rows( int visible_count )
{
    int extra = visible_count > 5 ? visible_count - 5 : 0;
    int rows = 2 + (extra + XBOX_COLS - 1) / XBOX_COLS;
    return rows < 2 ? 2 : rows;
}

static int xbox_find_cell( int grid[XBOX_MAX_ROWS][XBOX_COLS], int rows, int pos, int *out_row, int *out_col )
{
    int r, c;
    for (r = 0; r < rows; r++)
        for (c = 0; c < XBOX_COLS; c++)
            if (grid[r][c] == pos) { *out_row = r; *out_col = c; return 1; }
    return 0;
}

/* Cell pixel rect for a LOGICAL (row,col) -- the hero's 4 duplicate
 * cells all resolve to the identical on-screen rect since they share
 * row/col span math with a fixed hero size, so callers don't need to
 * special-case which of the 4 they hit. */
static nxgui_rect_t xbox_cell_rect( int row, int col, int scroll_row )
{
    nxgui_rect_t r;
    if (row < 2 && col < 2)
    {
        r.x = XBOX_MARGIN; r.y = XBOX_TOP + (0 - scroll_row) * (XBOX_CELL + XBOX_GAP);
        r.w = 2 * XBOX_CELL + XBOX_GAP; r.h = 2 * XBOX_CELL + XBOX_GAP;
        return r;
    }
    r.x = XBOX_MARGIN + col * (XBOX_CELL + XBOX_GAP);
    r.y = XBOX_TOP + (row - scroll_row) * (XBOX_CELL + XBOX_GAP);
    r.w = XBOX_CELL; r.h = XBOX_CELL;
    return r;
}

static nxmenu_action_t nav_theme_xbox( nxmenu_state_t *state, nxgui_surface_t *surf, const int *visible, int visible_count, const nxinput_state_t *input )
{
    static int grid[XBOX_MAX_ROWS][XBOX_COLS];
    int rows = xbox_grid_rows( visible_count );
    int pos, row = 0, col = 0, visible_rows;
    int r, c;

    if (visible_count == 0)
    {
        if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
        return NXMENU_ACTION_NONE;
    }

    xbox_build_grid( visible_count, grid );
    pos = nxmlib_find_pos( visible, visible_count, state->selected );
    if (pos < 0) pos = 0;
    xbox_find_cell( grid, rows, pos, &row, &col );

    if (input->nav_right) { int c2 = col + 1; while (c2 < XBOX_COLS && (grid[row][c2] == -1 || grid[row][c2] == pos)) c2++; if (c2 < XBOX_COLS && grid[row][c2] != -1) { pos = grid[row][c2]; state->selected = visible[pos]; } }
    if (input->nav_left)  { int c2 = col - 1; while (c2 >= 0 && (grid[row][c2] == -1 || grid[row][c2] == pos)) c2--; if (c2 >= 0 && grid[row][c2] != -1) { pos = grid[row][c2]; state->selected = visible[pos]; } }
    if (input->nav_down)  { int r2 = row + 1; while (r2 < rows && (grid[r2][col] == -1 || grid[r2][col] == pos)) r2++; if (r2 < rows && grid[r2][col] != -1) { pos = grid[r2][col]; state->selected = visible[pos]; } }
    if (input->nav_up)    { int r2 = row - 1; while (r2 >= 0 && (grid[r2][col] == -1 || grid[r2][col] == pos)) r2--; if (r2 >= 0 && grid[r2][col] != -1) { pos = grid[r2][col]; state->selected = visible[pos]; } }

    xbox_find_cell( grid, rows, pos, &row, &col );
    visible_rows = (surf->height - XBOX_TOP - 70) / (XBOX_CELL + XBOX_GAP);
    if (visible_rows < 2) visible_rows = 2;
    if (row < state->scroll_row) state->scroll_row = row;
    if (row >= state->scroll_row + visible_rows) state->scroll_row = row - visible_rows + 1;
    if (state->scroll_row < 0) state->scroll_row = 0;

    for (r = 0; r < rows; r++)
    {
        for (c = 0; c < XBOX_COLS; c++)
        {
            int p = grid[r][c];
            nxgui_rect_t cell;
            if (p < 0) continue;
            if (r < 2 && c < 2 && p != 0) continue;   /* hero's duplicate cells -- only test the canonical (0,0) one */
            cell = xbox_cell_rect( r, c, state->scroll_row );
            if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( cell, input->pointer_x, input->pointer_y ))
            {
                if (visible[p] == state->selected) { nxmlib_begin_launch( state, &state->games[visible[p]] ); return NXMENU_ACTION_LAUNCH; }
                state->selected = visible[p];
            }
        }
    }

    if (input->confirm) { nxmlib_begin_launch( state, &state->games[state->selected] ); return NXMENU_ACTION_LAUNCH; }
    if (input->override_game) { state->screen = NXMENU_SCREEN_GAME_OVERRIDE; state->settings_field = 0; }
    /* R (nxinput's dbg_r edge, reused -- see nxmenu_detail.h's own
     * comment on why this doesn't conflict with the debug chord). */
    if (input->dbg_r) { state->screen = NXMENU_SCREEN_GAME_DETAIL; state->settings_field = 0; }
    if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
    return NXMENU_ACTION_NONE;
}

static void render_theme_xbox( nxgui_surface_t *surf, nxmenu_state_t *state, const int *visible, int visible_count )
{
    const nxgui_palette_t *pal = &nxgui_palette_xbox;
    static int grid[XBOX_MAX_ROWS][XBOX_COLS];
    int rows;
    int r, c;

    nxgui_clear( surf, pal->bg );
    {
        nxgui_rect_t bar = { 0, 0, surf->width, 72 };
        nxgui_fill_rect( surf, bar, pal->panel );
        nxgui_draw_text( surf, 40, 24, 2, "WINE-NX", pal->text );
        if (state->favorites_filter) nxgui_draw_text( surf, surf->width - 220, 24, 1, "FAVORITES ONLY", pal->accent );
    }

    if (visible_count == 0)
    {
        const char *msg = "NO GAMES YET - PRESS + TO ADD ONE";
        int tw = nxgui_text_width( msg, 2 );
        nxgui_draw_text( surf, (surf->width - tw) / 2, surf->height / 2, 2, msg, pal->text_dim );
        return;
    }

    xbox_build_grid( visible_count, grid );
    rows = xbox_grid_rows( visible_count );

    for (r = 0; r < rows; r++)
    {
        for (c = 0; c < XBOX_COLS; c++)
        {
            int p = grid[r][c];
            const nxlib_game_t *g;
            int focused;
            nxgui_rect_t cell, inner;

            if (p < 0) continue;
            if (r < 2 && c < 2 && p != 0) continue;   /* draw the hero exactly once, from its canonical cell */

            cell = xbox_cell_rect( r, c, state->scroll_row );
            if (cell.y + cell.h < 72 || cell.y > surf->height) continue;

            g = &state->games[visible[p]];
            focused = (visible[p] == state->selected);

            nxgui_fill_rect( surf, cell, pal->panel_raised );   /* 0px rounding -- razor-sharp corners throughout, unlike every other theme */
            if (focused) nxgui_stroke_rect( surf, cell, 6, pal->accent );

            inner.x = cell.x + 8; inner.y = cell.y + 8; inner.w = cell.w - 16; inner.h = cell.h - 16;
            nxlib_ensure_icon( (nxlib_game_t *)g );
            if (g->icon_valid)
                nxgui_draw_image( surf, inner.x, inner.y, inner.w, inner.h, g->icon.pixels, g->icon.width, g->icon.height );
            else
                nxgui_fill_rect( surf, inner, g->primary_color );

            if (p == 0)
            {
                /* hero gets a title caption baked into the tile, the
                 * other cells stay icon-only like a real metro tile. */
                nxgui_rect_t caption = { cell.x, cell.y + cell.h - 40, cell.w, 40 };
                nxgui_fill_rect( surf, caption, pal->panel );
                nxmlib_draw_text_fit( surf, caption.x + 10, caption.y + 10, 2, g->name, caption.w - 20, pal->text );
            }
        }
    }

    nxgui_draw_text( surf, 40, surf->height - 30, 1, "D-PAD MOVE  A LAUNCH  - OVERRIDE  R DETAILS  X THEME  Y FAVORITES  + MENU", pal->text_dim );
}

/* ================================================================== */
/* THEME: PS3 -- Cross Media Bar. Left 20% column: vertical category    */
/* icons (ALL/FAVORITES/RECENT/32-BIT/64-BIT). Right 80%: a vertical,   */
/* text-only list of games in the active category. Transparent          */
/* backgrounds (this whole theme never fills a panel rect, only text    */
/* and thin selection markers -- the real XMB never used opaque panels  */
/* either), zero borders, soft white active-selection text. Two-axis    */
/* nav: left/right change category, up/down move within the list.       */
/* ================================================================== */

#define PS3_CAT_COUNT     5
#define PS3_CAT_X          60
#define PS3_CAT_TOP       140
#define PS3_CAT_GAP        70
#define PS3_LIST_X        340
#define PS3_LIST_TOP      140
#define PS3_LIST_ROW_H     44

static const char *ps3_category_label( int c )
{
    switch (c)
    {
    case 1: return "FAVORITES";
    case 2: return "RECENTLY PLAYED";
    case 3: return "32-BIT";
    case 4: return "64-BIT";
    case 0: default: return "ALL GAMES";
    }
}

static int ps3_game_matches_category( const nxlib_game_t *g, int category )
{
    switch (category)
    {
    case 1: return g->is_favorite;
    case 2: return g->last_played_timestamp != 0;
    case 3: return g->machine == IMAGE_FILE_MACHINE_I386;
    case 4: return g->machine == IMAGE_FILE_MACHINE_AMD64;
    case 0: default: return 1;
    }
}

/* Category filter layers on TOP of the favorites_filter Y-toggle
 * (nxmlib_build_visible already applied that before `visible` ever
 * reached here) -- both conditions must hold, so Y still means the
 * same thing on this theme as everywhere else, and category is an
 * additional, finer constraint specific to the XMB look. */
static int ps3_build_category_list( const nxmenu_state_t *state, const int *visible, int visible_count, int category, int *out )
{
    int i, n = 0;
    for (i = 0; i < visible_count; i++)
        if (ps3_game_matches_category( &state->games[visible[i]], category )) out[n++] = visible[i];
    return n;
}

static nxmenu_action_t nav_theme_ps3( nxmenu_state_t *state, nxgui_surface_t *surf, const int *visible, int visible_count, const nxinput_state_t *input )
{
    int cat_list[NXMENU_MAX_GAMES];
    int cat_count;
    int pos, i;
    (void)surf;

    if (state->ps3_category < 0) state->ps3_category = 0;
    if (state->ps3_category >= PS3_CAT_COUNT) state->ps3_category = PS3_CAT_COUNT - 1;

    if (input->nav_left && state->ps3_category > 0) state->ps3_category--;
    if (input->nav_right && state->ps3_category < PS3_CAT_COUNT - 1) state->ps3_category++;

    cat_count = ps3_build_category_list( state, visible, visible_count, state->ps3_category, cat_list );

    if (cat_count == 0)
    {
        if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
        return NXMENU_ACTION_NONE;
    }

    pos = nxmlib_find_pos( cat_list, cat_count, state->selected );
    if (pos < 0) pos = 0;
    if (input->nav_up && pos > 0) pos--;
    if (input->nav_down && pos < cat_count - 1) pos++;
    state->selected = cat_list[pos];

    /* category icon touch targets */
    for (i = 0; i < PS3_CAT_COUNT; i++)
    {
        nxgui_rect_t r = { PS3_CAT_X - 10, PS3_CAT_TOP + i * PS3_CAT_GAP - 20, 240, 40 };
        if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( r, input->pointer_x, input->pointer_y ))
            state->ps3_category = i;
    }
    /* list row touch targets */
    for (i = 0; i < cat_count; i++)
    {
        nxgui_rect_t r = { PS3_LIST_X, PS3_LIST_TOP + i * PS3_LIST_ROW_H, surf ? surf->width - PS3_LIST_X - 40 : 800, PS3_LIST_ROW_H - 4 };
        if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( r, input->pointer_x, input->pointer_y ))
        {
            if (cat_list[i] == state->selected) { nxmlib_begin_launch( state, &state->games[cat_list[i]] ); return NXMENU_ACTION_LAUNCH; }
            state->selected = cat_list[i];
        }
    }

    if (input->confirm) { nxmlib_begin_launch( state, &state->games[state->selected] ); return NXMENU_ACTION_LAUNCH; }
    if (input->override_game) { state->screen = NXMENU_SCREEN_GAME_OVERRIDE; state->settings_field = 0; }
    /* R (nxinput's dbg_r edge, reused -- see nxmenu_detail.h's own
     * comment on why this doesn't conflict with the debug chord). */
    if (input->dbg_r) { state->screen = NXMENU_SCREEN_GAME_DETAIL; state->settings_field = 0; }
    if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
    return NXMENU_ACTION_NONE;
}

static void render_theme_ps3( nxgui_surface_t *surf, nxmenu_state_t *state, const int *visible, int visible_count )
{
    const nxgui_palette_t *pal = &nxgui_palette_ps3;
    int cat_list[NXMENU_MAX_GAMES];
    int cat_count, i;

    nxgui_clear( surf, pal->bg );   /* fully transparent-feeling: no header/hint panel fills anywhere in this theme */
    nxgui_draw_text( surf, 40, 30, 2, "WINE-NX", pal->text );
    if (state->favorites_filter) nxgui_draw_text( surf, surf->width - 220, 34, 1, "FAVORITES FILTER ON", pal->accent );

    if (state->ps3_category < 0 || state->ps3_category >= PS3_CAT_COUNT) state->ps3_category = 0;

    for (i = 0; i < PS3_CAT_COUNT; i++)
    {
        int active = (i == state->ps3_category);
        nxgui_draw_text( surf, PS3_CAT_X, PS3_CAT_TOP + i * PS3_CAT_GAP, active ? 2 : 1,
                          ps3_category_label( i ), active ? pal->accent : pal->text_dim );
        if (active)
        {
            nxgui_rect_t marker = { PS3_CAT_X - 20, PS3_CAT_TOP + i * PS3_CAT_GAP + 6, 8, 8 };
            nxgui_fill_rounded_rect( surf, marker, 4, pal->accent );
        }
    }

    cat_count = ps3_build_category_list( state, visible, visible_count, state->ps3_category, cat_list );

    if (cat_count == 0)
    {
        nxgui_draw_text( surf, PS3_LIST_X, PS3_LIST_TOP, 1, "(NO GAMES IN THIS CATEGORY)", pal->text_dim );
    }
    else
    {
        for (i = 0; i < cat_count; i++)
        {
            const nxlib_game_t *g = &state->games[cat_list[i]];
            int active = (cat_list[i] == state->selected);
            int y = PS3_LIST_TOP + i * PS3_LIST_ROW_H;
            char line[192];

            if (y > surf->height) break;
            snprintf( line, sizeof(line), "%s%s", active ? "> " : "  ", g->name );
            nxmlib_draw_text_fit( surf, PS3_LIST_X, y, active ? 2 : 1, line, surf->width - PS3_LIST_X - 40,
                                  active ? pal->text : pal->text_dim );
        }
    }

    nxgui_draw_text( surf, 40, surf->height - 30, 1, "LEFT/RIGHT CATEGORY  UP/DOWN SELECT  A LAUNCH  - OVERRIDE  R DETAILS  X THEME  + MENU", pal->text_dim );
}

/* ================================================================== */
/* THEME: WII -- strict 4x3 grid of 4:3 "channel" boxes. High rounding, */
/* light grey/white panels, soft blue glow on the active channel, title */
/* centered inside an inner white container. Real uniform row-major     */
/* grid nav (unlike Xbox's asymmetric one). */
/* ================================================================== */

#define WII_COLS      4
#define WII_CELL_W  264
#define WII_CELL_H  198   /* 264:198 == 4:3 exactly */
#define WII_GAP      20
#define WII_TOP     110

static nxgui_rect_t wii_cell_rect( int pos, int scroll_row, int surf_w )
{
    nxgui_rect_t r;
    int col = pos % WII_COLS;
    int row = pos / WII_COLS - scroll_row;
    int total_w = WII_COLS * WII_CELL_W + (WII_COLS - 1) * WII_GAP;
    int margin_x = (surf_w - total_w) / 2;
    r.x = margin_x + col * (WII_CELL_W + WII_GAP);
    r.y = WII_TOP + row * (WII_CELL_H + WII_GAP);
    r.w = WII_CELL_W; r.h = WII_CELL_H;
    return r;
}

static nxmenu_action_t nav_theme_wii( nxmenu_state_t *state, nxgui_surface_t *surf, const int *visible, int visible_count, const nxinput_state_t *input )
{
    int pos, i, visible_rows, sel_row;

    if (visible_count == 0)
    {
        if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
        return NXMENU_ACTION_NONE;
    }

    pos = nxmlib_find_pos( visible, visible_count, state->selected );
    if (pos < 0) pos = 0;

    if (input->nav_left && pos % WII_COLS > 0) pos--;
    if (input->nav_right && pos % WII_COLS < WII_COLS - 1 && pos + 1 < visible_count) pos++;
    if (input->nav_up && pos - WII_COLS >= 0) pos -= WII_COLS;
    if (input->nav_down)
    {
        int cand = pos + WII_COLS;
        if (cand < visible_count) pos = cand;
        else if (pos / WII_COLS < (visible_count - 1) / WII_COLS) pos = visible_count - 1;
    }
    state->selected = visible[pos];

    visible_rows = (surf->height - WII_TOP - 70 + WII_GAP) / (WII_CELL_H + WII_GAP);
    if (visible_rows < 1) visible_rows = 1;
    sel_row = pos / WII_COLS;
    if (sel_row < state->scroll_row) state->scroll_row = sel_row;
    if (sel_row >= state->scroll_row + visible_rows) state->scroll_row = sel_row - visible_rows + 1;
    if (state->scroll_row < 0) state->scroll_row = 0;

    for (i = 0; i < visible_count; i++)
    {
        int row = i / WII_COLS;
        nxgui_rect_t r;
        if (row < state->scroll_row || row >= state->scroll_row + visible_rows) continue;
        r = wii_cell_rect( i, state->scroll_row, surf->width );
        if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( r, input->pointer_x, input->pointer_y ))
        {
            if (visible[i] == state->selected) { nxmlib_begin_launch( state, &state->games[visible[i]] ); return NXMENU_ACTION_LAUNCH; }
            state->selected = visible[i];
        }
    }

    if (input->confirm) { nxmlib_begin_launch( state, &state->games[state->selected] ); return NXMENU_ACTION_LAUNCH; }
    if (input->override_game) { state->screen = NXMENU_SCREEN_GAME_OVERRIDE; state->settings_field = 0; }
    /* R (nxinput's dbg_r edge, reused -- see nxmenu_detail.h's own
     * comment on why this doesn't conflict with the debug chord). */
    if (input->dbg_r) { state->screen = NXMENU_SCREEN_GAME_DETAIL; state->settings_field = 0; }
    if (input->menu) { state->screen = NXMENU_SCREEN_TOPMENU; state->topmenu_selected = 0; }
    return NXMENU_ACTION_NONE;
}

static void render_theme_wii( nxgui_surface_t *surf, nxmenu_state_t *state, const int *visible, int visible_count )
{
    const nxgui_palette_t *pal = &nxgui_palette_wii;
    int visible_rows, i;

    nxgui_clear( surf, pal->bg );
    {
        nxgui_rect_t bar = { 0, 0, surf->width, 80 };
        nxgui_fill_rounded_rect( surf, bar, 0, pal->panel );
        nxgui_draw_text( surf, 40, 28, 2, "WINE-NX", pal->text );
        if (state->favorites_filter) nxgui_draw_text( surf, surf->width - 220, 30, 1, "FAVORITES ONLY", pal->accent );
    }

    if (visible_count == 0)
    {
        const char *msg = "NO GAMES YET - PRESS + TO ADD ONE";
        int tw = nxgui_text_width( msg, 2 );
        nxgui_draw_text( surf, (surf->width - tw) / 2, surf->height / 2, 2, msg, pal->text_dim );
        return;
    }

    visible_rows = (surf->height - WII_TOP - 70 + WII_GAP) / (WII_CELL_H + WII_GAP);
    if (visible_rows < 1) visible_rows = 1;

    for (i = 0; i < visible_count; i++)
    {
        int row = i / WII_COLS;
        const nxlib_game_t *g;
        int focused;
        nxgui_rect_t cell, white_box, icon_rect;

        if (row < state->scroll_row || row >= state->scroll_row + visible_rows) continue;
        cell = wii_cell_rect( i, state->scroll_row, surf->width );
        g = &state->games[visible[i]];
        focused = (visible[i] == state->selected);

        nxgui_fill_rounded_rect( surf, cell, 16, pal->panel );
        if (focused) nxgui_stroke_rounded_rect( surf, cell, 16, 5, pal->accent );   /* soft blue glow */

        white_box.x = cell.x + 14; white_box.y = cell.y + 14; white_box.w = cell.w - 28; white_box.h = cell.h - 54;
        nxgui_fill_rounded_rect( surf, white_box, 12, pal->panel_raised );

        icon_rect.w = 96; icon_rect.h = 96;
        icon_rect.x = white_box.x + (white_box.w - icon_rect.w) / 2;
        icon_rect.y = white_box.y + 6;

        nxlib_ensure_icon( (nxlib_game_t *)g );
        if (g->icon_valid)
            nxgui_draw_image( surf, icon_rect.x, icon_rect.y, icon_rect.w, icon_rect.h, g->icon.pixels, g->icon.width, g->icon.height );
        else
        {
            char letter[2]; int tw;
            letter[0] = g->name[0] ? g->name[0] : '?'; letter[1] = 0;
            nxgui_fill_rounded_rect( surf, icon_rect, 12, g->primary_color );
            tw = nxgui_text_width( letter, 3 );
            nxgui_draw_text( surf, icon_rect.x + (icon_rect.w - tw) / 2, icon_rect.y + (icon_rect.h - nxgui_text_height(3)) / 2, 3, letter, pal->text );
        }

        {
            int tw = nxgui_text_width( g->name, 1 );
            int tx = white_box.x + (white_box.w - tw) / 2;
            if (tw > white_box.w) nxmlib_draw_text_fit( surf, white_box.x + 4, icon_rect.y + icon_rect.h + 10, 1, g->name, white_box.w - 8, pal->text );
            else nxgui_draw_text( surf, tx, icon_rect.y + icon_rect.h + 10, 1, g->name, pal->text );
        }
    }

    nxgui_draw_text( surf, 40, surf->height - 30, 1, "D-PAD MOVE  A LAUNCH  - OVERRIDE  R DETAILS  X THEME  Y FAVORITES  + MENU", pal->text_dim );
}

/* ------------------------------------------------------------------ */
/* public dispatch                                                      */
/* ------------------------------------------------------------------ */

nxmenu_action_t nxmenu_library_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input )
{
    int visible[NXMENU_MAX_GAMES];
    int visible_count;
    nxmenu_action_t action;

    if (input->button_x)
    {
        state->theme = (nxmenu_theme_id)(((int)state->theme + 1) % NXMENU_THEME_COUNT);
        nxlib_theme_save_global( (int)state->theme );
    }
    if (input->button_y) state->favorites_filter = !state->favorites_filter;

    visible_count = nxmlib_build_visible( state, visible );

    switch (state->theme)
    {
    case NXMENU_THEME_PS5:  action = nav_theme_ps5( state, surf, visible, visible_count, input ); break;
    case NXMENU_THEME_XBOX: action = nav_theme_xbox( state, surf, visible, visible_count, input ); break;
    case NXMENU_THEME_PS3:  action = nav_theme_ps3( state, surf, visible, visible_count, input ); break;
    case NXMENU_THEME_WII:  action = nav_theme_wii( state, surf, visible, visible_count, input ); break;
    case NXMENU_THEME_SWITCH:
    default:                action = nav_theme_switch( state, surf, visible, visible_count, input ); break;
    }
    if (action != NXMENU_ACTION_NONE) return action;

    nxmenu_library_draw_only( surf, state );
    return NXMENU_ACTION_NONE;
}

void nxmenu_library_draw_only( nxgui_surface_t *surf, nxmenu_state_t *state )
{
    int visible[NXMENU_MAX_GAMES];
    int visible_count = nxmlib_build_visible( state, visible );

    switch (state->theme)
    {
    case NXMENU_THEME_PS5:  render_theme_ps5( surf, state, visible, visible_count ); break;
    case NXMENU_THEME_XBOX: render_theme_xbox( surf, state, visible, visible_count ); break;
    case NXMENU_THEME_PS3:  render_theme_ps3( surf, state, visible, visible_count ); break;
    case NXMENU_THEME_WII:  render_theme_wii( surf, state, visible, visible_count ); break;
    case NXMENU_THEME_SWITCH:
    default:                render_theme_switch( surf, state, visible, visible_count ); break;
    }
}
