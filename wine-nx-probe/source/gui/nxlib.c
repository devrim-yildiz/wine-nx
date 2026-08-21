/* See nxlib.h for the overall design (plain text on the SD card,
 * global settings folded into the project's existing consolidated
 * config.txt, a separate library.txt only because a game list doesn't
 * fit that file's flat key=value shape).
 *
 * WINE_ROOT/WINE_DRIVE_C below are deliberately identical string
 * literals to source/runtime.c's own #defines of the same name --
 * duplicated, not shared via a header, because the launcher is built
 * as its own binary (see 3d-accel-scoping.md's reasoning for why one
 * NRO can't safely also be the deko3d/Vulkan runtime) and doesn't
 * compile runtime.c into itself. If runtime.c's WINE_ROOT ever changes,
 * this constant needs to change with it -- there is no way around that
 * without introducing a real shared header, which felt like more
 * machinery than a two-line constant warrants.
 *
 * NOT YET HARDWARE-VALIDATED.
 */
#include "nxlib.h"

#include <windows.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define WINE_ROOT     "sdmc:/switch/wine"
#define WINE_DRIVE_C  WINE_ROOT "/drive_c"
#define NXLIB_CONFIG_TXT  WINE_ROOT "/config.txt"
#define NXLIB_LIBRARY_TXT WINE_ROOT "/library.txt"

/* ------------------------------------------------------------------ */
/* small shared helpers                                                */
/* ------------------------------------------------------------------ */

static void trim( char *s )
{
    size_t len = strlen( s );
    char *start = s;

    while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                   s[len - 1] == ' '  || s[len - 1] == '\t')) s[--len] = 0;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) memmove( s, start, strlen( start ) + 1 );
}

/* Same minimal on-disk PE peek as source/runtime.c's peek_pe_machine()
 * -- duplicated for the same "separate binary" reason as WINE_ROOT
 * above. Best-effort: any failure just falls back to IMAGE_FILE_MACHINE_UNKNOWN
 * rather than treating the exe as unaddable over a header read glitch;
 * the library entry still gets added, just without a trustworthy
 * bitness badge until the user reopens/rescans it. */
static unsigned short nxlib_peek_machine( const char *path )
{
    unsigned short machine = IMAGE_FILE_MACHINE_UNKNOWN;
    IMAGE_DOS_HEADER dos;
    unsigned int pe_sig;
    IMAGE_FILE_HEADER file_header;
    FILE *f;

    if (!(f = fopen( path, "rb" ))) return machine;
    if (fread( &dos, sizeof(dos), 1, f ) == 1 && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        !fseek( f, dos.e_lfanew, SEEK_SET ) &&
        fread( &pe_sig, sizeof(pe_sig), 1, f ) == 1 && pe_sig == IMAGE_NT_SIGNATURE &&
        fread( &file_header, sizeof(file_header), 1, f ) == 1)
        machine = file_header.Machine;
    fclose( f );
    return machine;
}

static uint32_t nxlib_fnv1a( const char *s )
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

/* Deterministic per-game id + primary/accent colors, derived from
 * `path` so they never need their own persisted storage (see
 * nxlib_game_t's own comment in nxlib.h). Two independent hashes
 * (h, h^golden-ratio-constant) keep primary and accent from always
 * landing suspiciously close to each other. Components are kept in
 * [0x40,0xff] rather than the full [0,0xff] range so a hash never
 * lands on a muddy near-black/near-grey tile -- every derived color
 * stays visibly saturated enough to read as "this game's color" on a
 * dark UI. author/playtime/last_played stay "UNKNOWN"/0/0 for every
 * real game -- honestly placeholder, not fabricated, since this
 * project has no metadata source or usage-tracking for them yet. */
static void nxlib_derive_display_meta( nxlib_game_t *g )
{
    uint32_t h = nxlib_fnv1a( g->path );
    uint32_t h2 = h ^ 0x9e3779b9u;
    uint8_t pr = (uint8_t)(0x40 + (h & 0xbf));
    uint8_t pg = (uint8_t)(0x40 + ((h >> 8) & 0xbf));
    uint8_t pb = (uint8_t)(0x40 + ((h >> 16) & 0xbf));
    uint8_t ar = (uint8_t)(0x40 + (h2 & 0xbf));
    uint8_t ag = (uint8_t)(0x40 + ((h2 >> 8) & 0xbf));
    uint8_t ab = (uint8_t)(0x40 + ((h2 >> 16) & 0xbf));

    snprintf( g->id, sizeof(g->id), "%08x", h );
    snprintf( g->author, sizeof(g->author), "UNKNOWN" );
    g->primary_color = (uint32_t)pr | ((uint32_t)pg << 8) | ((uint32_t)pb << 16) | 0xff000000u;
    g->accent_color  = (uint32_t)ar | ((uint32_t)ag << 8) | ((uint32_t)ab << 16) | 0xff000000u;
}

static int has_exe_suffix( const char *name )
{
    size_t len = strlen( name );
    return len > 4 && strcasecmp( name + len - 4, ".exe" ) == 0;
}

/* ------------------------------------------------------------------ */
/* global settings, stored as two new keys in config.txt               */
/* ------------------------------------------------------------------ */

static const char *backend_to_str( nxlib_backend_t b )
{
    switch (b)
    {
    case NXLIB_BACKEND_VULKAN: return "vulkan";
    case NXLIB_BACKEND_ZINK:   return "zink";
    case NXLIB_BACKEND_D3D:    return "d3d";
    case NXLIB_BACKEND_GDI:    return "gdi";
    default:                   return "auto";
    }
}

static nxlib_backend_t backend_from_str( const char *s )
{
    if (!strcasecmp( s, "vulkan" )) return NXLIB_BACKEND_VULKAN;
    if (!strcasecmp( s, "zink" ))   return NXLIB_BACKEND_ZINK;
    if (!strcasecmp( s, "d3d" ))    return NXLIB_BACKEND_D3D;
    if (!strcasecmp( s, "gdi" ))    return NXLIB_BACKEND_GDI;
    return NXLIB_BACKEND_AUTO;
}

static const char *bitness_to_str( nxlib_bitness_mode_t m )
{
    switch (m)
    {
    case NXLIB_BITNESS_32: return "32";
    case NXLIB_BITNESS_64: return "64";
    default:                return "auto";
    }
}

static nxlib_bitness_mode_t bitness_from_str( const char *s )
{
    if (!strcmp( s, "32" )) return NXLIB_BITNESS_32;
    if (!strcmp( s, "64" )) return NXLIB_BITNESS_64;
    return NXLIB_BITNESS_AUTO;
}

void nxlib_settings_load_global( nxlib_settings_t *out )
{
    FILE *f;
    char line[128];

    out->backend = NXLIB_BACKEND_AUTO;
    out->bitness_mode = NXLIB_BITNESS_AUTO;

    if (!(f = fopen( NXLIB_CONFIG_TXT, "r" ))) return;
    while (fgets( line, sizeof(line), f ))
    {
        char *key, *value, *eq;

        trim( line );
        key = line;
        if (!key[0] || key[0] == '#') continue;
        if (!(eq = strchr( key, '=' ))) continue;
        *eq = 0;
        value = eq + 1;
        trim( key );
        trim( value );

        if (!strcasecmp( key, "LAUNCHER_DEFAULT_BACKEND" )) out->backend = backend_from_str( value );
        else if (!strcasecmp( key, "LAUNCHER_DEFAULT_BITNESS" )) out->bitness_mode = bitness_from_str( value );
    }
    fclose( f );
}

/* Rewrites config.txt with every existing line preserved verbatim
 * (so this project's other toggles -- deko3d/systrace/box64trace/etc,
 * see source/runtime.c -- are never disturbed by a launcher settings
 * change) except any prior LAUNCHER_DEFAULT_* lines, which are dropped
 * and re-appended at the end with their new values. A config file this
 * small (well under 128 lines in practice) is cheap to fully re-read
 * into memory and rewrite -- no in-place seeking/patching needed. */
void nxlib_settings_save_global( const nxlib_settings_t *in )
{
    FILE *f;
    char lines[128][128];
    int line_count = 0;
    int i;

    if ((f = fopen( NXLIB_CONFIG_TXT, "r" )))
    {
        while (line_count < 128 && fgets( lines[line_count], sizeof(lines[0]), f ))
        {
            char probe[128];
            char *key, *eq;

            snprintf( probe, sizeof(probe), "%s", lines[line_count] );
            trim( probe );
            key = probe;
            if (key[0] && key[0] != '#' && (eq = strchr( key, '=' )))
            {
                *eq = 0;
                trim( key );
                if (!strcasecmp( key, "LAUNCHER_DEFAULT_BACKEND" ) ||
                    !strcasecmp( key, "LAUNCHER_DEFAULT_BITNESS" ))
                    continue;   /* drop -- re-appended below with the new value */
            }
            line_count++;
        }
        fclose( f );
    }

    if (!(f = fopen( NXLIB_CONFIG_TXT, "w" ))) return;
    for (i = 0; i < line_count; i++)
    {
        char out[130];
        snprintf( out, sizeof(out), "%s", lines[i] );
        trim( out );   /* fputs back a clean line; trim already stripped the original \n/\r */
        fprintf( f, "%s\n", out );
    }
    fprintf( f, "LAUNCHER_DEFAULT_BACKEND=%s\n", backend_to_str( in->backend ) );
    fprintf( f, "LAUNCHER_DEFAULT_BITNESS=%s\n", bitness_to_str( in->bitness_mode ) );
    fclose( f );
}

int nxlib_theme_load_global( void )
{
    FILE *f;
    char line[128];
    int theme = 0;

    if (!(f = fopen( NXLIB_CONFIG_TXT, "r" ))) return theme;
    while (fgets( line, sizeof(line), f ))
    {
        char *key, *value, *eq;

        trim( line );
        key = line;
        if (!key[0] || key[0] == '#') continue;
        if (!(eq = strchr( key, '=' ))) continue;
        *eq = 0;
        value = eq + 1;
        trim( key );
        trim( value );

        if (!strcasecmp( key, "LAUNCHER_THEME" ))
        {
            theme = atoi( value );
            if (theme < 0 || theme > 4) theme = 0;   /* out-of-range/corrupt -- fall back to Switch rather than index past nxgui's palette table */
        }
    }
    fclose( f );
    return theme;
}

/* Same read-all/drop-own-key/rewrite pattern as
 * nxlib_settings_save_global above, kept as its own independent
 * function (not folded into that one) since either can be called
 * without the other, and each must only ever touch its own key --
 * calling this alone must leave LAUNCHER_DEFAULT_BACKEND/BITNESS
 * completely untouched, and vice versa. */
void nxlib_theme_save_global( int theme )
{
    FILE *f;
    char lines[128][128];
    int line_count = 0;
    int i;

    if ((f = fopen( NXLIB_CONFIG_TXT, "r" )))
    {
        while (line_count < 128 && fgets( lines[line_count], sizeof(lines[0]), f ))
        {
            char probe[128];
            char *key, *eq;

            snprintf( probe, sizeof(probe), "%s", lines[line_count] );
            trim( probe );
            key = probe;
            if (key[0] && key[0] != '#' && (eq = strchr( key, '=' )))
            {
                *eq = 0;
                trim( key );
                if (!strcasecmp( key, "LAUNCHER_THEME" )) continue;   /* drop -- re-appended below with the new value */
            }
            line_count++;
        }
        fclose( f );
    }

    if (!(f = fopen( NXLIB_CONFIG_TXT, "w" ))) return;
    for (i = 0; i < line_count; i++)
    {
        char out[130];
        snprintf( out, sizeof(out), "%s", lines[i] );
        trim( out );
        fprintf( f, "%s\n", out );
    }
    fprintf( f, "LAUNCHER_THEME=%d\n", theme );
    fclose( f );
}

int nxlib_discord_enabled_load_global( void )
{
    FILE *f;
    char line[128];
    int enabled = 0;

    if (!(f = fopen( NXLIB_CONFIG_TXT, "r" ))) return enabled;
    while (fgets( line, sizeof(line), f ))
    {
        char *key, *value, *eq;

        trim( line );
        key = line;
        if (!key[0] || key[0] == '#') continue;
        if (!(eq = strchr( key, '=' ))) continue;
        *eq = 0;
        value = eq + 1;
        trim( key );
        trim( value );

        if (!strcasecmp( key, "LAUNCHER_DISCORD_RPC" )) enabled = atoi( value ) ? 1 : 0;
    }
    fclose( f );
    return enabled;
}

void nxlib_discord_enabled_save_global( int enabled )
{
    FILE *f;
    char lines[128][128];
    int line_count = 0;
    int i;

    if ((f = fopen( NXLIB_CONFIG_TXT, "r" )))
    {
        while (line_count < 128 && fgets( lines[line_count], sizeof(lines[0]), f ))
        {
            char probe[128];
            char *key, *eq;

            snprintf( probe, sizeof(probe), "%s", lines[line_count] );
            trim( probe );
            key = probe;
            if (key[0] && key[0] != '#' && (eq = strchr( key, '=' )))
            {
                *eq = 0;
                trim( key );
                if (!strcasecmp( key, "LAUNCHER_DISCORD_RPC" )) continue;
            }
            line_count++;
        }
        fclose( f );
    }

    if (!(f = fopen( NXLIB_CONFIG_TXT, "w" ))) return;
    for (i = 0; i < line_count; i++)
    {
        char out[130];
        snprintf( out, sizeof(out), "%s", lines[i] );
        trim( out );
        fprintf( f, "%s\n", out );
    }
    fprintf( f, "LAUNCHER_DISCORD_RPC=%d\n", enabled ? 1 : 0 );
    fclose( f );
}

/* Shared by the deadzone/sensitivity pair below -- a single "get one
 * int-valued config.txt key" / "set one int-valued key, preserving
 * everything else" pattern, factored out now that a THIRD and FOURTH
 * near-identical copy of it would otherwise exist alongside the theme
 * and Discord-enabled load/save functions above (written before this
 * pattern was obviously worth sharing; not retrofitted onto those two
 * to avoid touching already-verified code for no functional gain). */
static int nxlib_config_get_int( const char *key, int default_value )
{
    FILE *f;
    char line[128];
    int value = default_value;

    if (!(f = fopen( NXLIB_CONFIG_TXT, "r" ))) return value;
    while (fgets( line, sizeof(line), f ))
    {
        char *k, *v, *eq;
        trim( line );
        k = line;
        if (!k[0] || k[0] == '#') continue;
        if (!(eq = strchr( k, '=' ))) continue;
        *eq = 0; v = eq + 1;
        trim( k ); trim( v );
        if (!strcasecmp( k, key )) value = atoi( v );
    }
    fclose( f );
    return value;
}

static void nxlib_config_set_int( const char *key, int value )
{
    FILE *f;
    char lines[128][128];
    int line_count = 0;
    int i;

    if ((f = fopen( NXLIB_CONFIG_TXT, "r" )))
    {
        while (line_count < 128 && fgets( lines[line_count], sizeof(lines[0]), f ))
        {
            char probe[128];
            char *k, *eq;
            snprintf( probe, sizeof(probe), "%s", lines[line_count] );
            trim( probe );
            k = probe;
            if (k[0] && k[0] != '#' && (eq = strchr( k, '=' )))
            {
                *eq = 0; trim( k );
                if (!strcasecmp( k, key )) continue;
            }
            line_count++;
        }
        fclose( f );
    }
    if (!(f = fopen( NXLIB_CONFIG_TXT, "w" ))) return;
    for (i = 0; i < line_count; i++)
    {
        char out[130];
        snprintf( out, sizeof(out), "%s", lines[i] );
        trim( out );
        fprintf( f, "%s\n", out );
    }
    fprintf( f, "%s=%d\n", key, value );
    fclose( f );
}

int nxlib_controller_deadzone_load_global( void )
{
    return nxlib_config_get_int( "LAUNCHER_CTRL_DEADZONE", NXLIB_CTRL_DEADZONE_DEFAULT );
}

void nxlib_controller_deadzone_save_global( int deadzone )
{
    nxlib_config_set_int( "LAUNCHER_CTRL_DEADZONE", deadzone );
}

int nxlib_controller_sensitivity_load_global( void )
{
    return nxlib_config_get_int( "LAUNCHER_CTRL_SENSITIVITY", NXLIB_CTRL_SENSITIVITY_DEFAULT );
}

void nxlib_controller_sensitivity_save_global( int px_per_frame )
{
    nxlib_config_set_int( "LAUNCHER_CTRL_SENSITIVITY", px_per_frame );
}

static void nxlib_controller_map_defaults( int actions[NXLIB_CTRL_BUTTON_COUNT] )
{
    actions[NXLIB_CTRL_BUTTON_A]     = NXLIB_CTRL_ACTION_CONFIRM;
    actions[NXLIB_CTRL_BUTTON_B]     = NXLIB_CTRL_ACTION_CANCEL;
    actions[NXLIB_CTRL_BUTTON_X]     = NXLIB_CTRL_ACTION_THEME_CYCLE;
    actions[NXLIB_CTRL_BUTTON_Y]     = NXLIB_CTRL_ACTION_FAVORITES;
    actions[NXLIB_CTRL_BUTTON_L]     = NXLIB_CTRL_ACTION_DEBUG_CHORD;
    actions[NXLIB_CTRL_BUTTON_R]     = NXLIB_CTRL_ACTION_DETAILS;
    actions[NXLIB_CTRL_BUTTON_ZL]    = NXLIB_CTRL_ACTION_NONE;
    actions[NXLIB_CTRL_BUTTON_ZR]    = NXLIB_CTRL_ACTION_NONE;
    actions[NXLIB_CTRL_BUTTON_PLUS]  = NXLIB_CTRL_ACTION_MENU;
    actions[NXLIB_CTRL_BUTTON_MINUS] = NXLIB_CTRL_ACTION_OVERRIDE;
}

void nxlib_controller_map_load_global( int actions[NXLIB_CTRL_BUTTON_COUNT] )
{
    FILE *f;
    char line[256];

    nxlib_controller_map_defaults( actions );

    if (!(f = fopen( NXLIB_CONFIG_TXT, "r" ))) return;
    while (fgets( line, sizeof(line), f ))
    {
        char *key, *value, *eq;
        trim( line );
        key = line;
        if (!key[0] || key[0] == '#') continue;
        if (!(eq = strchr( key, '=' ))) continue;
        *eq = 0; value = eq + 1;
        trim( key ); trim( value );
        if (!strcasecmp( key, "LAUNCHER_CTRL_MAP" ))
        {
            char *p = value;
            int i;
            for (i = 0; i < NXLIB_CTRL_BUTTON_COUNT; i++)
            {
                char *comma = strchr( p, ',' );
                int v;
                if (comma) *comma = 0;
                v = atoi( p );
                if (v >= 0 && v < NXLIB_CTRL_ACTION_COUNT) actions[i] = v;
                if (!comma) break;
                p = comma + 1;
            }
        }
    }
    fclose( f );
}

void nxlib_controller_map_save_global( const int actions[NXLIB_CTRL_BUTTON_COUNT] )
{
    FILE *f;
    char lines[128][128];
    int line_count = 0;
    int i;
    char mapbuf[128];
    int off = 0;

    if ((f = fopen( NXLIB_CONFIG_TXT, "r" )))
    {
        while (line_count < 128 && fgets( lines[line_count], sizeof(lines[0]), f ))
        {
            char probe[128];
            char *key, *eq;
            snprintf( probe, sizeof(probe), "%s", lines[line_count] );
            trim( probe );
            key = probe;
            if (key[0] && key[0] != '#' && (eq = strchr( key, '=' )))
            {
                *eq = 0; trim( key );
                if (!strcasecmp( key, "LAUNCHER_CTRL_MAP" )) continue;
            }
            line_count++;
        }
        fclose( f );
    }

    for (i = 0; i < NXLIB_CTRL_BUTTON_COUNT; i++)
    {
        int written = snprintf( mapbuf + off, sizeof(mapbuf) - (size_t)off, "%s%d", i ? "," : "", actions[i] );
        if (written > 0 && (size_t)off + (size_t)written < sizeof(mapbuf)) off += written;
        else break;   /* would overflow mapbuf -- stop early rather than let `off` run past it */
    }

    if (!(f = fopen( NXLIB_CONFIG_TXT, "w" ))) return;
    for (i = 0; i < line_count; i++)
    {
        char out[130];
        snprintf( out, sizeof(out), "%s", lines[i] );
        trim( out );
        fprintf( f, "%s\n", out );
    }
    fprintf( f, "LAUNCHER_CTRL_MAP=%s\n", mapbuf );
    fclose( f );
}

int nxlib_quickmenu_trigger_load_global( void )
{
    int v = nxlib_config_get_int( "LAUNCHER_QUICKMENU_TRIGGER", NXLIB_SHOULDER_DEFAULT );
    if (v < 0 || v >= NXLIB_SHOULDER_COUNT) v = NXLIB_SHOULDER_DEFAULT;
    return v;
}

void nxlib_quickmenu_trigger_save_global( int shoulder )
{
    nxlib_config_set_int( "LAUNCHER_QUICKMENU_TRIGGER", shoulder );
}

void nxlib_effective_settings( const nxlib_game_t *game, nxlib_settings_t *out )
{
    if (game->has_override) *out = game->override;
    else nxlib_settings_load_global( out );
}

/* ------------------------------------------------------------------ */
/* library.txt: one game per line, pipe-delimited                     */
/* path|name|machine|has_override|backend|bitness_mode                */
/* ------------------------------------------------------------------ */

static void sanitize_field( char *s )
{
    /* Real filesystem paths and exe names never legitimately contain
     * '|' -- this only guards against that assumption somehow being
     * wrong on some future weird filename, so the delimited format
     * can't be corrupted by it. */
    for (; *s; s++) if (*s == '|' || *s == '\n' || *s == '\r') *s = '_';
}

int nxlib_load( nxlib_game_t *games, int max_games )
{
    FILE *f;
    char line[1024];
    int count = 0;

    if (!(f = fopen( NXLIB_LIBRARY_TXT, "r" ))) return 0;

    while (count < max_games && fgets( line, sizeof(line), f ))
    {
        nxlib_game_t *g = &games[count];
        char *field[10];
        char *p = line;
        int nfields = 0;

        trim( line );
        if (!line[0] || line[0] == '#') continue;

        /* Up to 10 pipe-delimited fields; only the first 6 (path
         * through bitness_mode) are required -- has_active_mods/
         * is_favorite/mod_flags/resolution_scale_percent were all added
         * later and default to 0/0/0/100 for any library.txt written
         * before they existed, rather than treating an older, shorter
         * line as malformed. */
        while (nfields < 10)
        {
            char *sep = strchr( p, '|' );
            field[nfields++] = p;
            if (!sep) break;
            *sep = 0;
            p = sep + 1;
        }
        if (nfields < 6) continue;   /* malformed line -- skip rather than abort the whole load */

        memset( g, 0, sizeof(*g) );
        snprintf( g->path, sizeof(g->path), "%s", field[0] );
        snprintf( g->name, sizeof(g->name), "%s", field[1] );
        g->machine = (unsigned short)strtoul( field[2], NULL, 10 );
        g->has_override = atoi( field[3] );
        g->override.backend = backend_from_str( field[4] );
        g->override.bitness_mode = bitness_from_str( field[5] );
        g->has_active_mods = nfields > 6 ? atoi( field[6] ) : 0;
        g->is_favorite     = nfields > 7 ? atoi( field[7] ) : 0;
        g->mod_flags       = nfields > 8 ? (unsigned int)strtoul( field[8], NULL, 10 ) : 0;
        g->resolution_scale_percent = nfields > 9 ? atoi( field[9] ) : 100;
        if (g->resolution_scale_percent < 50 || g->resolution_scale_percent > 200) g->resolution_scale_percent = 100;
        nxlib_derive_display_meta( g );
        count++;
    }
    fclose( f );
    return count;
}

int nxlib_save( const nxlib_game_t *games, int count )
{
    FILE *f;
    int i;

    if (!(f = fopen( NXLIB_LIBRARY_TXT, "w" ))) return -1;
    for (i = 0; i < count; i++)
    {
        char path[NXLIB_PATH_MAX], name[NXLIB_NAME_MAX];

        snprintf( path, sizeof(path), "%s", games[i].path );
        snprintf( name, sizeof(name), "%s", games[i].name );
        sanitize_field( path );
        sanitize_field( name );

        fprintf( f, "%s|%s|%u|%d|%s|%s|%d|%d|%u|%d\n",
                 path, name, (unsigned)games[i].machine, games[i].has_override,
                 backend_to_str( games[i].override.backend ),
                 bitness_to_str( games[i].override.bitness_mode ),
                 games[i].has_active_mods, games[i].is_favorite,
                 games[i].mod_flags, games[i].resolution_scale_percent );
    }
    fclose( f );
    return count;
}

int nxlib_add_game( nxlib_game_t *games, int *count, int max_games, const char *exe_path )
{
    nxlib_game_t *g;
    const char *base;
    const char *dot;
    size_t namelen;
    unsigned short machine;

    if (*count >= max_games) return 0;

    machine = nxlib_peek_machine( exe_path );
    if (machine != IMAGE_FILE_MACHINE_I386 && machine != IMAGE_FILE_MACHINE_AMD64 &&
        machine != IMAGE_FILE_MACHINE_ARM64)
        return 0;   /* not a PE exe this launcher can run -- refuse rather than add a dead entry */

    g = &games[*count];
    memset( g, 0, sizeof(*g) );
    snprintf( g->path, sizeof(g->path), "%s", exe_path );
    g->machine = machine;
    g->resolution_scale_percent = 100;

    base = strrchr( exe_path, '/' );
    base = base ? base + 1 : exe_path;
    dot = strrchr( base, '.' );
    namelen = dot && dot > base ? (size_t)(dot - base) : strlen( base );
    if (namelen >= sizeof(g->name)) namelen = sizeof(g->name) - 1;
    memcpy( g->name, base, namelen );
    g->name[namelen] = 0;

    nxlib_derive_display_meta( g );

    (*count)++;
    return 1;
}

void nxlib_remove_game( nxlib_game_t *games, int *count, int index )
{
    if (index < 0 || index >= *count) return;
    nxlib_release_icon( &games[index] );
    memmove( &games[index], &games[index + 1], (size_t)(*count - index - 1) * sizeof(nxlib_game_t) );
    (*count)--;
}

void nxlib_ensure_icon( nxlib_game_t *game )
{
    if (game->icon_loaded) return;
    game->icon_valid = nxgui_pe_icon_extract( game->path, &game->icon );
    game->icon_loaded = 1;
}

void nxlib_release_icon( nxlib_game_t *game )
{
    if (game->icon_valid) free( game->icon.pixels );
    memset( &game->icon, 0, sizeof(game->icon) );
    game->icon_loaded = 0;
    game->icon_valid = 0;
}

/* ------------------------------------------------------------------ */
/* drive-C browsing                                                    */
/* ------------------------------------------------------------------ */

const char *nxlib_drive_c_root( void )
{
    return WINE_DRIVE_C;
}

static int browse_entry_cmp( const void *pa, const void *pb )
{
    const nxlib_browse_entry_t *a = pa, *b = pb;
    if (a->is_dir != b->is_dir) return b->is_dir - a->is_dir;   /* directories first */
    return strcasecmp( a->name, b->name );
}

int nxlib_browse_dir( const char *dir_path, nxlib_browse_entry_t *out )
{
    DIR *d;
    struct dirent *ent;
    int n = 0;

    if (!(d = opendir( dir_path ))) return -1;

    while (n < NXLIB_BROWSE_MAX_ENTRIES && (ent = readdir( d )))
    {
        int is_dir;

        if (!strcmp( ent->d_name, "." ) || !strcmp( ent->d_name, ".." )) continue;

        if (ent->d_type == DT_DIR) is_dir = 1;
        else if (ent->d_type == DT_REG) is_dir = 0;
        else
        {
            /* d_type not populated by this backend for this entry --
             * fall back to a real stat() rather than guessing. */
            char full[1024];
            struct stat st;
            snprintf( full, sizeof(full), "%s/%s", dir_path, ent->d_name );
            if (stat( full, &st ) != 0) continue;
            is_dir = S_ISDIR( st.st_mode );
        }

        if (!is_dir && !has_exe_suffix( ent->d_name )) continue;

        snprintf( out[n].name, sizeof(out[n].name), "%s", ent->d_name );
        out[n].is_dir = is_dir;
        n++;
    }
    closedir( d );

    qsort( out, n, sizeof(out[0]), browse_entry_cmp );
    return n;
}
