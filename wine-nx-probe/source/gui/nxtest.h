/* nxtest: "auto test mode" -- runs the project's existing smoke-test
 * targets (the WINE_NX_APP=gui/blit/cube32/vulkan_test/
 * vulkan_triangle_test/cube64/notepad/curl set already defined in
 * build-switch.sh, each just a target.txt-selected .exe run under
 * wine-nx-runtime.nro or wine-nx-vulkan-runtime.nro) and reports
 * pass/fail per test plus a final "N/M PASSED" summary, matching the
 * user's own spec ("test are like the old jit test only log and shows
 * like 8/8 passed or whatever and shows error on screen").
 *
 * IMPORTANT, READ BEFORE WIRING THIS UP: this module only owns the
 * TEST QUEUE and the on-disk result protocol below -- it never calls
 * envSetNextLoad itself (same reasoning as every other gui/ module:
 * process lifecycle belongs to the launcher's main loop, task #29).
 * More importantly, THE OTHER HALF OF THE PROTOCOL DOES NOT EXIST YET:
 * for a test to actually report PASS/FAIL, wine-nx-runtime.nro's own
 * exit path (source/runtime.c) needs to be taught to (1) write
 * sdmc:/switch/wine/config.txt... no -- write RUNTIME_DIR/autotest_result.txt
 * with "PASS" or "FAIL:<message>", and (2) call envSetNextLoad() back
 * to the launcher instead of exiting to hbmenu, whenever a test chain
 * is in progress (detectable by RUNTIME_DIR/autotest_index.txt
 * existing). That is a change to a small, already hardware-debugged
 * file (runtime.c's clean-exit sequence has its own hard-won history --
 * see its own comments) and was deliberately NOT made as a drive-by
 * part of building this GUI feature. Until it lands, nxtest_resume()
 * will faithfully record every test as NXTEST_RESULT_UNKNOWN (no result
 * file was ever written) -- which is honest, not broken: it correctly
 * reports "this test's outcome isn't known yet" rather than fabricating
 * a pass.
 *
 * On-disk protocol (all paths under RUNTIME_DIR, sdmc:/switch/wine):
 *   autotest_index.txt   -- decimal index of the test currently
 *                            running / most recently launched. Its
 *                            mere presence means "a chain is in
 *                            progress"; nxtest_resume() removes it once
 *                            the whole queue is done.
 *   autotest_result.txt  -- written by the test runtime just before it
 *                            hands control back: "PASS\n" or
 *                            "FAIL:<short message>\n". Consumed (read
 *                            then deleted) by the next nxtest_resume().
 *   autotest_results.txt -- this module's own append-only history, one
 *                            "index|PASS" / "index|FAIL|msg" /
 *                            "index|UNKNOWN|msg" line per completed
 *                            test, so the full result set survives
 *                            across every relaunch in the chain and can
 *                            be replayed once the chain finishes.
 *
 * NOT YET HARDWARE-VALIDATED, and functionally incomplete until the
 * runtime.c half above is written -- see that note again if you're
 * about to assume this "just works" end to end.
 */
#ifndef WINE_NX_TEST_H
#define WINE_NX_TEST_H

#include "nxgui.h"
#include "nxdebug.h"
#include <stddef.h>

typedef enum
{
    NXTEST_RESULT_NOT_RUN = 0,
    NXTEST_RESULT_PASS,
    NXTEST_RESULT_FAIL,
    NXTEST_RESULT_UNKNOWN   /* chain advanced past this test but no result file was ever found for it */
} nxtest_result_t;

/* 9, not 8: wine-nx-jit-smoke.nro (source/jit_smoke.c) plus the 8
 * WINE_NX_APP kinds from build-switch.sh. jit-smoke is the ONE entry in
 * this table that's actually wired end to end -- see jit_smoke.c's own
 * "chained mode" handling, added alongside this module specifically so
 * at least one real result exists without waiting on the larger
 * runtime.c change described above. It's also, in all likelihood, "the
 * old jit test" the user's own request referred to: it already reports
 * pass/fail out of a fixed round count (8, hence "8/8 passed") and
 * that framing predates this GUI effort entirely -- see jit_smoke.c's
 * own header and its `rounds = 8` constant. */
#define NXTEST_COUNT 9
#define NXTEST_MSG_MAX 96

typedef struct
{
    int active;         /* a chain was started or resumed this process lifetime */
    int pass_count;
    int run_count;      /* how many of NXTEST_COUNT have a recorded (non-NOT_RUN) result */
    nxtest_result_t results[NXTEST_COUNT];
    char error_msg[NXTEST_COUNT][NXTEST_MSG_MAX];
} nxtest_state_t;

void nxtest_init( nxtest_state_t *state );

/* Begins a fresh run from test 0: resets any prior on-disk state,
 * writes target.txt + autotest_index.txt for the first test, and fills
 * out_nro with which runtime .nro the caller should envSetNextLoad to
 * right away. Returns 1 (NXTEST_COUNT is a fixed compile-time constant
 * > 0, so this can't currently fail, but the return is checked like
 * any other I/O-touching call). */
int nxtest_start( nxtest_state_t *state, char *out_nro, size_t out_nro_size );

/* Call once at launcher startup, before drawing anything. If
 * autotest_index.txt exists, a chain is resuming: this records the
 * just-finished test's result (from autotest_result.txt, or UNKNOWN if
 * that file is missing), and either
 *   - queues the next test and returns 1 with out_nro filled (the
 *     caller must envSetNextLoad to it immediately, without ever
 *     drawing a frame this process lifetime), or
 *   - finishes the run, replays the full result history from
 *     autotest_results.txt into `state`, removes the on-disk markers,
 *     and returns 0 (state->run_count > 0 signals "a run just
 *     completed, show the summary").
 * If no chain was in progress at all, returns 0 immediately with
 * `state` left at nxtest_init() defaults (run_count == 0). */
int nxtest_resume( nxtest_state_t *state, char *out_nro, size_t out_nro_size );

/* Full-screen PASS/FAIL/NOT RUN/UNKNOWN list plus the "N/M PASSED"
 * headline, drawn the same way every other gui/ screen draws (clear +
 * header + rows), for whatever screen shows results after a run. */
void nxtest_draw_summary( const nxtest_state_t *state, nxgui_surface_t *surf );

/* Pushes one PASS/FAIL/UNKNOWN line per test (plus a final tally line)
 * into the shared debug log -- the user's own "shows error on screen -
 * more debugs for the buggy stuff". Safe to call whether or not the
 * debug overlay is currently visible. */
void nxtest_report_to_log( const nxtest_state_t *state, nxdebug_state_t *debug );

#endif /* WINE_NX_TEST_H */
