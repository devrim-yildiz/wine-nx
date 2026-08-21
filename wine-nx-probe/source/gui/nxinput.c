/* HID plumbing here is deliberately the same calls already proven in
 * source/runtime.c (padConfigureInput/padInitializeDefault/padUpdate/
 * padGetButtonsDown for buttons, hidInitializeTouchScreen/
 * hidGetTouchScreenStates for touch, see wine_nx_hud_input_thread_fn
 * and wine_nx_touch_poll there) -- not re-derived from docs, copied
 * from code that's already known to build and run on this project.
 *
 * NOT YET HARDWARE-VALIDATED as a whole (the individual HID calls are
 * proven; this specific merge-touch-and-stick-into-one-cursor logic is
 * new).
 */
#include "nxinput.h"

#include <switch.h>
#include <string.h>

static PadState nxinput_pad;
static int nxinput_screen_w = 1280, nxinput_screen_h = 720;
static int nxinput_cursor_x, nxinput_cursor_y;
static int nxinput_prev_pointer_down;
static int nxinput_touch_ready;

/* Stick deadzone and cursor speed. HidAnalogStickState fields are
 * s32 in [-32768, 32767] (a raw JOYSTICK_MAX-normalized value, same
 * convention libnx uses everywhere else it exposes a stick). Speed is
 * in px/frame at full deflection, scaled linearly inside the deadzone
 * so a light nudge moves the cursor slowly for fine menu work.
 *
 * Runtime-adjustable (nxinput_set_deadzone/nxinput_set_cursor_speed),
 * not compile-time constants -- nxmenu_controller.c's deadzone/
 * sensitivity sliders call these directly, so unlike that screen's
 * button-remap zones (which only display and persist an assignment,
 * not yet consulted by the button-reading code below), these two
 * really do change this file's own behavior live. */
#define NXINPUT_STICK_MAX 32767
static int nxinput_deadzone = 3000;
static int nxinput_cursor_speed_px = 18;

void nxinput_set_deadzone( int deadzone )
{
    if (deadzone < 0) deadzone = 0;
    if (deadzone > NXINPUT_STICK_MAX) deadzone = NXINPUT_STICK_MAX;
    nxinput_deadzone = deadzone;
}

void nxinput_set_cursor_speed( int px_per_frame )
{
    if (px_per_frame < 1) px_per_frame = 1;
    nxinput_cursor_speed_px = px_per_frame;
}

/* Held-direction repeat for d-pad/stick menu navigation -- a single
 * frame-granular edge would make holding a direction feel unresponsive
 * (it wouldn't repeat at all, since padGetButtonsDown only fires once
 * per physical press) and a raw per-frame "held" signal would instead
 * fire every single frame (far too fast to be a discrete nav step).
 * This gives an initial edge, a pause, then a steady repeat -- the same
 * shape as OS key-repeat. */
#define NXINPUT_NAV_REPEAT_DELAY_FRAMES 18
#define NXINPUT_NAV_REPEAT_RATE_FRAMES  6

typedef struct { int held_frames; } nxinput_repeat_t;
static nxinput_repeat_t nxinput_rep_up, nxinput_rep_down, nxinput_rep_left, nxinput_rep_right;

static int nxinput_repeat_edge( nxinput_repeat_t *rep, int held )
{
    if (!held) { rep->held_frames = 0; return 0; }
    rep->held_frames++;
    if (rep->held_frames == 1) return 1;   /* initial press edge */
    if (rep->held_frames < NXINPUT_NAV_REPEAT_DELAY_FRAMES) return 0;
    return ((rep->held_frames - NXINPUT_NAV_REPEAT_DELAY_FRAMES) % NXINPUT_NAV_REPEAT_RATE_FRAMES) == 0;
}

void nxinput_init( int screen_w, int screen_h )
{
    nxinput_screen_w = screen_w;
    nxinput_screen_h = screen_h;
    nxinput_cursor_x = screen_w / 2;
    nxinput_cursor_y = screen_h / 2;

    /* NpadStandard alone already covers every controller shape the
     * user's own spec asked for -- confirmed against libnx's own
     * definition, not assumed: HidNpadStyleSet_NpadStandard =
     * NpadFullKey (Pro Controller) | NpadHandheld | NpadJoyDual |
     * NpadJoyLeft | NpadJoyRight, i.e. Pro Controller, handheld mode,
     * and dual OR single Joy-Cons, all in one bitmask. Genuine 3rd-party
     * HID controllers paired over Bluetooth get normalized into one of
     * these same styles by Horizon OS itself before this code ever sees
     * them -- there's no separate "3rd-party" path to add. NpadGc is
     * ADDED here (not part of Standard) specifically so
     * padGetGcTriggerPos() below can report real analog ZL/ZR pressure
     * when a GameCube-style controller is actually connected -- see its
     * own libnx doc comment: "NpadGc must have been previously
     * configured... for GC trigger data to be readable." */
    padConfigureInput( 1, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadGc );
    padInitializeDefault( &nxinput_pad );

    hidInitializeTouchScreen();
    nxinput_touch_ready = 1;

    memset( &nxinput_rep_up, 0, sizeof(nxinput_rep_up) );
    memset( &nxinput_rep_down, 0, sizeof(nxinput_rep_down) );
    memset( &nxinput_rep_left, 0, sizeof(nxinput_rep_left) );
    memset( &nxinput_rep_right, 0, sizeof(nxinput_rep_right) );
}

void nxinput_exit( void )
{
    /* Nothing to tear down: padInitializeDefault/hidInitializeTouchScreen
     * don't hand back a resource this module owns exclusively -- both are
     * shared HID service handles libnx itself manages for the process
     * lifetime. Kept as an explicit no-op entry point (rather than
     * omitted) so call sites don't need to know that, matching
     * nxgui.h's own init/no-teardown-needed shape. */
}

void nxinput_poll( nxinput_state_t *out )
{
    HidTouchScreenState touch = {0};
    HidAnalogStickState stick;
    u64 down, held;
    int dx, dy, mag2;
    int have_touch;

    memset( out, 0, sizeof(*out) );

    padUpdate( &nxinput_pad );
    down = padGetButtonsDown( &nxinput_pad );
    held = padGetButtons( &nxinput_pad );
    stick = padGetStickPos( &nxinput_pad, 0 );

    have_touch = nxinput_touch_ready && hidGetTouchScreenStates( &touch, 1 ) && touch.count > 0;

    if (have_touch)
    {
        out->pointer_x = touch.touches[0].x;
        out->pointer_y = touch.touches[0].y;
        out->pointer_down = 1;
        out->pointer_is_touch = 1;
        nxinput_cursor_x = out->pointer_x;
        nxinput_cursor_y = out->pointer_y;
    }
    else
    {
        /* No finger down -- the left stick drives a persistent virtual
         * cursor instead, so joycon-only play still gets a real pointer
         * (not just discrete focus-hopping) for hit-testing the same
         * nxgui_rect_t regions touch uses. A/ZL/ZR etc. never move this
         * cursor; only the stick does, so resting a thumb on the stick
         * without deflecting it can't cause drift. */
        dx = stick.x;
        dy = -stick.y;   /* HID's +y is up; screen +y is down */
        mag2 = dx * dx + dy * dy;
        if (mag2 > nxinput_deadzone * nxinput_deadzone)
        {
            nxinput_cursor_x += (dx * nxinput_cursor_speed_px) / NXINPUT_STICK_MAX;
            nxinput_cursor_y += (dy * nxinput_cursor_speed_px) / NXINPUT_STICK_MAX;
            if (nxinput_cursor_x < 0) nxinput_cursor_x = 0;
            if (nxinput_cursor_y < 0) nxinput_cursor_y = 0;
            if (nxinput_cursor_x >= nxinput_screen_w) nxinput_cursor_x = nxinput_screen_w - 1;
            if (nxinput_cursor_y >= nxinput_screen_h) nxinput_cursor_y = nxinput_screen_h - 1;
        }
        out->pointer_x = nxinput_cursor_x;
        out->pointer_y = nxinput_cursor_y;
        out->pointer_down = (held & HidNpadButton_A) ? 1 : 0;
        out->pointer_is_touch = 0;
    }

    out->pointer_pressed  = out->pointer_down && !nxinput_prev_pointer_down;
    out->pointer_released = !out->pointer_down && nxinput_prev_pointer_down;
    nxinput_prev_pointer_down = out->pointer_down;

    out->nav_up    = nxinput_repeat_edge( &nxinput_rep_up,    (held & (HidNpadButton_Up    | HidNpadButton_StickLUp))    != 0 );
    out->nav_down  = nxinput_repeat_edge( &nxinput_rep_down,  (held & (HidNpadButton_Down  | HidNpadButton_StickLDown))  != 0 );
    out->nav_left  = nxinput_repeat_edge( &nxinput_rep_left,  (held & (HidNpadButton_Left  | HidNpadButton_StickLLeft))  != 0 );
    out->nav_right = nxinput_repeat_edge( &nxinput_rep_right, (held & (HidNpadButton_Right | HidNpadButton_StickLRight)) != 0 );

    out->confirm       = (down & HidNpadButton_A) != 0;
    out->cancel        = (down & HidNpadButton_B) != 0;
    out->menu          = (down & HidNpadButton_Plus) != 0;
    out->override_game = (down & HidNpadButton_Minus) != 0;
    out->button_x       = (down & HidNpadButton_X) != 0;
    out->button_y       = (down & HidNpadButton_Y) != 0;

    out->dbg_r = (down & HidNpadButton_R) != 0;

    out->held_l  = (held & HidNpadButton_L)  != 0;
    out->held_zl = (held & HidNpadButton_ZL) != 0;
    out->held_r  = (held & HidNpadButton_R)  != 0;
    out->held_zr = (held & HidNpadButton_ZR) != 0;
    out->held_minus = (held & HidNpadButton_Minus) != 0;

    /* Real analog trigger pressure -- see nxinput.h's own comment on
     * why this is 0 for the vast majority of real controllers (no
     * analog trigger hardware on standard Joy-Con/Pro Controller) and
     * only nonzero for a GameCube-style one, which padConfigureInput()
     * above already enabled reading from. */
    out->trigger_l_analog = padGetGcTriggerPos( &nxinput_pad, 0 );
    out->trigger_r_analog = padGetGcTriggerPos( &nxinput_pad, 1 );
}
