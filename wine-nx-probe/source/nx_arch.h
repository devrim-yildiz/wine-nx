/* nx_arch: the merged single-NRO build's in-process launch entry
 * point -- see CMakeLists.txt's own comment on wine-nx-launcher for
 * the overall shape of this merge.
 */
#ifndef WINE_NX_ARCH_H
#define WINE_NX_ARCH_H

#include "runtime_boot.h"

/* Launches `target_path` IN-PROCESS, replacing the separate-binary
 * envSetNextLoad+exit handoff for GDI-backend games (Vulkan-backend
 * games still go through that separate path -- see launcher_main.c's
 * resolve_runtime_nro(), unchanged by this merge). Calls straight into
 * wine_nx_boot_and_run_target() (source/runtime.c) -- the exact same
 * Wine loader/box64 engine wine-nx-runtime.nro itself uses, not a
 * second implementation of it.
 *
 * CALLER MUST have already released its own framebuffer
 * (framebufferClose() then nwindowReleaseBuffers(nwindowGetDefault()))
 * before calling this -- the loader's own eventual GDI presentation
 * (reached lazily, the first time the running guest actually draws a
 * window) needs sole ownership of the same NWindow this binary's own
 * GUI was just presenting through; libnx's Framebuffer/NWindow model
 * is one-producer-at-a-time, mirroring the exact release pattern
 * source/runtime.c's own wine_nx_fb_init() already uses for its
 * console-to-framebuffer handoff.
 *
 * This function can ONLY return on a failure that happened before any
 * guest code ran -- see runtime_boot.h's own comment: a successful run
 * never returns at all. By the time it does return, this process's own
 * TEB/PEB and registered-module state have already been partially
 * mutated in ways this launcher's own GUI was never built to coexist
 * with. The caller should log the failure and exit the process
 * cleanly, not try to resume the GUI -- exactly how a fully-failed
 * launch already behaves in the separate wine-nx-runtime.nro binary
 * today (it parks instead of recovering), not a new limitation this
 * merge introduces. */
wine_nx_boot_result_t nx_arch_launch_exe( const char *target_path );

#endif /* WINE_NX_ARCH_H */
