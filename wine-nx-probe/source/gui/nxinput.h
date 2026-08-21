/* nxinput: one merged input model for the launcher -- touch and joycon
 * are two ways to drive the exact same on-screen cursor/menu-nav state,
 * so every screen (nxgui.h call sites) hit-tests against ONE pointer
 * and reacts to ONE set of nav edges, never asking "was this a tap or
 * a stick nudge." Polled once per frame from the launcher's own main
 * loop (not a background thread like source/runtime.c's
 * wine_nx_hud_input_thread_fn) -- a GUI's own render loop already runs
 * at a fixed cadence, so sampling HID state inline keeps pointer output
 * and the frame it's drawn into trivially consistent with each other.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#ifndef WINE_NX_INPUT_H
#define WINE_NX_INPUT_H

typedef struct
{
    /* Unified pointer: a finger on the touch screen while one is down,
     * otherwise a virtual cursor nudged by the left stick (see
     * nxinput.c's own comment on why a cursor rather than pure d-pad
     * focus-hopping -- both a touch UI and a stick-driven UI need to
     * hit-test the exact same nxgui_rect_t regions). Coordinates are in
     * the same space as the nxgui_surface_t passed to nxinput_init --
     * i.e. already framebuffer pixels, no separate scaling needed at
     * call sites. */
    int pointer_x, pointer_y;
    int pointer_down;       /* held this frame (finger down, or A/stick-click held) */
    int pointer_pressed;    /* edge: became down this frame */
    int pointer_released;   /* edge: became up this frame */
    int pointer_is_touch;   /* 1 if driven by a real finger this frame, 0 if stick-cursor */

    /* Digital menu-navigation edges -- d-pad OR left-stick past a
     * deadzone, whichever the player is using; both drive the same
     * edges so grid/list navigation code only has one path to write. */
    int nav_up, nav_down, nav_left, nav_right;

    int confirm;   /* A -- edge */
    int cancel;    /* B -- edge */
    int menu;      /* + -- edge, opens/closes the top-level menu */
    int override_game; /* - -- edge, matches the user's spec: pressing
                         * minus on a selected library entry opens its
                         * per-game override screen. Also half of the
                         * debug overlay's ZR+Minus combo (see nxdebug.c) --
                         * that combo only fires while ZR is held, and
                         * launcher_main.c substitutes an all-zero input
                         * into nxmenu_frame() on any frame nxdebug_update()
                         * reports as consumed, so a Minus press that opens
                         * the debug overlay can't also open the per-game
                         * override screen underneath it. */
    int button_x;  /* X -- edge, library screen: cycle console theme */
    int button_y;  /* Y -- edge, library screen: toggle favorites-only filter */

    /* R -- edge, opens the selected game's detail screen (see
     * nxmenu_library.c's own nav functions / nxmenu_detail.h). Plain R,
     * no modifier -- unlike Minus above, nothing else in this launcher
     * currently wants a bare R press, so there's no collision to guard
     * against here. */
    int dbg_r;

    /* Level-triggered (held-this-frame) shoulder-button state -- exists
     * so a caller can build its OWN combo out of "whichever shoulder
     * button the user configured" (see nxmenu.h's quick-menu trigger
     * setting, task #37/#38) without this module hardcoding which one
     * that is. held_zr doubles as the debug overlay's own modifier (see
     * nxdebug.c) -- ZR was chosen there specifically because it has no
     * other single-button meaning anywhere in this launcher (unlike R)
     * and isn't any control's default trigger (unlike ZL, which is
     * nxlib.h's own NXLIB_SHOULDER_DEFAULT for the quick-menu combo
     * below), so there's nothing for it to collide with. */
    int held_l, held_zl, held_r, held_zr;
    int held_minus;   /* the other half of the quick-menu combo (configured shoulder + Minus) -- see nxmenu_quickmenu.c */

    /* Real analog ZL/ZR pressure, straight from libnx's own
     * padGetGcTriggerPos() (range 0-0x7FFF per that API's own
     * documented range -- not rescaled here, so a caller comparing
     * against the raw libnx docs sees the same numbers). Standard
     * Joy-Con/Pro Controller/handheld input -- the overwhelming
     * majority of real Switch play -- has no analog trigger hardware
     * at all, so this genuinely reads 0 there; that's an honest fact
     * about the controller, not a gap in this code. Only a GameCube-
     * style controller (official GC adapter, or a 3rd-party controller
     * Horizon happens to expose that way) reports real nonzero
     * pressure here. */
    unsigned int trigger_l_analog, trigger_r_analog;
} nxinput_state_t;

/* screen_w/screen_h must match the nxgui_surface_t dimensions every
 * screen is drawing into (source/runtime.c's WINE_NX_FB_W/H at the time
 * of writing) -- passed in explicitly rather than duplicated as a
 * second hardcoded constant, so the two files can't silently drift. */
void nxinput_init( int screen_w, int screen_h );
void nxinput_poll( nxinput_state_t *out );
void nxinput_exit( void );

/* Live-adjustable stick-cursor tuning for nxmenu_controller.c's
 * deadzone/sensitivity sliders -- unlike that screen's button-remap
 * zones (display + persist only, see its own header comment), these
 * two calls actually change nxinput_poll()'s own behavior starting
 * the very next call. `deadzone` is raw stick units, clamped to
 * [0, 32767]; `px_per_frame` is cursor speed at full stick deflection,
 * clamped to >= 1. */
void nxinput_set_deadzone( int deadzone );
void nxinput_set_cursor_speed( int px_per_frame );

#endif /* WINE_NX_INPUT_H */
