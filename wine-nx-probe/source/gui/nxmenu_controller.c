/* See nxmenu_controller.h. Uses the fixed NXGUI_COLOR_* palette, not
 * the per-theme nxgui_palette_* -- like every other non-library
 * screen, this one isn't part of the 5-theme system.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#include "nxmenu_controller.h"

#include <stdio.h>

#define CTRL_FOCUS_COUNT (NXLIB_CTRL_BUTTON_COUNT + 3)
#define CTRL_FOCUS_DEADZONE    NXLIB_CTRL_BUTTON_COUNT
#define CTRL_FOCUS_SENSITIVITY (NXLIB_CTRL_BUTTON_COUNT + 1)
#define CTRL_FOCUS_QUICKMENU   (NXLIB_CTRL_BUTTON_COUNT + 2)

#define CTRL_DEADZONE_MIN 0
#define CTRL_DEADZONE_MAX 16000
#define CTRL_SENSITIVITY_MIN 4
#define CTRL_SENSITIVITY_MAX 60

static const char *button_label( int button )
{
    switch (button)
    {
    case NXLIB_CTRL_BUTTON_A:     return "A";
    case NXLIB_CTRL_BUTTON_B:     return "B";
    case NXLIB_CTRL_BUTTON_X:     return "X";
    case NXLIB_CTRL_BUTTON_Y:     return "Y";
    case NXLIB_CTRL_BUTTON_L:     return "L";
    case NXLIB_CTRL_BUTTON_R:     return "R";
    case NXLIB_CTRL_BUTTON_ZL:    return "ZL";
    case NXLIB_CTRL_BUTTON_ZR:    return "ZR";
    case NXLIB_CTRL_BUTTON_PLUS:  return "+";
    case NXLIB_CTRL_BUTTON_MINUS: return "-";
    default:                      return "?";
    }
}

static const char *action_label( int action )
{
    switch (action)
    {
    case NXLIB_CTRL_ACTION_CONFIRM:     return "CONFIRM";
    case NXLIB_CTRL_ACTION_CANCEL:      return "CANCEL";
    case NXLIB_CTRL_ACTION_MENU:        return "MENU";
    case NXLIB_CTRL_ACTION_OVERRIDE:    return "OVERRIDE";
    case NXLIB_CTRL_ACTION_DETAILS:     return "DETAILS";
    case NXLIB_CTRL_ACTION_THEME_CYCLE: return "THEME CYCLE";
    case NXLIB_CTRL_ACTION_FAVORITES:   return "FAVORITES";
    case NXLIB_CTRL_ACTION_DEBUG_CHORD: return "DEBUG CHORD";
    case NXLIB_CTRL_ACTION_NONE:        return "NONE";
    default:                            return "?";
    }
}

static const char *shoulder_label( int shoulder )
{
    switch (shoulder)
    {
    case NXLIB_SHOULDER_L:  return "L";
    case NXLIB_SHOULDER_R:  return "R";
    case NXLIB_SHOULDER_ZL: return "ZL";
    case NXLIB_SHOULDER_ZR: return "ZR";
    default:                return "?";
    }
}

/* Fixed positions on the drawn gamepad body (body rect below, roughly
 * centered in the surface). Shared by input (touch hit-test) and
 * drawing so they can't disagree, same reasoning as every zone_rect-
 * style helper in nxmenu_library.c. */
static void zone_rect( int button, nxgui_rect_t *out )
{
    int cx = 890, cy = 300, d = 55, s = 56;
    switch (button)
    {
    case NXLIB_CTRL_BUTTON_X: out->x = cx - s / 2;     out->y = cy - d - s / 2; out->w = s; out->h = s; break;
    case NXLIB_CTRL_BUTTON_Y: out->x = cx - d - s / 2; out->y = cy - s / 2;     out->w = s; out->h = s; break;
    case NXLIB_CTRL_BUTTON_A: out->x = cx + d - s / 2; out->y = cy - s / 2;     out->w = s; out->h = s; break;
    case NXLIB_CTRL_BUTTON_B: out->x = cx - s / 2;     out->y = cy + d - s / 2; out->w = s; out->h = s; break;
    case NXLIB_CTRL_BUTTON_L:     out->x = 300; out->y = 150; out->w = 140; out->h = 34; break;
    case NXLIB_CTRL_BUTTON_R:     out->x = 840; out->y = 150; out->w = 140; out->h = 34; break;
    case NXLIB_CTRL_BUTTON_ZL:    out->x = 300; out->y = 106; out->w = 140; out->h = 34; break;
    case NXLIB_CTRL_BUTTON_ZR:    out->x = 840; out->y = 106; out->w = 140; out->h = 34; break;
    case NXLIB_CTRL_BUTTON_MINUS: out->x = 555; out->y = 220; out->w = 60;  out->h = 32; break;
    case NXLIB_CTRL_BUTTON_PLUS:  out->x = 665; out->y = 220; out->w = 60;  out->h = 32; break;
    default: out->x = 0; out->y = 0; out->w = 0; out->h = 0; break;
    }
}

nxmenu_action_t nxmenu_controller_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input )
{
    int i;

    if (input->nav_up && state->settings_field > 0) state->settings_field--;
    if (input->nav_down && state->settings_field < CTRL_FOCUS_COUNT - 1) state->settings_field++;

    if (state->settings_field < NXLIB_CTRL_BUTTON_COUNT)
    {
        int dir = input->nav_right ? 1 : (input->nav_left ? -1 : 0);
        if (dir)
        {
            int v = state->ctrl_actions[state->settings_field] + dir;
            if (v < 0) v = NXLIB_CTRL_ACTION_COUNT - 1;
            if (v >= NXLIB_CTRL_ACTION_COUNT) v = 0;
            state->ctrl_actions[state->settings_field] = v;
            nxlib_controller_map_save_global( state->ctrl_actions );
        }
        for (i = 0; i < NXLIB_CTRL_BUTTON_COUNT; i++)
        {
            nxgui_rect_t r;
            zone_rect( i, &r );
            if (input->pointer_released && input->pointer_is_touch && nxgui_rect_contains( r, input->pointer_x, input->pointer_y ))
                state->settings_field = i;
        }
    }
    else if (state->settings_field == CTRL_FOCUS_DEADZONE)
    {
        if (input->nav_left || input->nav_right)
        {
            state->ctrl_deadzone += input->nav_right ? 200 : -200;
            if (state->ctrl_deadzone < CTRL_DEADZONE_MIN) state->ctrl_deadzone = CTRL_DEADZONE_MIN;
            if (state->ctrl_deadzone > CTRL_DEADZONE_MAX) state->ctrl_deadzone = CTRL_DEADZONE_MAX;
            nxinput_set_deadzone( state->ctrl_deadzone );
            nxlib_controller_deadzone_save_global( state->ctrl_deadzone );
        }
    }
    else if (state->settings_field == CTRL_FOCUS_SENSITIVITY)
    {
        if (input->nav_left || input->nav_right)
        {
            state->ctrl_sensitivity += input->nav_right ? 2 : -2;
            if (state->ctrl_sensitivity < CTRL_SENSITIVITY_MIN) state->ctrl_sensitivity = CTRL_SENSITIVITY_MIN;
            if (state->ctrl_sensitivity > CTRL_SENSITIVITY_MAX) state->ctrl_sensitivity = CTRL_SENSITIVITY_MAX;
            nxinput_set_cursor_speed( state->ctrl_sensitivity );
            nxlib_controller_sensitivity_save_global( state->ctrl_sensitivity );
        }
    }
    else /* CTRL_FOCUS_QUICKMENU */
    {
        if (input->nav_left || input->nav_right)
        {
            int v = state->ctrl_quickmenu_trigger + (input->nav_right ? 1 : -1);
            if (v < 0) v = NXLIB_SHOULDER_COUNT - 1;
            if (v >= NXLIB_SHOULDER_COUNT) v = 0;
            state->ctrl_quickmenu_trigger = v;
            nxlib_quickmenu_trigger_save_global( v );
        }
    }

    if (input->cancel || input->menu) { state->screen = NXMENU_SCREEN_LIBRARY; return NXMENU_ACTION_NONE; }

    /* ---- draw ---- */
    nxgui_clear( surf, NXGUI_COLOR_BG );
    {
        nxgui_rect_t bar = { 0, 0, surf->width, 72 };
        nxgui_fill_rect( surf, bar, NXGUI_COLOR_PANEL );
        nxgui_draw_text( surf, 40, 24, 2, "CONTROLLER MAPPING STUDIO", NXGUI_COLOR_TEXT );
    }

    {
        nxgui_rect_t body = { 280, 190, 720, 280 };
        nxgui_rect_t stick_l = { 400, 250, 90, 90 };
        nxgui_rect_t stick_r = { 700, 320, 90, 90 };
        nxgui_fill_rounded_rect( surf, body, 70, NXGUI_COLOR_PANEL );
        nxgui_fill_rounded_rect( surf, stick_l, 45, NXGUI_COLOR_PANEL_RAISED );
        nxgui_stroke_rounded_rect( surf, stick_l, 45, 3, NXGUI_COLOR_BORDER );
        nxgui_fill_rounded_rect( surf, stick_r, 45, NXGUI_COLOR_PANEL_RAISED );
        nxgui_stroke_rounded_rect( surf, stick_r, 45, 3, NXGUI_COLOR_BORDER );
    }

    for (i = 0; i < NXLIB_CTRL_BUTTON_COUNT; i++)
    {
        nxgui_rect_t r;
        int focused = (i == state->settings_field);
        int radius;
        int tw;

        zone_rect( i, &r );
        radius = (r.w == r.h) ? r.w / 2 : 8;   /* face buttons are circular (w==h); shoulders/+/- are pill-ish rects */
        nxgui_fill_rounded_rect( surf, r, radius, focused ? NXGUI_COLOR_ACCENT_DIM : NXGUI_COLOR_PANEL_RAISED );
        if (focused) nxgui_stroke_rounded_rect( surf, r, radius, 3, NXGUI_COLOR_ACCENT );
        tw = nxgui_text_width( button_label( i ), 1 );
        nxgui_draw_text( surf, r.x + (r.w - tw) / 2, r.y + (r.h - nxgui_text_height(1)) / 2, 1, button_label( i ),
                          focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
    }

    {
        char status[128];
        snprintf( status, sizeof(status), "%s -> %s  (STORED -- NOT YET ENFORCED, SEE NX_DISCORD.H-STYLE NOTE IN NXLIB.H)",
                  button_label( state->settings_field < NXLIB_CTRL_BUTTON_COUNT ? state->settings_field : 0 ),
                  action_label( state->settings_field < NXLIB_CTRL_BUTTON_COUNT ? state->ctrl_actions[state->settings_field] : 0 ) );
        if (state->settings_field < NXLIB_CTRL_BUTTON_COUNT)
            nxgui_draw_text( surf, 280, 490, 1, status, NXGUI_COLOR_TEXT_DIM );
    }

    {
        int y = 500;
        float dz01 = (float)(state->ctrl_deadzone - CTRL_DEADZONE_MIN) / (float)(CTRL_DEADZONE_MAX - CTRL_DEADZONE_MIN);
        float sn01 = (float)(state->ctrl_sensitivity - CTRL_SENSITIVITY_MIN) / (float)(CTRL_SENSITIVITY_MAX - CTRL_SENSITIVITY_MIN);
        char buf[64];
        int focused;

        focused = (state->settings_field == CTRL_FOCUS_DEADZONE);
        snprintf( buf, sizeof(buf), "STICK DEADZONE: %d", state->ctrl_deadzone );
        nxgui_draw_text( surf, 280, y, 1, buf, focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
        nxgui_draw_slider( surf, 280, y + 30, surf->width - 560, dz01, NXGUI_COLOR_PANEL_RAISED,
                           focused ? NXGUI_COLOR_ACCENT : NXGUI_COLOR_BORDER, NXGUI_COLOR_ACCENT );

        y += 64;
        focused = (state->settings_field == CTRL_FOCUS_SENSITIVITY);
        snprintf( buf, sizeof(buf), "CURSOR SENSITIVITY: %d PX/FRAME", state->ctrl_sensitivity );
        nxgui_draw_text( surf, 280, y, 1, buf, focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
        nxgui_draw_slider( surf, 280, y + 30, surf->width - 560, sn01, NXGUI_COLOR_PANEL_RAISED,
                           focused ? NXGUI_COLOR_ACCENT : NXGUI_COLOR_BORDER, NXGUI_COLOR_ACCENT );

        y += 64;
        focused = (state->settings_field == CTRL_FOCUS_QUICKMENU);
        snprintf( buf, sizeof(buf), "QUICK MENU: HOLD %s + MINUS", shoulder_label( state->ctrl_quickmenu_trigger ) );
        nxgui_draw_text( surf, 280, y, 1, buf, focused ? NXGUI_COLOR_ACCENT : NXGUI_COLOR_TEXT_DIM );
    }

    {
        nxgui_rect_t hint = { 0, surf->height - 56, surf->width, 56 };
        nxgui_fill_rect( surf, hint, NXGUI_COLOR_PANEL );
        nxgui_draw_text( surf, 40, surf->height - 40, 1, "UP/DOWN SELECT  LEFT/RIGHT ADJUST  B BACK", NXGUI_COLOR_TEXT_DIM );
    }

    return NXMENU_ACTION_NONE;
}
