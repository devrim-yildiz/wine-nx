# Performance Lab Log

This is the detailed investigation trail behind the performance numbers in
the top-level README — every bug found, every hypothesis tested (including
the ones that turned out wrong), and the hardware numbers behind each
change. If you just want current status, read the README instead; this is
for anyone digging into *why* a number is what it is, or debugging a
similar class of problem.

Organized in two parts: the native ARM64 GDI pipeline investigation, and
the 32-bit WOW64/box64 guest path investigation.

## Part 1: Native ARM64 GDI Pipeline (2fps → 8fps)

### Presentation Is Still Too Slow

CPU headroom remains, which strongly suggests the bottleneck is presentation
and synchronization, not raw CPU rasterization.

The current libnx Framebuffer path uses `framebufferMakeLinear()`. That makes
the driver easy to write, but every `framebufferEnd()` converts the full
1280x720 shadow buffer into block-linear layout before queueing it to the
Switch compositor. Even if Wine only changes a small menu highlight, the
present path can still become expensive.

The current batching patch reduces how often this happens, and a present-rate
cap (~60Hz, in `wine_nx_fb_present()`) was added on top of that so redundant
full-buffer conversions beyond what the display can even show get folded into
the next due present instead of each paying the full cost.

**First real hardware measurement (via the animated HUD demo):** this theory
does **not** hold up as the current bottleneck. Real numbers, both at stock
clock and under a CPU overclock:

| | FPS | attempted | executed | `framebufferEnd()` ms |
|---|---|---|---|---|
| overclocked | 2 | 2 | 2 | 3-4 |
| stock clock | 2 | 2 | 2 | 4-5 |

`framebufferEnd()` itself is fast — 3-5ms, nowhere near the ~500ms/frame
that a steady 2fps implies — and attempted == executed means the ~60Hz
throttle isn't gating anything; every attempt is already going through. The
full-buffer block-linear conversion is not the bottleneck.

**Root cause found (partially) and fixed, hardware-confirmed 2fps → 4fps:**
deko3d's own GPU-sync points (`dkFenceWait`, `dkQueueAcquireImage`,
submit+signal+present) were timed directly and are all sub-millisecond, so
the bottleneck is not presentation-mechanism-specific — it affects both the
libnx and deko3d backends equally. Per-phase timing added to the GUI smoke
test's own message loop (`gui_smoke.c`) pointed at `paint_avg` (the
`InvalidateRect`+`UpdateWindow` span) as the outlier: ~800ms/frame, dwarfing
dispatch/update/sleep. Tracing *why* found two stacked problems, both real:

1. **Every NT syscall was being traced with a synchronous SD-card
   `fflush()` per line.** `wine_nx_do_syscall()`
   (`dlls/ntdll/unix/signal_arm64.c`) logs syscall entry/exit for
   essentially every PE→native syscall, and `log_line()`
   (`wine-nx-probe/source/runtime.c`) `fflush()`s the log file on every
   single call — and that log file lives on the SD card, so every traced
   syscall was a real, synchronous filesystem-IPC write. A single
   `WM_PAINT` dispatch in the GUI smoke test alone produced ~1440 of these.
2. **`draw_pixel_ramp()` in `gui_smoke.c` drew a 240px gradient with 720
   individual `SetPixel()` calls** (3 rows x 240 columns), each one a full
   syscall round-trip — by far the largest single contributor.

Fixed both: raw per-syscall tracing gated behind
`wine_nx_syscall_trace_enabled` (off by default); `draw_pixel_ramp()` now
blits the gradient with a single `StretchDIBits()` call.

**Result: 2fps → 4fps.** `paint_avg` dropped from ~800ms to ~270-300ms/frame.
Still ~95% of total frame time. Of that, three synchronous IPC round-trips
hidden inside `InvalidateRect`+`BeginPaint` (`redraw_window`,
`get_update_region`, `get_visible_region`) account for roughly 25%
(~70-80ms/frame, at a ~14ms-per-call transport floor that turned out to be
universal across every IPC request type). The rest (~200ms/frame) needed
further tracing.

The Vulkan/NVK question was resolved along the way: checked directly
against the devkitpro/devkita64 toolchain — no Vulkan-capable driver
exists in it, and NVK specifically can't run under Horizon OS (it depends
on Linux's `nouveau` DRM kernel driver, which Horizon doesn't have).
**deko3d** (Switch homebrew's own low-level GPU API) is the real working
path, hardware-confirmed at 60-62fps as a standalone smoke test (device/
queue/swapchain/present, texture upload, real shader-driven 3D geometry —
see `wine-nx-probe/3d-accel-scoping.md`).

Wiring deko3d into the main runtime as `wine_nx_fb_lock/unlock/present`
consistently failed at swapchain/display creation — root-caused to
`nwindowGetDefault()` unconditionally opening a `vi` display before
`main()` runs, before deko3d gets a chance to be the first consumer. Fixed
with a separate build target (`wine-nx-runtime-deko3d`, compiled with
`WINE_NX_DEKO3D_ONLY`) that never links in the libnx-framebuffer code path
at all, so deko3d really is the first and only `vi` consumer.
**Hardware-confirmed working end-to-end.**

### The ~14ms Per-Call IPC Floor

Every `wine_server_call()` round-trip through `horizon.c`'s in-process pipe
transport costs roughly 14-15ms, regardless of request type. Checked
exhaustively for an avoidable sleep — none found; every
`usleep`/`nanosleep`/`svcSleepThread` call in the relevant files was traced
and confirmed unrelated to this path. The transport itself is a plain
`pthread_mutex` + `pthread_cond_wait`/`signal` ring buffer, no polling
interval. The floor is consistent with genuine Horizon OS thread-wake/
scheduling latency, not an application-level bug — meaning the only real
levers are fewer round-trips (batching) or skipping them outright
(caching).

### Where fps Actually Landed: 5-6 → 8

Combining the fflush-per-call fix (found and independently fixed in *six*
separate locations — `trace_surface_samples()`, `winnx_drv.c`'s per-pixel
trace, three call sites in `wine_nx_deko3d_trace()`, `dibdrv/dc.c`'s
`windrv_StretchDIBits`/`PutImage`, and `message.c`'s `nx_trace_winproc()`,
all rate-limited the same way), a provably-safe skip of `update_now()`'s
redundant second `get_update_flags()` call per `WM_PAINT` (gated behind
`WINE_NX_SKIP_REDUNDANT_UPDATE_CHECK`), and a fix to `windrv_StretchDIBits`/
`PutImage`:

- **IPC round trips per paint cycle: 3, down from 5** — `redraw_window` +
  `get_update_flags_ex` + `get_paint_regions`, confirmed via a 1:1:1
  call-count ratio.
- **`blit_avg`: ~19-20ms → ~2ms** (~90% reduction).
- **`presents/sec`: stable 8**, up from the 5-6 baseline.
- **`paint_avg` settled at ~113-116ms**, down from ~198-241ms.

A batched `get_paint_regions` request (folding `get_update_region` +
`get_visible_region` into one round trip, protocol ID 310) was implemented
and hardware-confirmed working correctly (~96% hit rate, zero validation
mismatches) — but **did not move fps**: summing the traced sub-phases came
to ~121ms against a ~200-210ms `paint_avg`, meaning an unidentified
~80-90ms/frame gap dwarfed the ~14ms this change removed. That gap was
never fully closed in this investigation; whether it's unmeasured drawing
cost outside the traced span, or an external present-rate cap, is the open
question for whoever picks this back up.

`GetTickCount`/`GetTickCount64` were also found frozen platform-wide
(`user_shared_data->TickCount` is only ever written by real Wine's
`wineserver` poll loop, which this port never runs) — fixed on the
internal/native-facing side via a real `armGetSystemTick()`-backed clock in
`NtGetTickCount()`; the PE-visible `kernel32.dll` APIs read
`user_shared_data` directly and were out of scope for that fix (later
addressed for the WOW64 guest path specifically — see Part 2).

## Part 2: 32-bit WOW64/box64 Guest Path (1fps → 60fps)

### Boot-Sequence Bug Chain

Getting a real, unmodified 32-bit Windows game (OpenTTD, no bundled DLLs)
to boot at all surfaced a chain of boot-sequence bugs a synthetic test
could plausibly never have exercised, each hiding the next until the
previous one was fixed:

- `__SWITCH__` was never defined when building the PE-format DLLs, only
  the native-static build — every `#ifdef __SWITCH__` block in the loader
  was silently absent from the guest's own ntdll.
- The guest's initial thread context never had `Eax`/`Ebx`/`Eip` set at
  all — real Wine gets these from native OS thread-creation before
  `LdrInitializeThunk` runs, which this port's handoff bypassed entirely.
- Once fixed, `Esp` landed exactly on `StackBase` with no headroom, so
  `RtlUserThreadStart`'s first instruction write-faulted immediately.
- `TlsSlots[WOW64_TLS_USERCALLBACKDATA]` was never explicitly cleared — a
  `longjmp()` inside stock Wine's `wow64_NtCallbackReturn` read garbage
  from it.
- `signal_set_full_context()` was an unconditional
  `STATUS_NOT_IMPLEMENTED` stub — every guest `NtContinue` call was a
  silent no-op.
- Wine's own `longjmp` (NTDLL.@) goes through `RtlUnwind()`, which needs
  `.pdata`/`.xdata` metadata for every native frame it crosses — box64's
  own JIT/dispatch frames have none, so the walk silently failed.
  Reimplemented as a direct register restore, matching `_setjmpex`'s save
  sequence.
- `GetTickCount`/`GetTickCount64` were wired to the already-fixed
  `NtGetTickCount()` for the guest-facing entry points specifically.

With all of the above fixed, the guest boots, loads its DLLs, creates real
windows, and paints real GDI content — confirmed with both OpenTTD (a real
message-box dialog rendered on screen) and `cube32`
(`wine-nx-probe/samples/cube32/`, a purpose-built i686-w64-mingw32 smoke
test — the existing `gui_smoke.c` demo is aarch64-windows native and never
touches the WOW64/box64 path at all).

### Performance: 1fps → 21fps → 60fps

`cube32` initially measured around 1fps. Three hardware rounds, each
finding a real cause and re-testing:

**Round 1 (1fps → 21fps).** Direct count against the actual hardware log:
**19,358 of 23,067 lines (84%) were `[SYSCALL]` trace lines** — per-syscall
diagnostic tracing added while chasing the boot-sequence bugs above, each
unconditionally `fflush()`ing to the SD card on every syscall crossing the
WOW64 boundary (~9,679 syscalls in one short capture). Same bug class as
Part 1's fflush storm, just never rate-limited on this path. It was still
on because `build-switch.sh`'s `openttd` app-kind branch force-wrote
`systrace.txt=1` to debug the (by-then-fixed) silent boot hang and never
turned it back off; `cube32`, having no app-kind of its own, inherited the
leftover flag from whatever SD card state existed. Fixed: rate-limited the
syscall trace to 5 samples, `build-switch.sh` now defaults `systrace.txt`
to `0`, and `cube32` got a real `WINE_NX_APP=cube32` build target instead
of manual staging. Also force-rebuilt `win32u.dll` for i386-windows, since
its unity-build `main.c` means `make`'s dependency tracking can silently
miss edits to files it `#include`s — no guarantee the deployed 32-bit DLL
had the native path's paint/redraw fixes at all.

**Round 2 (21fps → still 21fps on the surface, two more causes found and
fixed).** A follow-up log showed `[SYSCALL]` correctly capped at 5, but two
more instances of the exact same bug turned up:
- **Box64's own dynarec-block logging.** `BOX64_LOG=2` (box64's own log
  level: `LOG_NONE=0`/`LOG_INFO=1`/`LOG_DEBUG=2`) was hardcoded into every
  guest process's environment unconditionally — box64 tracing every
  translated x86 block, forever. Fixed to default `0`, only raised to `2`
  when explicitly tracing.
- **`[NX-DIAG]` box64-side traces bypassing `BOX64_LOG` entirely.** 9
  `printf_log()` call sites added while chasing the boot bugs
  (`BTCpuSetContext`, `BTCpuSimulate`, `calculate_fs`, etc., in the
  vendored `wowbox64.c`) used `LOG_NONE`, which always prints regardless
  of `BOX64_LOG` — unlike every other call site in that file (`LOG_DEBUG`).
  Measured at 1,171 of 3,312 log lines (35%). Changed all 9 to `LOG_DEBUG`.

**Round 3 (confirmed fixed, new bottleneck found and fixed).** A follow-up
log confirmed both Round 2 fixes held (`[NX-DIAG]` dropped from 1,171 to
2), but turned up a fourth instance: `[NXFONT] select_cached` (win32u's
font-cache-*hit* trace, fired on every `TextOutW`-class call once a font is
already loaded) was 3,945 of 5,753 lines (69%) — rate-limited the same way.

**Result: 60fps**, hardware-confirmed — the same present-rate cap the
native ARM64 path already sits at, i.e. genuine parity with the platform's
own refresh rate, not an emulation-imposed ceiling. All four fixes across
these three rounds were the exact same bug pattern in four different
places: unconditional `fflush()`-to-SD-card diagnostic logging added while
chasing the boot-sequence bug chain, never rate-limited or turned back off
once those bugs were fixed. None of it was a real WOW64/box64 architectural
limitation.
