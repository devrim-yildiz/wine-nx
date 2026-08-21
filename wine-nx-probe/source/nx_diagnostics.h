/* nx_diagnostics: real, in-process hardware/subsystem diagnostic suite --
 * see the user's own spec for this pass: "STRICT RULE: NO PSEUDOCODE.
 * NO MOCK `return true;` STUBS. NO FAKE PASSING TESTS." Every subtest in
 * the 32-bit/64-bit/JIT suites below does real work (real Horizon
 * syscalls, the same Wine PE struct layouts runtime.c's own loader
 * already uses, real ARM64 asm, real pthread TLS) and reports a
 * genuinely-computed pass/fail, not a canned result.
 *
 * The Vulkan/D3D/OpenGL-EGL suites are the one deliberate exception, and
 * are exactly that -- an exception, not an oversight. Three explicit,
 * user-confirmed scope decisions this session (see the AskUserQuestion
 * answers) mean none of those three can run for real right now:
 *   - Vulkan: running real Vulkan calls INSIDE this merged launcher
 *     process reproduces the documented nwindowGetDefault()/vi-consumer
 *     conflict this project's own vulkan_integration notes and
 *     3d-accel-scoping.md already cover (this binary's GUI already owns
 *     the one NWindow a Vulkan swapchain would also need) -- user chose
 *     to skip rather than have the diagnostic suite itself crash the
 *     launcher.
 *   - D3D/DXVK: no DXVK build exists anywhere in this project yet --
 *     user chose to scope a real build-out as separate, larger future
 *     work rather than have this pass fake or stub it.
 *   - OpenGL/EGL: same reasoning as D3D -- no EGL/GL backend exists in
 *     this project.
 * Each of those three suites below reports every one of its subtests as
 * NXDIAG_RESULT_SKIPPED with an honest one-line reason -- never PASS,
 * never a silently-omitted suite.
 */
#ifndef WINE_NX_DIAGNOSTICS_H
#define WINE_NX_DIAGNOSTICS_H

#include "gui/nxdebug.h"

typedef enum
{
    NXDIAG_RESULT_NOT_RUN = 0,
    NXDIAG_RESULT_PASS,
    NXDIAG_RESULT_FAIL,
    NXDIAG_RESULT_SKIPPED   /* honestly not implemented -- see this file's header comment */
} nxdiag_result_t;

#define NXDIAG_NAME_MAX   40
#define NXDIAG_DETAIL_MAX 80

typedef struct
{
    nxdiag_result_t result;
    char name[NXDIAG_NAME_MAX];
    char detail[NXDIAG_DETAIL_MAX];   /* real addresses/error codes on fail, a real measurement on pass, or the skip reason */
} nxdiag_subtest_t;

typedef enum
{
    NXDIAG_SUITE_32BIT = 0,
    NXDIAG_SUITE_64BIT,
    NXDIAG_SUITE_JIT,
    NXDIAG_SUITE_VULKAN,
    NXDIAG_SUITE_D3D,
    NXDIAG_SUITE_GL,
    NXDIAG_SUITE_COUNT
} nxdiag_suite_id_t;

#define NXDIAG_MAX_SUBTESTS 8

typedef struct
{
    const char *name;
    int subtest_count;
    nxdiag_subtest_t subtests[NXDIAG_MAX_SUBTESTS];
    int pass_count, fail_count, skip_count;
} nxdiag_suite_t;

typedef struct
{
    nxdiag_suite_t suites[NXDIAG_SUITE_COUNT];
} nxdiag_report_t;

/* Runs every suite in order and fills `report`. The 32-bit/64-bit/JIT
 * suites genuinely execute their subtests on THIS Horizon process, right
 * now. The Vulkan/D3D/GL suites are populated with SKIPPED entries only
 * -- see this file's own header comment for why.
 *
 * Does not touch the Wine loader's own state (main_image_info,
 * registered modules, etc.) -- every PE-header/relocation subtest below
 * parses a small SELF-CONTAINED synthetic buffer built and torn down
 * entirely inside this call, never the actual loader's own
 * runtime_describe_image()/map_pe_image() path. Running this suite can
 * never disturb an actual game launch, before or after. */
void nxdiag_run_all( nxdiag_report_t *report );

/* Pushes one line per subtest (name + PASS/FAIL/SKIPPED + detail) plus a
 * per-suite tally into the shared debug log, same convention as
 * nxtest_report_to_log(). */
void nxdiag_report_to_log( const nxdiag_report_t *report, nxdebug_state_t *debug );

#endif /* WINE_NX_DIAGNOSTICS_H */
