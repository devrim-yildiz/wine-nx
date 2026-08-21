/* nxlib: the launcher's game library -- persistence, drive-C browsing,
 * and per-game settings overrides. Everything's plain text on the SD
 * card, matching every other config mechanism already in this project
 * (source/runtime.c's config.txt/target.txt/deko3d.txt family): no
 * database, trivially inspectable and hand-editable if something ever
 * goes wrong on real hardware.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#ifndef WINE_NX_LIB_H
#define WINE_NX_LIB_H

#include "nxgui_pe_icon.h"

/* Matches the user's own spec: "vulkan, zink/mesaopengl, direct3d or
 * directx or gdi" plus auto-detect. AUTO's actual detection logic
 * belongs to the launcher's handoff/CMake wiring (task #29), not here
 * -- this module only stores which mode was chosen. */
typedef enum
{
    NXLIB_BACKEND_AUTO = 0,
    NXLIB_BACKEND_VULKAN,
    NXLIB_BACKEND_ZINK,
    NXLIB_BACKEND_D3D,
    NXLIB_BACKEND_GDI
} nxlib_backend_t;

typedef enum
{
    NXLIB_BITNESS_AUTO = 0,
    NXLIB_BITNESS_32,
    NXLIB_BITNESS_64
} nxlib_bitness_mode_t;

typedef struct
{
    nxlib_backend_t backend;
    nxlib_bitness_mode_t bitness_mode;
} nxlib_settings_t;

#define NXLIB_MAX_GAMES  256
#define NXLIB_PATH_MAX   512
#define NXLIB_NAME_MAX   128

/* Mock patches the mod manager screen (nxmenu_mod_manager.c) offers --
 * see nxlib_game_t::mod_flags. "Mock" describes what's behind them
 * (nothing actually patches anything yet), not the toggle mechanism,
 * which is real and persists like any other setting here. */
#define NXLIB_MOD_60FPS_PATCH    0x1u
#define NXLIB_MOD_WIDESCREEN_FIX 0x2u
#define NXLIB_MOD_SKIP_INTRO     0x4u
#define NXLIB_MOD_HD_TEXTURES    0x8u

#define NXLIB_ID_MAX     64
#define NXLIB_AUTHOR_MAX 64

typedef struct
{
    char path[NXLIB_PATH_MAX];   /* native sdmc: path -- the stable identity key for this entry */
    char name[NXLIB_NAME_MAX];   /* display name/title, defaults to the exe's own basename minus ".exe" --
                                   * this IS the "title" field: kept as `name` rather than adding a
                                   * separate, duplicate `title[128]` that would just shadow it, since
                                   * every existing screen already reads/writes `name`. */
    unsigned short machine;      /* IMAGE_FILE_MACHINE_I386/AMD64/ARM64 -- detected once, at add time */

    int has_override;            /* 0: inherits nxlib_settings_load_global(); 1: uses `override` below */
    nxlib_settings_t override;

    /* Frontend display metadata. `id` and the two colors are derived
     * deterministically from `path` (see nxlib_add_game/nxlib_load) so
     * they don't need their own persisted storage -- recomputing them
     * is exact and free. `author`/`playtime_hours`/`last_played_timestamp`
     * are genuinely mock/placeholder for real games right now (this
     * project has no metadata source or usage-tracking for them yet) --
     * honestly zero/"UNKNOWN" rather than fabricated-looking nonzero
     * values, so a real game and a mock one are both internally
     * consistent, just with different (real vs invented) numbers.
     * has_active_mods/is_favorite ARE real, user-toggleable state and
     * do persist (see nxlib_load/nxlib_save). */
    char id[NXLIB_ID_MAX];
    char author[NXLIB_AUTHOR_MAX];
    float playtime_hours;
    int last_played_timestamp;   /* unix time, 0 = never */
    uint32_t primary_color;      /* RGBA8888, NXGUI_RGBA-compatible -- see nxgui.h for the byte order */
    uint32_t accent_color;
    int has_active_mods;   /* kept in sync with (mod_flags != 0) -- see nxmenu_mod_manager.c */
    int is_favorite;

    /* Individual mock-patch toggles for the mod manager screen
     * (nxmenu_mod_manager.c) -- see NXLIB_MOD_* below. Real, persisted,
     * user-toggleable state, same as has_active_mods/is_favorite; what's
     * mock here is that no actual patch/backend does anything with
     * these bits yet, not that the toggle state itself is fake. */
    unsigned int mod_flags;
    /* Percent, e.g. 100 = native resolution; nxmenu_detail.c's slider
     * lets this range 50-200 in 5% steps. Persisted like the mod flags
     * above -- also not yet applied anywhere at runtime (no scaling
     * implementation exists in this project to hook into), same
     * "real setting, honestly not wired to a backend yet" category as
     * the ZINK/D3D backend choices (see launcher_main.c). */
    int resolution_scale_percent;

    /* In-memory only -- never written to library.txt. A decoded icon is
     * tens of KB of raw RGBA8888 per game; re-decoding it from the exe
     * every time the library loads (rather than caching pixels to disk)
     * keeps library.txt itself tiny and trivially portable/editable,
     * matching the "just text" philosophy above. See nxlib_ensure_icon. */
    int icon_loaded;
    int icon_valid;
    nxgui_icon_t icon;
} nxlib_game_t;

/* Global (whole-launcher) default settings -- any game with
 * has_override==0 inherits these. Stored as new keys in the project's
 * existing consolidated sdmc:/switch/wine/config.txt (see that file's
 * own header comment in source/runtime.c: "every new toggle from here
 * on should only ever gain a config.txt key, never another dedicated
 * file") rather than a launcher-private file. */
void nxlib_settings_load_global( nxlib_settings_t *out );
void nxlib_settings_save_global( const nxlib_settings_t *in );

/* Same config.txt (LAUNCHER_THEME key), same reasoning, for the
 * library screen's active console theme -- a plain int (0..4) rather
 * than nxmenu.h's nxmenu_theme_id, so this header/nxlib.c don't need to
 * depend on nxmenu.h. Unrecognized/missing values return 0 (Switch). */
int  nxlib_theme_load_global( void );
void nxlib_theme_save_global( int theme );

/* Same pattern again (LAUNCHER_DISCORD_RPC key) for the settings
 * screen's Discord Rich Presence toggle -- kept separate from
 * nxlib_settings_t deliberately: that struct doubles as each game's
 * OWN per-game override (nxlib_game_t::override), and Discord
 * presence isn't a thing an individual game should be able to
 * override, only a global launcher preference. 1 = enabled, 0 (also
 * the default) = disabled. Enabling this only means "try to report
 * presence" -- see source/gui/nx_discord.h for why there is currently
 * nothing on Horizon OS for that to actually reach. */
int  nxlib_discord_enabled_load_global( void );
void nxlib_discord_enabled_save_global( int enabled );

/* Controller Mapping Studio (nxmenu_controller.c) persistence.
 * Deadzone/sensitivity really do change nxinput.c's live behavior --
 * see nxinput_set_deadzone/nxinput_set_cursor_speed, which
 * launcher_main.c calls with these loaded values once at startup.
 * Button-action assignment (nxlib_controller_map_load_global/
 * save_global) is stored the same honest way as several fields in
 * nxlib_game_t above: real, persisted, editable on screen -- but NOT
 * yet consulted by nxinput.c's own button-reading logic, which still
 * always reads A=confirm/B=cancel/etc directly regardless of what's
 * stored here. Wiring nxinput.c to actually consult this map is a
 * separate, more invasive change (it's the one input core every
 * screen in this launcher depends on) deliberately not made as part
 * of this pass. */
#define NXLIB_CTRL_DEADZONE_DEFAULT    3000   /* matches nxinput.c's own prior hardcoded default */
#define NXLIB_CTRL_SENSITIVITY_DEFAULT   18

int  nxlib_controller_deadzone_load_global( void );
void nxlib_controller_deadzone_save_global( int deadzone );
int  nxlib_controller_sensitivity_load_global( void );
void nxlib_controller_sensitivity_save_global( int px_per_frame );

/* One physical-button slot per index; nxmenu_controller.c draws these
 * at fixed positions on its gamepad graphic. */
#define NXLIB_CTRL_BUTTON_A      0
#define NXLIB_CTRL_BUTTON_B      1
#define NXLIB_CTRL_BUTTON_X      2
#define NXLIB_CTRL_BUTTON_Y      3
#define NXLIB_CTRL_BUTTON_L      4
#define NXLIB_CTRL_BUTTON_R      5
#define NXLIB_CTRL_BUTTON_ZL     6
#define NXLIB_CTRL_BUTTON_ZR     7
#define NXLIB_CTRL_BUTTON_PLUS   8
#define NXLIB_CTRL_BUTTON_MINUS  9
#define NXLIB_CTRL_BUTTON_COUNT 10

/* What a button can be displayed as doing -- this launcher's own
 * current fixed scheme, named so the default map below reads as
 * self-documenting. One action per button, so DEBUG_CHORD is shown on L
 * only (the debug overlay's held modifier -- see nxdebug.c); Minus's own
 * entry stays OVERRIDE since that's its unconditional, primary meaning,
 * even though holding L while pressing it also opens the debug overlay.
 * NONE marks a button with no launcher-assigned role at all -- ZL/ZR
 * used to both show DEBUG_CHORD too, back when the debug overlay was a
 * 4-button L+ZL+R+ZR chord; now that it's just L+Minus, showing that
 * label on buttons that no longer do anything toward it would be an
 * honest-sounding UI lying about real behavior. */
#define NXLIB_CTRL_ACTION_CONFIRM     0
#define NXLIB_CTRL_ACTION_CANCEL      1
#define NXLIB_CTRL_ACTION_MENU        2
#define NXLIB_CTRL_ACTION_OVERRIDE    3
#define NXLIB_CTRL_ACTION_DETAILS     4
#define NXLIB_CTRL_ACTION_THEME_CYCLE 5
#define NXLIB_CTRL_ACTION_FAVORITES   6
#define NXLIB_CTRL_ACTION_DEBUG_CHORD 7
#define NXLIB_CTRL_ACTION_NONE        8
#define NXLIB_CTRL_ACTION_COUNT       9

/* Fills/reads all NXLIB_CTRL_BUTTON_COUNT slots in one call, indexed by
 * NXLIB_CTRL_BUTTON_*; missing/malformed storage resets to this
 * launcher's real current fixed mapping (A/B/X/Y/L/R/ZL/ZR/+/- exactly
 * as every other file here already wires them), so this screen's
 * "default" always matches what's actually true today, not a guess. */
void nxlib_controller_map_load_global( int actions[NXLIB_CTRL_BUTTON_COUNT] );
void nxlib_controller_map_save_global( const int actions[NXLIB_CTRL_BUTTON_COUNT] );

/* Quick Access overlay trigger (nxmenu_quickmenu.c) -- one of the four
 * shoulder buttons, held together with Minus, opens/closes it. This is
 * the "should not be hardcoded, should work on L/R/ZL/ZR" button the
 * user's own spec asked for: genuinely configurable from the
 * Controller Studio screen, not fixed to ZL the way a literal "L2"
 * reading of that spec would have been. Default is ZL (matching the
 * spec's own suggested "Joy-Con L2 + Minus"), but changing it here is
 * real and immediate -- see nxmenu_quickmenu.c's own use of this. */
#define NXLIB_SHOULDER_L      0
#define NXLIB_SHOULDER_R      1
#define NXLIB_SHOULDER_ZL     2
#define NXLIB_SHOULDER_ZR     3
#define NXLIB_SHOULDER_COUNT  4
#define NXLIB_SHOULDER_DEFAULT NXLIB_SHOULDER_ZL

int  nxlib_quickmenu_trigger_load_global( void );
void nxlib_quickmenu_trigger_save_global( int shoulder );

/* Resolves what a specific game should actually run with: its own
 * override if has_override, otherwise the current global defaults. */
void nxlib_effective_settings( const nxlib_game_t *game, nxlib_settings_t *out );

/* library.txt persistence -- unlike the global settings above, a
 * growable list of per-game records doesn't fit config.txt's flat
 * key=value shape, so this genuinely is a second file, not scope creep
 * against that stated policy (which is about avoiding one-file-per-
 * toggle sprawl, a different problem). Both return the number of games
 * present after the operation (nxlib_load: read; nxlib_save: written),
 * or -1 on an I/O failure. */
int nxlib_load( nxlib_game_t *games, int max_games );
int nxlib_save( const nxlib_game_t *games, int count );

/* Adds one exe to the in-memory list (caller still owns calling
 * nxlib_save() afterward): rejects anything that isn't a readable PE
 * file (returns 0), otherwise detects bitness, derives a display name
 * from the basename, defaults has_override to 0, and appends. Does NOT
 * touch icon_loaded/icon_valid -- see nxlib_ensure_icon. Returns 1 on
 * success, 0 on a bad exe or a full list. */
int nxlib_add_game( nxlib_game_t *games, int *count, int max_games, const char *exe_path );
void nxlib_remove_game( nxlib_game_t *games, int *count, int index );

/* Lazily decodes and caches `game`'s icon the first time it's called
 * for that entry; every later call for the same entry is a no-op
 * (icon_loaded is set whether or not decoding actually succeeded --
 * icon_valid is what distinguishes "confirmed no icon" from "haven't
 * tried yet"). Safe to call once per frame per visible library-grid
 * entry without re-decoding anything every frame. */
void nxlib_ensure_icon( nxlib_game_t *game );
/* Releases icon.pixels and resets icon_loaded/icon_valid to 0, so a
 * later nxlib_ensure_icon() call decodes fresh -- used when a game
 * entry is dropped from the visible set (removed from the library, or
 * scrolled far enough off-screen that the launcher wants its memory
 * back) rather than leaking whatever was last decoded for it. */
void nxlib_release_icon( nxlib_game_t *game );

/* Drive-C browsing for the "browse for an exe" flow (the file-picker
 * screen itself is task #26's job; this module only lists). Returns
 * the fixed root every browse session starts from -- the same
 * sdmc:/switch/wine/drive_c that source/runtime.c's WINE_DRIVE_C
 * already treats as this project's one and only C: drive. */
const char *nxlib_drive_c_root( void );

#define NXLIB_BROWSE_MAX_ENTRIES 256
#define NXLIB_BROWSE_NAME_MAX    256

typedef struct
{
    char name[NXLIB_BROWSE_NAME_MAX];   /* entry name only (no path) -- caller joins it onto the current dir */
    int is_dir;
} nxlib_browse_entry_t;

/* Lists `dir_path`'s immediate children (not recursive -- a classic
 * one-level-at-a-time file-open dialog, not a background full-tree
 * scan, which would mean doing PE/icon work on every .exe under C:\
 * whether the user ever navigates there or not). Directories are
 * listed first, then files; both groups alphabetically. Only
 * subdirectories and files ending in ".exe" (case-insensitive) survive
 * the filter -- anything else in the directory is simply not listed.
 * Returns the entry count (0..NXLIB_BROWSE_MAX_ENTRIES), or -1 if
 * dir_path couldn't be opened. */
int nxlib_browse_dir( const char *dir_path, nxlib_browse_entry_t *out );

#endif /* WINE_NX_LIB_H */
