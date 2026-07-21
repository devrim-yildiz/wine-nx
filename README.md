# Wine-NX

**A Wine port that runs Windows programs directly on Nintendo Switch
homebrew (Horizon OS via libnx) — no Linux layer, no second kernel
underneath.**

Originally started by [dantiicu](https://github.com/dantiicu/wine-nx) as an
early Horizon/libnx Wine bring-up — the loader, initial win32u driver, and
NTDLL substrate are his foundation. This fork continues that work.

`Status: experimental.` Real Windows programs boot, create windows, and
render — but this is not yet a polished, general-purpose way to run
Windows software on a Switch. See "Current Status" below.

## Discord

Day-to-day progress, bugfixes, and screenshots are posted on Discord —
that's the place to ask questions or follow along between releases.

**[Join the Discord server](https://discord.gg/vprPQ46Q3)**

## What This Is

Wine-NX has two execution paths:

1. **Native ARM64** — runs AArch64 Windows PE binaries directly, with
   GPU-accelerated presentation via [deko3d](https://github.com/devkitPro/deko3d)
   (Switch homebrew's native low-level GPU API). This is the original,
   more mature path.
2. **WOW64 / box64** — runs real, unmodified 32-bit (x86) Windows binaries
   by emulating x86 via [box64](https://github.com/box64-proj/box64)'s
   dynarec, wrapped in a from-scratch WOW64 (32-on-64) compatibility
   layer. This is what gets actual 32-bit Windows software — the kind
   people already have — running at all.

Both talk straight to Horizon OS via libnx. There's no Switchroot/L4T, no
second kernel, no Linux compatibility layer anywhere in the stack.

## Current Status

**Native ARM64 path:** Wine Notepad is the current real-app milestone — it
boots, shows its window, frame, menu bar, and renders text and controls.
Touch-driven menu interaction works. GDI rendering is CPU-side and still
the bottleneck: the GDI/IPC pipeline that feeds the display runs around
**8fps**, even though the GPU compositor itself (deko3d) is proven capable
of 60-62fps in isolation. The gap between those two numbers is an open,
partially-diagnosed problem — see `wine-nx-probe/perf-lab-log.md` for the
full investigation.

**32-bit WOW64 path:** boots and runs real, unmodified 32-bit Windows
binaries — confirmed with OpenTTD (no bundled DLLs) and a purpose-built
smoke test (`cube32`). After a chain of boot-sequence bugs and a
performance investigation, this path now runs at a hardware-confirmed
**60fps**, matching the native path's own present-rate cap. Text rendering
is still inconsistent in some contexts (see "Known Limitations").

Neither path is production-usable yet: UI completeness, input handling,
and several Wine subsystems are still early. Treat this as a real,
working prototype, not a finished emulator.

## Building

### Host System Setup

The Switch NRO build runs inside Docker with the `devkitpro/devkita64`
image. The script installs missing Switch portlibs inside that container
when needed, including FreeType/Harfbuzz for font rendering.

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

After configure, the normal Switch package build will compile the missing
Notepad executable and DLLs from that tree.

For WOW64 (aarch64 native + i386 guest — see
`wine-nx-probe/wowbox64-scoping.md` for the full integration writeup)
instead, change `--enable-archs=aarch64` above to
`--enable-archs=aarch64,i386` (not `i686` — `configure.ac` only accepts
`{i386,x86_64,arm,aarch64}`). Requires a newer bison on `PATH` ahead of
macOS's stock 2.3 (`brew install bison` provides 3.8.2; 3.0+ is required).
`dlls/wow64cpu` deliberately does *not* build for an aarch64 host —
`configure.ac` hardcodes it to `x86_64` only, by design: Wine expects a
third-party emulator (box64's WowBox64, here) to provide the CPU-backend
DLL for an emulated host/guest pairing, not its own `dlls/wow64cpu`.

### Build Commands

From the repository root:

```sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

`WINE_NX_APP` selects the staged target:

```sh
WINE_NX_APP=gui ./wine-nx-probe/build-switch.sh       # animated GDI smoke test
WINE_NX_APP=cube32 ./wine-nx-probe/build-switch.sh     # 32-bit WOW64 smoke test
WINE_NX_APP=curl ./wine-nx-probe/build-switch.sh       # networking smoke test
WINE_NX_APP=openttd ./wine-nx-probe/build-switch.sh    # real 32-bit game (bring your own install)
```

The staged SD package is written to:

```text
wine-nx-probe/build-switch/sd-card/switch/wine
```

Copy or sync that package to `sdmc:/switch/wine`. The build prints a sync
helper:

```text
Sync mounted SD with: wine-nx-probe/tools/sync-switch-wine-package.sh
```

### Host-Side Development (No Console Needed)

The Switch display driver (`dlls/win32u/winnx_drv.c`) only calls a
handful of `wine_nx_fb_*`/`wine_nx_touch_poll()` hooks, which
`dlls/win32u/winnx_host_sim.c` reimplements with SDL2 — so the real,
unmodified display driver links and runs against a plain host build:

```sh
WINE_NX_HOST_SIM=1 wine ...
```

See `wine-nx-probe/switch-shims/README.md` for what is and isn't
faithfully simulated (multi-touch and presentation *performance* aren't —
this is for rendering/input correctness, not perf).
`wine-nx-probe/docker-host-sim/` runs this in a Linux container, needed on
macOS hosts hitting an unrelated OS-level loader issue — see
`wine-nx-probe/switch-shims/macos26-loader-incompatibility.md`.

## Testing On Hardware

1. Build the package with the target you want:

```sh
WINE_NX_APP=notepad ./wine-nx-probe/build-switch.sh
```

2. Deploy the staged `switch/wine` package to `sdmc:/switch/wine`.
3. Run on Switch.
4. Confirm the runtime log starts with the expected build marker (printed
   at the top of `sdmc:/switch/wine/wine-nx-runtime.log`) — this catches
   the common mistake of testing against a stale NRO.
5. Logs:

```text
sdmc:/switch/wine/wine-nx-runtime.log
sdmc:/switch/wine/horizon-trace.log
```

## Project Layout

Runtime and packaging:

```text
wine-nx-probe/build-switch.sh
wine-nx-probe/source/runtime.c
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
dlls/win32u/font.c
```

Horizon/NTDLL substrate (this port's stand-in for a real wineserver
process — there isn't one; `horizon.c` plays that role in-process):

```text
dlls/ntdll/unix/horizon.c
dlls/ntdll/unix/file.c
dlls/ntdll/unix/process.c
dlls/ntdll/unix/thread.c
dlls/ntdll/unix/signal_arm64.c
```

WOW64 / box64 CPU backend:

```text
wine-nx-probe/third_party/box64      # vendored box64, incl. wine/wow64/wowbox64.c
wine-nx-probe/wowbox64-scoping.md    # integration writeup
```

Host-side development:

```text
dlls/win32u/winnx_host_sim.c
wine-nx-probe/switch-shims/
wine-nx-probe/docker-host-sim/
```

Smoke test targets:

```text
wine-nx-probe/samples/gui-smoke
wine-nx-probe/samples/cube32
wine-nx-probe/samples/direct-blit
wine-nx-probe/samples/curl-arm64
wine-nx-probe/source/deko3d_smoke.c
```

Detailed technical write-ups (build history, root-cause chains, hardware
numbers behind every fix):

```text
wine-nx-probe/perf-lab-log.md        # performance investigation trail
wine-nx-probe/wowbox64-scoping.md    # WOW64 integration planning + bring-up
wine-nx-probe/3d-accel-scoping.md    # deko3d GPU compositor bring-up
```

## Known Limitations

- **Native GDI pipeline is still CPU-bound at ~8fps**, despite the GPU
  compositor itself being proven fast (60-62fps in isolation). Root cause
  is an unidentified ~80-90ms/frame gap not yet fully traced — see
  `wine-nx-probe/perf-lab-log.md`.
- **32-bit WOW64 text rendering is inconsistent in some contexts** — a
  dialog box's message body rendered blank while its title and one button
  label did not. Not yet root-caused; the GDI blit path itself is
  otherwise confirmed working.
- **No GPU-accelerated rendering for apps that need it** — no WGL/D3D
  driver exists yet, so OpenGL/Direct3D content won't render (unrelated to
  the GPU-accelerated *presentation* path above, which does work).
- **UI completeness is early**: menus work but aren't pixel-perfect, no
  keyboard/text-input path, limited controller/touch handling, no
  double-click/drag polish.
- **Several Wine subsystems are incomplete or untested**: common controls
  and dialogs, shell/comdlg/shell32, COM/OLE, registry/prefix behavior,
  font discovery beyond the staged font set, clipboard, IME, audio,
  networking, and multi-process behavior.

## Roadmap

1. Close the native path's remaining ~80-90ms/frame gap (see
   `wine-nx-probe/perf-lab-log.md` for where the investigation left off).
2. Root-cause the WOW64 text-rendering inconsistency.
3. Input polish: keyboard/text input, controller mouse mode, better touch
   capture/focus, double-click/drag behavior.
4. USER/window-manager behavior: popup/menu stacking, owner/activation
   edge cases, clipping/invalidation, modal dialogs, child-window
   ordering.
5. More real-app testing beyond Notepad and OpenTTD — small GUI apps that
   exercise dialogs, common controls, and file browsing without needing a
   browser engine or a GPU API first.

## License

Wine-NX is a fork of [Wine](https://www.winehq.org/) and inherits its
license: the GNU Lesser General Public License v2.1 (or later). See
[`LICENSE`](LICENSE) and [`COPYING.LIB`](COPYING.LIB).
