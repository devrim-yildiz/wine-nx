# Switch Shims

Fakes for libnx-only pieces so parts of the Switch bring-up can run on a
host machine (macOS/Linux) without a console or emulator.

## Presentation path (framebuffer + touch)

### The plan going in, and why it changed

The original plan was a fake `<switch.h>` in this directory: give
`wine-nx-probe/source/runtime.c` a header that satisfies its libnx types and
calls with host equivalents (SDL2 for the framebuffer, pthreads for the
rest), then compile the real `runtime.c` against it on macOS.

That doesn't work. `runtime.c`'s `call_pe_entry_point()` contains inline
AArch64 assembly (`mov x16, ...; blr x16; ...`) that hands off to the loaded
PE entry point using Switch/AArch64 calling-convention register juggling
(`x18` swap for the TEB, etc.). No amount of header shimming makes that
compile with a host compiler -- it's not a libnx dependency, it's literally
target-architecture machine code baked into the file. `runtime.c` also mixes
that PE-loader logic with the framebuffer/touch code we actually care about
in the same translation unit, via `extern wine_nx_loader_*` calls that pull
in the rest of the PE loading machinery.

So instead of faking libnx and compiling the real `runtime.c`, the host sim
is a **new** file that reimplements just the five hook functions
`winnx_drv.c` actually calls, with SDL2 standing in for libnx:

```text
dlls/win32u/winnx_host_sim.c
```

`winnx_drv.c` itself calls zero libnx symbols directly -- it only calls
`wine_nx_fb_lock/unlock/present()`, `wine_nx_touch_poll()`, and
`wine_nx_runtime_trace()`. On hardware, `runtime.c` implements those via
`framebufferCreate/Begin/End/MakeLinear`, `hidGetTouchScreenStates`, etc.
`winnx_host_sim.c` implements the same five hooks with SDL2 instead, so the
real, unmodified `winnx_drv.c` links against it and runs.

### Why the file lives in dlls/win32u/, not here

It had to go in `dlls/win32u/` rather than this directory because Wine's
`makedep` build tool expects a module's `Makefile.in` `SOURCES` to stay
inside the module's own directory tree (no `../` escapes) -- see other
entries like `dibdrv/bitblt.c` for the pattern it expects. This directory
remains the place to describe *what* is being faked and *why*; the linkable
object lives wherever Wine's build system requires it.

### Why an env var, not a compile-time macro

`driver.c` already had a `#ifdef __SWITCH__` block wiring `winnx_drv.c`'s
functions into the null user driver. The host sim needed the same wiring,
but gating it behind another compile-time macro would mean a second build
configuration just for this. Instead it's a runtime check:

```sh
WINE_NX_HOST_SIM=1 wine ...
```

`winnx_drv.c`/`winnx_host_sim.c` are always compiled into a host build's
`win32u.so` (so SDL2 becomes a build-time dependency of any host build --
`brew install sdl2`), but a normal host Wine run is unaffected unless this
env var is set, since `load_display_driver()` only takes the host-sim branch
when it's present. No separate configure invocation needed to switch
between "real host Wine" and "host presentation sim" -- just the env var.

See `SDL2_CFLAGS`/`SDL2_LIBS` in `dlls/win32u/Makefile.in` (hardcoded to the
Homebrew prefix for now -- `configure.ac` has no real SDL2 detection yet).

### What's NOT faithfully simulated

- Multi-touch: only one contact (the host mouse) is ever reported. Gestures
  and multi-window popup interactions that depend on more than one
  simultaneous contact aren't exercised.
- Presentation performance: SDL2's texture-upload/present cost has nothing
  to do with the Switch's block-linear framebuffer conversion, which is the
  presentation-cost model investigated in wine-nx-probe/perf-lab-log.md
  (block-linear conversion turned out not to be the dominant cost, but the
  point stands: SDL2 tells you nothing about real present cost). This sim is for rendering/input *correctness*, not for
  reproducing or validating fixes to that performance problem.
- Window close: `SDL_QUIT` currently calls `exit(0)` directly. There's no
  host-side path back to a real `WM_CLOSE`/HOME-menu-equivalent yet.

## NTDLL/loader backend (out of scope so far)

`dlls/ntdll/unix/horizon.c` (the Horizon NTDLL unix backend: process/thread
setup, JIT/code-memory mapping via `svcMapProcessCodeMemory`,
`virtmemFindCodeMemory`, thread core affinity, etc.) is untouched and has no
host simulation yet. It stays real-hardware/emulator-only for now. A host
sim of that would need a fake `<switch.h>` covering its `svc*`/`virtmem*`/
`env*` surface -- tracked separately, not attempted here.

`check-wine-horizon.sh` / `check-wine-ntdll-switch.sh` in this directory's
parent predate the current repo layout (they reference `Proton/wine/...`
paths that no longer exist here) and are stale; they're syntax-only checks
against a real devkitPro toolchain anyway, not a host sim.
