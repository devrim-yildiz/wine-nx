/* nxmenu_mod_manager: individual mock-patch toggles for the currently
 * selected game (state->selected), entered from nxmenu_detail.c's
 * "MANAGE MODS / PATCHES" row. See nxlib.h's NXLIB_MOD_* bits and their
 * own comment on what "mock" means here (the toggle state is real and
 * persists; no actual patch exists behind any of them yet).
 */
#ifndef WINE_NX_MENU_MOD_MANAGER_H
#define WINE_NX_MENU_MOD_MANAGER_H

#include "nxmenu.h"

nxmenu_action_t nxmenu_mod_manager_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input );

#endif /* WINE_NX_MENU_MOD_MANAGER_H */
