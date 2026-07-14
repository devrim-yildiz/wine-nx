# 3D-accel (GPU compositor) milestone -- scoping

Status: the Mesa/NVK dependency question is **resolved** (see below), and
step 1 of the concrete next steps -- an isolated deko3d bring-up smoke test
-- is now **built and hardware-confirmed working**: device, queue, and
swapchain creation against `nwindowGetDefault()` all function on real
Switch hardware, presenting a scissor+clear-color test pattern every frame
(`wine-nx-probe/source/deko3d_smoke.c`, target `wine-nx-deko3d-smoke`). See
"What's actually built" at the bottom for the full picture, including what
still isn't built (texture upload, the actual compositor).

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
   compositor~~ **Done, partially.** `wine-nx-probe/source/deko3d_smoke.c`
   (target `wine-nx-deko3d-smoke`) proves device/queue/swapchain creation
   and present against `nwindowGetDefault()`, **hardware-confirmed**: dark
   background with a cyan test-pattern box rendering correctly. Scoped
   deliberately smaller than originally planned here -- scissor+clear only,
   no shaders, no texture, no romfs -- so this first hardware round-trip
   only had to prove the bare pipeline, not shader compilation too. The
   texture-upload/quad step this doc originally described as part of step 2
   is still open; `deko_examples/Example04_TexturedCube.cpp` in the
   toolchain image remains the directly usable template for it (minus the
   cube -- a static quad is simpler).
3. **Next**: extend `deko3d_smoke.c` (or a new sample) to upload one static
   texture and draw one textured quad, hardware-confirm that too -- this is
   the actual shape of the compositor's real workload (a Wine DIB becomes
   the uploaded texture), unlike the shader-free clear-only step just done.
4. **Only then** replace `wine_nx_fb_lock/unlock/present`'s libnx-framebuffer
   implementation in `wine-nx-probe/source/runtime.c` with the validated
   deko3d logic, gated behind the existing `__SWITCH__` build, kept
   alongside (not replacing) the current path so there's a fallback if the
   GPU path regresses something the CPU path didn't.

## What's actually built vs. planned

- **Built and hardware-verified**: the `wine_nx_fb_lock/unlock/present`
  interface boundary is proven stable across two independent presentation
  backends (libnx framebuffer on real hardware, SDL2 in the host-sim).
  `wine-nx-deko3d-smoke` additionally proves deko3d device/queue/swapchain
  creation and present against the same `nwindowGetDefault()` handle work
  on real hardware, standalone (not yet wired into that interface). The
  Mesa/NVK dependency question is resolved with direct evidence, not
  assumption (see above).
- **Not built**: GPU texture upload, a textured quad, or any actual
  compositor logic -- `deko3d_smoke.c` only proves the pipeline itself
  (clear-color present), not the DIB-upload workload the real milestone
  needs, and nothing is wired into `wine_nx_fb_lock/unlock/present` yet.
  Texture upload and the real compositor swap remain open, in that order.
