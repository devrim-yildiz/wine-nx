/* See nxtest.h for the on-disk protocol and, importantly, the note
 * about the runtime.c half of it not existing yet. WINE_ROOT here is
 * the same intentional duplicate-literal as nxlib.c's copy (see that
 * file's own header comment for why).
 */
#include "nxtest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINE_ROOT              "sdmc:/switch/wine"
#define NXTEST_TARGET_TXT      WINE_ROOT "/target.txt"
#define NXTEST_INDEX_TXT       WINE_ROOT "/autotest_index.txt"
#define NXTEST_RESULT_TXT      WINE_ROOT "/autotest_result.txt"
#define NXTEST_RESULTS_LOG_TXT WINE_ROOT "/autotest_results.txt"

typedef struct
{
    const char *label;
    const char *runtime_nro;
    const char *target_exe;   /* NULL: launch runtime_nro directly, no target.txt write (a standalone smoke .nro, not a target.txt-driven WINE_NX_APP test) */
} nxtest_def_t;

/* Index 0 is the one entry actually wired end to end today -- see
 * jit_smoke.c's own "chained mode" handling, added alongside this
 * module. The other 8 match build-switch.sh's WINE_NX_APP=<kind>
 * staging exactly (see that script's target.txt printf lines);
 * runtime_nro reflects which built binary each needs: the Vulkan tests
 * require wine-nx-vulkan-runtime.nro specifically (see
 * 3d-accel-scoping.md and this project's vulkan_integration notes --
 * Vulkan only works as the sole `vi` consumer in its own binary, same
 * constraint as deko3d), everything else runs under the plain
 * wine-nx-runtime.nro. */
static const nxtest_def_t nxtest_defs[NXTEST_COUNT] =
{
    { "JIT",     "wine-nx-jit-smoke.nro",      NULL },
    { "GUI",     "wine-nx-runtime.nro",        "sdmc:/switch/wine/drive_c/gui/gui_smoke.exe" },
    { "BLIT",    "wine-nx-runtime.nro",        "sdmc:/switch/wine/drive_c/blit/direct_blit.exe" },
    { "CUBE32",  "wine-nx-runtime.nro",        "sdmc:/switch/wine/drive_c/cube32/cube32.exe" },
    { "VULKAN",  "wine-nx-vulkan-runtime.nro", "sdmc:/switch/wine/drive_c/vulkan_test/vulkan_test.exe" },
    { "VK TRI",  "wine-nx-vulkan-runtime.nro", "sdmc:/switch/wine/drive_c/vulkan_triangle_test/vulkan_triangle_test.exe" },
    { "CUBE64",  "wine-nx-runtime.nro",        "sdmc:/switch/wine/drive_c/cube64/cube64.exe" },
    { "NOTEPAD", "wine-nx-runtime.nro",        "sdmc:/switch/wine/drive_c/windows/system32/notepad.exe" },
    { "CURL",    "wine-nx-runtime.nro",        "sdmc:/switch/wine/drive_c/curl/curl.exe" },
};

void nxtest_init( nxtest_state_t *state )
{
    memset( state, 0, sizeof(*state) );
}

static void write_target_file( const char *exe )
{
    FILE *f;
    if (!exe) return;   /* this test's runtime_nro is a standalone smoke .nro, nothing for it to read target.txt for */
    f = fopen( NXTEST_TARGET_TXT, "w" );
    if (!f) return;
    fprintf( f, "%s\n", exe );
    fclose( f );
}

static void write_index_file( int idx )
{
    FILE *f = fopen( NXTEST_INDEX_TXT, "w" );
    if (!f) return;
    fprintf( f, "%d\n", idx );
    fclose( f );
}

/* Returns the stored index, or -1 if the file is absent/unreadable --
 * absence is exactly how nxtest_resume() tells "no chain in progress"
 * apart from "chain in progress at test 0". */
static int read_index_file( void )
{
    FILE *f = fopen( NXTEST_INDEX_TXT, "r" );
    int idx;
    if (!f) return -1;
    if (fscanf( f, "%d", &idx ) != 1) idx = -1;
    fclose( f );
    return idx;
}

/* Reads autotest_result.txt if present (and deletes it -- the result is
 * consumed exactly once), classifying it as PASS/FAIL/UNKNOWN into
 * *result and, for FAIL, copying the message after ':' into msg. No
 * file at all -- the expected case until runtime.c's half of this
 * protocol is written, see nxtest.h -- is UNKNOWN with an explanatory
 * message, not silently treated as a pass. */
static void read_and_consume_result( nxtest_result_t *result, char *msg, size_t msg_size )
{
    FILE *f = fopen( NXTEST_RESULT_TXT, "r" );
    char line[160];

    msg[0] = 0;
    if (!f)
    {
        *result = NXTEST_RESULT_UNKNOWN;
        snprintf( msg, msg_size, "NO RESULT FILE (CRASH, OR RUNTIME.C NOT WIRED YET)" );
        return;
    }

    if (!fgets( line, sizeof(line), f ))
    {
        *result = NXTEST_RESULT_UNKNOWN;
        snprintf( msg, msg_size, "EMPTY RESULT FILE" );
    }
    else
    {
        size_t len = strlen( line );
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;

        if (!strcmp( line, "PASS" )) *result = NXTEST_RESULT_PASS;
        else if (!strncmp( line, "FAIL", 4 ))
        {
            *result = NXTEST_RESULT_FAIL;
            snprintf( msg, msg_size, "%s", line[4] == ':' ? line + 5 : "" );
        }
        else
        {
            *result = NXTEST_RESULT_UNKNOWN;
            snprintf( msg, msg_size, "UNRECOGNIZED RESULT: %s", line );
        }
    }
    fclose( f );
    remove( NXTEST_RESULT_TXT );
}

static void append_result_log( int index, nxtest_result_t result, const char *msg )
{
    FILE *f = fopen( NXTEST_RESULTS_LOG_TXT, "a" );
    const char *tag;
    if (!f) return;

    tag = (result == NXTEST_RESULT_PASS) ? "PASS" : (result == NXTEST_RESULT_FAIL) ? "FAIL" : "UNKNOWN";
    fprintf( f, "%d|%s|%s\n", index, tag, msg );
    fclose( f );
}

static void replay_results_log( nxtest_state_t *state )
{
    FILE *f = fopen( NXTEST_RESULTS_LOG_TXT, "r" );
    char line[256];

    if (!f) return;
    while (fgets( line, sizeof(line), f ))
    {
        int idx;
        char tag[16], msg[NXTEST_MSG_MAX];
        size_t len = strlen( line );

        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        msg[0] = 0;
        if (sscanf( line, "%d|%15[^|]|%95[^\n]", &idx, tag, msg ) < 2) continue;
        if (idx < 0 || idx >= NXTEST_COUNT) continue;

        if (!strcmp( tag, "PASS" )) { state->results[idx] = NXTEST_RESULT_PASS; state->pass_count++; }
        else if (!strcmp( tag, "FAIL" )) state->results[idx] = NXTEST_RESULT_FAIL;
        else state->results[idx] = NXTEST_RESULT_UNKNOWN;
        snprintf( state->error_msg[idx], sizeof(state->error_msg[idx]), "%s", msg );
        state->run_count++;
    }
    fclose( f );
}

int nxtest_start( nxtest_state_t *state, char *out_nro, size_t out_nro_size )
{
    nxtest_init( state );
    state->active = 1;

    remove( NXTEST_RESULT_TXT );
    remove( NXTEST_RESULTS_LOG_TXT );
    write_index_file( 0 );
    write_target_file( nxtest_defs[0].target_exe );

    snprintf( out_nro, out_nro_size, "%s", nxtest_defs[0].runtime_nro );
    return 1;
}

int nxtest_resume( nxtest_state_t *state, char *out_nro, size_t out_nro_size )
{
    int finished_index, next_index;
    nxtest_result_t result;
    char msg[NXTEST_MSG_MAX];

    nxtest_init( state );

    finished_index = read_index_file();
    if (finished_index < 0) return 0;   /* no chain in progress */
    state->active = 1;

    if (finished_index >= NXTEST_COUNT) finished_index = NXTEST_COUNT - 1;   /* defensive -- a hand-edited/corrupt index file shouldn't index out of bounds */

    read_and_consume_result( &result, msg, sizeof(msg) );
    append_result_log( finished_index, result, msg );

    next_index = finished_index + 1;
    if (next_index < NXTEST_COUNT)
    {
        write_index_file( next_index );
        write_target_file( nxtest_defs[next_index].target_exe );
        snprintf( out_nro, out_nro_size, "%s", nxtest_defs[next_index].runtime_nro );
        return 1;
    }

    remove( NXTEST_INDEX_TXT );
    replay_results_log( state );
    return 0;
}

void nxtest_draw_summary( const nxtest_state_t *state, nxgui_surface_t *surf )
{
    char headline[64];
    int y = 96;
    int i;

    nxgui_clear( surf, NXGUI_COLOR_BG );
    {
        nxgui_rect_t bar = { 0, 0, surf->width, 72 };
        nxgui_fill_rect( surf, bar, NXGUI_COLOR_PANEL );
        nxgui_draw_text( surf, 40, 24, 2, "TEST RESULTS", NXGUI_COLOR_TEXT );
    }

    snprintf( headline, sizeof(headline), "%d/%d PASSED", state->pass_count, NXTEST_COUNT );
    nxgui_draw_text( surf, 40, y, 3, headline,
                      state->pass_count == NXTEST_COUNT ? NXGUI_COLOR_GOOD : NXGUI_COLOR_WARN );
    y += 64;

    for (i = 0; i < NXTEST_COUNT; i++)
    {
        nxgui_rect_t row = { 40, y, surf->width - 80, 40 };
        const char *status;
        uint32_t color;
        char line[160];

        switch (state->results[i])
        {
        case NXTEST_RESULT_PASS:    status = "PASS";    color = NXGUI_COLOR_GOOD; break;
        case NXTEST_RESULT_FAIL:    status = "FAIL";     color = NXGUI_COLOR_BAD;  break;
        case NXTEST_RESULT_UNKNOWN: status = "UNKNOWN"; color = NXGUI_COLOR_WARN; break;
        default:                    status = "NOT RUN"; color = NXGUI_COLOR_TEXT_DIM; break;
        }

        if (state->error_msg[i][0])
            snprintf( line, sizeof(line), "%-8s %-8s %s", nxtest_defs[i].label, status, state->error_msg[i] );
        else
            snprintf( line, sizeof(line), "%-8s %-8s", nxtest_defs[i].label, status );

        nxgui_draw_text( surf, row.x, row.y, 1, line, color );
        y += 40;
    }
}

void nxtest_report_to_log( const nxtest_state_t *state, nxdebug_state_t *debug )
{
    int i;

    nxdebug_log( debug, "AUTO-TEST RUN: %d/%d PASSED", state->pass_count, NXTEST_COUNT );
    for (i = 0; i < NXTEST_COUNT; i++)
    {
        const char *status;
        switch (state->results[i])
        {
        case NXTEST_RESULT_PASS:    status = "PASS";    break;
        case NXTEST_RESULT_FAIL:    status = "FAIL";    break;
        case NXTEST_RESULT_UNKNOWN: status = "UNKNOWN"; break;
        default:                    status = "NOT RUN"; break;
        }
        if (state->error_msg[i][0])
            nxdebug_log( debug, "  %-8s %-8s %s", nxtest_defs[i].label, status, state->error_msg[i] );
        else
            nxdebug_log( debug, "  %-8s %-8s", nxtest_defs[i].label, status );
    }
}
