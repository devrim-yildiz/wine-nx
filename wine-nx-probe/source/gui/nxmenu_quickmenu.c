/* See nxmenu_quickmenu.h.
 *
 * Brightness is mock, not real, unlike volume/battery next to it: the
 * real API (setsysGetBacklightSettings/setsysSetBacklightSettings)
 * takes a whole SetSysBacklightSettings struct this project hasn't
 * characterized field-by-field, versus volume/battery's simple float/
 * u32 APIs (audctlGetSystemOutputMasterVolume, psmGetBatteryChargePercentage)
 * which are safe to call blind. Guessing at an unfamiliar system-
 * settings struct's fields to write brightness for real risked writing
 * something unintended to real hardware state -- exactly the kind of
 * unverified change this project avoids elsewhere. Wiring it for real
 * is a small, well-scoped follow-up once that struct's fields are
 * actually read and understood, not attempted here.
 *
 * NOT YET HARDWARE-VALIDATED (including the parts that use real,
 * previously-unused-in-this-project libnx services -- psm/audctl have
 * never been exercised anywhere else in this codebase before now).
 */
#include "nxmenu_quickmenu.h"

#include <switch.h>
#include <stdio.h>
#include <string.h>

#define QUICKMENU_FOCUS_COUNT 4
#define QUICKMENU_FOCUS_VOLUME     0
#define QUICKMENU_FOCUS_BRIGHTNESS 1
#define QUICKMENU_FOCUS_BATTERY    2
#define QUICKMENU_FOCUS_THEME      3

static const char *theme_name( nxmenu_theme_id theme )
{
    switch (theme)
    {
    case NXMENU_THEME_PS5:  return "PS5";
    case NXMENU_THEME_XBOX: return "XBOX";
    case NXMENU_THEME_PS3:  return "PS3";
    case NXMENU_THEME_WII:  return "WII";
    case NXMENU_THEME_SWITCH:
    default: return "SWITCH";
    }
}

static int shoulder_held( const nxinput_state_t *input, int shoulder )
{
    switch (shoulder)
    {
    case NXLIB_SHOULDER_L:  return input->held_l;
    case NXLIB_SHOULDER_R:  return input->held_r;
    case NXLIB_SHOULDER_ZL: return input->held_zl;
    case NXLIB_SHOULDER_ZR: return input->held_zr;
    default:                return 0;
    }
}

void nxquickmenu_init( nxquickmenu_state_t *state )
{
    memset( state, 0, sizeof(*state) );
    state->psm_ready = R_SUCCEEDED( psmInitialize() );
    state->audctl_ready = R_SUCCEEDED( audctlInitialize() );
}

void nxquickmenu_shutdown( nxquickmenu_state_t *state )
{
    if (state->audctl_ready) audctlExit();
    if (state->psm_ready) psmExit();
}

int nxquickmenu_update( nxquickmenu_state_t *state, nxmenu_state_t *menu_state, nxtoast_state_t *toasts,
                        const nxinput_state_t *input )
{
    int combo_held, combo_edge;

    combo_held = shoulder_held( input, menu_state->ctrl_quickmenu_trigger ) && input->held_minus;
    combo_edge = combo_held && !state->combo_prev_held;
    state->combo_prev_held = combo_held;
    if (combo_edge) state->visible = !state->visible;

    if (!state->visible) return 0;

    if (input->nav_up && state->focus > 0) state->focus--;
    if (input->nav_down && state->focus < QUICKMENU_FOCUS_COUNT - 1) state->focus++;

    if (state->focus == QUICKMENU_FOCUS_VOLUME && state->audctl_ready && (input->nav_left || input->nav_right))
    {
        float volume = 0.0f;
        if (R_SUCCEEDED( audctlGetSystemOutputMasterVolume( &volume ) ))
        {
            volume += input->nav_right ? 0.05f : -0.05f;
            if (volume < 0.0f) volume = 0.0f;
            if (volume > 1.0f) volume = 1.0f;
            audctlSetSystemOutputMasterVolume( volume );
        }
    }
    else if (state->focus == QUICKMENU_FOCUS_THEME && (input->confirm || input->nav_left || input->nav_right))
    {
        int dir = input->nav_left ? -1 : 1;
        menu_state->theme = (nxmenu_theme_id)(((int)menu_state->theme + dir + NXMENU_THEME_COUNT) % NXMENU_THEME_COUNT);
        nxlib_theme_save_global( (int)menu_state->theme );
        {
            char msg[64];
            snprintf( msg, sizeof(msg), "THEME: %s", theme_name( menu_state->theme ) );
            nxtoast_push( toasts, msg );
        }
    }
    /* BRIGHTNESS/BATTERY: no input handling at all -- brightness is
     * display-only by design (see this file's own header comment),
     * battery is a read-only system value with nothing to adjust. */

    if (input->cancel || input->menu) { state->visible = 0; return 1; }

    return 1;
}

void nxquickmenu_draw( const nxquickmenu_state_t *state, const nxmenu_state_t *menu_state, nxgui_surface_t *surf )
{
    float volume = 0.0f;
    u32 battery = 0;
    int has_volume, has_battery;
    nxgui_rect_t panel;
    int y;

    if (!state->visible) return;

    has_volume = state->audctl_ready && R_SUCCEEDED( audctlGetSystemOutputMasterVolume( &volume ) );
    has_battery = state->psm_ready && R_SUCCEEDED( psmGetBatteryChargePercentage( &battery ) );

    /* ---- draw: slide-out panel along the right edge ---- */
    panel.w = 380; panel.h = surf->height; panel.x = surf->width - panel.w; panel.y = 0;
    nxgui_fill_rect( surf, panel, NXGUI_COLOR_PANEL_RAISED );
    nxgui_stroke_rect( surf, panel, 3, NXGUI_COLOR_BORDER );
    nxgui_draw_text( surf, panel.x + 24, 30, 2, "QUICK MENU", NXGUI_COLOR_TEXT );

    y = 100;
    {
        int focused = (state->focus == QUICKMENU_FOCUS_VOLUME);
        char buf[48];
        if (has_volume) snprintf( buf, sizeof(buf), "VOLUME: %d%%", (int)(volume * 100.0f) );
        else snprintf( buf, sizeof(buf), "VOLUME: N/A" );
        nxgui_draw_text( surf, panel.x + 24, y, 1, buf, focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
        nxgui_draw_slider( surf, panel.x + 24, y + 30, panel.w - 48, has_volume ? volume : 0.0f,
                           NXGUI_COLOR_PANEL, focused ? NXGUI_COLOR_ACCENT : NXGUI_COLOR_BORDER, NXGUI_COLOR_ACCENT );
    }

    y += 80;
    {
        int focused = (state->focus == QUICKMENU_FOCUS_BRIGHTNESS);
        nxgui_draw_text( surf, panel.x + 24, y, 1, "BRIGHTNESS (MOCK)", focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
        nxgui_draw_slider( surf, panel.x + 24, y + 30, panel.w - 48, 0.7f, NXGUI_COLOR_PANEL, NXGUI_COLOR_BORDER, NXGUI_COLOR_TEXT_DIM );
    }

    y += 80;
    {
        int focused = (state->focus == QUICKMENU_FOCUS_BATTERY);
        char buf[48];
        if (has_battery) snprintf( buf, sizeof(buf), "BATTERY: %u%%", (unsigned)battery );
        else snprintf( buf, sizeof(buf), "BATTERY: N/A" );
        nxgui_draw_text( surf, panel.x + 24, y, 1, buf, focused ? NXGUI_COLOR_TEXT : NXGUI_COLOR_TEXT_DIM );
    }

    y += 60;
    {
        int focused = (state->focus == QUICKMENU_FOCUS_THEME);
        char buf[48];
        snprintf( buf, sizeof(buf), "THEME: %s", theme_name( menu_state->theme ) );
        nxgui_draw_text( surf, panel.x + 24, y, 1, buf, focused ? NXGUI_COLOR_ACCENT : NXGUI_COLOR_TEXT_DIM );
    }

    nxgui_draw_text( surf, panel.x + 24, surf->height - 40, 1, "UP/DOWN SELECT  LEFT/RIGHT ADJUST  B CLOSE", NXGUI_COLOR_TEXT_DIM );
}
