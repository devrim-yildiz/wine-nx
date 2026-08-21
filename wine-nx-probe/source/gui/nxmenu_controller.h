/* nxmenu_controller: the Controller Mapping Studio -- a drawn gamepad
 * with selectable button zones (cycle each one's displayed action) and
 * deadzone/sensitivity sliders. See nxlib.h's own comment on the
 * NXLIB_CTRL_BUTTON_* / NXLIB_CTRL_ACTION_* constants for exactly
 * what's real here (deadzone/sensitivity, wired live into nxinput.c)
 * versus stored-but-not-yet-enforced (the button->action assignment).
 */
#ifndef WINE_NX_MENU_CONTROLLER_H
#define WINE_NX_MENU_CONTROLLER_H

#include "nxmenu.h"

/* Call once, after nxlib_controller_deadzone_load_global()/
 * nxlib_controller_sensitivity_load_global() have been applied via
 * nxinput_set_deadzone()/nxinput_set_cursor_speed() -- this screen
 * reads its starting slider values back out of nxinput.c/nxlib.c
 * itself rather than caching its own copy, so there's nothing here to
 * separately initialize. */
nxmenu_action_t nxmenu_controller_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input );

#endif /* WINE_NX_MENU_CONTROLLER_H */
