# Wine-NX Switch
`Status: experimental — GPU compositor proven on a demo target; full
Notepad app still software-rendered and slow.`

**A native ARM64 Wine port running directly on Nintendo Switch homebrew —
no Linux layer, no x86 translation, with real GPU-accelerated rendering.**

Wine-NX runs AArch64 Windows PE programs inside a Switch `.nro`, talking
straight to Horizon OS via libnx — no Switchroot L4T, no Box64, no second
kernel underneath. Windows GDI content renders through a real GPU compositor
(deko3d), hardware-confirmed at 60fps with shaders and 3D geometry.

Originally started by [dantiicu](https://github.com/dantiicu/wine-nx) as an
early Horizon/libnx Wine bring-up — the loader, initial win32u driver, and
NTDLL substrate are his foundation. This fork continues that work.

## What's new in this fork

- **GPU-accelerated presentation, hardware-confirmed at 60fps** — real
  Win32 GDI output rendered through deko3d (device/queue/swapchain, texture
  upload, shader-driven 3D geometry), replacing the original software
  framebuffer path.
- **The Vulkan/NVK question, settled with evidence, not assumption** —
  checked directly against the real toolchain: NVK is structurally
  impossible on Horizon OS (no DRM subsystem). deko3d is the real path,
  and it works.
- **A platform-wide performance bug found and fixed** — a diagnostic
  logging system was silently flushing to the SD card on every syscall,
  costing every app on this port real frame time. Fixing it doubled
  frame rate on the software-rendered GDI test target (2fps → 4fps) —
  a separate, still-open investigation from the GPU compositor above.
- **A full host-side development loop** — build and test the presentation/
  input code on macOS or Linux with zero Switch hardware required.
  
The GPU compositor above runs against a test/demo target today, not yet
the full Notepad app — see "Current Status" below for where the main
Notepad milestone stands.
## Current Status

As of the latest runtime work, the project can build and stage a Switch NRO
runtime that launches Wine PE targets from:

```text
sdmc:/switch/wine
```

Notepad is the main real-GUI test target right now. It can launch far enough to
show the main window, frame, menu bar, menu popups, text rendering, and touch
driven menu interaction. It is still experimental and not production usable:
responsiveness is poor, presentation is software-heavy, and several real-app
subsystems remain incomplete.

## What Works So Far

### Switch Build And Packaging

- `wine-nx-probe/build-switch.sh` builds the Switch runtime with the
  `devkitpro/devkita64` Docker image.
- The script stages a runnable SD package under:

```text
wine-nx-probe/build-switch/sd-card/switch/wine
```

- Runtime NRO output:

```text
wine-nx-probe/build-switch/wine-nx-runtime.nro
wine-nx-probe/build-switch/sd-card/switch/wine/wine-nx-runtime.nro
```

- `WINE_NX_APP` selects the staged target:

```sh
WINE_NX_APP=gui ./wine-nx-probe/build-switch.sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

- The package now stages Wine NLS files and Wine fonts into the Switch package,
  including `C:\windows\fonts` and `share/wine/fonts` equivalents.

### Runtime Loader

- The runtime can bootstrap a Wine ARM64 PE target from the staged package.
- PE import resolution, loader handoff, Wine DLL staging, and runtime target
  selection are in place for the smoke apps and Notepad path.
- The runtime carries a build marker in the NRO so hardware logs can confirm
  which binary is actually running.
- Logging goes to:

```text
sdmc:/switch/wine/wine-nx-runtime.log
sdmc:/switch/wine/horizon-trace.log
```

### Horizon And Wine Server Substrate

- Early Horizon primitives were proven: TLS model, executable memory/JIT alias
  handling, thread setup, address-space reservation, and Switch-safe runtime
  logging.
- The in-process Horizon/Wine server path is active enough to service the
  current NTDLL and USER/GDI flows.
- Win32 syscall dispatch is working for the exercised `win32u` calls.
- USER server work has enough coverage for the GUI path:
  - atoms;
  - window station and desktop basics;
  - class creation/registration;
  - top-level and popup window objects;
  - shared-session style locators/data sufficient for current windows.

### GDI And USER Bring-Up

- GDI software rendering is working through Wine's DIB path.
- The GUI smoke app proved DC creation, compatible DCs, DIB sections, bitmaps,
  StretchDIBits-style raster work, object deletion, and cursor/menu drawing.
- `RegisterClassW` and `CreateWindow` moved past the original server-object
  blockers.
- Basic Notepad non-client rendering works:
  - main frame;
  - title/action buttons;
  - menu bar;
  - menu popup surfaces;
  - text through FreeType-backed Wine fonts.

### Switch Display Driver

- `dlls/win32u/winnx_drv.c` is the Switch-specific win32u display driver.
- It reports a single 1280x720 primary monitor to Wine.
- Wine window surfaces render into normal software DIB memory, then the driver
  blits dirty pixels to the libnx framebuffer.
- Popup/menu surface restore was added so old menu pixels can be covered again
  when popups hide or move.
- Dirty-present work avoids some full-surface flushes.
- Batched-present work keeps a pending framebuffer open and presents at
  higher-level boundaries instead of queueing every tiny dirty rectangle.

### Input

- Switch touchscreen input is sampled through libnx.
- Touch is translated into absolute Win32 mouse input.
- Non-client hit tests work well enough to interact with the Notepad frame and
  top menus.
- `get_window_children_from_point` support was added for correct child-window
  hit routing.

### Real GUI Milestone

The current real-app milestone is Wine Notepad:

- window appears on Switch;
- frame/control glyphs render correctly;
- menu fonts render;
- top menu interaction is partially working via touch;
- popups can be drawn and dismissed;
- the app is visibly alive, but slow and not yet a comfortable UI.

### GUI Smoke Demo: Animated HUD

`wine-nx-probe/samples/gui-smoke/gui_smoke.c` (`WINE_NX_APP=gui`) extends the
Notepad-adjacent GDI smoke test with a small bouncing-ball/rectangles
animation (touch-attraction while held) driven by a real `PeekMessageW` loop,
instead of a single static paint. A native-side HUD (drawn directly into the
framebuffer, no PE-side cost) overlays live present-rate stats: instantaneous
FPS, an 8-second rolling avg/min/max, attempted-vs-executed presents per
second, and the wall-clock cost of `framebufferEnd()` itself -- toggled with
Plus.

**Tested on hardware:** shapes animate and bounce correctly; Plus reliably
toggles the HUD overlay on/off.
**Known issue, not yet fixed:** Minus (intended to exit cleanly back to
hbmenu) crashes to the system Home Menu instead. Deferred to a later pass --
not blocking, but real; the physical HOME button is the only clean way out
of this demo for now.

### Deko3d Bring-Up Smoke Test

`wine-nx-probe/source/deko3d_smoke.c` (`wine-nx-deko3d-smoke.nro`, standalone
homebrew, independent of the Wine runtime and SD package) is the GPU-
compositor milestone's proof-of-pipeline, built in three stages, all
**hardware-confirmed running together at 60-62fps** for a 5-10 second test:
device/queue/swapchain/present against the same `nwindowGetDefault()` handle
`wine_nx_fb_init()` uses (scissor+clear only, no shaders); a CPU-generated
texture uploaded straight onto a framebuffer sub-rect via
`dkCmdBufCopyBufferToImage()`; and a real shader-driven rotating cube (uam-
compiled shaders, depth buffer, a fence-protected dynamic command ring for
the per-frame rotation matrix). Real shader/geometry work costs no
measurable fps over the clear-only baseline. See
`wine-nx-probe/3d-accel-scoping.md` for the full writeup, including what's
still open (an actual sampled-textured-quad -- upload and shaders are each
proven, not combined -- and the real `wine_nx_fb_lock/unlock/present` swap).

Two real crash bugs surfaced building this, both root-caused from hardware
crash logs rather than guessed at: a missing `romfsInit()` call (so
`fopen("romfs:/shaders/...")` returned `NULL` and the very next line
crashed), and `DkMemBlockCreate()` sizes that weren't rounded up to the
required 4096-byte alignment for small CPU-data buffers. Both are documented
at the top of `deko3d_smoke.c` for anyone touching deko3d in this project
next.

### Host-Side Development (macOS/Linux, No Console Needed)

`dlls/win32u/winnx_drv.c` (the Switch display driver) turns out to call zero
libnx symbols directly -- it only calls a handful of `wine_nx_fb_*`/
`wine_nx_touch_poll()` hooks that `wine-nx-probe/source/runtime.c` implements
on hardware via the libnx framebuffer. `dlls/win32u/winnx_host_sim.c`
implements those same hooks with SDL2 instead, so the real, unmodified
display driver links and runs against a plain host build:

```sh
WINE_NX_HOST_SIM=1 wine ...
```

See `wine-nx-probe/switch-shims/README.md` for the architecture and what's
*not* faithfully simulated (multi-touch, and critically, presentation
*performance* -- SDL2's cost model has nothing to do with the real
block-linear conversion bottleneck below, so this is for rendering/input
correctness, not perf).

`wine-nx-probe/docker-host-sim/` runs this in a Linux container, needed
because native execution on at least one macOS host hit an unrelated,
OS-level block -- see
`wine-nx-probe/switch-shims/macos26-loader-incompatibility.md`.

Status: compiles and links cleanly on both macOS and Linux/aarch64.
Actually rendering something on screen through this path is still blocked by
a separate, pre-existing bug in this fork's customized PE loader -- any
top-level `wine <program>` invocation fails to load kernel32.dll right after
the automatic wineboot spawn/wait, reproducible even with a trivial
hello-world and independent of the host-sim itself. Not yet fixed.

## Current Problems

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
it, and there structurally can't be one under Horizon OS, since NVK is built
on Linux's `nouveau` DRM kernel driver and Horizon isn't Linux. Real Mesa
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

### GetTickCount/GetTickCount64 Are Frozen (Platform-Wide)

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

### UI Completeness Is Still Early

Known rough areas:

- menus are functional but not fully Windows-perfect;
- popup/menu z-order and invalidation still need more compositor logic;
- touch input needs better capture, focus, drag, and double-click behavior;
- no controller/keyboard text-input path yet;
- no real GPU acceleration yet;
- Notepad works as a milestone, not as proof that arbitrary GUI apps are ready.

### Wine Subsystem Gaps

Real applications will need more work in:

- USER message queue and window state edge cases;
- common controls and dialogs;
- shell/comdlg/shell32 behavior;
- COM/OLE paths used by many apps;
- registry and prefix behavior;
- font discovery/fallback beyond the staged Wine font set;
- clipboard, IME, keyboard, and controller input;
- audio, networking, and multi-process behavior as later milestones.

## Build

### Host System Setup

The Switch NRO build runs inside Docker with the `devkitpro/devkita64` image.
The script installs missing Switch portlibs inside that container when needed,
including FreeType/Harfbuzz for font rendering.

The host-side Wine PE rebuild path uses LLVM-MinGW. The default expected path is
ignored by git:

```text
wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal
```

If `wine-nx-probe/toolchains/` is missing, restore it from the pinned
LLVM-MinGW release:

```sh
mkdir -p wine-nx-probe/toolchains
curl -L -o wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal.tar.xz \
  https://github.com/mstorsjo/llvm-mingw/releases/download/20260505/llvm-mingw-20260505-ucrt-macos-universal.tar.xz
tar -C wine-nx-probe/toolchains \
  -xf wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal.tar.xz
```

For another host/toolchain layout, point the build at an extracted LLVM-MinGW
directory:

```sh
LLVM_MINGW_DIR=/path/to/llvm-mingw WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

The ARM64 PE Wine build trees are also ignored by git:

```text
wine-nx-probe/build-wine-arm64-pe-clean
wine-nx-probe/build-wine-arm64-pe-local
```

Those directories are disposable Wine PE build trees used to rebuild Notepad
and Wine PE DLLs. `wine-nx-probe/build-wine-arm64-pe-clean` is the default PE
build directory. When `WINE_NX_APP=notepad` and
`wine-nx-probe/build-wine-arm64-pe-local` exists, `build-switch.sh` prefers the
local tree for the staged program. Override either path with:

```sh
WINE_NX_PROGRAM_BUILD_DIR=/path/to/wine-pe-build WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

To recreate a PE build tree from the repository root:

```sh
PE_BUILD_DIR=wine-nx-probe/build-wine-arm64-pe-clean
mkdir -p "$PE_BUILD_DIR"
(
  cd "$PE_BUILD_DIR"
  PATH="$PWD/../toolchains/llvm-mingw-20260505-ucrt-macos-universal/bin:$PATH" \
    ../../configure \
      --enable-archs=aarch64 \
      --disable-tests \
      --without-x \
      --without-freetype \
      --without-fontconfig \
      --without-gettext \
      --without-gnutls \
      --without-opengl \
      --without-vulkan \
      --without-sdl \
      --without-cups \
      --without-coreaudio \
      --without-alsa \
      --without-pulse \
      --without-gstreamer \
      --without-ffmpeg \
      --without-dbus \
      --without-gphoto \
      --without-gssapi \
      --without-krb5 \
      --without-netapi \
      --without-opencl \
      --without-pcap \
      --without-pcsclite \
      --without-sane \
      --without-usb \
      --without-v4l2 \
      --without-wayland \
      --without-unwind \
      --with-mingw=llvm-mingw
)
```

After configure, the normal Switch package build will compile the missing
Notepad executable and DLLs from that tree.

From the repository root:

```sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

Other useful targets:

```sh
WINE_NX_APP=gui ./wine-nx-probe/build-switch.sh
WINE_NX_APP=curl ./wine-nx-probe/build-switch.sh
```

The staged SD package is written to:

```text
wine-nx-probe/build-switch/sd-card/switch/wine
```

Copy or sync that package to:

```text
sdmc:/switch/wine
```

The package helper prints:

```text
Sync mounted SD with: wine-nx-probe/tools/sync-switch-wine-package.sh
```

## Hardware Test Checklist

1. Build the package with the target you want, usually:

```sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

2. Deploy `wine-nx-runtime.nro` and the staged `switch/wine` package.

3. Run on Switch.

4. Confirm the runtime log starts with the expected marker, for example:

```text
[BUILD] nx-present-throttle-1
```

5. Check:

```text
sdmc:/switch/wine/wine-nx-runtime.log
sdmc:/switch/wine/horizon-trace.log
```

The marker matters. Several performance/debug loops looked confusing until the
logs showed an older NRO was still being run.

## Important Files

Runtime and packaging:

```text
wine-nx-probe/build-switch.sh
wine-nx-probe/source/runtime.c
wine-nx-probe/source/runtime_platform.c
wine-nx-probe/CMakeLists.txt
```

Switch display and USER/GDI integration:

```text
dlls/win32u/winnx_drv.c
dlls/win32u/window.c
dlls/win32u/dce.c
dlls/win32u/sysparams.c
dlls/win32u/message.c
dlls/win32u/input.c
```

Horizon/NTDLL substrate:

```text
dlls/ntdll/unix/horizon.c
dlls/ntdll/unix/file.c
dlls/ntdll/unix/process.c
dlls/ntdll/unix/thread.c
dlls/ntdll/unix/signal_arm64.c
```

Host-side development (no console needed, see "Host-Side Development" above):

```text
dlls/win32u/winnx_host_sim.c
wine-nx-probe/switch-shims/README.md
wine-nx-probe/switch-shims/macos26-loader-incompatibility.md
wine-nx-probe/docker-host-sim/
wine-nx-probe/3d-accel-scoping.md
```

Smoke targets:

```text
wine-nx-probe/samples/gui-smoke
wine-nx-probe/samples/curl-arm64
wine-nx-probe/source/deko3d_smoke.c
```

## Next Milestones

1. Presentation performance

Replace or bypass the expensive linear framebuffer path. **NVK confirmed
not available** for Switch homebrew (verified directly against the
devkitpro/devkita64 toolchain image, not assumed), and **deko3d's core
pipeline is hardware-confirmed working**, both in isolation (device/queue/
swapchain/present, texture upload, and real shader-driven geometry, all at
60-62fps -- see "Deko3d Bring-Up Smoke Test" above) and as the actual
compositor backend for the full Wine GUI stack, via the separate
`wine-nx-runtime-deko3d` binary (see "Presentation Is Still Too Slow" above) --
`wine_nx_fb_lock/unlock/present` wired to deko3d inside the original
`wine-nx-runtime` binary is still architecturally blocked for the
`__appInit`/`nwindowGetDefault()` reason documented there, but that no
longer matters since the separate binary is the real path forward and
already works.

**Current bottleneck on the deko3d binary: presentation is CPU-bound, not
GPU-bound.** With deko3d confirmed fast (sub-ms GPU-sync) and the
fflush/SetPixel fix above landed, the GUI smoke test runs at a
hardware-confirmed 4fps (up from 2fps), still dominated by `paint_avg`
(~270-300ms/frame). Remaining known contributors: ~25% from three
synchronous IPC round-trips per paint (`redraw_window`/`get_update_region`/
`get_visible_region`), the rest not yet identified -- see "Presentation Is
Still Too Slow" above for the current state of that investigation.

Real Mesa **OpenGL ES** (not Vulkan) is also confirmed available in the same
toolchain (`switch-mesa`/EGL/GLES, via a Switch-native `libdrm_nouveau`
shim) -- meaning Wine's existing `wined3d` OpenGL backend is a more
realistic long-term path to real D3D acceleration on this platform than
DXVK/vkd3d, which need Vulkan and therefore aren't viable here. Neither has
been prototyped yet; this is still a scoping conclusion, not built code.
Since it would hit the exact same `vi`-bootstrap conflict as deko3d if
wired the same way, it's not a workaround for the blocker above either.

With GPU compositing blocked for this binary shape, the more realistic
near-term path is the simpler fallback already scoped:

- direct block-linear dirty conversion;
- persistent software backing store plus one present per frame.

2. Input polish

- keyboard/text input;
- controller mouse mode;
- better touch capture and focus;
- double-click/drag behavior;
- native `WM_TOUCH` later.

3. USER/window manager behavior

- popup/menu stacking;
- owner/activation edge cases;
- clipping and invalidation;
- modal dialogs;
- child-window ordering.

4. More real apps

After Notepad is smoother, the next useful test targets should be small
non-network GUI apps that exercise dialogs, common controls, edit controls, and
file browsing without requiring a browser engine or GPU API first.
