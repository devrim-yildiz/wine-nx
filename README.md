# Wine-NX Switch

Wine-NX is a Nintendo Switch focused Wine bring-up. The goal is to run
AArch64 Windows PE programs inside a Switch NRO, using libnx/Horizon services
for process, memory, input, files, display, and eventually GPU presentation.

This repository is no longer just a primitive Horizon probe. It is the working
tree for the Switch Wine runtime, the in-process Horizon/Wine server work, the
win32u Switch display driver, and the SD-card package builder used for hardware
testing.

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
the next due present instead of each paying the full cost -- **compile-verified
only, not yet measured on hardware or an emulator.**

The real performance step is still a GPU presentation path. The preferred
direction is a Vulkan/NVK compositor: upload Wine DIB/window surfaces into GPU
textures, composite them, and present once per frame -- but there is currently
**no** Mesa/NVK Vulkan port anywhere in this tree (a prior version of this
README claimed otherwise; that wasn't accurate). See
`wine-nx-probe/3d-accel-scoping.md` for scoping notes on this milestone,
including the deko3d-vs-NVK tradeoff and why finding/building that port is the
actual first blocker, ahead of writing any compositor code.

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
[BUILD] nx-batched-present-1
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
```

## Next Milestones

1. Presentation performance

Replace or bypass the expensive linear framebuffer path. Preferred direction:

- Vulkan/NVK compositor for Wine software window surfaces;
- DIB/window surface upload into GPU textures;
- popup/window composition on GPU;
- one present per frame;
- DXVK/vkd3d later, after Wine's Vulkan path is clean enough for real
  D3D-to-Vulkan acceleration.

Fallback or lower-level options:

- deko3d compositor;
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
