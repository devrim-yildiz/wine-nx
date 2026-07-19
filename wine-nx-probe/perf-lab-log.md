# wine-nx performance lab log (chronological)

This is the running investigation journal that used to live inside the
top-level README's "Current Problems" section, moved here verbatim so the
README can describe *current* state while this file preserves *how we got
there* -- the theories, the dead ends, the hardware numbers, and the fixes,
in the order they actually happened. Sections are chronological: numbers
stated in present tense ("the 5fps problem") were current *when written*
and are superseded by later sections. The final entry's numbers ("Where fps
Actually Landed": stable 8fps, 3 IPC round trips per paint cycle) are the
ones the README's status reflects.

New hardware sessions should append dated sections at the bottom.

---
## Presentation Is Still Too Slow

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

**Update, first real hardware measurement (via the animated HUD demo, see
below):** this theory does **not** hold up as the current bottleneck. Real
numbers, both at stock clock and under a CPU overclock:

| | FPS | attempted | executed | `framebufferEnd()` ms |
|---|---|---|---|---|
| overclocked | 2 | 2 | 2 | 3-4 |
| stock clock | 2 | 2 | 2 | 4-5 |

`framebufferEnd()` itself is fast -- 3-5ms, nowhere near the ~500ms/frame
that a steady 2fps implies -- and attempted == executed means the ~60Hz
throttle isn't gating anything right now; every attempt is already going
through. The full-buffer block-linear conversion is not the bottleneck.

**Update, root cause found (partially) and fixed, hardware-confirmed 2fps
-> 4fps:** deko3d's own GPU-sync points (`dkFenceWait`, `dkQueueAcquireImage`,
submit+signal+present) were timed directly and are all sub-millisecond, so
the bottleneck is not presentation-mechanism-specific -- it affects both the
libnx and deko3d backends equally. Per-phase timing added to the GUI smoke
test's own message loop (`gui_smoke.c`) pointed at `paint_avg` (the
`InvalidateRect`+`UpdateWindow` span) as the outlier: ~800ms/frame, dwarfing
dispatch/update/sleep. Tracing *why* found two stacked problems, both real:

1. **Every NT syscall was being traced with a synchronous SD-card
   `fflush()` per line.** `wine_nx_do_syscall()`
   (`dlls/ntdll/unix/signal_arm64.c`) logs syscall entry/exit for
   essentially every PE->native syscall, and `log_line()`
   (`wine-nx-probe/source/runtime.c`) `fflush()`s the log file on every
   single call -- and that log file lives on the SD card
   (`sdmc:/switch/wine/...`), so every traced syscall was a real,
   synchronous filesystem-IPC write. A single `WM_PAINT` dispatch in the
   GUI smoke test alone produced ~1440 of these (see next point), enough
   at even a fraction of a millisecond each to plausibly account for most
   of the measured 800ms.
2. **`draw_pixel_ramp()` in `gui_smoke.c` drew a 240px gradient with 720
   individual `SetPixel()` calls** (3 rows x 240 columns), each one a
   full syscall round-trip -- by far the single largest contributor to
   the per-frame syscall-trace volume above.

Fixed both: the raw per-syscall trace in `signal_arm64.c` is now gated
behind `wine_nx_syscall_trace_enabled` (off by default; opt in via
`WINE_NX_SYSCALL_TRACE` env var or `sdmc:/switch/wine/systrace.txt`, same
file-fallback pattern as `deko3d.txt`, since hbmenu launches don't reliably
see a real process environment) -- when off, neither the trace call nor its
`fflush()` runs. `log_line()` itself, and every other (far lower-frequency)
trace call site (`[NXIPC]`, `[NXDRV]`, `[NXDK]`, HUD stats), is untouched --
those still `fflush()` every write, since they need to survive a crash and
aren't high-frequency enough to matter here. `draw_pixel_ramp()` now blits
the gradient with a single `StretchDIBits()` call instead of 720 `SetPixel`
calls.

**Result, hardware-confirmed with tracing off by default: 2fps -> 4fps.**
`paint_avg` dropped from ~800ms to ~270-300ms/frame. That's real progress,
not the full answer -- `paint_avg` is still ~95% of total frame time. Of
that remainder, three synchronous IPC round-trips hidden inside
`InvalidateRect`+`BeginPaint` (`redraw_window`, `get_update_region`,
`get_visible_region`, all real handlers in `dlls/ntdll/unix/horizon.c`)
account for roughly 25% (~70-80ms/frame, at a ~14ms-per-call transport
floor that turned out to be universal across every IPC request type, not
specific to these three). The rest (~200ms/frame) is still unidentified --
next candidates to trace are GDI resource churn in the sample app itself
(three `HFONT`s created and destroyed from scratch every single frame in
`draw_scene()`/`draw_badge()`, each triggering full font-family/file/
charmap resolution, plus per-character `GetGlyphOutline` rasterization with
no cross-frame glyph cache) and the ~14ms generic IPC transport floor
itself (real syscall/FS-IPC cost vs. an avoidable sleep/poll interval,
not yet determined).

The real performance step is still a GPU presentation path: upload Wine
DIB/window surfaces into GPU textures, composite them, and present once per
frame. **The Vulkan/NVK question is resolved:** checked directly against the
devkitpro/devkita64 toolchain image (the same one `build-switch.sh` builds
with) -- there is no Vulkan, NVK, or any Vulkan-capable driver anywhere in
it, and NVK specifically cannot run under Horizon OS: it's built on Linux's
`nouveau` DRM kernel driver (GEM/TTM buffer management, the Linux DRM/KMS
uAPI), and Horizon has no DRM subsystem at all -- Switch homebrew talks to
the GPU through Horizon's own `nvhost`/`nvmap` IPC services instead. That
rules out NVK, the only Vulkan implementation that exists for this
hardware today; it isn't a claim that no Vulkan driver could ever be
written against `nvhost`/`nvmap` directly. The only way to run NVK itself
here would be booting a real Linux kernel on the console (Switchroot/L4T),
a completely different target than "Switch homebrew via libnx." Real Mesa
**OpenGL ES** is available instead (a Switch-native `libdrm_nouveau` shim
under the same old, pre-NVK Mesa), and **deko3d** (Switch homebrew's own
low-level GPU API) is available too -- both bind to the same
`nwindowGetDefault()` handle `wine_nx_fb_init()` already uses.

**deko3d is hardware-confirmed as a standalone smoke test, and as a real
compositor backend for the full Wine GUI stack, via a separate binary:**
the smoke test (see "Deko3d Bring-Up Smoke Test" above) proves device/
queue/swapchain/present, a CPU-to-GPU texture upload, and real
shader-driven 3D geometry all running together at 60-62fps. Wiring that
into `wine_nx_fb_lock/unlock/present` as an opt-in backend *inside
`wine-nx-runtime`* consistently failed at swapchain/display creation,
root-caused through hardware-log-driven diagnosis into libnx's own
`__appInit`/`__nx_win_init` crt0 hook: that binary's required
libnx-framebuffer fallback references `nwindowGetDefault()`, which
unconditionally opens a `vi` display before `main()` even runs, before
deko3d ever gets a chance to be the first consumer -- the exact scenario
every real deko3d reference (Borealis, `deko_basic`, `deko_console`)
requires and none of them violate, because none of them ship a
runtime-selectable libnx/deko3d choice in one binary. Full root-cause
trace in `wine-nx-probe/3d-accel-scoping.md`.

The real fix was exactly what that trace called for: a separate build
target, not a change to the existing one. **`wine-nx-runtime-deko3d`**
(`wine-nx-probe/CMakeLists.txt`) compiles `source/runtime.c` a second time
with `WINE_NX_DEKO3D_ONLY` defined, which compiles out every
libnx-framebuffer/console code path entirely -- not just branches around
it at runtime, the symbol references themselves don't exist in this
binary, so `nwindowGetDefault()`/`consoleInit()` are never linked in and
deko3d really is the first and only `vi` consumer. **Hardware-confirmed
working end-to-end:** all six ViLayer/swapchain init steps succeed, and
the full GUI smoke test app (see "GUI Smoke Demo" above) renders and
presents real Win32 GDI output through it, including the fflush/SetPixel
performance investigation above, which was run against this exact binary.
`wine-nx-runtime` (the libnx-only binary) is untouched and behaves
byte-for-byte as before -- the opt-in `WINE_NX_DEKO3D` runtime flag inside
it is kept parked, not removed, since it can never work for the
`__appInit` reason above.

## The ~14ms Per-Call IPC Floor: Diagnosed, Not Yet Optimized

Both the message-dispatch investigation and the paint sub-phase tracing
above independently found the same thing: every `wine_server_call()`
round-trip through `horizon.c`'s in-process pipe transport costs roughly
14-15ms, regardless of request type -- including trivial ones. With
`window_surface_flush()`'s span alone issuing several of these
sequentially per frame (`redraw_window`, up to three `get_update_region`
calls, one `get_visible_region`), this floor adds up to a real, measurable
slice of `paint_avg`. Investigated three questions about it (30 minutes,
no hardware access this round) without landing any code changes -- every
lever found needs a hardware round-trip to verify safely, which this
window didn't have.

**Is it an avoidable sleep? No -- checked exhaustively.** Every
`usleep`/`nanosleep`/`svcSleepThread` call in `dlls/ntdll/unix/horizon.c`
and `dlls/ntdll/unix/server.c` was found and individually confirmed
unrelated to this path: `server.c`'s `usleep` is inside
`server_connect()`, the desktop-Wine socket-based wineserver connection
routine this port never invokes (`horizon.c` plays that role in-process
instead); `horizon.c`'s `usleep(50000)` is in
`horizon_sock_poller_thread`, a completely separate WinSock
(`WSAEventSelect`) background poller with no relationship to window
messages; the two `svcSleepThread(1s)` calls nearby are unrelated idle
loops. The actual transport (`horizon_pipe_write_r`/`horizon_pipe_read_r`
in `horizon.c`) is a plain `pthread_mutex` +
`pthread_cond_wait`/`signal` ring buffer (`HORIZON_PIPE_BUFFER_SIZE` =
64KB, far larger than the small request/reply structs and single-rect
region data these specific calls carry, so no multi-round-trip
buffer-full blocking either) -- no polling interval, no artificial delay
anywhere in the chain. The floor is consistent with genuine Horizon OS
thread-wake/scheduling latency between the calling thread and
`horizon_server_thread`, not an application-level bug. That means it
isn't fixable by deleting code -- the only real levers are fewer
round-trips (batching) or skipping round-trips outright (caching).

**A concrete, real redundancy exists, but it's stock Wine, not a Switch
bug -- not touched.** `get_update_region` fires roughly 3x more often
than `redraw_window`/`get_visible_region` per paint cycle. Traced to
`update_now()` (`dlls/win32u/dce.c`): its redraw loop calls
`get_update_flags()` (a `get_update_region` variant with
`UPDATE_NOREGION`) *before* dispatching `WM_PAINT`, `BeginPaint()`'s own
`send_ncpaint()` calls `get_update_region()` again once `WM_PAINT` is
actually handled, and the loop calls `get_update_flags()` a third time
afterward to check whether there's still more to repaint. All three
calls are unmodified upstream Wine control flow (not Switch-specific
code), designed to correctly handle multi-child/partial-repaint
scenarios this single-window smoke test doesn't exercise. Skipping any
of them without hardware verification risks silently breaking repaint
correctness for more complex real apps (Notepad, multi-window UIs) in
ways that wouldn't show up in this test -- exactly the kind of
regression that ships unnoticed and is hard to debug later. Scoped, not
fixed.

**What a batched request would need** (`redraw_window` +
`get_update_region` + `get_visible_region` -> one round-trip): a new
`HORIZON_REQ_*` request type in `horizon.c` whose server-side handler
internally calls the same three existing handler functions back-to-back
under one held lock (avoiding three separate acquire/release/wake
cycles), returning a combined reply struct. Client side, a new wrapper
in `dce.c` replacing the three `SERVER_START_REQ` blocks currently
spread across `redraw_window_rects()`, `get_update_region()`, and
`NtUserGetDCEx()`'s `update_visible_region()` call. A real wire-protocol
change touching every window-paint path on this port -- worth doing, not
something to land without hardware confirming the combined reply is
parsed correctly by all three original call sites' worth of logic.

**What safe caching would need** (skip `get_visible_region` when nothing
visibility-relevant changed since the last call): a cache keyed on
window handle, invalidated on every `WM_WINDOWPOSCHANGED`,
`WM_WINDOWPOSCHANGING`, parent/z-order change, and monitor change --
missing even one invalidation trigger produces a *silent* stale-region
bug (drawing clipped to an old visible rect, or not drawing into a
newly-visible area) that a quick visual check on hardware might not
catch. Same verdict: scoped, not built.

Diagnosis is solid -- both the `usleep` scan and the `get_update_region`
3x trace are grounded directly in source, not inference. The concrete
next step for either optimization is hardware-in-the-loop testing, not
more source reading.

## Autonomous Session Follow-Ups

Two further autonomous rounds, done on a checkpoint branch, since
hardware-verified, merged into `main`, and deleted. Full per-change
reasoning is in the commit history; summarized here, with hardware
results folded in below each item.

**Round 1** -- WM_ERASEBKGND (`erase`, ~47ms) split into
`erase_get_dcex`/`erase_clipbox`/`erase_wm_erasebkgnd`/`erase_release_dc`
sub-phases, same `switch_paint_trace()` gating as everything else.
The Minus-button crash-to-Home-Menu bug got granular step-by-step
logging through its whole exit sequence, plus a real (not just logged)
attempted fix: swapping the final termination call from libc's `exit(0)`
to `svcExitProcess()` -- the input-polling thread that handles Minus is
a background thread, not the main thread, and libc's `exit()` runs
atexit/destructor cleanup that could race whatever the main thread is
doing to GPU/socket state at that exact moment; `svcExitProcess()` is
the raw Horizon kernel primitive other homebrew reference examples rely
on instead. The frozen-`GetTickCount` write-side fix was investigated
and deliberately not attempted: `user_shared_data` is a fixed address
(`0x7ffe0000`) real Wine maps read-only for ordinary processes, and
writing to it without hardware to verify write access risks trading a
frozen clock for a hard crash. **Hardware-tested since: the crash
persists identically with the `svcExitProcess()` swap in place**,
ruling out the "libc `exit()`'s atexit path races the main thread's
GPU/socket state" theory this fix was built around. Root cause remains
unresolved; the step-by-step `[EXIT]` logging stays in place for
whenever this gets picked back up.

**Round 2** -- three more ideas, chosen freely:

1. **`NtGetTickCount()` fixed for real, on the read side instead of the
   write side.** `dlls/ntdll/unix/sync.c`'s `NtGetTickCount()` (used
   internally by code that calls it directly, e.g.
   `flush_window_surfaces()`'s debounce fixed earlier tonight) now
   computes elapsed time from `armGetSystemTick()`/`armTicksToNs()`
   instead of reading the never-written `user_shared_data->TickCount` --
   zero writes to any shared/system memory, same clock source already
   proven reliable all night. **Scope is real but partial:** this does
   *not* fix what a hosted PE application sees calling the Win32
   `GetTickCount()`/`GetTickCount64()` APIs -- `dlls/kernel32/sync.c`
   reads `user_shared_data` directly (the real-Windows fast-path design,
   bypassing a syscall on purpose), and `kernel32.dll` is built by a
   separate PE cross-compile pipeline this session never touched or
   verified. Fixing the PE-visible APIs needs either that separate,
   higher-blast-radius rebuild (kernel32 is the single most
   fundamental import every PE program has) or writing the
   possibly-read-only shared page -- both deliberately out of scope
   here. Logs a rate-limited `[NXTICK]` sample once/second so real,
   monotonically-increasing values are visible without reintroducing
   per-call fflush cost. **Hardware-confirmed: `[NXTICK]` samples show
   real, monotonically increasing values** (no longer frozen). This fix
   also had a real, initially-surprising side effect on
   `flush_window_surfaces()`'s idle debounce -- see "Two More Threads"
   below for how that was resolved.
2. **The paint-trace tier's own instrumentation overhead, fixed at the
   tool level.** `switch_paint_trace()` (`dce.c`) no longer
   `fflush()`es one line per call -- it accumulates each phase's
   sum/count/max in memory and flushes one combined `[NXPAINT][AVG]`
   line per second, the same pattern `gui_smoke.c`'s own
   `gui_timing.log` already uses. `winnx_drv.c`'s three timers
   (`trace_samples`/`fb_lock_call`/`fb_unlock_call`) now route through
   the same shared aggregator instead of their own raw calls, for one
   unified view. The `[NXPAINT][CALLER]` return-address trace is
   rate-limited to 5 samples instead of logging every frame forever,
   since the address should be constant now that both bugs it was
   built to find are fixed. This directly applies tonight's biggest
   lesson (diagnostic overhead compounding with itself) to the
   diagnostic tooling itself -- when `WINE_NX_PAINT_TRACE` is flipped
   on for debugging, the numbers it reports should now be much closer
   to real, since taking the measurement costs far less than it used
   to.
3. **Checked `fill_rect()`'s full-window `PatBlt` path for a
   SetPixel-style bug -- ruled out, nothing changed.** Directly
   informed by tonight's biggest confirmed win, so worth checking
   rather than assuming: traced `WM_ERASEBKGND`'s default handling
   (`fill_rect()`, `defwnd.c`) through `NtGdiPatBlt` into dibdrv's
   `solid_rects_32()` (`dibdrv/primitives.c`). For the common solid-fill
   case it uses `memset_32()` per scanline, not a per-pixel loop --
   already efficient. A clean negative result, reported rather than
   forced into a fix that wasn't needed.

Items 1 and 2 above are hardware-confirmed (see inline notes); item 3
needed no further testing since it was a pure negative/ruled-out finding
with no code change.

## Two More Threads: The NtUserGetDCEx Gap, and the Clock Fix's Side Effect

Two more tracks, both hardware-tested and closed out cleanly.

**Track A -- the ~30ms `NtUserGetDCEx` gap.** `getdcex_flags_fixup`,
`getdcex_dce_lookup`, and `getdcex_clip_setup` (the three candidates
flagged when `erase_get_dcex`'s cost was first isolated) all came back at
0ms on hardware. The gap lives entirely inside `update_visible_region()`'s
own client-side work following the `get_visible_region` IPC call
(`getdcex_vis_rgn` reproduces `erase`'s full ~46-48ms), not in
`NtUserGetDCEx`'s own bookkeeping. Narrowed, not closed --
`update_visible_region()`'s internals (`NtGdiCombineRgn`, surface lookup,
`pGetDC`, `set_visible_region`) haven't been sub-instrumented themselves
yet.

**Track B -- was the `NtGetTickCount()` fix a net win or a net loss?**
Fixing the frozen clock had an accidental, non-diagnostic side effect:
`flush_window_surfaces()`'s idle debounce, structurally unable to run
while `now - last_flush` always read `0 < 50` on the frozen clock, started
actually evaluating for the first time -- discovered via a `paint_avg`
drop the first time both fixes were tested together, not predicted in
advance. `WINE_NX_FLUSH_LEGACY` (env var / `sdmc:/switch/wine/flushlegacy.txt`)
was added specifically to A/B this in one binary/session, forcing the old
always-skip behavior back on for direct same-run comparison instead of
trusting noisy separate launches. Result: -1.1% -- within run-to-run
noise, net-neutral. Confirmed rather than assumed.

## Batched `get_paint_regions` Request: Landed, Hardware-Confirmed, Didn't Fix fps

The batched request scoped out above ("What a batched request would need")
was implemented and hardware-tested. `get_update_region` and
`get_visible_region` -- the two calls `NtUserBeginPaint` always issues
back-to-back (`redraw_window` was deliberately left out; it's decoupled
from `BeginPaint` via `InvalidateRect`'s separate API entry point, so
forcibly coupling it would be architecturally wrong for the general Win32
contract) -- are now optionally fetched in one round trip via a new
`get_paint_regions` server request (protocol ID 310,
`horizon_server_handle_get_paint_regions()` in `horizon.c`). Off by
default behind `WINE_NX_BATCH_PAINT_REGIONS`
(env var / `sdmc:/switch/wine/batchpaint.txt`), same file-fallback pattern
as every other toggle here.

The real correctness risk -- `send_ncpaint()` can dispatch `WM_NCPAINT` to
the app *between* the two fetches, and an app's handler can change window
geometry, so a visible-region reply fetched before that dispatch could be
stale -- is handled by fetching optimistically and validating before use,
never by assuming the optimistic fetch is safe: the visible-region half is
stashed thread-locally and only consumed if the window handle and the one
flag bit the server's reply actually depends on (`DCX_WINDOW`) still match
when `update_visible_region()` goes to use it; any mismatch, or an actual
`WM_NCPAINT` dispatch, falls through to the exact old sequential call, so
every fallback path costs no more than not having this change at all.

**Hardware-confirmed working correctly**: 129 `get_paint_regions` calls
against only 5 leftover `get_visible_region` calls in one full test run --
a ~96% hit rate, zero `[NXBATCH] miss` (validation mismatches), no visual
corruption, clean run to the (separately tracked, unrelated) Minus-button
exit issue.

**Confirmed this does NOT fix the 5fps problem.** `presents/sec` stayed at
5 and `gui_timing.log`'s `paint_avg` was statistically unchanged
(~200-210ms steady state, same as before batching) despite eliminating a
real ~14-15ms round trip on ~96% of frames. Why: summing the sub-phases
that are actually traced inside the `NtUserBeginPaint`/`NtUserEndPaint`
span (`ncpaint` + `erase` + `present_dirty`) comes to roughly 121ms in the
same log where `paint_avg` reads ~200-210ms -- an **unidentified
~80-90ms/frame gap** that was never accounted for by any trace this
session, and that dwarfs the ~14ms this change removed. The IPC floor was
real and is now measurably smaller, but it was never the dominant term in
`paint_avg`; something else, roughly 6x the size of what this change
touched, still is.

**Next step, not yet started:** find where that ~80-90ms actually lives.
Two concrete things to check before adding more Wine-side tracing: (1)
verify exactly where `gui_smoke.c`'s own `paint_avg` timer starts and
stops -- if its boundary wraps more than just the `BeginPaint`/`EndPaint`
call pair (e.g. the actual drawing calls in between, which live outside
`dce.c` and have never been instrumented), the gap may simply be
unmeasured drawing cost, not hidden IPC; (2) check whether `presents/sec`
is being externally capped (a vsync wait or frame limiter somewhere in the
present path) rather than being bound by how long painting itself takes --
if presentation is rate-limited independently of `paint_avg`, shaving
`paint_avg` further won't move fps until that cap is found and accounted
for either.

## Is 5-6fps a `gui_smoke.c`-Specific Ceiling, or the Whole Port?

A minimal Win32 PE app (`samples/direct-blit/direct_blit.c`,
`WINE_NX_APP=blit`) was built specifically to answer this -- one that
never calls `InvalidateRect`/`UpdateWindow`/`BeginPaint`/`EndPaint` in its
steady-state loop, relying entirely on `GetDC()` called once (cached)
plus a direct `StretchDIBits()` per frame, deliberately bypassing the
entire GDI paint-message IPC chain `gui_smoke.c` exercises.

Two things resolved this cleanly:

1. **This port has no OpenGL/D3D driver at all** -- confirmed directly
   against source (`dlls/win32u/driver.c`'s `nulldrv_OpenGLInit()` returns
   `STATUS_NOT_IMPLEMENTED` unconditionally, never overridden). Vulkan/
   vkd3d/DXVK are separately ruled out too, since all three need NVK and
   NVK can't run on Horizon OS (see "Presentation Is Still Too Slow" above
   for the full DRM-dependency finding). A real GPU-rendering
   Win32 app can't be built as a minimal test case here; that would mean
   writing a new WGL driver backend from scratch.
2. **`deko3d_smoke.c`** (a standalone libnx test, not a Wine PE app at
   all) already proves the raw GPU render+present path itself holds
   **60-62fps** on this exact hardware -- so whatever's limiting
   `gui_smoke.c` isn't the hardware or the presentation mechanism.

`direct_blit.c`'s own numbers confirmed the port-wide-ceiling theory,
though not for the reason first assumed: with no `BeginPaint`/`EndPaint`
at all, painting instead falls entirely on real Wine's own `dibdrv`
`FLUSH_PERIOD` mechanism (`dlls/win32u/dibdrv/dc.c` -- a surface flush
forced whenever a window's been continuously dirty longer than 50ms,
upstream code, not Switch-specific), triggered synchronously inside the
drawing call itself. fps landed in the same 5-7 range `gui_smoke.c` hits,
for an entirely different-looking reason.

## A Recurring Bug: Unconditional fflush() Hiding in Diagnostic Code

Chasing two ~2.7-second stalls in `direct_blit.c`'s own hardware log
(`fb_lock_call` hit 262ms/200ms against a normal ~7-9ms; `surface_funcs_flush`
hit 542ms against a normal ~42-52ms, while the GPU-side `dkFenceWait`'s own
self-reported duration stayed 0ms throughout) led to the same bug, found
and fixed six separate times across this investigation: a diagnostic
trace call with **no rate limit at all**, unconditionally `fflush()`ing
to the SD card on every call. Once in `trace_surface_samples()` (first 40
frames of every run), once in `winnx_drv.c`'s per-pixel-loop trace (every
frame, forever), three times in `wine_nx_deko3d_trace()`'s per-present
calls (`fenceWait`/`dkQueueAcquireImage`/`submit+signal+present`, all
three unrate-limited), once in `dibdrv/dc.c`'s `windrv_StretchDIBits`/
`windrv_PutImage` (up to 5 unconditional calls per single `StretchDIBits`
call -- confirmed via hardware log that both fire for the same logical
call), and once more in `message.c`'s `nx_trace_winproc()` (fires on
every `WM_PAINT` dispatch). All six rate-limited to a fixed sample count
(40 or 5, matching whichever convention already existed in that file),
same pattern each time.

Hardware-confirmed: the two 2.7-second stalls are gone (`NtGetTickCount64`
samples now land within ~30-40ms of the expected 1000ms interval for a
full 19-second run, versus multi-second gaps before), `presents/sec`
climbed from a chaotic 5-7 (dipping to 1 during the stalls) to a stable
8-9, and `surface_funcs_flush`'s steady-state average dropped from
~42-52ms to ~7ms -- most of what looked like real CPU-side
pixel-conversion cost turned out to be this same bug, not genuine
conversion work.

## `update_now()`'s Redundant Round Trip: Skipped, Provably Safe

`update_now()`'s loop always calls `get_update_flags()` twice per
`WM_PAINT` dispatch -- `UpdateWindow()`'s own `RDW_ALLCHILDREN` forces
this even for a window with zero children, where the second call is
provably guaranteed to find nothing (confirmed against
`horizon_server_find_window_update_locked`'s exact recursion, not
assumed). A new `get_update_flags_ex` protocol (ID 311) folds a
`has_children` bit into the existing round trip at zero extra cost, and a
`switch_window_tree_generation` counter (bumped on every
`NtUserCreateWindowEx`/`NtUserSetParent` call, attempted or not) lets the
skip be proven safe rather than assumed: the window must have had zero
children before dispatch *and* nothing must have created or reparented a
window during the synchronous `WM_PAINT` handling. Either condition
failing falls through to the exact calls `update_now()` would have made.
Gated behind `WINE_NX_SKIP_REDUNDANT_UPDATE_CHECK`, off by default.

Hardware-confirmed: exactly one `get_update_flags_ex` call per frame
instead of two, zero fallbacks logged across a full test run.

## Where fps Actually Landed: 5-6 -> 8, IPC Round Trips 5 -> 3

Combining the fflush-per-call fix, the `update_now()` skip, and the
`windrv_StretchDIBits`/`PutImage` fix together on hardware:

- **IPC round trips per paint cycle: 3, down from the original 5** --
  `redraw_window` + `get_update_flags_ex` + `get_paint_regions`, one
  each, confirmed via a perfect 1:1:1 call-count ratio across a full test
  run.
- **`blit_avg`: ~19-20ms -> ~2ms** (~90% reduction) -- a bigger win than
  the `windrv_StretchDIBits` fix was scoped for, since `BitBlt`'s dibdrv
  implementation turned out to route through the same `PutImage` dispatch
  internally.
- **`presents/sec`: stable 8**, up from the 5-6 baseline this entire
  investigation started from.
- **`paint_avg` settled at ~113-116ms**, down from ~198-241ms -- the
  sub-phase breakdown is now small and coherent (`ncpaint`~21ms,
  `erase`~24ms, `surface_funcs_flush`~7ms, `present_dirty`~9ms), no more
  dominant unexplained gap.

A fifth change (an A/B toggle lowering `dibdrv`'s `FLUSH_PERIOD` from
50ms to 16ms, on the theory that a now-cheap ~7ms flush could afford to
fire more often) was hardware-tested as a clean, honest null result --
both `gui_smoke.c` and `direct_blit.c`'s natural paint cadence
(60-125ms between frames) already exceeds both threshold values, so the
flush was already firing on essentially every frame either way. Left in,
off by default (`WINE_NX_FAST_FLUSH_PERIOD` / `sdmc:/switch/wine/fastflushperiod.txt`),
in case a faster workload ever reaches the regime where it'd matter.

## GetTickCount/GetTickCount64 Are Frozen (Platform-Wide)

Found while debugging the animated-HUD demo's timing instrumentation (see
"GUI Smoke Demo" above), but this affects any app, not just that one:
`GetTickCount()`/`GetTickCount64()` read `user_shared_data->TickCount`, a
shared-memory field that is only ever written by `set_current_time()` in
`server/fd.c`, which is itself only called from the wineserver's poll loop
(`main_loop()`, also `server/fd.c`). On this Switch port `main_loop()` is
never invoked -- the server code is linked in as a function library, not
run as its own continuously-polling process the way real Wine's separate
`wineserver` process would -- so that field stays at its zero-initialized
value for the entire life of the process. Any app or Wine subsystem relying
on `GetTickCount`/`GetTickCount64` for timers, animation pacing, or
elapsed-time checks gets a frozen value, not real time.
`QueryPerformanceCounter`/`QueryPerformanceFrequency` are unaffected (they
route through a different, working `clock_gettime()`-backed path) and are a
safe substitute today.

**Update: the originally-proposed fix (a periodic thread calling
`set_current_time()`) does not work and was not built.** Investigated while
chasing the 2fps stall above, on the theory that a frozen clock might be
making `Sleep()` or a message-dispatch timeout compute something huge.
Tracing `Sleep()` -> `NtDelayExecution` on this fork found it's backed by a
`select()` loop keyed on `NtQuerySystemTime` (real `clock_gettime()`), never
`user_shared_data->TickCount` -- so the frozen clock does not explain that
stall (the actual cause was the fflush/SetPixel issue documented above).
Separately, `server/fd.c` (where `set_current_time()` lives) is not part of
this Switch build's link closure at all (`server/` isn't compiled in --
this port's entire wineserver role is played by
`dlls/ntdll/unix/horizon.c`, a from-scratch reimplementation that never
references `TickCount`/`user_shared_data`/`set_current_time()`), so the
proposed fix would not even link as described. The clock is still frozen
and still worth fixing for any app that does rely on it for real elapsed
time, but the fix needs to write `user_shared_data->TickCount` from
somewhere reachable in this port's actual link closure -- not yet
implemented.


## 2026-07-19 — Verification run: NXWAKE verdict, rate-limited traces, rebuilt DLLs (build 2e4af89)

On-hardware verification pass for the fix batch (build `2e4af89`, main). Config: `painttrace=1` from config.txt only; **all batching toggles OFF** (all 8 [NXTRACE] boot lines print correct `(source=...)` — the parsing fix works). Target gui_smoke.exe; both binaries run (deko3d `nx-deko3d-only-1` ~11.3s, plain libnx `nx-present-throttle-1` ~136s, 3 boot sessions total in horizon-trace.log).

### NXWAKE: the scheduler-wake hypothesis is dead

The instrumentation I added last session finally reported, and the answer is emphatic:

| metric | value |
|---|---|
| [NXWAKE] aggregate samples | 224 (horizon-trace.log:211–3312) |
| n-weighted avg wake latency | **6.95us** (137,860 wakes) |
| avg-of-avgs / max sample avg | 7.09us / 11us |
| overall max single wake | 43us (line 1907) |
| wake rate median / p90 / peak | 670 / 705 / **1383 per sec** |
| drift early-half vs late-half | 6.97us vs 6.94us (none) |
| corr(avg, load) | r = -0.785 (faster when busy) |

I hypothesized ~7ms per wake, two wakes per call = the 14ms floor. Reality is ~7**us** — a clean 1000x miss. Two wakes explain ~0.1% of the floor. The 1383 wakes/sec peak alone kills the theory (a 14ms/call thread caps out near 71/sec). **The 14ms floor was the unconditional fflush-to-SD traces in get_visible_region/get_update_region, executed under the server mutex, hiding inside every prior session's "IPC call took Xms".** This run proves it directly: the first [NXPAINT][AVG] window — while the 5 rate-limited [HZPAINT] samples still fire — replays the old baseline (ncpaint=21ms, erase=19ms, deko3d:1081), then collapses the moment they exhaust. The planned inline-dispatch work loses its premise: ~14us/call is the ceiling of what it can save. Dropping it.

### FPS and sub-phases vs baseline

[NXDK][TIMING] presents/sec: 7, 23, 31, 39, 39, 27, 33, 31, 33, 38, 35 (deko3d:1147–3786).

| metric | baseline | this run |
|---|---|---|
| presents/sec (steady) | 8 | **34.4** (27–39, peak 39) — 4.3x |
| paint_avg (gui_timing.log) | ~113–116ms | **25.2ms** |
| ncpaint | ~21ms | 1.0ms |
| erase | ~24ms | 3.61ms (all of it = getdcex_vis_rgn) |
| surface_funcs_flush | ~7ms | 5.0ms (now the top phase) |
| present_dirty | ~9ms | 3.45ms |
| blit_avg | ~2ms | 1ms |

The two ~20x collapses are exactly the two handlers whose fflush traces I capped. Reconciliation is exact: 1000/total_avg per gui_timing row tracks presents/sec one-for-one (330 iters vs 336 presents); no hidden throttle remains. The libnx run independently sustains ~35 cycles/sec (fb_present_call n=35–36/sec) with p50=0ms/max 6ms on 22,662 paint-path IPC calls — only 3 calls ≥10ms in 136s, all boot-time create_mapping. The one fps dip (27) lines up with a dispatch_avg burst of 4–7ms, not paint.

### Fix confirmations

- **Rate limits**: exactly 5 [HZPAINT] samples per type (visible/get_update/redraw) per boot, all 3 sessions (15/15/15). Careful: 3 boots share the file — whole-file counts falsely look like a cap failure.
- **Boot fix**: `[LDR] PE ntdll hash_table initialized (32 buckets at 0x7fff440be0, 13 modules inserted)` in both binaries (deko3d:302, libnx:309). Zero [EXC] anywhere; both runs ended via Minus with full [EXIT] sequences in the logs (note: prior sessions crashed to Home Menu *after* completing [EXIT] logging, so whether that symptom is finally gone needs an eyeball confirmation, not just these logs).
- **Rebuilt DLLs**: all 13 modules attach status=00000000 (deko3d:304–545); imm32/uxtheme/ole32 dynamic loads clean.
- **Toggles**: 8/8 with correct source attribution; only painttrace non-default.

### Anomalies

- 7 of 13 staged fonts fail FreeType load in both font dirs (the bitmap-font stand-ins: fixedsys/system/courier/small_fonts/ms_sans_serif + _jp) — unparseable files, not a staging miss. Falls back to Tahoma.
- [NXFONT] select_cached is the new unguarded trace: 2 lines/paint cycle, 29–33% of both runtime logs. Same cap treatment needed. Its status=20000057 is a flags/status field swap in the format string, cosmetic.
- ~12ms of the 25.2ms paint_avg sits outside all phase timers (phases sum ~13.1ms), and phase timers say ~3–5ms where per-call timers say 0ms — ms-granularity truncation; need finer timers before the next optimization call.
- [NXIPC][TIMING] was 0 lines in horizon-trace.log (toggle off), so no direct per-request-type server-side costs this run.
- Cold calls still pay ~6–9ms (create_mapping avg 6.3–7.2ms); hot calls ~0ms. One-off 73ms vis_rgn spike at ~t=57s (libnx:14851), no recurrence. Torn log lines at shutdown (no line atomicity). 6 benign HZDIR err=80000006 (end of font enumeration). setpos-dirty trace shows 9/session — low volume, but verify its cap.
- pe-real-run.nro: loader clean (38/38 relocs, loader_failures=0) but blocked at 46/89 unresolved imports — target.txt pointed the console-shim loader at a GUI exe; rerun against curl.exe.

### Next

1. Turn batching toggles ON: 5–6 round trips/frame → 2–3 projects ~50–70fps.
2. Rerun with NXIPC timing on + sub-ms paint-path timers to find the untimed ~12ms/frame.
3. Cap the NXFONT trace; fix the 7 font files; rerun pe-real-run with curl.exe; add build sha/hashes + exe/NROs to runtime-manifest.txt.
## 2026-07-19 (second deploy) — select_cached cap pays off big; ALLOW_BITMAP lands but can't win (build b9a76d6)

Second on-hardware pass of the day, build `b9a76d6`. Two changes vs `2e4af89`: (1) the [NXFONT] select_cached trace capped at 5 samples (it was 2 unguarded fflush-to-SD writes per paint cycle, in the hot paint path), (2) the Switch font scan now passes ADDFONT_ALLOW_BITMAP for both dirs. Both verifiably deployed: `nx_open_dir flags=2` (deko3d:357) and `flags=3` (deko3d:444). Config unchanged: `painttrace=1` only, all batching toggles OFF (8/8 [NXTRACE] lines correct). Both binaries run + the two parked smokes; 3 boot segments in horizon-trace.log (third is a 5-line aborted pe-real boot — the file appends across processes, segment on the [VA] ASLR marker).

### FPS: 8 → 34.4 → 50.06, still without batching

[NXDK][TIMING]: 35 windows (deko3d:1016–9718) — ramp 17, 36, then **33 steady windows mean 50.06** (47–51, median 50, stdev 1.12). No dips this time; dispatch_avg is 0 in every window.

| metric | first light | 2e4af89 | b9a76d6 |
|---|---|---|---|
| presents/sec (steady) | 8 | 34.4 (27–39) | **50.06** (47–51) |
| frame budget | ~125ms | 29.07ms | **19.98ms** |
| libnx paint-cycles/sec | — | ~35 | **39.4** |
| gui total_avg (app-side) | — | 29.4ms | 24.35ms (libnx run) |

### Where the 9.1ms/frame came from: the cap, twice over

N-weighted steady sub-phases (last 33 AVG windows, n=1636 cycles):

| phase | 2e4af89 | b9a76d6 |
|---|---|---|
| ncpaint | 1.0 | 1.03 |
| erase (= getdcex_vis_rgn) | **3.61** | **1.06** |
| surface_funcs_flush (flush_call) | 5.0 | 5.03 (3.27) |
| present_dirty (fb_present_call) | 3.45 | 3.30 (0.00) |
| untimed gap | ~12.1 | **~6.6** |

Reconciliation: −2.55ms timed (all of it erase/vis_rgn) + −5.58ms untimed = 8.22 of the 9.09ms recovered; the rest is ms-truncation dust. Both deltas are exactly where the 2 removed fflush-to-SD writes/cycle (~2–4ms each) lived. Direct per-write evidence: the first AVG window — while the 5 capped samples still fire — shows trace_samples=2ms/max 4 (deko3d:845) and 17ms (libnx:749), then 0.00 forever. **Cap verified: exactly 5 select_cached lines in BOTH binaries** (deko3d:556–560, libnx:560–564); NXFONT log share fell 29–33% → 3.46%. The libnx binary is now cleanly "deko3d + ~5ms CPU present": present_dirty 8.63 vs 3.30, fb_present_call 4.00 vs 0.00, every other phase within 0.1ms.

New cost ranking per 19.98ms frame: untimed ~6.6ms > surface_funcs_flush 5.03 > present_dirty 3.30 > erase/ncpaint ~1 each. Measured round trips: 5.22 tracked IPC calls/frame (get_update 3.13, vis_rgn 1.04, redraw 1.04). Batching away 2–3 saves 1–3ms depending on how much of the 1ms phase readings is truncation (raw per-call avgs are 0.01–0.03ms) → **projects ~52–59 presents/sec, not 60**. The untimed gap and surface_flush_call are the real remaining targets; sub-ms timers first.

### NXWAKE holds at 50fps

74 reports, 52,308 wakes: **7.02us n-weighted, max 39us** (line 785) — indistinguishable from 6.95us/43us at 34.4fps. Deko3d steady ~933 wakes/sec ≈ 19 wakes/present, below last run's 1383/sec peak. The scheduler theory stays dead at 45% higher throughput.

### Fonts: the flag landed; wine itself is the gate

The 7 bitmap TTFs **still face_fail** (14 lines: 7 files × 2 dirs, flags=2 and flags=3), with map_font_ok preceding every failure — the files map fine, the flag arrives, the face dies anyway. Root cause found in source + byte-level parse of all 13 staged TTFs (13/13 match): the failing 7 all carry OS/2 achVendID=**'Wine'** plus **EBSC**/EBDT/EBLC — and wine deliberately rejects that in BOTH paths: opentype.c:669–681 ("intermediate step in building its bitmap fonts") and the same probe in new_ft_face (freetype.c:865–873). allow_bitmap is only consulted for non-SFNT faces (freetype.c:846) — never applies here. The Mac-names/charmap theory is refuted (all 7 have (3,1) cmaps and Windows name records; charmap selection happens at load, not face creation). These TTFs are upstream's build-time *sources*; upstream ships the .fon conversions.

Plan: stage the built .fon set (50 files in build-host-sim-linux/fonts/; build-switch.sh:204–209 copies only `*.ttf`), build the missing vgaoem.fon/serife.fon (6 of 8 for locale 1252/437 exist), drop the 7 dead TTFs. **Caveat discovered server-side: all 56 [HZDIR] queries use mask=*.ttf** — staged .fon files would never be enumerated as-is; the scan must also issue *.fon (or wildcard). Side note: GDI "System" requests are being substitution-served by Tahoma (fs=20000057 match) until vgasys.fon loads.

### Smokes

- **ntdll-file-smoke**: 15/15 [OK], failures=0 — open→server handle→fd passing (write+read)→seek→read-back→close all real; real-file.txt holds the exact payload.
- **pe-real-run**: loader machinery 100% clean (13 sections, 38/38 relocs, TLS, per-section PROT, loader_failures=0) but it loaded gui_smoke.exe from target.txt, ignoring its curl argv; blocked at 46/89 unresolved imports by design.
- **pe-real-report**: curl.exe unopenable at all 4 paths — the gui-target package stages no drive_c/curl (manifest confirms). Packaging, not loader. Its boot is the 5-line third horizon-trace segment: aborted before any mapping request.

### Anomalies

- gui_timing.log is from the **libnx** run (mtimes prove it; deko3d ran first and was overwritten) — app-side deko3d rows lost; make it per-run and flush before svcExitProcess (25 rows vs 27 windows).
- 27–53ms single-paint spike class persists in ncpaint/erase/vis_rgn max columns (~5–6 windows/run, both binaries; erase max=53 deko3d:1728, ncpaint max=50 libnx:4100). The 73ms magnitude didn't recur; the class did. Boot-only: fb_lock_call max=105ms first window, open_mapping 54ms.
- 4 torn lines, all at exit ([EXIT] racing the trace thread); zero mid-run, zero in horizon-trace — still no line atomicity.
- Zero [EXC] anywhere; both runs clean Minus-press exits through svcExitProcess. Only nonzero server status: 4× HZDIR err=80000006 end-of-enumeration.
- setpos-dirty trace: 9/session again, now explained — per-hwnd cap (max 4 per hwnd), deterministic across sessions. Closing last entry's "verify its cap" item.

### Next

1. Batching ON — expect ~52–59, treat as a per-round-trip cost measurement.
2. Sub-ms timers; then attack the 6.6ms untimed gap and 5.03ms surface_funcs_flush.
3. .fon staging + *.fon scan query + missing 2 .fon builds; drop the 7 intermediate TTFs.
4. Stage curl, make pe-real-run honor argv, add the 7 KERNEL32 + 5 CRT console shims.
5. Spike-capture trace for the 27–53ms class; per-run gui_timing; build sha in the manifest.## 2026-07-19 — Third deploy: batching live, the frame finally measured (build b22a96e)

Provenance: runtime-manifest.txt git=b22a96e @ 2026-07-19T19:37:25Z, sha256 on all 5 NROs. Config intent: painttrace=1, batchpaint=1, skipredundantupdate=1, batchredrawupdatenow=1. **Reality: 3 of 4** — skipredundantupdate never engaged (both logs: `source=default`). Root cause found post-run: the staged config.txt used the wrong key name (`skipredundantupdate`; the toggle's real key is `skipupdatecheck`) and unknown keys were silently ignored. The parser was fine; the config was typo'd. Fixed both ways since: the staged config uses the real key, and the runtime now logs a `[NXCONF] WARNING` for any config.txt key nothing consumed. Zero [EXC], both runs clean HUD-Minus exits.

### FPS trajectory

| run | build | presents/sec steady | frame ms |
|---|---|---|---|
| 0 (pre-lab) | — | 8 | 125 |
| 1 | 2e4af89 | 34.4 | 29.1 |
| 2 | b9a76d6 | 50.06 (47-51) | 19.98 |
| **3** | **b22a96e** | **55.97 (53-58, stdev 1.25, 88 windows)** | **17.87** |

Ramp 11/32/52 then steady; mode 57 (35 windows); 5020 presents over ~92.3s. App-side gui_timing.log independently agrees: 55.92. Landed inside the run-2 batching projection band (52-59).

### Batching A/B — round trips confirmed at the protocol level

| IPC call | run 2 /frame | run 3 /frame |
|---|---|---|
| get_update_region | 3.13 | 2.000 (10104 total) |
| get_visible_region | 1.04 | **0 — 5 calls in the whole run, all boot** |
| redraw_window | 1.04 | 1.000 (5052 total) |
| **total** | **5.22** | **3.00** |

get_paint_regions prefetch: ACTIVE at L1201, 100% hit after boot (2 invalidations, both boot WM_NCPAINT; zero miss lines in either binary). Delta attribution is clean: ncpaint 1.03→0.05, erase 1.06→0.04 (the vis_rgn IPC stalls inside them are gone) = -2.00 of the -2.11ms frame delta. Batching is ~100% of the gain, cost the server nothing (NXWAKE 6.97us over 106,002 wakes ≈ run 2's 7.02us; 58/58 HZPAINT err=0), and introduced zero spikes. Caveat: protocols 311/312 never appear under their own [NXIPC][TIMING] labels — folded under legacy names; verify in source. IPC is now ~0.03ms/frame. **Off the list.**

### The frame, at us resolution (steady n-weighted, deko3d)

| phase | ms | notes |
|---|---|---|
| redraw_to_paintregions_gap | 2.480 | max 30.35 — spike home #1 |
| ncpaint + erase | 0.090 | was 2.09 — batching collapse |
| app_draw_gap | 3.019 | NEW — run 2's "~6.6 inferred" was overstated; app-owned |
| flush_surfaces | 0.723 | |
| present_dirty | 4.943 | run 2's "3.30" was truncation lying |
| — fb_blit_loop | **5.080** | NEW — 99% of surface_funcs_flush interior |
| check_for_events + rest | 0.075 | locks/rect_math/getdcex all ~0 |
| **accounted** | **11.33** | nesting identities verified ±0.01 in both binaries |
| **residue** | **6.54** | identical on libnx (6.33) → CPU-side, not backend |

fb_blit_loop: always 921600px full-window, 5046-5053us, ~5.5ns/px — a per-pixel loop, not memcpy; dirty rects can't help (dirty=full on all 58 flushes). **60fps needs -1.2ms; blit→2ms clears it alone.** Rank: (1) blit, (2) instrument the residue, (3) gap 2.48, (4) app_draw_gap is the app's, (5) IPC done.

### Spike forensics (capture worked: 8 deko3d + 7 libnx = 11 events)

Boot one-offs (4): fb_lock_call 111.86/44.01ms = lazy backend init inside first lock (L1179-1194); app_draw_gap 304.71/310.59ms = first WM_PAINT incl. 79 first-time Tahoma glyph rasterizations. Steady class (~1 per 15s, 6 of 90 windows): redraw_to_paintregions_gap 27-55ms amid all-0ms IPC + rare 20-60ms flush stalls with flat interior timers — run 2's 27-53ms mystery class, now located, and **not server latency**. New suspect: in both binaries a spike lands on the exact sample after an [NXPAINT][AVG] dump (L2749→2750, libnx L1720→1721) — our own ~1.5KB SD write in the paint path. Cap gripe: 8-line budget spent by ~40s; windows 56/63/71/91 stalls have no context line. Per-phase caps or steady re-arm next time.

### Fonts: 14 face_fail → 0

172 face_ok per binary (50 .fon + 6 TTF + 6 shared, 2.56MB mapped), zero glyph errors. **Payoff: 'System' resolves to a real bitmap .fon** (fs=00080000, cache cap holding) — Tahoma substitution gone. But it picked the KOREAN hvgasys.fon: select_family is charset-blind first-match on FAT order and hvgasys enumerates at idx 4; the correct vgasys.fon (cp1252) is loaded and ignored. Fixedsys/Small Fonts have the same collision armed. Only 'System' was exercised — Fixedsys/Courier/Sans Serif never requested. Still missing from the plan: vgaoem.fon, serife.fon (1252/437 set is 6 of 8). Manifest hole: all 50 .fon files absent from [fonts] (TTF-only glob). Scan cost untimed, ~100-300ms by ramp evidence.

### curl.exe scoping (pe-real-report vs real binary)

Loader: 3.4MB ARM64, 7 sections, 10803/10803 relocs, protections, IAT/loadcfg/pdata located, loader_failures=0 — **zero loader risk left**. run_blockers=2: imports + TLS. unresolved=276 is dishonest — the report resolves against nothing; all 43 run-shim exports are used by curl, so the true gap is **233** exports (CRT is 127/276 of imports; ~65 can be bind-time stubs). TLS machinery already exists in pe-real-run. Work-list: flip target.txt (still gui_smoke!), fix report resolution, 233 exports (--version hot path real, rest stubs), run 2 TLS callbacks, GS cookie + unwind. Or: run curl under the main runtime against the 611 staged DLLs and skip shim growth — decide before building.

### Anomalies
- skipredundantupdate silent no-op (above) — fix the key name.
- 3 torn [NXIPC][TIMING]/[EXIT] lines at teardown (L17115 etc.) — still no exit-path newline.
- libnx binary: 42.8/s, entire deficit is fb_present 4.70 + fb_lock 1.27 vs deko3d 0.52/0.00; blit identical. deko3d ships.
- gui_timing.log append fix verified (2 headers) — note header embeds qpc_ms, grep 'run start'.

### Next
1. Blit → memcpy/NEON (the 60fps move). 2. Fix skipredundantupdate key, re-run 4-of-4. 3. Bracket the 6.5ms residue. 4. fsCsb-aware select_family + bitmap-family smoke. 5. vgaoem/serife + .fon manifest glob. 6. curl: target.txt flip + shim-vs-main-runtime decision. 7. AVG dumps off the paint path; SPIKE re-arm; exit newline. 8. Confirm 311/312 on the wire.