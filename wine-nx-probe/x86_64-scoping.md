# x86_64 (64-bit) guest path -- scoping

Adding a third, coexisting execution path: unmodified x86_64 Windows
portable apps, alongside native ARM64 and the 32-bit WOW64/box64 path.
Both existing paths stay exactly as they are. Companion to
`wowbox64-scoping.md` (the 32-bit bring-up this builds on); findings below
are source- or build-verified on this machine unless marked otherwise.

## What is already done (verified, committed)

1. **One PE build tree takes all three archs.** `configure
   --enable-archs=aarch64,i386,x86_64` was actually run (scratch tree
   `build-wine-arm64-pe-3arch-test/`, gitignored): `configure.ac`'s arch
   loop accepts any subset of `{i386,x86_64,arm,aarch64,arm64ec}`, and the
   tree comes out with `PE_ARCHS = aarch64 i386 x86_64` and per-arch
   `x86_64-windows/` build dirs for every DLL. No second build tree needed
   -- the production tree (`build-wine-arm64-pe-clean`) just needs the same
   reconfigure plus the same `-D__SWITCH__` Makefile patch it already
   carries (see `Makefile.bak-before-switch-define` there for the exact
   pattern; configure args are recorded in its `config.status`).
2. **The wine-nx-modified DLL sources compile for x86_64-windows.** Built
   real artifacts from the scratch tree with `__SWITCH__` defined: ntdll
   (3.1MB PE32+), kernel32, kernelbase, ucrtbase, user32, gdi32, win32u --
   all clean. The guest-side API surface for a 64-bit guest is therefore
   buildable today; nobody has to port the `#ifdef __SWITCH__` code first.
3. **Detection and routing.** `peek_pe_machine()` already returned the raw
   COFF machine; what was missing was everything downstream accepting it.
   Now: `horizon.c`'s PE validation accepts `0x8664` (PE32+, same
   HDR64-magic branch as ARM64), the client-side supported-machines list
   carries AMD64, and `runtime.c` routes an AMD64 main image around native
   import fixup/attach (same reason the i386 path skips them: the host
   loader must not resolve an x86-64 IAT against aarch64 DLLs or call
   x86-64 DllMains natively) into an explicit, logged
   "x86_64 execution path not implemented yet" stop instead of jumping to
   x86-64 machine code on an aarch64 CPU. `WINE_NX_APP=cube64` stages an
   x86_64 build of the cube32 source to hardware-verify exactly that
   routing. Auto-detection works with no manual toggle, per the design
   goal; a 64-bit portable app is picked up by the same target.txt
   mechanism as everything else.

## The architecture decision that shapes the rest

**Stock `wow64.dll` cannot host a 64-bit guest -- verified in source, not
assumed.** `dlls/wow64/syscall.c`'s `current_machine` only ever takes
I386/ARMNT; every thunk in wow64/wow64win converts 32-bit structs
(`EXCEPTION_RECORD32`, `I386_CONTEXT`, ULONG handles/pointers). The
`enable_wow64=${enable_wow64:-aarch64,x86_64}` default in `configure.ac`
means "build wow64.dll when the *native* arch is aarch64 or x86_64" (both
can host i386 guests), not "aarch64 hosting x86_64 is a wow64 pairing."
Upstream's actual answer for x64-on-ARM64 is the ARM64EC build mode plus
an xtajit64-interface emulator -- already ruled out for this port
(`dlls/xtajit64/cpu.c` is a terminate-process stub, and ARM64EC would be a
whole-tree rebuild into hybrid binaries, a different project).

So the 64-bit path needs a **bespoke, wow64.dll-shaped-but-simpler glue
layer** of wine-nx's own. What makes it *simpler* than the 32-bit case,
not harder:

- **No dual PEB/TEB.** An x86_64 PE guest is LLP64 with 64-bit pointers --
  the same PEB/TEB/process-parameters layouts the aarch64 side already
  uses. `runtime_init_peb_process()`'s `is_machine_64bit()` gate already
  does the right thing: no `wow_peb`, no `build_wow64_parameters()`, the
  guest shares the native structures directly.
- **No struct thunking.** wow64win.dll exists to convert thousands of
  32-bit call frames to 64-bit. For an x86_64 guest, NT structs are
  bit-identical to the native side's; a guest syscall's argument *values*
  can be handed to the native implementations as-is. The only conversion
  is calling convention (Microsoft x64: RCX/RDX/R8/R9 + stack, vs AArch64
  x0-x7) -- a register shuffle, not a thunk library.
- **Box64 runs x86_64 natively.** This is its primary, best-tested mode;
  the 32-bit path needed box64 bent into a non-native mode (the
  `box64_is32bits` / FS-segment workarounds documented throughout the
  vendored `wowbox64.c`). The Horizon-specific work already done --
  dual-alias JIT chunks (`jitCreate` RW/RX), the `os_horizon.c` mmap
  seam, the RX->RW translation fixes -- is arch-independent and carries
  over unchanged.

## What remains (the real work, in dependency order)

1. **A 64-bit CPU-backend DLL** (aarch64-windows PE, linking box64's emu/
   dynarec like `wowbox64.dll` does) exposing the `BTCpu*` surface with
   `CONTEXT`/`AMD64_CONTEXT` layouts: BTCpuProcessInit/ThreadInit/
   Simulate/GetContext/SetContext/ResetToConsistentState, a bop/unix-call
   opcode pair, and the syscall entry that reads the guest's x64-ABI args
   and dispatches into the native syscall table (the piece that replaces
   `Wow64SystemServiceEx` -- for 64-bit it is a register shuffle into the
   same `__wine_syscall_dispatcher`-backed table the native path uses).
   `dlls/wow64cpu/cpu.c` documents the interface contract; `wowbox64.c`
   documents which entry points matter in practice and why.
2. **A `wine_nx_x64_handoff()`** replacing today's routing stub, playing
   the role `wine_nx_wow64_handoff()` plays for i386 but without
   wow64.dll: allocate the guest stack, build the initial AMD64 context
   (Rip at the x86_64 ntdll's `LdrInitializeThunk`, Rsp with headroom --
   every bug class from the 32-bit boot chain in `perf-lab-log.md` Part 2
   has a 64-bit analog waiting), load the x86_64 guest ntdll, and enter
   BTCpuSimulate.
3. **Guest DLL staging.** A third staged directory for the x86_64 DLL set
   plus loader search-path awareness for it. Real Windows-on-ARM never
   ships an x86_64 system directory (ARM64EC makes system32 dual-use), so
   there is no convention to copy; proposal: `drive_c/windows/sysx64`,
   mirroring how `syswow64` is staged today, wired the same way
   `SYSWOW64_DIR` is in `build-switch.sh`.
4. **Hardware bring-up.** Expect the boot-sequence bug chain again
   (initial context, stack headroom, TLS state, NtContinue routing,
   unwind-vs-JIT-frames); the categories are documented from the 32-bit
   campaign even though the specifics will differ.

## Constraint noted for later

The vendored box64 tree carries upstream's `AGENTS.md`: box64 does not
accept AI-generated contributions. That governs *upstreaming* -- none of
the Horizon-side box64 modifications in this repo should be sent upstream
as-is. It does not affect this repo's own development, but is worth
knowing before ever proposing the dual-alias JIT work to box64 proper.

## Out of scope (already decided, do not reopen)

Vulkan (no `nouveau` DRM under Horizon, so NVK is impossible -- not a
maturity gap), Steam client, and installers (.msi/NSIS/InstallShield --
registry/COM dependencies this port doesn't have). Portable-folder apps
only, same as the 32-bit path.
