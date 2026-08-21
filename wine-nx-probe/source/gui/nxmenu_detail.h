/* nxmenu_detail: the per-game hero/detail view -- artwork + mock perf
 * stats on the left 40%, a small option list (launch, mod manager,
 * backend/bitness override, resolution scaling) on the right 60%.
 * Entered from any library theme via R (nxinput's dbg_r edge, reused
 * for this -- see nxmenu_library.c's own nav functions; it doesn't
 * conflict with the debug chord, which needs all four shoulder buttons
 * together, not R alone).
 */
#ifndef WINE_NX_MENU_DETAIL_H
#define WINE_NX_MENU_DETAIL_H

#include "nxmenu.h"

nxmenu_action_t nxmenu_detail_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input );

#endif /* WINE_NX_MENU_DETAIL_H */
