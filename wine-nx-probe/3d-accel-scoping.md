# 3D-accel (Vulkan/NVK compositor) milestone -- scoping

Status: **scoping only**. Nothing Switch-GPU-specific was built in this
pass -- see "What's actually built" at the bottom for the one concrete,
tested artifact that's relevant here.

## Correction to the top-level README first

The main `README.md` (`Next Milestones` section) says:

> Since this tree already has a Mesa/NVK Vulkan port available, the
> preferred direction is a Vulkan/NVK compositor...

This isn't accurate as of this checkout: there is no Mesa, NVK, deko3d,
or Vulkan-related directory, submodule, or file reference anywhere in this
repository (checked `dlls/win32u/vulkan.c` -- plain unmodified upstream
Wine code, no `__SWITCH__` handling at all -- `wine-nx-probe/CMakeLists.txt`,
and searched for `mesa`/`nvk`/`deko3d` across the tree). Whatever this
sentence refers to isn't present here; either it describes a separate,
not-yet-merged branch/project, or it's aspirational wording that got left
in. Worth fixing in the README so the next person doesn't go looking for a
Mesa/NVK port that isn't there.

## What the milestone actually is

Per the README's own framing, this is **not** "get D3D games running via
DXVK/vkd3d." It's narrower: replace the *presentation* path only --

- Keep Wine's existing software GDI/DIB rendering exactly as-is.
- Instead of `framebufferBegin()`/`framebufferEnd()` (CPU block-linear
  conversion of the full 1280x720 buffer every present, the thing
  `switch-performance-work` -- see [Task 6] -- is about reducing the
  *frequency* of), upload the DIB pixels as a GPU texture and composite/
  present via a real GPU API.
- DXVK/vkd3d (actual D3D-to-Vulkan acceleration) is explicitly the *later*
  step, gated on this compositor existing and being solid first.

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

## Two candidate GPU APIs, and the tradeoff

**deko3d** (devkitPro's homebrew-native low-level GPU API for Switch):
- Pro: purpose-built for homebrew, well-documented in devkitPro examples,
  no Vulkan validation/loader overhead, smaller surface to get wrong first.
- Con: not Vulkan -- if the long-term goal is real DXVK/vkd3d D3D
  acceleration, this is a dead end for that later step; it only solves
  presentation.

**Mesa NVK** (Mesa's open-source Vulkan driver for NVIDIA/Tegra, i.e. real
Vulkan on the Switch's GPU):
- Pro: real Vulkan, so the compositor *and* the later DXVK/vkd3d step share
  one GPU API instead of needing a second migration later.
- Con: heavier dependency (full Mesa build for the Switch target), less
  homebrew-battle-tested than deko3d, and -- per the correction above --
  not actually present/available in this tree despite the README's claim,
  so step zero is finding or building that port before any compositor code
  can be written against it.

Given the README explicitly frames DXVK/vkd3d as a stated later goal, NVK
is the architecturally consistent choice *if* a working Mesa/NVK port can
actually be obtained -- that's an open dependency question, not a coding
question, and needs resolving before writing compositor code, not after.

## Concrete next steps (in order)

1. **Resolve the Mesa/NVK availability question.** Find out where the
   README's claimed port actually lives (ask whoever wrote that line, or
   check devkitPro's portlibs for an NVK package), or make the deko3d-vs-NVK
   call explicitly instead of assuming NVK is ready to use.
2. **Prototype in the host-sim first, not on hardware.** The `pkg-config`
   pattern already proven for SDL2 in `dlls/win32u/Makefile.in` generalizes:
   a Vulkan-based *host* prototype (MoltenVK on macOS, real Vulkan on Linux)
   of the same "upload DIB as texture, composite, present" logic can be
   built and iterated on in the Docker host-sim environment (see Task 5)
   before ever touching the Switch-specific NVK/deko3d calls -- much faster
   iteration loop, same interface contract (`wine_nx_fb_*`).
3. **Only then** port the validated logic to `wine-nx-probe/source/runtime.c`
   against the real Switch GPU API, gated behind the existing `__SWITCH__`
   build, alongside (not replacing) the current libnx-framebuffer path so
   there's a fallback.

## What's actually built vs. planned

- **Built and verified**: the `wine_nx_fb_lock/unlock/present` interface
  boundary is proven stable across two independent presentation backends
  (libnx framebuffer on real hardware, SDL2 in the host-sim) -- meaning a
  future GPU-compositor backend has a known, tested contract to implement
  against, not a hypothetical one.
- **Not built**: any deko3d/NVK/Vulkan code, any GPU texture upload, any
  compositor. Nothing Switch-GPU-specific exists yet. The dependency
  question (does a usable Mesa/NVK port actually exist anywhere for this
  project?) is unresolved and is the real first blocker, ahead of writing
  any code.
