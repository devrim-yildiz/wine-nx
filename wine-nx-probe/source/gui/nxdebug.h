/* nxdebug: the launcher's hidden debug overlay, opened/closed by holding
 * ZR and pressing Minus. This replaces an earlier L+ZL+R+ZR 4-button
 * design (matching the user's original spec: "a debug system which i
 * can make with l1 l2 r1 r2" -- L/ZL/R/ZR are what those four shoulder
 * buttons are actually called on a Joy-Con/Pro Controller; Switch has no
 * "L1/L2/R1/R2" of its own) -- that design had a real bug, not just a
 * naming one: R already means "open this game's detail screen" in
 * nxmenu_library.c, and a rolling-window multi-frame chord doesn't
 * report itself as "consumed" until the FULL combo completes, so the
 * very first press of R (accumulating toward the chord) leaked straight
 * through to nxmenu_frame() and navigated away before the other three
 * buttons were even down. A two-button held-modifier design closes that
 * hole structurally rather than by picking different buttons and hoping:
 * the modifier and Minus together are evaluated and toggled in one
 * atomic step (see nxdebug_update() below), so `visible` and this
 * function's "consumed" return value flip together, on the exact same
 * frame -- there's no multi-frame partial state for a lone button press
 * to leak out of. That still leaves Minus's OTHER meaning (override_game,
 * opens the per-game override screen) to guard against, which works out
 * for the same reason: launcher_main.c substitutes an all-zero input into
 * nxmenu_frame() on any frame this function reports as consumed, and
 * consumption starts on the very frame the combo fires, not one frame
 * later.
 *
 * ZR, not L: L was the first replacement tried and it froze the launcher
 * on real hardware (all navigation stopped responding). That wasn't
 * actually L's fault -- see this file's own nxdebug_draw() comment for
 * the real cause, a separate bug that ALSO explains why the overlay
 * never became visible in the first place. ZR was picked over L for this
 * follow-up anyway, since L has no such issue but ZL -- the more obvious
 * "L2-flavored" choice -- is nxlib.h's own NXLIB_SHOULDER_DEFAULT for
 * the quick-menu trigger; ZL+Minus would therefore collide with that
 * combo out of the box for every user who hasn't customized it. ZR has
 * no fixed meaning anywhere in this launcher (see nxlib.c's controller
 * map defaults, NXLIB_CTRL_ACTION_NONE) and isn't any control's default
 * trigger, so there is nothing left for it to collide with.
 *
 * Deliberately decoupled from nxmenu.h -- it knows nothing about the
 * library/settings screen state machine, it just draws a log panel on
 * top of whatever's already on screen. task #28's auto-test mode reuses
 * nxdebug_log() as its one shared place to report "8/8 passed" and
 * per-test failures; nothing else about this module is
 * test-mode-specific.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#ifndef WINE_NX_DEBUG_H
#define WINE_NX_DEBUG_H

#include "nxgui.h"
#include "nxinput.h"

#define NXDEBUG_MAX_LOG_LINES 64
#define NXDEBUG_LOG_LINE_MAX  96

typedef struct
{
    int visible;

    char log_lines[NXDEBUG_MAX_LOG_LINES][NXDEBUG_LOG_LINE_MAX];
    int log_count;   /* valid lines so far, caps at NXDEBUG_MAX_LOG_LINES */
    int log_next;    /* ring-buffer write cursor */
    int scroll;      /* logical index (0 = oldest ever kept) of the newest line currently shown at the bottom of the panel */
} nxdebug_state_t;

void nxdebug_init( nxdebug_state_t *state );

/* Call exactly once per frame, every frame, regardless of visibility --
 * the ZR+Minus combo detector needs to see every frame's input to work
 * at all. When the overlay is visible, this also handles its own
 * scrolling (nav_up/nav_down) and closing (Cancel/B).
 *
 * Returns 1 if the overlay is visible and just consumed this frame's
 * input (nav/confirm/cancel) for itself, 0 otherwise. The caller (the
 * launcher's main loop) should skip feeding this same nxinput_state_t
 * to nxmenu_frame() on any frame this returns 1, the same way a modal
 * dialog would steal input focus from whatever's behind it -- nxdebug.c
 * has no way to suppress nxmenu's input handling itself, since the two
 * modules don't know about each other.
 *
 * Deliberately does NOT draw -- call nxdebug_draw() separately, and
 * LATER in the frame, after nxmenu_frame() (see that function's own
 * comment for why the split exists at all). */
int nxdebug_update( nxdebug_state_t *state, const nxinput_state_t *input );

/* Draws the overlay on top of `surf` if (and only if) it's currently
 * visible -- a no-op otherwise, so callers don't need their own
 * state->visible check first.
 *
 * MUST be called after whatever else draws into `surf` this frame
 * (nxmenu_frame() and friends), not before. nxmenu_frame() redraws the
 * ENTIRE surface every single frame regardless of input (framebufferBegin's
 * buffer isn't guaranteed to retain the previous frame's contents -- see
 * launcher_main.c's own comment on that), so calling this before it would
 * mean nxmenu_frame() immediately paints over whatever this function just
 * drew, in the very same frame. That was a real, hardware-confirmed bug
 * in this launcher: nxdebug_update()+draw used to be one combined call,
 * invoked before nxmenu_frame(), so the overlay was toggled on
 * (input correctly stopped reaching nxmenu_frame(), which is exactly why
 * the screen also stopped responding to navigation) but its own drawing
 * was erased before framebufferEnd() ever presented it -- from the
 * player's seat, indistinguishable from a freeze: the screen looks
 * unchanged, nothing you press does anything, and there's no visible
 * overlay to explain why. B still worked to close it the whole time
 * (nxdebug_update() reads the real, unsuppressed input), there was just
 * no way to know that without seeing this comment. */
void nxdebug_draw( const nxdebug_state_t *state, nxgui_surface_t *surf );

/* Appends one printf-style line to the on-screen debug log (a
 * fixed-size ring buffer -- once full, the oldest line is silently
 * dropped to make room, same as a scrolling console). The formatted
 * line is truncated at NXDEBUG_LOG_LINE_MAX-1 chars. Auto-scrolls the
 * panel to the newest line. Safe to call whether or not the overlay is
 * currently visible -- logging and visibility are independent; nothing
 * is lost by logging while it's closed. */
void nxdebug_log( nxdebug_state_t *state, const char *fmt, ... );

#endif /* WINE_NX_DEBUG_H */
