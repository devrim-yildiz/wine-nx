/* See nxmenu_mod_manager.h. NOT YET HARDWARE-VALIDATED. */
#include "nxmenu_mod_manager.h"

#include <stdio.h>

typedef struct { unsigned int flag; const char *label; } mod_def_t;

#define MOD_COUNT 4
static const mod_def_t mod_defs[MOD_COUNT] =
{
    { NXLIB_MOD_60FPS_PATCH,    "60FPS PATCH" },
    { NXLIB_MOD_WIDESCREEN_FIX, "WIDESCREEN FIX" },
    { NXLIB_MOD_SKIP_INTRO,     "SKIP INTRO" },
    { NXLIB_MOD_HD_TEXTURES,    "HD TEXTURE PACK" },
};

#define MOD_ROW_TOP  140
#define MOD_ROW_H     64
#define MOD_ROW_X     80

nxmenu_action_t nxmenu_mod_manager_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input )
{
    nxlib_game_t *g;
    int i, y;

    if (state->game_count == 0) { state->screen = NXMENU_SCREEN_LIBRARY; return NXMENU_ACTION_NONE; }
    g = &state->games[state->selected];

    if (input->nav_up && state->settings_field > 0) state->settings_field--;
    if (input->nav_down && state->settings_field < MOD_COUNT - 1) state->settings_field++;
    if (input->confirm)
    {
        g->mod_flags ^= mod_defs[state->settings_field].flag;
        g->has_active_mods = (g->mod_flags != 0);
        nxlib_save( state->games, state->game_count );
    }
    if (input->cancel) { state->screen = NXMENU_SCREEN_GAME_DETAIL; return NXMENU_ACTION_NONE; }

    nxgui_clear( surf, NXGUI_COLOR_BG );
    {
        nxgui_rect_t bar = { 0, 0, surf->width, 72 };
        char title[192];
        nxgui_fill_rect( surf, bar, NXGUI_COLOR_PANEL );
        snprintf( title, sizeof(title), "MOD MANAGER - %s", g->name );
        nxgui_draw_text( surf, 40, 24, 2, title, NXGUI_COLOR_TEXT );
    }

    y = MOD_ROW_TOP;
    for (i = 0; i < MOD_COUNT; i++)
    {
        int focused = (i == state->settings_field);
        int checked = (g->mod_flags & mod_defs[i].flag) != 0;
        nxgui_rect_t row = { MOD_ROW_X, y, surf->width - 2 * MOD_ROW_X, MOD_ROW_H - 8 };

        nxgui_fill_rect( surf, row, focused ? NXGUI_COLOR_PANEL_RAISED : NXGUI_COLOR_PANEL );
        if (focused) nxgui_stroke_rect( surf, row, 3, NXGUI_COLOR_ACCENT );
        nxgui_draw_toggle( surf, row.x + 16, row.y + (row.h - NXGUI_TOGGLE_SIZE) / 2, checked, NXGUI_COLOR_GOOD, NXGUI_COLOR_TEXT_DIM );
        nxgui_draw_text( surf, row.x + 16 + NXGUI_TOGGLE_SIZE + 20, row.y + (row.h - nxgui_text_height(2)) / 2, 2,
                          mod_defs[i].label, focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
        y += MOD_ROW_H;
    }

    {
        nxgui_rect_t hint = { 0, surf->height - 56, surf->width, 56 };
        nxgui_fill_rect( surf, hint, NXGUI_COLOR_PANEL );
        nxgui_draw_text( surf, 40, surf->height - 40, 1, "UP/DOWN SELECT  A TOGGLE  B BACK TO DETAIL", NXGUI_COLOR_TEXT_DIM );
    }

    return NXMENU_ACTION_NONE;
}
