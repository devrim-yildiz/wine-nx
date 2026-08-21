/* wine_nx_boot_and_run_target(): the exact PE-map-and-run sequence
 * source/runtime.c's own main() has always used, extracted into a
 * callable, non-static entry point so wine-nx-launcher's nx_arch.c
 * (the merged single-NRO GUI+engine build) can call the identical,
 * unmodified path in-process, not a reimplementation of it.
 *
 * CRITICAL, unchanged from before this ever existed as a separate
 * function: wine_nx_x64_handoff()/wine_nx_wow64_handoff() never
 * return once a guest starts running successfully. A return from
 * wine_nx_boot_and_run_target() therefore only ever means "something
 * failed before any guest code ran" -- there is no "the game closed,
 * here's your return value" case. When a guest process really exits,
 * the whole Horizon process exits with it. See runtime.c's own comment
 * on this function for the full reasoning.
 */
#ifndef WINE_NX_RUNTIME_BOOT_H
#define WINE_NX_RUNTIME_BOOT_H

typedef enum
{
    WINE_NX_BOOT_NOT_RUN = 0,   /* image mapped fine; autorun was off, nothing was ever handed off */
    WINE_NX_BOOT_FAIL_TEB,
    WINE_NX_BOOT_FAIL_PROCESS_INIT,
    WINE_NX_BOOT_FAIL_MAP,
    WINE_NX_BOOT_FAIL_DESCRIBE,
    WINE_NX_BOOT_FAIL_PARAMS,
    WINE_NX_BOOT_FAIL_HANDOFF   /* the x64/wow64 handoff call itself returned -- only possible on failure */
} wine_nx_boot_result_t;

wine_nx_boot_result_t wine_nx_boot_and_run_target( const char *target, int autorun );

/* Runs the exact one-shot Wine-loader bootstrap wine_nx_boot_and_run_target()
 * itself starts with (directories, log file, backend/trace prefs,
 * wine_nx_runtime_platform_init(), virtual_init(), NLS/environment init,
 * then the real first TEB via virtual_alloc_first_teb()) if it hasn't
 * run yet this process, or returns the already-established TEB
 * immediately if it has -- idempotent and safe to call from anywhere,
 * any number of times, including before the user has picked a game.
 *
 * Exists so a diagnostic can exercise the REAL NtAllocateVirtualMemory
 * path (which requires virtual_init() to have already set up its own
 * mutex/view tables -- calling it without this would crash, not
 * gracefully fail) without duplicating or racing the one-shot bootstrap
 * a real game launch also needs. Triggering it early from a diagnostic
 * doesn't waste or repeat that work -- the real launch path's own call
 * just becomes a no-op early-return once this has already run.
 *
 * Returns NULL on failure (matches WINE_NX_BOOT_FAIL_TEB's own
 * condition in wine_nx_boot_and_run_target() -- see that function's
 * body for exactly what "failure" means here). Declared as `struct
 * _TEB *` rather than pulling in winternl.h's full TEB typedef, purely
 * to keep this header's own zero-include footprint -- callers that
 * already have winternl.h's TEB visible (nx_diagnostics.c does) can
 * treat the two interchangeably; they name the same struct. */
struct _TEB *wine_nx_ensure_lazy_init( void );

#endif /* WINE_NX_RUNTIME_BOOT_H */
