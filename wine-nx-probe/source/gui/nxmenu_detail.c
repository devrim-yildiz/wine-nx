/* See nxmenu_detail.h. Uses the fixed NXGUI_COLOR_* palette (not the
 * per-theme nxgui_palette_*) -- this screen, like GAME_OVERRIDE/
 * GLOBAL_SETTINGS before it, isn't part of the 5-theme system, only
 * the library grid itself is.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#include "nxmenu_detail.h"

#include <stdio.h>
#include <string.h>

typedef enum
{
    DETAIL_ROW_LAUNCH = 0,
    DETAIL_ROW_MODS,
    DETAIL_ROW_OVERRIDE,
    DETAIL_ROW_RESOLUTION,
    DETAIL_ROW_COUNT
} detail_row_t;

#define DETAIL_LEFT_W   480
#define DETAIL_ROW_TOP  140
#define DETAIL_ROW_H     72

static void draw_row( nxgui_surface_t *surf, int x, int y, int w, const char *label, const char *value, int focused )
{
    nxgui_rect_t row = { x, y, w, DETAIL_ROW_H - 12 };
    int tw;

    nxgui_fill_rect( surf, row, focused ? NXGUI_COLOR_PANEL_RAISED : NXGUI_COLOR_PANEL );
    if (focused) nxgui_stroke_rect( surf, row, 3, NXGUI_COLOR_ACCENT );
    nxgui_draw_text( surf, row.x + 20, row.y + (row.h - nxgui_text_height(2)) / 2, 2, label,
                      focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
    if (value)
    {
        tw = nxgui_text_width( value, 2 );
        nxgui_draw_text( surf, row.x + row.w - tw - 20, row.y + (row.h - nxgui_text_height(2)) / 2, 2, value,
                          focused ? NXGUI_COLOR_ACCENT : NXGUI_COLOR_TEXT_DIM );
    }
}

nxmenu_action_t nxmenu_detail_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input )
{
    nxlib_game_t *g;
    int row_x, row_w, y, i;
    char resbuf[16];
    static const char *row_labels[DETAIL_ROW_COUNT] = { "LAUNCH VIA WINE-NX", "MANAGE MODS / PATCHES", "BACKEND / BITNESS OVERRIDE", "RESOLUTION SCALING" };

    if (state->game_count == 0) { state->screen = NXMENU_SCREEN_LIBRARY; return NXMENU_ACTION_NONE; }
    g = &state->games[state->selected];

    if (input->nav_up && state->settings_field > 0) state->settings_field--;
    if (input->nav_down && state->settings_field < DETAIL_ROW_COUNT - 1) state->settings_field++;

    if (state->settings_field == DETAIL_ROW_RESOLUTION && (input->nav_left || input->nav_right))
    {
        g->resolution_scale_percent += input->nav_right ? 5 : -5;
        if (g->resolution_scale_percent < 50) g->resolution_scale_percent = 50;
        if (g->resolution_scale_percent > 200) g->resolution_scale_percent = 200;
        nxlib_save( state->games, state->game_count );
    }

    if (input->confirm)
    {
        switch (state->settings_field)
        {
        case DETAIL_ROW_LAUNCH:
            snprintf( state->launch_path, sizeof(state->launch_path), "%s", g->path );
            nxlib_effective_settings( g, &state->launch_settings );
            state->launch_machine = g->machine;
            return NXMENU_ACTION_LAUNCH;
        case DETAIL_ROW_MODS:
            state->screen = NXMENU_SCREEN_MOD_MANAGER;
            state->settings_field = 0;
            break;
        case DETAIL_ROW_OVERRIDE:
            state->screen = NXMENU_SCREEN_GAME_OVERRIDE;
            state->settings_field = 0;
            break;
        default:
            break;
        }
    }
    if (state->screen != NXMENU_SCREEN_GAME_DETAIL) return NXMENU_ACTION_NONE;   /* just navigated away above */
    if (input->cancel) { state->screen = NXMENU_SCREEN_LIBRARY; return NXMENU_ACTION_NONE; }

    /* ---- draw ---- */
    nxgui_clear( surf, NXGUI_COLOR_BG );
    {
        nxgui_rect_t bar = { 0, 0, surf->width, 72 };
        nxgui_fill_rect( surf, bar, NXGUI_COLOR_PANEL );
        nxgui_draw_text( surf, 40, 24, 2, g->name, NXGUI_COLOR_TEXT );
    }

    /* Left 40%: artwork banner + mock perf stats. The stats are
     * deterministic (hashed off the game's own name/path), not random
     * per frame, but they are still entirely made up -- this project
     * has no actual memory/CPU profiler for guest processes -- labeled
     * plainly rather than presented as a real measurement. */
    {
        nxgui_rect_t banner = { 0, 72, DETAIL_LEFT_W, 360 };
        double ram_gb;
        static const char *loads[3] = { "LOW", "MODERATE", "HIGH" };
        char stat[96];

        nxgui_fill_rect( surf, banner, NXGUI_COLOR_PANEL );
        nxlib_ensure_icon( g );
        if (g->icon_valid)
        {
            int size = banner.h - 40;
            nxgui_draw_image( surf, banner.x + (banner.w - size) / 2, banner.y + 20, size, size, g->icon.pixels, g->icon.width, g->icon.height );
        }
        else
        {
            nxgui_rect_t art = { banner.x + 40, banner.y + 20, banner.w - 80, banner.h - 40 };
            char letter[2];
            int tw;
            letter[0] = g->name[0] ? g->name[0] : '?'; letter[1] = 0;
            nxgui_fill_rect( surf, art, g->primary_color );
            tw = nxgui_text_width( letter, 8 );
            nxgui_draw_text( surf, art.x + (art.w - tw) / 2, art.y + (art.h - nxgui_text_height(8)) / 2, 8, letter, NXGUI_COLOR_TEXT );
        }

        y = 72 + 360 + 20;
        nxgui_draw_text( surf, 40, y, 1, "SYSTEM ESTIMATE (MOCK -- NOT A REAL PROFILER)", NXGUI_COLOR_TEXT_DIM ); y += 28;
        ram_gb = 0.5 + (double)(strlen( g->name ) % 8) * 0.3;
        snprintf( stat, sizeof(stat), "ESTIMATED RAM: %.1fGB", ram_gb );
        nxgui_draw_text( surf, 40, y, 1, stat, NXGUI_COLOR_TEXT ); y += 24;
        snprintf( stat, sizeof(stat), "ESTIMATED CPU LOAD: %s", loads[strlen( g->name ) % 3] );
        nxgui_draw_text( surf, 40, y, 1, stat, NXGUI_COLOR_TEXT ); y += 24;
        nxgui_draw_text( surf, 40, y, 1, g->author, NXGUI_COLOR_TEXT_DIM ); y += 24;
        snprintf( stat, sizeof(stat), "PLAYTIME: %.1f HOURS", (double)g->playtime_hours );
        nxgui_draw_text( surf, 40, y, 1, stat, NXGUI_COLOR_TEXT_DIM );
    }

    row_x = DETAIL_LEFT_W + 40;
    row_w = surf->width - row_x - 40;
    y = DETAIL_ROW_TOP;
    for (i = 0; i < DETAIL_ROW_COUNT; i++)
    {
        const char *value = NULL;
        if (i == DETAIL_ROW_RESOLUTION) { snprintf( resbuf, sizeof(resbuf), "%d%%", g->resolution_scale_percent ); value = resbuf; }
        if (i == DETAIL_ROW_MODS) value = g->has_active_mods ? "ACTIVE" : "NONE";
        draw_row( surf, row_x, y, row_w, row_labels[i], value, i == state->settings_field );
        y += DETAIL_ROW_H;
    }

    {
        nxgui_rect_t hint = { 0, surf->height - 56, surf->width, 56 };
        nxgui_fill_rect( surf, hint, NXGUI_COLOR_PANEL );
        nxgui_draw_text( surf, 40, surf->height - 40, 1, "UP/DOWN SELECT  LEFT/RIGHT ADJUST  A CONFIRM  B BACK", NXGUI_COLOR_TEXT_DIM );
    }

    return NXMENU_ACTION_NONE;
}
