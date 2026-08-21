/* nxmenu_quickmenu: the slide-out Quick Access overlay -- volume
 * (real, via libnx's audctl), brightness (mock -- see this file's own
 * comment on why), battery percentage (real, via libnx's psm), and
 * theme swapping (real). Opened by holding the configured shoulder
 * button (nxlib.h's NXLIB_SHOULDER_*, set from the Controller Studio
 * screen, default ZL) together with Minus -- genuinely configurable,
 * not hardcoded to one specific button.
 */
#ifndef WINE_NX_MENU_QUICKMENU_H
#define WINE_NX_MENU_QUICKMENU_H

#include "nxmenu.h"
#include "nxtoast.h"

typedef struct
{
    int visible;
    int focus;              /* 0=volume, 1=brightness(display only), 2=battery(display only), 3=theme swap */
    int combo_prev_held;    /* edge-detects the trigger combo so holding it doesn't toggle every frame */
    int psm_ready;          /* psmInitialize() succeeded -- battery reads are skipped (shown as N/A) if not */
    int audctl_ready;       /* audctlInitialize() succeeded -- same idea for volume */
} nxquickmenu_state_t;

/* Initializes the psm/audctl service sessions this screen reads from --
 * call once at launcher startup, paired with nxquickmenu_shutdown() at
 * exit. Both are best-effort: if either service fails to initialize,
 * that one field just displays N/A instead of the whole screen
 * failing (matching this project's established "degrade gracefully"
 * pattern elsewhere, e.g. source/runtime.c's deko3d fallback). */
void nxquickmenu_init( nxquickmenu_state_t *state );
void nxquickmenu_shutdown( nxquickmenu_state_t *state );

/* Returns 1 if the overlay is visible and just consumed this frame's
 * input, 0 otherwise -- same contract as nxdebug_update (see that
 * file's own comment): the caller should feed a neutral/zeroed
 * nxinput_state_t to nxmenu_frame() on any frame this returns 1. Pushes
 * a toast (via `toasts`) confirming a theme swap, same as any other
 * user-visible state change this launcher wants to visibly confirm.
 *
 * Deliberately does NOT draw -- call nxquickmenu_draw() separately, and
 * LATER in the frame, after nxmenu_frame(). See nxdebug.h's own
 * nxdebug_draw() comment for exactly why: this screen used to be one
 * combined update+draw call too, called before nxmenu_frame(), and
 * nxmenu_frame()'s own unconditional full-surface redraw immediately
 * erased whatever this function had just drawn, every single frame --
 * the same bug, in the same shape, as nxdebug had. */
int nxquickmenu_update( nxquickmenu_state_t *state, nxmenu_state_t *menu_state, nxtoast_state_t *toasts,
                        const nxinput_state_t *input );

/* Draws the slide-out panel on top of `surf` if (and only if) it's
 * currently visible -- a no-op otherwise. MUST be called after
 * nxmenu_frame(), same requirement and same reason as nxdebug_draw(). */
void nxquickmenu_draw( const nxquickmenu_state_t *state, const nxmenu_state_t *menu_state, nxgui_surface_t *surf );

#endif /* WINE_NX_MENU_QUICKMENU_H */
