/* nxmenu: the launcher's actual screen flow -- library grid, drive-C
 * file browser for adding games, per-game override editor, global
 * settings editor -- built entirely on nxgui (drawing), nxinput
 * (unified touch+joycon), and nxlib (persistence). One call per frame
 * (nxmenu_frame) drives the whole thing; the caller (the launcher's
 * main loop, task #29) only needs to notice NXMENU_ACTION_LAUNCH /
 * NXMENU_ACTION_QUIT / NXMENU_ACTION_OPEN_DEBUG and act on
 * state->launch_* / do its own clean-exit sequence / open the debug
 * overlay -- everything else (which screen is showing, list scrolling,
 * editing settings) is this module's own business.
 *
 * NXMENU_ACTION_OPEN_DEBUG (Settings screen's own "DEBUG OVERLAY" row)
 * is the primary, reliable way into the debug overlay -- an ordinary
 * menu item, reached and confirmed the exact same way as every other
 * settings row, with none of the collision surface a hidden shoulder-
 * button combo has (see nxdebug.h's own header comment for the real
 * history there: a 4-button chord that leaked into other screens, then
 * a 2-button combo that turned out invisible due to a draw-order bug,
 * then a working ZR+Minus combo that nonetheless needed the user to
 * remember a specific, undiscoverable button pair instead of just
 * opening Settings). nxdebug.c itself still doesn't know nxmenu.h
 * exists -- this module only RETURNS the action, launcher_main.c is the
 * one place that knows both nxmenu_state_t and nxdebug_state_t and
 * connects the two, same as it already does for NXMENU_ACTION_LAUNCH/
 * QUIT. The ZR+Minus combo still works too (kept as a fast path for
 * when the overlay's already needed mid-session), it's just no longer
 * the only door in.
 *
 * Navigation model is deliberately flat, not a stack: every non-library
 * screen's Cancel (B) returns straight to the library grid. A real
 * back-stack would only matter for menus nested more than one level
 * deep, which this launcher doesn't have -- the top menu, browser, and
 * both settings screens are each reached directly from the library and
 * each exit directly back to it.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#ifndef WINE_NX_MENU_H
#define WINE_NX_MENU_H

#include "nxgui.h"
#include "nxinput.h"
#include "nxlib.h"

typedef enum
{
    NXMENU_ACTION_NONE = 0,
    NXMENU_ACTION_LAUNCH,       /* state->launch_path/launch_settings/launch_machine are ready */
    NXMENU_ACTION_QUIT,         /* user asked to exit the launcher entirely */
    NXMENU_ACTION_OPEN_DEBUG    /* user confirmed Settings' "DEBUG OVERLAY" row -- caller should set its nxdebug_state_t's visible=1 */
} nxmenu_action_t;

typedef enum
{
    NXMENU_SCREEN_LIBRARY = 0,
    NXMENU_SCREEN_TOPMENU,
    NXMENU_SCREEN_BROWSE,
    NXMENU_SCREEN_GAME_OVERRIDE,
    NXMENU_SCREEN_GLOBAL_SETTINGS,
    NXMENU_SCREEN_GAME_DETAIL,        /* per-game hero view: artwork, mock perf stats, launch/mods/overrides/resolution */
    NXMENU_SCREEN_MOD_MANAGER,        /* entered from GAME_DETAIL -- individual mock-patch toggles for the same game */
    NXMENU_SCREEN_CONTROLLER_STUDIO   /* global (not per-game) -- drawn gamepad, button remap zones, deadzone/sensitivity sliders */
} nxmenu_screen_t;

/* Ordinal values are load-bearing: nxgui_palette_for_theme() (nxgui.c)
 * and nxlib_theme_load_global/save_global (nxlib.c, which store this as
 * a bare int so nxlib.c doesn't need to depend on this enum) both index
 * by this same 0..4 ordering. Keep new themes appended at the end. */
typedef enum
{
    NXMENU_THEME_SWITCH = 0,
    NXMENU_THEME_PS5,
    NXMENU_THEME_XBOX,
    NXMENU_THEME_PS3,
    NXMENU_THEME_WII,
    NXMENU_THEME_COUNT
} nxmenu_theme_id;

#define NXMENU_MAX_GAMES NXLIB_MAX_GAMES

typedef struct
{
    nxmenu_screen_t screen;

    nxlib_game_t games[NXMENU_MAX_GAMES];
    int game_count;
    int selected;          /* focused index into games[], library grid -- also which game GAME_DETAIL/MOD_MANAGER are showing */
    int scroll_row;        /* first visible grid row */

    nxmenu_theme_id theme;       /* persisted (nxlib_theme_load_global/save_global); cycled with X on the library screen */
    int favorites_filter;        /* toggled with Y on the library screen -- when set, only is_favorite games are shown/navigable */
    int ps3_category;            /* PS3 theme only (nxmenu_library.c) -- which XMB category column is active; every
                                   * other theme ignores this field entirely, it's harmless dead weight for them */

    int topmenu_selected;

    char browse_dir[NXLIB_PATH_MAX];
    nxlib_browse_entry_t browse_entries[NXLIB_BROWSE_MAX_ENTRIES];
    int browse_count;
    int browse_selected;
    int browse_scroll;     /* first visible row in the browse list */

    nxlib_settings_t global_settings;
    int settings_field;    /* focused row: meaning is per-screen (GLOBAL_SETTINGS/GAME_OVERRIDE/GAME_DETAIL/MOD_MANAGER each reuse this the same way -- one small vertical option list, one focus index) */
    int discord_rpc_enabled;   /* persisted (nxlib_discord_enabled_load_global/save_global); toggled from GLOBAL_SETTINGS -- see nx_discord.h for why it's a real toggle over a stub backend */

    /* Controller Mapping Studio (nxmenu_controller.c), loaded once here
     * like theme/discord_rpc_enabled above rather than re-read from
     * disk every frame -- reuses settings_field too (0..9 = button
     * zones, 10 = deadzone slider, 11 = sensitivity slider, 12 = quick-
     * menu trigger selector). */
    int ctrl_actions[NXLIB_CTRL_BUTTON_COUNT];
    int ctrl_deadzone;
    int ctrl_sensitivity;
    int ctrl_quickmenu_trigger;   /* NXLIB_SHOULDER_* -- see nxmenu_quickmenu.c, the actual consumer */

    char launch_path[NXLIB_PATH_MAX];
    nxlib_settings_t launch_settings;
    unsigned short launch_machine;
} nxmenu_state_t;

/* Loads the library and global settings from disk (nxlib_load /
 * nxlib_settings_load_global) and starts on the library screen. */
void nxmenu_init( nxmenu_state_t *state );

/* Frontend-only test aid: replaces state->games with 16 fixed, varied
 * mock entries (genre/title-length/playtime/favorite/mods/machine-type
 * spread -- including deliberate edge cases like a very long title and
 * an ARM64-native badge) so every theme and screen can be visually
 * exercised without needing real exes staged on the SD card. Does NOT
 * touch disk (no nxlib_save call) and does NOT persist -- a real boot
 * with nxmenu_init() alone never calls this. Their `path` fields are
 * synthetic and don't point at real files: selecting one to "launch"
 * still safely reaches wine-nx-runtime.nro, which just fails a normal,
 * already-handled PE-load-not-found error rather than crashing anything
 * launcher-side. Wired to a debug-overlay action, not auto-invoked --
 * see launcher_main.c. */
void nxmenu_inject_mock_games( nxmenu_state_t *state );

/* One frame: consumes `input` (already polled this frame via
 * nxinput_poll), updates state, draws the active screen into `surf`.
 * Any on-disk change (add/remove a game, edit an override, edit global
 * settings) is saved immediately inside this call -- there is no
 * separate "save" step for the caller to remember. */
nxmenu_action_t nxmenu_frame( nxmenu_state_t *state, nxgui_surface_t *surf, const nxinput_state_t *input );

#endif /* WINE_NX_MENU_H */
