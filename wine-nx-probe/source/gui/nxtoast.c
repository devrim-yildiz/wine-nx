/* See nxtoast.h. NOT YET HARDWARE-VALIDATED. */
#include "nxtoast.h"

#include <stdio.h>
#include <string.h>

#define NXTOAST_SLIDE_IN_FRAMES   15
#define NXTOAST_HOLD_FRAMES      150
#define NXTOAST_SLIDE_OUT_FRAMES  20
#define NXTOAST_LIFETIME_FRAMES  (NXTOAST_SLIDE_IN_FRAMES + NXTOAST_HOLD_FRAMES + NXTOAST_SLIDE_OUT_FRAMES)

#define NXTOAST_CARD_W 340
#define NXTOAST_CARD_H  56
#define NXTOAST_CARD_GAP 10
#define NXTOAST_MARGIN   40

void nxtoast_init( nxtoast_state_t *state )
{
    memset( state, 0, sizeof(*state) );
}

void nxtoast_push( nxtoast_state_t *state, const char *message )
{
    int i, oldest = 0, oldest_age = -1;

    for (i = 0; i < NXTOAST_MAX; i++)
    {
        if (!state->slots[i].active)
        {
            snprintf( state->slots[i].message, sizeof(state->slots[i].message), "%s", message );
            state->slots[i].active = 1;
            state->slots[i].age_frames = 0;
            return;
        }
        if (state->slots[i].age_frames > oldest_age) { oldest_age = state->slots[i].age_frames; oldest = i; }
    }

    /* every slot busy -- evict whichever is furthest into its own
     * lifetime (closest to disappearing on its own anyway) */
    snprintf( state->slots[oldest].message, sizeof(state->slots[oldest].message), "%s", message );
    state->slots[oldest].active = 1;
    state->slots[oldest].age_frames = 0;
}

void nxtoast_frame( nxtoast_state_t *state, nxgui_surface_t *surf )
{
    int i, visual_index = 0;

    for (i = 0; i < NXTOAST_MAX; i++)
    {
        nxtoast_slot_t *slot = &state->slots[i];
        int rest_x, x, y;
        nxgui_rect_t card;

        if (!slot->active) continue;

        /* Top-LEFT corner, deliberately not top-right: nxmenu_quickmenu.c's
         * slide-out panel occupies the right edge of the screen when
         * open, and toasts need to stay visible (not overlapped) even
         * while it's up. */
        rest_x = NXTOAST_MARGIN;
        y = NXTOAST_MARGIN + visual_index * (NXTOAST_CARD_H + NXTOAST_CARD_GAP);

        if (slot->age_frames < NXTOAST_SLIDE_IN_FRAMES)
        {
            float t = (float)slot->age_frames / (float)NXTOAST_SLIDE_IN_FRAMES;
            x = -NXTOAST_CARD_W + (int)(t * (float)(rest_x + NXTOAST_CARD_W));
        }
        else if (slot->age_frames < NXTOAST_SLIDE_IN_FRAMES + NXTOAST_HOLD_FRAMES)
        {
            x = rest_x;
        }
        else if (slot->age_frames < NXTOAST_LIFETIME_FRAMES)
        {
            float t = (float)(slot->age_frames - NXTOAST_SLIDE_IN_FRAMES - NXTOAST_HOLD_FRAMES) / (float)NXTOAST_SLIDE_OUT_FRAMES;
            x = rest_x - (int)(t * (float)(rest_x + NXTOAST_CARD_W));
        }
        else
        {
            slot->active = 0;
            continue;
        }

        card.x = x; card.y = y; card.w = NXTOAST_CARD_W; card.h = NXTOAST_CARD_H;
        nxgui_fill_rounded_rect( surf, card, 10, NXGUI_COLOR_PANEL_RAISED );
        nxgui_stroke_rounded_rect( surf, card, 10, 2, NXGUI_COLOR_ACCENT );
        nxgui_draw_text( surf, card.x + 16, card.y + (card.h - nxgui_text_height(1)) / 2, 1, slot->message, NXGUI_COLOR_TEXT );

        slot->age_frames++;
        visual_index++;
    }
}
