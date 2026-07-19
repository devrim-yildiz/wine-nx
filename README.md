# Wine-NX Switch

`Status: experimental — GPU compositor proven on a demo target; full
Notepad app still software-rendered and slow.`

**A native ARM64 Wine port running directly on Nintendo Switch homebrew —
no Linux layer, no x86 translation, with real GPU-accelerated presentation.**

Wine-NX runs AArch64 Windows PE programs inside a Switch `.nro`, talking
straight to Horizon OS via libnx — no Switchroot L4T, no Box64, no second
kernel underneath. Windows GDI content is composited and presented through
a real GPU pipeline (deko3d) instead of a linear CPU framebuffer.

Two different numbers matter here, and they measure different things: the
compositor itself is hardware-confirmed at **60fps** in an isolated smoke
test (device/queue/present, real shaders and 3D geometry, no Wine
involved); the full Win32 GUI pipeline that feeds it — message loop, GDI
drawing, Wine's client/server IPC — currently reaches a hardware-confirmed
**~34fps steady state** (27–39) end to end, up from 8fps after diagnostic
overhead hidden in the IPC path was found and removed. The full
investigation trail behind those numbers lives in
[wine-nx-probe/perf-lab-log.md](wine-nx-probe/perf-lab-log.md).

Originally started by [dantiicu](https://github.com/dantiicu/wine-nx) as an
early Horizon/libnx Wine bring-up — the loader, initial win32u driver, and
NTDLL substrate are his foundation. This fork continues that work.

## Current Status

The project builds and stages a Switch NRO runtime that launches Wine PE
targets from `sdmc:/switch/wine`.

Notepad is the main real-GUI test target. It launches far enough to show
the main window, frame, menu bar, menu popups, text rendering, and
touch-driven menu interaction. It is experimental and not production
usable: responsiveness is poor and several real-app subsystems remain
incomplete (see Known Limitations).

## What Works

- **Switch build and packaging** — `wine-nx-probe/build-switch.sh` builds
  the runtime NRO with the `devkitpro/devkita64` Docker image and stages a
  runnable SD package (including Wine NLS files and fonts) under
  `wine-nx-probe/build-switch/sd-card/switch/wine`. `WINE_NX_APP` selects
  the staged target (`notepad`, `gui`, `blit`, `curl`).
- **Runtime loader** — bootstraps a Wine ARM64 PE target from the staged
  package: PE import resolution, loader handoff, Wine DLL staging, runtime
  target selection. The NRO carries a build marker so hardware logs confirm
  which binary is actually running. Logs go to
  `sdmc:/switch/wine/wine-nx-runtime.log` and `horizon-trace.log`.
- **Horizon/Wine server substrate** — TLS model, executable-memory/JIT
  alias handling, thread setup, address-space reservation, and an
  in-process Horizon/Wine server (`dlls/ntdll/unix/horizon.c`) servicing
  the current NTDLL and USER/GDI flows; win32u syscall dispatch for the
  exercised calls; atoms, window stations/desktops, window classes,
  top-level and popup windows.
- **GDI/USER bring-up** — software rendering through Wine's DIB path: DCs,
  compatible DCs, DIB sections, StretchDIBits-style raster work, cursor and
  menu drawing, FreeType-backed text. `RegisterClassW`/`CreateWindow` work.
  Notepad's non-client rendering (frame, title buttons, menu bar, popups)
  draws correctly.
- **Switch display driver** — `dlls/win32u/winnx_drv.c` reports a single
  1280x720 monitor; window surfaces render into software DIB memory and the
  driver blits dirty pixels to the framebuffer, with batched/dirty-present
  logic and popup-surface restore.
- **Input** — touchscreen sampled via libnx, translated to absolute Win32
  mouse input; non-client hit tests and child-window hit routing good
  enough to drive Notepad's frame and menus. No keyboard/controller text
  input yet.
- **deko3d GPU compositor** — hardware-confirmed twice over: the standalone
  smoke test (`wine-nx-probe/source/deko3d_smoke.c`) runs device/queue/
  swapchain/present, a CPU-to-GPU texture upload, and a shader-driven
  rotating cube together at 60-62fps; and the separate
  `wine-nx-runtime-deko3d` binary (built with `WINE_NX_DEKO3D_ONLY`)
  presents the full Win32 GDI smoke stack through deko3d end to end. The
  in-binary runtime-selectable backend swap is permanently blocked by
  libnx's pre-`main()` `vi` bootstrap — see
  [wine-nx-probe/3d-accel-scoping.md](wine-nx-probe/3d-accel-scoping.md).
- **GUI smoke demo** — `WINE_NX_APP=gui`
  (`wine-nx-probe/samples/gui-smoke/gui_smoke.c`): bouncing-shapes
  animation on a real `PeekMessageW` loop with a native-side HUD showing
  live present-rate stats (toggled with Plus).
- **Host-side development (macOS/Linux, no console needed)** — the Switch
  display driver calls zero libnx symbols directly, so
  `dlls/win32u/winnx_host_sim.c` implements its `wine_nx_fb_*`/touch hooks
  with SDL2 and the real driver runs on a host build
  (`WINE_NX_HOST_SIM=1`), in Docker via `wine-nx-probe/docker-host-sim/`.
  See `wine-nx-probe/switch-shims/README.md` for what is and isn't
  faithfully simulated (rendering/input correctness — not presentation
  performance).

## Known Limitations

- **~34fps GUI steady state, CPU-bound in the paint/IPC pipeline** — not
  the GPU and not the pixel blit. The long-suspected "~14ms per IPC call
  transport floor" turned out to be diagnostic logging (unconditional
  fflush-to-SD in the hot server handlers), not Horizon scheduler latency —
  measured server-thread wake latency is ~7µs, and with the traces
  rate-limited the same calls now measure ~0ms median. Remaining frame cost
  is ~25ms/frame: several synchronous server round trips per paint plus
  ~12ms not yet covered by any timer. Full trail, numbers, and next levers
  in [wine-nx-probe/perf-lab-log.md](wine-nx-probe/perf-lab-log.md).
- **`GetTickCount`/`GetTickCount64` are frozen for PE apps** — the shared
  user data page's `TickCount` is never written on this port (the
  wineserver poll loop that updates it in real Wine never runs here). The
  unix-side `NtGetTickCount()` was fixed to compute real elapsed time, but
  PE-side `kernel32` reads the shared page directly, so hosted apps relying
  on it for timers/animation get a frozen value.
  `QueryPerformanceCounter`/`QueryPerformanceFrequency` work and are the
  safe substitute.
- **Blocking synchronization is incomplete in the in-process server** —
  the current wait path is non-blocking (unsatisfied waits return
  immediately rather than parking with a timeout), waitable-timer expiry
  has no timekeeping engine behind it, and thread/process handles don't
  track real exit state. The single-window smoke targets don't exercise
  this; real multithreaded apps will. This is the next major substrate
  workstream.
- **No GPU-accelerated *rendering* for apps** — presentation is
  GPU-composited (deko3d), but there is no WGL/OpenGL/D3D driver at all
  (`nulldrv_OpenGLInit` is unimplemented). Vulkan/DXVK/vkd3d are ruled out
  on Horizon (NVK needs Linux's `nouveau` DRM stack, which Horizon doesn't
  have); Mesa **OpenGL ES** exists in the toolchain and a wined3d-over-GLES
  path is the realistic long-term direction — scoped, not built. See
  [wine-nx-probe/3d-accel-scoping.md](wine-nx-probe/3d-accel-scoping.md).
- **UI completeness is early** — menus aren't Windows-perfect; popup/menu
  z-order and invalidation need more compositor logic; touch needs better
  capture/focus/drag/double-click; no controller/keyboard text input;
  Notepad is a milestone, not proof that arbitrary GUI apps run.
- **Wine subsystem gaps** — real applications will need more: USER message
  queue and window-state edge cases, common controls and dialogs,
  shell/comdlg/shell32, COM/OLE, registry/prefix behavior, font
  discovery/fallback beyond the staged set, clipboard/IME/keyboard/
  controller input, audio, networking, multi-process.
- ~~Host-sim rendering is blocked by a loader bug~~ — **fixed**: the
  "kernel32.dll fails to load" symptom was the old
  `docker-host-sim/run-host-sim.sh` never building any PE DLLs, not a
  loader bug. The script now does a full build and is a self-checking
  smoke gate (a test PE must actually render through the SDL2 path for it
  to pass).
- **Known crash** — the GUI smoke demo's Minus-button exit crashes to the
  Home Menu instead of returning to hbmenu (root cause unresolved; `[EXIT]`
  step logging is in place). The physical HOME button is the clean way out.

## Build

### Host System Setup

The Switch NRO build runs inside Docker with the `devkitpro/devkita64`
image. The script installs missing Switch portlibs inside that container
when needed, including FreeType/Harfbuzz for font rendering.

Third-party sample binaries (the `WINE_NX_APP=curl` target and the 7-Zip
installer) are not tracked in git; fetch them (sha256-pinned) with:

```sh
./wine-nx-probe/tools/fetch-samples.sh
```

The host-side Wine PE rebuild path uses LLVM-MinGW. The default expected
path is ignored by git:

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

For another host/toolchain layout, point the build at an extracted
LLVM-MinGW directory:

```sh
LLVM_MINGW_DIR=/path/to/llvm-mingw WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

The ARM64 PE Wine build trees are also ignored by git:

```text
wine-nx-probe/build-wine-arm64-pe-clean
wine-nx-probe/build-wine-arm64-pe-local
```

Those directories are disposable Wine PE build trees used to rebuild
Notepad and Wine PE DLLs. `wine-nx-probe/build-wine-arm64-pe-clean` is the
default PE build directory. When `WINE_NX_APP=notepad` and
`wine-nx-probe/build-wine-arm64-pe-local` exists, `build-switch.sh` prefers
the local tree for the staged program. Override either path with:

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

After configure, the normal Switch package build compiles the missing
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

The staged SD package is written to
`wine-nx-probe/build-switch/sd-card/switch/wine`; copy or sync it to
`sdmc:/switch/wine` (the package helper prints the
`wine-nx-probe/tools/sync-switch-wine-package.sh` sync command).

Runtime toggles live in one consolidated `sdmc:/switch/wine/config.txt`
(`key=value` per line, bare key means true) with `WINE_NX_*` env vars
taking priority where an environment exists; `=0`/`=false` disables a
toggle through either mechanism.

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

The marker matters. Several performance/debug loops looked confusing until
the logs showed an older NRO was still being run.

## Important Files

Runtime and packaging:

```text
wine-nx-probe/build-switch.sh
wine-nx-probe/source/runtime.c
wine-nx-probe/source/runtime_platform.c
wine-nx-probe/CMakeLists.txt
wine-nx-probe/tools/fetch-samples.sh
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

Host-side development (no console needed):

```text
dlls/win32u/winnx_host_sim.c
wine-nx-probe/switch-shims/README.md
wine-nx-probe/switch-shims/macos26-loader-incompatibility.md
wine-nx-probe/docker-host-sim/
```

Docs and smoke targets:

```text
wine-nx-probe/perf-lab-log.md
wine-nx-probe/3d-accel-scoping.md
wine-nx-probe/samples/gui-smoke
wine-nx-probe/samples/direct-blit
wine-nx-probe/samples/curl-arm64
wine-nx-probe/source/deko3d_smoke.c
wine-nx-probe/source/jit_smoke.c
```

## Next Milestones

1. **Paint-pipeline performance** — the compositor is no longer the open
   question (deko3d is hardware-confirmed, in isolation and as the real
   backend via `wine-nx-runtime-deko3d`), and the "~14ms IPC floor" theory
   is settled (it was diagnostic-logging overhead; scheduler wakes measure
   ~7µs). At ~34fps the scoped next levers are: turn the already-landed
   batching toggles on (5–6 round trips/frame → 2–3, projecting roughly
   50–70fps), and add sub-ms timers to account for the ~12ms/frame no
   phase timer currently covers — see
   [wine-nx-probe/perf-lab-log.md](wine-nx-probe/perf-lab-log.md) for
   what's been tried, ruled out, and measured.
2. **Substrate correctness** — real blocking waits/timeouts, a timer
   expiry engine, thread/process exit tracking, and unfreezing the
   PE-visible tick count (see Known Limitations).
3. **Input polish** — keyboard/text input, controller mouse mode, better
   touch capture and focus, double-click/drag, native `WM_TOUCH` later.
4. **USER/window manager behavior** — popup/menu stacking,
   owner/activation edge cases, clipping and invalidation, modal dialogs,
   child-window ordering.
5. **More real apps** — after Notepad is smoother: small non-network GUI
   apps exercising dialogs, common controls, edit controls, and file
   browsing, before anything needing a browser engine or GPU API.

## Community

Questions, previews, and releases: join the
[Discord server](https://discord.gg/vprPQ46Q3).

## License

Wine-NX is a fork of [Wine](https://www.winehq.org/) and inherits its
license: **GNU Lesser General Public License v2.1 or later** — see
[COPYING.LIB](COPYING.LIB) and [LICENSE](LICENSE). The fork's own additions
(`wine-nx-probe/`, `dlls/ntdll/unix/horizon.c`, the `dlls/win32u/winnx_*`
drivers, and the other `wine-nx`-marked changes) are provided under the
same LGPL-2.1-or-later terms as upstream Wine.
