/* See nx_arch.h. NOT YET HARDWARE-VALIDATED (the merge itself is new;
 * wine_nx_boot_and_run_target() underneath it is the same
 * hardware-validated logic wine-nx-runtime.nro has always used, just
 * called from a different process context).
 */
#include "nx_arch.h"

#include <stdio.h>

/* Kept in sync purely for compatibility/diagnostics (inspectable on
 * the SD card mid-run, matching every other runtime binary's own
 * target.txt) -- wine_nx_boot_and_run_target() itself takes
 * target_path directly as a parameter below, it does not re-read this
 * file the way runtime.c's own standalone main() does at cold start. */
static void nx_arch_write_target_file( const char *native_path )
{
    FILE *f = fopen( "sdmc:/switch/wine/target.txt", "w" );
    if (!f) return;
    fprintf( f, "%s\n", native_path );
    fclose( f );
}

wine_nx_boot_result_t nx_arch_launch_exe( const char *target_path )
{
    nx_arch_write_target_file( target_path );
    return wine_nx_boot_and_run_target( target_path, 1 );
}
