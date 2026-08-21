#include "nxdebug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void nxdebug_init( nxdebug_state_t *state )
{
    memset( state, 0, sizeof(*state) );
}

void nxdebug_log( nxdebug_state_t *state, const char *fmt, ... )
{
    va_list ap;

    va_start( ap, fmt );
    vsnprintf( state->log_lines[state->log_next], NXDEBUG_LOG_LINE_MAX, fmt, ap );
    va_end( ap );

    state->log_next = (state->log_next + 1) % NXDEBUG_MAX_LOG_LINES;
    if (state->log_count < NXDEBUG_MAX_LOG_LINES) state->log_count++;
    state->scroll = state->log_count - 1;   /* auto-follow the newest line */
}

/* Maps a logical line index (0 = oldest line still kept) to its
 * physical ring-buffer slot -- needed because once the ring has
 * wrapped, log_lines[0] is no longer the oldest line, log_lines[log_next]
 * is. */
static const char *log_line_at( const nxdebug_state_t *state, int logical_index )
{
    int start = (state->log_count < NXDEBUG_MAX_LOG_LINES) ? 0 : state->log_next;
    int phys = (start + logical_index) % NXDEBUG_MAX_LOG_LINES;
    return state->log_lines[phys];
}

static void draw_debug_overlay( const nxdebug_state_t *state, nxgui_surface_t *surf )
{
    nxgui_rect_t panel;
    int line_h = nxgui_text_height( 1 ) + 4;
    int visible_lines, first_logical, i;

    panel.x = 40;
    panel.y = 40;
    panel.w = surf->width - 80;
    panel.h = surf->height - 80;

    nxgui_fill_rect( surf, panel, NXGUI_COLOR_PANEL );
    nxgui_stroke_rect( surf, panel, 3, NXGUI_COLOR_ACCENT );
    nxgui_draw_text( surf, panel.x + 16, panel.y + 12, 2, "DEBUG LOG", NXGUI_COLOR_TEXT );

    visible_lines = (panel.h - 80) / line_h;
    if (visible_lines < 1) visible_lines = 1;

    first_logical = state->scroll - visible_lines + 1;
    if (first_logical < 0) first_logical = 0;

    for (i = 0; i < visible_lines; i++)
    {
        int logical = first_logical + i;
        if (logical > state->scroll || logical >= state->log_count) break;
        nxgui_draw_text( surf, panel.x + 16, panel.y + 56 + i * line_h, 1, log_line_at( state, logical ), NXGUI_COLOR_TEXT_DIM );
    }

    nxgui_draw_text( surf, panel.x + 16, panel.y + panel.h - 28, 1,
                      "UP/DOWN SCROLL   A RUN DIAGNOSTICS   B CLOSE   HOLD ZR + MINUS TOGGLE", NXGUI_COLOR_TEXT_DIM );
}

int nxdebug_update( nxdebug_state_t *state, const nxinput_state_t *input )
{
    /* Held modifier (ZR) + a single edge (Minus), evaluated and toggled
     * in one step -- see this file's own header comment for why that
     * "atomic" shape, not a multi-frame rolling window, is what actually
     * fixes the leak the old 4-button chord had (a single button's press
     * reaching nxmenu_frame() before the rest of a slower multi-button
     * combo ever completed). */
    if (input->held_zr && input->override_game)
    {
        state->visible = !state->visible;
        nxdebug_log( state, state->visible ? "DEBUG OVERLAY ON" : "DEBUG OVERLAY OFF" );
    }

    if (!state->visible) return 0;

    if (input->nav_up && state->scroll > 0) state->scroll--;
    if (input->nav_down && state->scroll < state->log_count - 1) state->scroll++;
    if (input->cancel) state->visible = 0;

    return 1;
}

void nxdebug_draw( const nxdebug_state_t *state, nxgui_surface_t *surf )
{
    if (!state->visible) return;
    draw_debug_overlay( state, surf );
}
