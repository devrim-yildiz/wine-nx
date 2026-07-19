# 3D-accel (GPU compositor) milestone -- scoping

Status (updated since this doc was first written): the Mesa/NVK dependency
question is **resolved** (see below), the deko3d bring-up smoke test
(`wine-nx-probe/source/deko3d_smoke.c`, target `wine-nx-deko3d-smoke`)
hardware-confirmed device/queue/swapchain creation, a CPU-to-GPU texture
upload, and a real shader-driven rotating cube all running together at
60-62fps -- and the compositor path this doc's blocker analysis called for
**has since landed and is hardware-confirmed working**: the separate
`wine-nx-runtime-deko3d` binary (built with `WINE_NX_DEKO3D_ONLY`, see
`wine-nx-probe/CMakeLists.txt`) compiles out every libnx-framebuffer/
console path so deko3d really is the first `vi` consumer, and the full
Win32 GUI smoke stack renders and presents through it on hardware. What
remains blocked -- permanently, for the pre-main() `__appInit` reason
documented below -- is the *single-binary, runtime-selectable* backend
swap inside the original `wine-nx-runtime`: that binary's parked
`WINE_NX_DEKO3D` flag can never work and is kept only as documentation.
See "Compositor-swap attempt: blocked by libnx's pre-main() vi bootstrap"
below for that root cause (still accurate for the single-binary case),
and the top-level README plus `wine-nx-probe/perf-lab-log.md` for the
hardware results measured through the separate binary.

## Resolved: does a usable Mesa/NVK (Vulkan) port exist for this project?

**No, and it's not a packaging gap -- it's architectural.** Verified by
inspecting the actual `devkitpro/devkita64` Docker image this project's own
`build-switch.sh` builds with (not assumed, not guessed):

- No Vulkan headers, no Vulkan libraries, no ICD, no `nvk` anywhere in the
  image. `dkp-pacman -Ss vulkan` returns nothing Switch-GPU-related.
- Mesa's NVK driver is built on top of Linux's `nouveau` DRM kernel driver
  (GEM/TTM buffer management, DRM syncobjs, the whole Linux DRM/KMS uAPI).
  Horizon OS is not Linux and has no DRM subsystem at all -- Switch
  homebrew talks to the GPU through Horizon's own `nvhost`/`nvmap` IPC
  services instead. NVK cannot run in a Horizon-OS homebrew process
  regardless of how mature the driver gets upstream; there's no kernel
  underneath it to attach to. The only way to run NVK on this hardware at
  all would be booting a real Linux kernel on the console (e.g. a
  Switchroot/L4T-style Linux distro) -- a completely different target than
  "Switch homebrew via libnx," not a library this project could link
  against.

**But real GPU acceleration does exist and is already installed** in the
same toolchain image, just not Vulkan:

- `deko3d` 0.5.0 -- Switch homebrew's own low-level, Vulkan-adjacent GPU
  API (explicit command buffers/memory pools), built and maintained by the
  devkitPro/switchbrew team specifically for this OS. Headers/static libs
  already present (`deko3d.h`, `libdeko3d.a`), with nine worked examples
  under `/opt/devkitpro/examples/switch/graphics/deko3d/deko_examples`
  (triangle through deferred shading and compute).
- `switch-mesa` 20.1.0 -- real Mesa, providing EGL + OpenGL ES 1.1/2.0/3.x
  (`libEGL.a`, `libGLESv2.a`, etc.), backed by `switch-libdrm_nouveau`: a
  Switch-native shim that reimplements just enough of Mesa's old
  gallium/nouveau winsys against Horizon's `nvhost`/`nvmap` services to
  make Mesa's *OpenGL* driver work, without any real Linux DRM underneath.
  This is a 2020-era Mesa branch, well before NVK existed upstream (NVK
  landed in Mesa ~24.x, in 2024) -- it only ever provided OpenGL, not
  Vulkan, so there was never a Vulkan capability here to lose.

Both `deko3d`'s and the OpenGL examples' display setup call
`nwindowGetDefault()` -- the exact same nwindow handle
`wine_nx_fb_init()` already passes to `framebufferCreate()` in
`wine-nx-probe/source/runtime.c`. That's a second, independent confirmation
(after the host-sim's SDL2 backend) that swapping the presentation backend
here is a real, bounded interface swap, not a rewrite -- true for either
GPU API choice.

**Side effect worth flagging:** since Vulkan doesn't exist on this OS, the
README's framing of "DXVK/vkd3d later, after Wine's Vulkan path is clean"
for real D3D-game acceleration is a dead end on Switch specifically. Wine's
older `wined3d` OpenGL backend (predates DXVK, still fully present upstream)
is the actually-viable long-term path to accelerated D3D on this platform,
*because* Mesa/GLES is real and installed. Noting this now so the
"Vulkan later" framing doesn't quietly stay in anyone's head as the plan --
it isn't reachable here. Not scoped further in this pass.

## What the milestone actually is

This is **not** "get D3D games running via DXVK/vkd3d" -- and per the
resolved Vulkan/NVK question above, it structurally can't be; DXVK/vkd3d
both require Vulkan, which doesn't exist on Horizon OS. It's narrower:
replace the *presentation* path only --

- Keep Wine's existing software GDI/DIB rendering exactly as-is.
- Instead of `framebufferBegin()`/`framebufferEnd()` (CPU block-linear
  conversion of the full 1280x720 buffer every present, the thing
  `switch-performance-work` -- see [Task 6] -- is about reducing the
  *frequency* of), upload the DIB pixels as a GPU texture and composite/
  present via a real GPU API.
- Real D3D acceleration, if it happens later, routes through Wine's
  existing OpenGL `wined3d` backend against Mesa/GLES instead -- a
  separate, larger, not-yet-scoped milestone, and not a dependency of this
  one.

That's a much more tractable first target than it might sound: it's a
presentation-backend swap, not a renderer rewrite.

## Where this hooks in (already validated, just with a different backend)

The exact interface this needs to fill is the same one `winnx_host_sim.c`
(the macOS/Linux host-sim, see `switch-shims/README.md`) already fills with
SDL2:

```text
wine_nx_fb_lock( int *width, int *height, int *stride_px )   -- get a writable buffer
wine_nx_fb_unlock( void )                                    -- mark it dirty
wine_nx_fb_present( void )                                   -- push it to the display
```

implemented today in `wine-nx-probe/source/runtime.c` (lines ~140-208) via
`framebufferCreate/Begin/End/MakeLinear`. A GPU-compositor backend replaces
*only* this file's implementation of those three functions -- nothing in
`dlls/win32u/winnx_drv.c` (the driver that calls them) or above it needs to
change at all, the same way it didn't need to change for the SDL2 host-sim.
That's a real, tested fact about the interface boundary, not a guess: two
independent backends (libnx framebuffer, SDL2) already implement it
without touching the caller.

## Two real candidate GPU APIs, and the tradeoff

NVK is off the table (see above). The actual choice is between the two
APIs confirmed available in the toolchain:

**deko3d** (Switch homebrew's own low-level GPU API):
- Pro: purpose-built for this exact OS/hardware by the same team that
  maintains libnx, actively maintained (0.5.0, current), nine worked
  examples to crib from, no extra translation layer between the app and
  Horizon's `nvhost`/`nvmap` services.
- Con: explicit/low-level (manual command buffers, memory pools, descriptor
  sets) -- more boilerplate for a first "upload a texture, draw one quad,
  present" prototype than a high-level API would need. For a workload this
  small (one textured quad, not a full renderer), that boilerplate is
  bounded and a reasonable, known cost, not open-ended risk.

**Mesa OpenGL ES** (`switch-mesa` + EGL, via the `libdrm_nouveau` shim):
- Pro: much higher-level -- `glTexImage2D` + a textured quad is very few
  lines, fastest to a first working prototype, and it's the same API
  `wined3d`'s existing OpenGL backend targets, which matters for the D3D
  angle noted above.
- Con: two layers of indirection instead of one (Mesa gallium -> the
  Switch-specific `libdrm_nouveau` shim -> Horizon's `nvhost`/`nvmap`,
  versus deko3d's direct path), on a Mesa branch frozen around the 2020
  20.1 release -- unknown performance/stability on this specific
  workload until actually measured, whereas deko3d is what most current
  Switch homebrew 3D projects actually ship with.

**Recommendation: deko3d first.** The milestone here is presentation only
(one texture upload, one quad, one present per frame) -- small enough that
deko3d's extra boilerplate is a one-time, bounded cost, and it avoids
depending on a frozen third-party translation layer for the thing this
project's performance actually hinges on. Mesa/GLES stays the right choice
*later*, specifically for `wined3d`-backed D3D acceleration (a separate,
larger, not-yet-scoped milestone) -- no reason to require it for the
presentation-compositor milestone specifically.

## Concrete next steps (in order)

1. ~~Resolve the Mesa/NVK availability question~~ **Done** -- see above.
2. ~~Prototype deko3d in isolation first, not against the real
   compositor~~ **Done.** `wine-nx-probe/source/deko3d_smoke.c` (target
   `wine-nx-deko3d-smoke`), built in three hardware-confirmed stages:
   device/queue/swapchain/present (scissor+clear only, no shaders); a
   CPU-generated texture uploaded via `dkCmdBufCopyBufferToImage()`
   straight onto a framebuffer sub-rect (still no shaders -- proves the
   upload path, not sampling); and a real shader-driven rotating cube
   (uam-compiled shaders, depth buffer, per-frame uniform updates via a
   fence-protected dynamic command ring). All three run together, holding
   60-62fps on real hardware for a 5-10 second test -- no measurable fps
   cost from real shader/geometry work over the clear-only baseline.
3. **Still open**: an actual *sampled* textured quad (shader + sampler +
   descriptor set reading the uploaded texture, the one combination not
   yet built -- stage 2's upload and stage 3's shaders are proven
   separately, not together). `deko_examples/Example04_TexturedCube.cpp`
   remains the template for this (minus the cube -- a flat quad is
   simpler). Only actually needed if the real compositor swap turns out to
   require GPU-side sampling rather than a second direct
   `CopyBufferToImage`-style upload; not yet clear which one it needs.
4. ~~Replace `wine_nx_fb_lock/unlock/present`'s libnx-framebuffer
   implementation with the validated deko3d logic, kept alongside (not
   replacing) the current path~~ **Attempted, architecturally blocked.**
   Wired as an opt-in backend (`wine-nx-probe/source/runtime_deko3d.c`,
   selected via `WINE_NX_DEKO3D` env var or `sdmc:/switch/wine/deko3d.txt`,
   same pattern as `WINE_NX_HOST_SIM`) with the existing libnx path left
   completely untouched and still the default. See the dedicated section
   below for why this can't work as a single-binary runtime choice, and
   what the real path forward looks like.

## Compositor-swap attempt: blocked by libnx's pre-main() vi bootstrap

The opt-in deko3d compositor backend (`wine-nx-probe/source/runtime_deko3d.c`)
implements `wine_nx_fb_lock/unlock/present` using the exact mechanism
`deko3d_smoke.c` stage 2 proved on hardware: a CPU-writable staging buffer
copied onto the swapchain image via `dkCmdBufCopyBufferToImage()`, no
shaders needed since Wine's DIB is already flat RGBA8 pixel data. It builds
and, when selected, engages correctly (`[NXFB] backend: deko3d` logs, device
and framebuffer-image creation succeed) -- but swapchain creation
consistently fails or crashes, root-caused through three rounds of
hardware-log-driven diagnosis, each grounded in real reference source
(devkitPro's `deko3d`/`libnx` GitHub repos, not memory):

1. **First failure**: `dkSwapchainCreate()` crashed hard (no debug-callback
   fire, no further log line) when handed `nwindowGetDefault()` -- the same
   nwindow the libnx-framebuffer path already reuses successfully after
   `consoleExit()`. Traced into deko3d's real `Swapchain::initialize()`
   source (`dk_swapchain.cpp`): it calls `nwindowSetDimensions()`
   internally as its first step. A direct diagnostic call to the same
   function, with the same args, confirmed the actual failure:
   `MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)` (0xf59) --
   not a crash, a clean, documented precondition violation
   (`nwindowSetDimensions()`: "cannot be called when there are buffers
   registered"). `nwindowReleaseBuffers()`, added as a fix, compiled and
   ran but had zero observable effect on the outcome.
2. **Root cause of #1**: `nwindowGetDefault()`'s underlying `NWindow` gets
   `nwindowSetDimensions()` called on it exactly once, automatically, by
   libnx's own `__nx_win_init()` (`nx/source/display/default_window.c`) --
   *before `main()` even runs*, regardless of `consoleInit()`. No public
   libnx API undoes that lock.
3. **Second attempt**: build a dedicated `ViDisplay`/`ViLayer`/`NWindow`
   instead of sharing the default one, following libnx's own
   `__nx_win_init()` sequence exactly
   (`viInitialize` -> `viOpenDefaultDisplay` -> `viCreateLayer` ->
   `viSetLayerScalingMode` -> `nwindowCreateFromLayer` ->
   `nwindowSetDimensions`) -- confirmed via `vi.c` that `viInitialize` is
   refcounted and `viCreateLayer` is a plain per-call IPC request, not a
   singleton. `viInitialize` succeeded; `viOpenDefaultDisplay` failed with
   a genuine `vi`-service-level rejection (module 114, description 9 --
   undocumented in switchbrew's public error table).
4. **Root cause of #3, and the actual architectural blocker**: traced into
   libnx's `nx/source/runtime/init.c`. `__nx_win_init` is a *weak* symbol;
   `default_window.c`'s real definition (and the `viOpenDefaultDisplay`
   call inside it) only gets linked in -- and only then gets invoked,
   unconditionally, before `main()`, from `__appInit()`'s
   `if (&__nx_win_init) __nx_win_init();` -- if `nwindowGetDefault` is
   *referenced anywhere in the binary*. This binary's libnx-framebuffer
   path (the required fallback, and the pre-existing default) calls
   `nwindowGetDefault()` directly, so that reference always exists, so
   `__nx_win_init()` always runs, so a `ViDisplay` for "Default" is always
   already open before `main()` starts -- **completely independent of
   whether `consoleInit()` is ever called, or which backend gets selected
   at runtime.** Confirmed this isn't console-specific by checking whether
   skipping `consoleInit()` on the deko3d path would help: it can't,
   because the conflict is created at link/startup time, not by anything
   `main()` does.

**The real constraint**: a single binary that contains *any* reference to
`nwindowGetDefault()` -- which the required libnx fallback always does --
can never let deko3d be the first and only `vi` consumer, because libnx's
own crt0-level bootstrap gets there first, unconditionally. Every real
deko3d reference this project checked (`borealis`, `deko3d`'s own
`deko_basic`/`deko_console` examples) is a *single-purpose* binary that
either never touches console or *is* console's own renderer from the very
first `consoleInit()` call -- none of them ship a runtime-selectable
choice between a libnx path and a deko3d path in the same executable,
because that combination is exactly what triggers this conflict.

**Safety net (kept, and generalized beyond this specific failure)**: any
deko3d init failure, at any step and for any reason, now falls back to the
libnx-framebuffer path automatically -- confirmed on hardware that without
this, a failed deko3d init left the app with a permanently zeroed
framebuffer and an unthrottled, spinning message loop (no crash, no
fallback, just a dead compositor). `wine_nx_fb_init()` and
`wine_nx_fb_lock()` (the two call sites that can trigger deko3d init) both
check its result and flip the shared backend flag on failure; `unlock()`
and `present()` need no changes since they already just read that flag.

**Path forward, if revisited**: not a runtime flag in the shared
`wine-nx-runtime` binary -- a genuinely separate build target/executable
where deko3d is the *only* thing that ever touches `vi`/`nwindow`, with no
reference to `nwindowGetDefault()` or the libnx-framebuffer path anywhere
in that binary's link closure. That's a materially different project
shape (two runtime binaries instead of one with a flag), not a bug fix, so
it's being parked here rather than attempted in this pass.

## What's actually built vs. planned

- **Built and hardware-verified**: the `wine_nx_fb_lock/unlock/present`
  interface boundary is proven stable across two independent presentation
  backends (libnx framebuffer on real hardware, SDL2 in the host-sim).
  `wine-nx-deko3d-smoke` separately proves, all hardware-confirmed and all
  running together at 60-62fps: deko3d device/queue/swapchain
  creation/present against the same `nwindowGetDefault()` handle, a CPU
  texture upload onto a live framebuffer, and real shader-compiled,
  depth-tested, per-frame-animated 3D geometry. The Mesa/NVK dependency
  question is resolved with direct evidence, not assumption (see above).
  The compositor swap itself is wired as an opt-in backend
  (`runtime_deko3d.c`) with a hardware-confirmed automatic fallback to the
  libnx path on any deko3d failure -- the app works today exactly as
  before this milestone, regardless of the outcome below.
- **Not built, now understood to be architecturally blocked as a
  single-binary runtime choice**: the actual compositor swap. See
  "Compositor-swap attempt: blocked by libnx's pre-main() vi bootstrap"
  above for the full root-cause trace (into libnx's `__appInit`/
  `__nx_win_init` crt0 hook) and the real path forward (a separate build
  target with no libnx-fallback reference at all, not a fix to this one).
  Sampling an uploaded texture from a shader (upload and shaders are each
  proven separately, never combined) is now moot for this milestone as
  scoped, since there's no compositor left to feed it into. Two crash bugs
  surfaced building the smoke test's cube stage, both root-caused from
  hardware crash logs rather than guessed at (missing `romfsInit()`;
  un-rounded `DkMemBlockCreate()` sizes) -- worth knowing about before
  writing more deko3d code in this project, see the top of
  `deko3d_smoke.c` for details.
