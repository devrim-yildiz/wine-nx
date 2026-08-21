/* nxmenu_library: the theme-switchable library grid -- 5 fully
 * independent layout engines (Switch/PS5/Xbox/PS3/Wii), each with its
 * own navigation math and its own drawing, selected via
 * nxmenu_state_t::theme. See nxmenu_library.c's own header comment for
 * what "5 independent" actually means here and its honest limits.
 */
#ifndef WINE_NX_MENU_LIBRARY_H
#define WINE_NX_MENU_LIBRARY_H

#include "nxmenu.h"

/* Full library-screen frame: builds the (favorites-filtered) visible
 * list, handles X/Y (theme cycle / favorites toggle) once regardless of
 * theme, then dispatches to the active theme's own nav+render pair.
 * Same contract as every other *_frame-shaped function in this project:
 * consumes `input`, mutates `state`, draws into `surf`, returns what the
 * caller should do next. */
nxmenu_action_t nxmenu_library_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input );

/* Draws the current theme WITHOUT touching input or state -- used as
 * the base layer under the TOPMENU overlay (nxmenu.c), the same role
 * the old single-theme draw_library() played before this file existed. */
void nxmenu_library_draw_only( nxgui_surface_t *surf, nxmenu_state_t *state );

#endif /* WINE_NX_MENU_LIBRARY_H */
