/* See nxmenu.h for the overall navigation model (flat -- every non-
 * library screen's Cancel returns straight to the library grid). This
 * file is pure application logic on top of nxgui/nxinput/nxlib: no
 * libnx calls of its own, so it's exercised the same way those modules
 * were -- compiled against the real toolchain -- but the actual screen
 * flow has only been reasoned through, not run on a device yet.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#include "nxmenu.h"
#include "nxmenu_library.h"
#include "nxmenu_detail.h"
#include "nxmenu_mod_manager.h"
#include "nxmenu_controller.h"
#include "nx_discord.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* layout constants -- tuned for the fixed 1280x720 launcher surface   */
/* (see nxgui.h's own header comment for why that's the fixed target)  */
/* ------------------------------------------------------------------ */

/* The old uniform library grid (NXMENU_CELL_W/H, grid_cell_rect, etc.)
 * lived here before nxmenu_library.c's 5 independent theme engines
 * replaced it -- only NXMENU_GRID_BOTTOM_MARGIN survives, still used by
 * the file browser's own list layout below. */
#define NXMENU_GRID_BOTTOM_MARGIN 72

#define NXMENU_LIST_TOP        168
#define NXMENU_LIST_MARGIN_X   64
#define NXMENU_ROW_H            48

/* ------------------------------------------------------------------ */
/* small label / layout helpers                                        */
/* ------------------------------------------------------------------ */

static const char *backend_label( nxlib_backend_t b )
{
    switch (b)
    {
    case NXLIB_BACKEND_VULKAN: return "VULKAN";
    case NXLIB_BACKEND_ZINK:   return "ZINK/GL";
    case NXLIB_BACKEND_D3D:    return "D3D";
    case NXLIB_BACKEND_GDI:    return "GDI";
    default:                   return "AUTO";
    }
}

static const char *bitness_label( nxlib_bitness_mode_t m )
{
    switch (m)
    {
    case NXLIB_BITNESS_32: return "32-BIT";
    case NXLIB_BITNESS_64: return "64-BIT";
    default:                return "AUTO";
    }
}

static nxlib_backend_t cycle_backend( nxlib_backend_t b, int dir )
{
    int v = (int)b + dir;
    if (v < 0) v = NXLIB_BACKEND_GDI;
    if (v > NXLIB_BACKEND_GDI) v = NXLIB_BACKEND_AUTO;
    return (nxlib_backend_t)v;
}

static nxlib_bitness_mode_t cycle_bitness( nxlib_bitness_mode_t m, int dir )
{
    int v = (int)m + dir;
    if (v < 0) v = NXLIB_BITNESS_64;
    if (v > NXLIB_BITNESS_64) v = NXLIB_BITNESS_AUTO;
    return (nxlib_bitness_mode_t)v;
}

static int list_visible_rows( int surf_h )
{
    int rows = (surf_h - NXMENU_LIST_TOP - NXMENU_GRID_BOTTOM_MARGIN) / NXMENU_ROW_H;
    return rows < 1 ? 1 : rows;
}

static nxgui_rect_t list_row_rect( int visible_index, int surf_w )
{
    nxgui_rect_t r;
    r.x = NXMENU_LIST_MARGIN_X;
    r.y = NXMENU_LIST_TOP + visible_index * NXMENU_ROW_H;
    r.w = surf_w - 2 * NXMENU_LIST_MARGIN_X;
    r.h = NXMENU_ROW_H - 4;
    return r;
}

static void refresh_browse( nxmenu_state_t *state )
{
    int n = nxlib_browse_dir( state->browse_dir, state->browse_entries );
    state->browse_count = n < 0 ? 0 : n;
    state->browse_selected = 0;
    state->browse_scroll = 0;
}

static void browse_go_up( nxmenu_state_t *state )
{
    const char *root = nxlib_drive_c_root();
    char *slash;

    if (!strcmp( state->browse_dir, root )) { state->screen = NXMENU_SCREEN_LIBRARY; return; }

    slash = strrchr( state->browse_dir, '/' );
    if (slash && (size_t)(slash - state->browse_dir) >= strlen( root ))
        *slash = 0;
    else
        snprintf( state->browse_dir, sizeof(state->browse_dir), "%s", root );

    refresh_browse( state );
}

/* ------------------------------------------------------------------ */
/* drawing                                                              */
/* ------------------------------------------------------------------ */

static void draw_header( nxgui_surface_t *surf, const char *title )
{
    nxgui_rect_t bar = { 0, 0, surf->width, 72 };
    nxgui_fill_rect( surf, bar, NXGUI_COLOR_PANEL );
    nxgui_draw_text( surf, 40, 24, 2, title, NXGUI_COLOR_TEXT );
}

static void draw_hint_bar( nxgui_surface_t *surf, const char *text )
{
    nxgui_rect_t bar = { 0, surf->height - 56, surf->width, 56 };
    nxgui_fill_rect( surf, bar, NXGUI_COLOR_PANEL );
    nxgui_draw_text( surf, 40, surf->height - 40, 1, text, NXGUI_COLOR_TEXT_DIM );
}

static void draw_option_row( nxgui_surface_t *surf, int y, const char *label, const char *value, int focused )
{
    nxgui_rect_t row = { 120, y, surf->width - 240, 56 };
    uint32_t fg = focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM;
    char line[96];

    nxgui_fill_rect( surf, row, focused ? NXGUI_COLOR_PANEL_RAISED : NXGUI_COLOR_PANEL );
    if (focused) nxgui_stroke_rect( surf, row, 3, NXGUI_COLOR_ACCENT );
    snprintf( line, sizeof(line), "%s: %s", label, value );
    nxgui_draw_text( surf, row.x + 24, row.y + (row.h - nxgui_text_height(2)) / 2, 2, line, fg );
    if (focused)
    {
        nxgui_draw_text( surf, row.x + row.w - 64, row.y + (row.h - nxgui_text_height(2)) / 2, 2, "<", NXGUI_COLOR_ACCENT );
        nxgui_draw_text( surf, row.x + row.w - 32, row.y + (row.h - nxgui_text_height(2)) / 2, 2, ">", NXGUI_COLOR_ACCENT );
    }
}

static void draw_topmenu_overlay( nxmenu_state_t *state, nxgui_surface_t *surf )
{
    static const char *items[4] = { "ADD GAME", "SETTINGS", "CONTROLLER", "EXIT" };
    nxgui_rect_t panel;
    int i;

    panel.w = 360;
    panel.h = 4 * 64 + 40;
    panel.x = surf->width - panel.w - 40;
    panel.y = 88;

    nxgui_fill_rect( surf, panel, NXGUI_COLOR_PANEL_RAISED );
    nxgui_stroke_rect( surf, panel, 3, NXGUI_COLOR_BORDER );

    for (i = 0; i < 4; i++)
    {
        nxgui_rect_t row;
        int focused = (i == state->topmenu_selected);

        row.x = panel.x + 16;
        row.y = panel.y + 20 + i * 64;
        row.w = panel.w - 32;
        row.h = 56;

        if (focused)
        {
            nxgui_fill_rect( surf, row, NXGUI_COLOR_ACCENT_DIM );
            nxgui_stroke_rect( surf, row, 2, NXGUI_COLOR_ACCENT );
        }
        nxgui_draw_text( surf, row.x + 20, row.y + (row.h - nxgui_text_height(2)) / 2, 2, items[i],
                          focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
    }
}

static void draw_browse( nxmenu_state_t *state, nxgui_surface_t *surf )
{
    const char *root = nxlib_drive_c_root();
    const char *rel = state->browse_dir + strlen( root );
    char shown[600];
    int visible_rows = list_visible_rows( surf->height );
    int i;
    char *p;

    nxgui_clear( surf, NXGUI_COLOR_BG );
    draw_header( surf, "SELECT AN EXE" );

    snprintf( shown, sizeof(shown), "C:%s", rel[0] ? rel : "\\" );
    for (p = shown; *p; p++) if (*p == '/') *p = '\\';
    nxgui_draw_text( surf, 40, 84, 1, shown, NXGUI_COLOR_TEXT_DIM );

    if (state->browse_count == 0)
    {
        nxgui_draw_text( surf, NXMENU_LIST_MARGIN_X, NXMENU_LIST_TOP, 1, "(EMPTY)", NXGUI_COLOR_TEXT_DIM );
    }
    else
    {
        for (i = 0; i < visible_rows; i++)
        {
            int idx = state->browse_scroll + i;
            nxlib_browse_entry_t *e;
            nxgui_rect_t r;
            int focused;
            char label[300];

            if (idx >= state->browse_count) break;
            e = &state->browse_entries[idx];
            focused = (idx == state->browse_selected);
            r = list_row_rect( i, surf->width );

            if (focused)
            {
                nxgui_fill_rect( surf, r, NXGUI_COLOR_PANEL_RAISED );
                nxgui_stroke_rect( surf, r, 2, NXGUI_COLOR_ACCENT );
            }
            snprintf( label, sizeof(label), "%s%s", e->is_dir ? "[DIR] " : "", e->name );
            nxgui_draw_text( surf, r.x + 16, r.y + (r.h - nxgui_text_height(1)) / 2, 1, label,
                              focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
        }
    }

    draw_hint_bar( surf, "A OPEN/ADD   B UP A LEVEL" );
}

static void draw_game_override( nxmenu_state_t *state, nxgui_surface_t *surf )
{
    nxlib_game_t *g;
    nxgui_rect_t row;
    int y = NXMENU_LIST_TOP;
    int focused;

    if (state->game_count == 0) return;
    g = &state->games[state->selected];

    nxgui_clear( surf, NXGUI_COLOR_BG );
    draw_header( surf, g->name );

    draw_option_row( surf, y, "OVERRIDE", g->has_override ? "ON" : "OFF", state->settings_field == 0 ); y += 64;
    draw_option_row( surf, y, "BACKEND", backend_label( g->has_override ? g->override.backend : state->global_settings.backend ), state->settings_field == 1 ); y += 64;
    draw_option_row( surf, y, "BITNESS", bitness_label( g->has_override ? g->override.bitness_mode : state->global_settings.bitness_mode ), state->settings_field == 2 ); y += 64;

    focused = (state->settings_field == 3);
    row.x = 120;
    row.y = y;
    row.w = surf->width - 240;
    row.h = 56;
    nxgui_fill_rect( surf, row, focused ? NXGUI_COLOR_BAD : NXGUI_COLOR_PANEL );
    if (focused) nxgui_stroke_rect( surf, row, 3, NXGUI_COLOR_ACCENT );
    nxgui_draw_text( surf, row.x + 24, row.y + (row.h - nxgui_text_height(2)) / 2, 2, "REMOVE FROM LIBRARY", NXGUI_COLOR_TEXT );

    draw_hint_bar( surf, "LEFT/RIGHT CHANGE   A TOGGLE/CONFIRM   B BACK" );
}

static void draw_global_settings( nxmenu_state_t *state, nxgui_surface_t *surf )
{
    int y = NXMENU_LIST_TOP;
    nxgui_rect_t card, avatar;
    const char *state_line;
    int connected, tw;

    nxgui_clear( surf, NXGUI_COLOR_BG );
    draw_header( surf, "SETTINGS" );
    draw_option_row( surf, y, "DEFAULT BACKEND", backend_label( state->global_settings.backend ), state->settings_field == 0 ); y += 64;
    draw_option_row( surf, y, "DEFAULT BITNESS", bitness_label( state->global_settings.bitness_mode ), state->settings_field == 1 ); y += 64;
    draw_option_row( surf, y, "DISCORD RICH PRESENCE", state->discord_rpc_enabled ? "ON" : "OFF", state->settings_field == 2 ); y += 64;
    draw_option_row( surf, y, "DEBUG OVERLAY", "PRESS A TO OPEN", state->settings_field == 3 ); y += 80;

    /* Rich Presence card mockup -- see nx_discord.h for why "NOT
     * CONNECTED" is the only honest badge here whether the toggle
     * above is on or off; nothing on Horizon OS is listening for this
     * yet, only the relay design that file describes would change
     * that. */
    card.x = 120; card.y = y; card.w = surf->width - 240; card.h = 140;
    nxgui_fill_rounded_rect( surf, card, 12, NXGUI_COLOR_PANEL_RAISED );
    nxgui_stroke_rounded_rect( surf, card, 12, 2, NXGUI_COLOR_BORDER );

    avatar.x = card.x + 20; avatar.y = card.y + 20; avatar.w = 96; avatar.h = 96;
    nxgui_fill_rounded_rect( surf, avatar, 48, NXGUI_COLOR_ACCENT );
    tw = nxgui_text_width( "W", 5 );
    nxgui_draw_text( surf, avatar.x + (avatar.w - tw) / 2, avatar.y + (avatar.h - nxgui_text_height(5)) / 2, 5, "W", NXGUI_COLOR_TEXT );

    state_line = (state->game_count > 0) ? state->games[state->selected].name : "IN THE LAUNCHER";
    nxgui_draw_text( surf, card.x + 140, card.y + 20, 2, "WINE-NX USER", NXGUI_COLOR_TEXT );
    nxgui_draw_text( surf, card.x + 140, card.y + 52, 1, "PLAYING WINE-NX", NXGUI_COLOR_TEXT_DIM );
    nxgui_draw_text( surf, card.x + 140, card.y + 74, 1, state_line, NXGUI_COLOR_TEXT_DIM );

    connected = nx_discord_is_connected();
    {
        const char *badge = connected ? "CONNECTED" : "NOT CONNECTED";
        int btw = nxgui_text_width( badge, 1 );
        nxgui_draw_text( surf, card.x + card.w - btw - 20, card.y + 20, 1, badge, connected ? NXGUI_COLOR_GOOD : NXGUI_COLOR_BAD );
    }

    draw_hint_bar( surf, "LEFT/RIGHT CHANGE   A TOGGLE/OPEN   B BACK" );
}

/* ------------------------------------------------------------------ */
/* input                                                                */
/* ------------------------------------------------------------------ */

/* Shared DATA-selection step every theme renderer (nxmenu_library.c,
 * task #33) starts from -- which games are currently in scope, not how
 * they're arranged on screen. This is deliberately NOT layout/rendering
 * logic (that stays fully independent per theme, per spec), just the
 * favorites_filter membership test every theme needs identically.
 * Fills `out` with up to NXMENU_MAX_GAMES indices into state->games[]
 * and returns the count. */
static nxmenu_action_t handle_topmenu_input( nxmenu_state_t *state, const nxinput_state_t *input )
{
    if (input->nav_up && state->topmenu_selected > 0) state->topmenu_selected--;
    if (input->nav_down && state->topmenu_selected < 3) state->topmenu_selected++;

    if (input->confirm)
    {
        switch (state->topmenu_selected)
        {
        case 0:
            snprintf( state->browse_dir, sizeof(state->browse_dir), "%s", nxlib_drive_c_root() );
            refresh_browse( state );
            state->screen = NXMENU_SCREEN_BROWSE;
            break;
        case 1:
            state->settings_field = 0;
            state->screen = NXMENU_SCREEN_GLOBAL_SETTINGS;
            break;
        case 2:
            state->settings_field = 0;
            state->screen = NXMENU_SCREEN_CONTROLLER_STUDIO;
            break;
        case 3:
            return NXMENU_ACTION_QUIT;
        }
    }
    if (input->cancel || input->menu) state->screen = NXMENU_SCREEN_LIBRARY;
    return NXMENU_ACTION_NONE;
}

static nxmenu_action_t handle_browse_input( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input )
{
    int visible_rows = list_visible_rows( surf->height );
    int activate = input->confirm && state->browse_count > 0;
    int i;

    if (input->nav_up && state->browse_selected > 0) state->browse_selected--;
    if (input->nav_down && state->browse_selected + 1 < state->browse_count) state->browse_selected++;

    for (i = 0; i < visible_rows; i++)
    {
        int idx = state->browse_scroll + i;
        nxgui_rect_t r;
        if (idx >= state->browse_count) break;
        r = list_row_rect( i, surf->width );
        if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( r, input->pointer_x, input->pointer_y ))
        {
            state->browse_selected = idx;
            activate = 1;
        }
    }

    if (activate)
    {
        nxlib_browse_entry_t *e = &state->browse_entries[state->browse_selected];
        if (e->is_dir)
        {
            size_t len = strlen( state->browse_dir );
            snprintf( state->browse_dir + len, sizeof(state->browse_dir) - len, "/%s", e->name );
            refresh_browse( state );
        }
        else
        {
            char full[NXLIB_PATH_MAX];
            snprintf( full, sizeof(full), "%s/%s", state->browse_dir, e->name );
            if (nxlib_add_game( state->games, &state->game_count, NXMENU_MAX_GAMES, full ))
            {
                nxlib_save( state->games, state->game_count );
                state->selected = state->game_count - 1;
            }
            state->screen = NXMENU_SCREEN_LIBRARY;
        }
    }
    else if (input->cancel) browse_go_up( state );

    if (state->browse_selected < state->browse_scroll) state->browse_scroll = state->browse_selected;
    if (state->browse_selected >= state->browse_scroll + visible_rows) state->browse_scroll = state->browse_selected - visible_rows + 1;
    if (state->browse_scroll < 0) state->browse_scroll = 0;

    return NXMENU_ACTION_NONE;
}

static nxmenu_action_t handle_game_override_input( nxmenu_state_t *state, const nxinput_state_t *input )
{
    nxlib_game_t *g;
    int dir, changed = 0;

    if (state->game_count == 0) { state->screen = NXMENU_SCREEN_LIBRARY; return NXMENU_ACTION_NONE; }
    g = &state->games[state->selected];
    dir = input->nav_right ? 1 : (input->nav_left ? -1 : 0);

    if (input->nav_up && state->settings_field > 0) state->settings_field--;
    if (input->nav_down && state->settings_field < 3) state->settings_field++;

    switch (state->settings_field)
    {
    case 0:
        if (input->confirm) { g->has_override = !g->has_override; changed = 1; }
        break;
    case 1:
        if (dir)
        {
            if (!g->has_override) g->override.backend = state->global_settings.backend;
            g->override.backend = cycle_backend( g->override.backend, dir );
            g->has_override = 1;
            changed = 1;
        }
        break;
    case 2:
        if (dir)
        {
            if (!g->has_override) g->override.bitness_mode = state->global_settings.bitness_mode;
            g->override.bitness_mode = cycle_bitness( g->override.bitness_mode, dir );
            g->has_override = 1;
            changed = 1;
        }
        break;
    case 3:
        if (input->confirm)
        {
            nxlib_remove_game( state->games, &state->game_count, state->selected );
            if (state->selected >= state->game_count) state->selected = state->game_count - 1;
            if (state->selected < 0) state->selected = 0;
            state->screen = NXMENU_SCREEN_LIBRARY;
            changed = 1;
        }
        break;
    }
    if (changed) nxlib_save( state->games, state->game_count );

    if (input->cancel || input->override_game) state->screen = NXMENU_SCREEN_LIBRARY;
    return NXMENU_ACTION_NONE;
}

static nxmenu_action_t handle_global_settings_input( nxmenu_state_t *state, const nxinput_state_t *input )
{
    int dir = input->nav_right ? 1 : (input->nav_left ? -1 : 0);
    int changed = 0;

    if (input->nav_up && state->settings_field > 0) state->settings_field--;
    if (input->nav_down && state->settings_field < 3) state->settings_field++;

    if (state->settings_field == 3)
    {
        if (input->confirm) return NXMENU_ACTION_OPEN_DEBUG;
    }
    else if (state->settings_field == 2)
    {
        if (input->confirm)
        {
            state->discord_rpc_enabled = !state->discord_rpc_enabled;
            nxlib_discord_enabled_save_global( state->discord_rpc_enabled );
        }
    }
    else if (dir)
    {
        if (state->settings_field == 0) state->global_settings.backend = cycle_backend( state->global_settings.backend, dir );
        else state->global_settings.bitness_mode = cycle_bitness( state->global_settings.bitness_mode, dir );
        changed = 1;
    }
    if (changed) nxlib_settings_save_global( &state->global_settings );

    if (input->cancel || input->menu) state->screen = NXMENU_SCREEN_LIBRARY;
    return NXMENU_ACTION_NONE;
}

/* ------------------------------------------------------------------ */
/* public API                                                           */
/* ------------------------------------------------------------------ */

void nxmenu_init( nxmenu_state_t *state )
{
    memset( state, 0, sizeof(*state) );
    state->screen = NXMENU_SCREEN_LIBRARY;
    state->game_count = nxlib_load( state->games, NXMENU_MAX_GAMES );
    nxlib_settings_load_global( &state->global_settings );
    state->theme = (nxmenu_theme_id)nxlib_theme_load_global();
    state->discord_rpc_enabled = nxlib_discord_enabled_load_global();
    nxlib_controller_map_load_global( state->ctrl_actions );
    state->ctrl_deadzone = nxlib_controller_deadzone_load_global();
    state->ctrl_sensitivity = nxlib_controller_sensitivity_load_global();
    state->ctrl_quickmenu_trigger = nxlib_quickmenu_trigger_load_global();
    /* NOT applied to nxinput.c here -- nxinput_init() runs separately
     * in launcher_main.c's own startup sequence, so that's where
     * nxinput_set_deadzone()/nxinput_set_cursor_speed() get called with
     * these same values (see task #38's wiring). */
}

/* Static field data for one mock entry -- primary/accent colors written
 * directly rather than hashed (nxlib_derive_display_meta is for real
 * games with real paths; these have synthetic ones, and hand-picked
 * colors give more useful visual variety across only 16 entries than a
 * hash would). Genre/length/machine-type spread is deliberate: see
 * nxmenu.h's own comment on why entry 13 has a long title and entry 15
 * is ARM64. */
typedef struct
{
    const char *name, *author;
    float playtime_hours;
    int last_played_timestamp;
    uint32_t primary_color, accent_color;
    int has_active_mods, is_favorite;
    unsigned short machine;
} nxmenu_mock_def_t;

static const nxmenu_mock_def_t nxmenu_mock_defs[16] =
{
    { "Half-Life 2",                                                   "Valve",              42.5f, 1752600000, NXGUI_RGB(0xb8,0x3a,0x2a), NXGUI_RGB(0xe0,0x6a,0x40), 1, 1, IMAGE_FILE_MACHINE_AMD64 },
    { "Doom Eternal",                                                  "id Software",         18.2f, 1751900000, NXGUI_RGB(0x8a,0x14,0x14), NXGUI_RGB(0xe0,0x30,0x20), 0, 0, IMAGE_FILE_MACHINE_AMD64 },
    { "Portal",                                                        "Valve",                7.8f, 1748300000, NXGUI_RGB(0x2a,0x8a,0x6a), NXGUI_RGB(0x50,0xe0,0xb0), 0, 1, IMAGE_FILE_MACHINE_I386  },
    { "Minecraft: Java Edition",                                       "Mojang",             210.3f, 1753100000, NXGUI_RGB(0x3a,0x7a,0x2a), NXGUI_RGB(0x70,0xc0,0x40), 1, 1, IMAGE_FILE_MACHINE_AMD64 },
    { "Stardew Valley",                                                "ConcernedApe",        88.0f, 1750200000, NXGUI_RGB(0xd8,0x9a,0x20), NXGUI_RGB(0xf0,0xc8,0x60), 1, 0, IMAGE_FILE_MACHINE_I386  },
    { "The Witcher 3: Wild Hunt",                                      "CD Projekt Red",     130.6f, 1752900000, NXGUI_RGB(0x6a,0x2a,0x8a), NXGUI_RGB(0xa0,0x50,0xe0), 0, 1, IMAGE_FILE_MACHINE_AMD64 },
    { "Celeste",                                                       "Extremely OK Games",  12.4f, 1747100000, NXGUI_RGB(0x2a,0x5a,0x8a), NXGUI_RGB(0x50,0x90,0xe0), 0, 0, IMAGE_FILE_MACHINE_I386  },
    { "Terraria",                                                      "Re-Logic",            65.9f, 1751500000, NXGUI_RGB(0x5a,0x8a,0x4a), NXGUI_RGB(0x9a,0xd0,0x70), 1, 1, IMAGE_FILE_MACHINE_I386  },
    { "Hollow Knight",                                                 "Team Cherry",         40.1f, 1749800000, NXGUI_RGB(0x3a,0x3a,0x4a), NXGUI_RGB(0x70,0x70,0x9a), 0, 0, IMAGE_FILE_MACHINE_AMD64 },
    { "OpenTTD",                                                       "OpenTTD Team",       300.7f, 1752200000, NXGUI_RGB(0x2a,0x6a,0x8a), NXGUI_RGB(0x50,0xb0,0xe0), 1, 0, IMAGE_FILE_MACHINE_AMD64 },
    { "Factorio",                                                      "Wube Software",       95.5f, 1753000000, NXGUI_RGB(0xd8,0x6a,0x20), NXGUI_RGB(0xf0,0x9a,0x50), 0, 1, IMAGE_FILE_MACHINE_AMD64 },
    { "Undertale",                                                     "tobyfox",              6.0f, 1745000000, NXGUI_RGB(0x8a,0x2a,0x2a), NXGUI_RGB(0xd8,0xd8,0xd8), 0, 0, IMAGE_FILE_MACHINE_I386  },
    { "Sid Meier's Civilization VI",                                   "Firaxis Games",      150.2f, 1751000000, NXGUI_RGB(0x2a,0x6a,0x4a), NXGUI_RGB(0xd8,0xb8,0x40), 0, 0, IMAGE_FILE_MACHINE_AMD64 },
    { "A Very Long Test Title That Should Truncate Nicely In The Grid","Test Author",          0.0f,          0, NXGUI_RGB(0x50,0x50,0x50), NXGUI_RGB(0x90,0x90,0x90), 0, 0, IMAGE_FILE_MACHINE_I386  },
    { "Cave Story+",                                                   "Nicalis",             15.3f, 1746400000, NXGUI_RGB(0x8a,0x5a,0x2a), NXGUI_RGB(0xe0,0xa0,0x50), 0, 0, IMAGE_FILE_MACHINE_I386  },
    { "ARM64 Native Test App",                                         "Wine-NX",              3.2f, 1753150000, NXGUI_RGB(0x4a,0x4a,0x8a), NXGUI_RGB(0x80,0x80,0xe0), 0, 0, IMAGE_FILE_MACHINE_ARM64 },
};

void nxmenu_inject_mock_games( nxmenu_state_t *state )
{
    int i;

    state->game_count = 16;
    state->selected = 0;
    state->scroll_row = 0;

    for (i = 0; i < 16; i++)
    {
        const nxmenu_mock_def_t *def = &nxmenu_mock_defs[i];
        nxlib_game_t *g = &state->games[i];

        memset( g, 0, sizeof(*g) );
        snprintf( g->path, sizeof(g->path), "mock:/%02d/%s.exe", i, def->name );
        snprintf( g->name, sizeof(g->name), "%s", def->name );
        snprintf( g->id, sizeof(g->id), "mock-%03d", i );
        snprintf( g->author, sizeof(g->author), "%s", def->author );
        g->machine = def->machine;
        g->playtime_hours = def->playtime_hours;
        g->last_played_timestamp = def->last_played_timestamp;
        g->primary_color = def->primary_color;
        g->accent_color = def->accent_color;
        g->has_active_mods = def->has_active_mods;
        g->is_favorite = def->is_favorite;
        g->resolution_scale_percent = 100;
        /* Give every has_active_mods mock entry actual mod_flags bits
         * (a varied subset, not all four) so the mod manager screen has
         * real pre-checked toggles to show, not just an empty-but-true
         * badge -- state, not literal flag-for-flag realism. */
        g->mod_flags = def->has_active_mods ? (NXLIB_MOD_60FPS_PATCH | NXLIB_MOD_HD_TEXTURES) : 0;
        /* icon_loaded stays 0 -- nxlib_ensure_icon will try the synthetic
         * "mock:/..." path, fail cleanly (fopen returns NULL), and every
         * screen already falls back to a placeholder letter-tile for
         * icon_valid==0, exactly like a real exe with no icon resource. */
    }
}

nxmenu_action_t nxmenu_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input )
{
    nxmenu_action_t action = NXMENU_ACTION_NONE;

    switch (state->screen)
    {
    case NXMENU_SCREEN_LIBRARY:         action = nxmenu_library_frame( state, surf, input ); break;
    case NXMENU_SCREEN_TOPMENU:         action = handle_topmenu_input( state, input ); break;
    case NXMENU_SCREEN_BROWSE:          action = handle_browse_input( state, surf, input ); break;
    case NXMENU_SCREEN_GAME_OVERRIDE:   action = handle_game_override_input( state, input ); break;
    case NXMENU_SCREEN_GLOBAL_SETTINGS: action = handle_global_settings_input( state, input ); break;
    /* nxmenu_detail_frame/nxmenu_mod_manager_frame are "frame"-shaped
     * (handle input AND draw in one call, like nxmenu_library_frame)
     * rather than the split handle_X_input/draw_X pattern the other
     * screens use -- they already draw internally, so unlike those
     * other screens they have no matching entry in the second (drawing)
     * switch below; that switch's own `default: break;` covers them. */
    case NXMENU_SCREEN_GAME_DETAIL:       action = nxmenu_detail_frame( state, surf, input ); break;
    case NXMENU_SCREEN_MOD_MANAGER:       action = nxmenu_mod_manager_frame( state, surf, input ); break;
    case NXMENU_SCREEN_CONTROLLER_STUDIO: action = nxmenu_controller_frame( state, surf, input ); break;
    }

    if (action != NXMENU_ACTION_NONE) return action;

    if (state->screen == NXMENU_SCREEN_LIBRARY || state->screen == NXMENU_SCREEN_TOPMENU)
        nxmenu_library_draw_only( surf, state );

    switch (state->screen)
    {
    case NXMENU_SCREEN_TOPMENU:         draw_topmenu_overlay( state, surf ); break;
    case NXMENU_SCREEN_BROWSE:          draw_browse( state, surf ); break;
    case NXMENU_SCREEN_GAME_OVERRIDE:   draw_game_override( state, surf ); break;
    case NXMENU_SCREEN_GLOBAL_SETTINGS: draw_global_settings( state, surf ); break;
    default: break;
    }

    return NXMENU_ACTION_NONE;
}
