# WowBox64 integration -- scoping

Status: exploratory scoping only, no implementation. This follows up on
`wine-nx-probe/source/jit_smoke.c`, which hardware-confirmed libnx's
`jitCreate()`/`jitTransitionToWritable()`/`jitTransitionToExecutable()`
dual-alias W^X-bypass mechanism works correctly, including under concurrent
read-while-write access from two threads (8/8 single-threaded rounds, then
a 4-second concurrent stress test, both clean on real hardware). That
result answers "does the OS support the memory primitive a dynarec needs"
with a confirmed yes. This document is the next question: what would it
actually take to wire Box64's WowBox64 build into this port.

**Headline finding, stated up front because it changes the shape of the
whole plan**: this is not "expose an existing WoW64 seam and drop in a
replacement CPU-backend DLL." Verified directly against this project's own
Wine source tree and build output (not assumed): **wine-nx currently
builds zero WoW64 infrastructure of any kind.** `dlls/wow64cpu`'s build
directory has no Makefile at all; `dlls/wow64` and `dlls/wow64win` have
Makefiles but empty output directories (nothing has ever been compiled
into them). The root cause is `--enable-archs=aarch64` -- a single
architecture -- in the configure invocation this project's own
`build-switch.sh`/README already use for the Wine PE rebuild path. Real
Wine's WoW64 machinery only gets built at all when a second (guest)
architecture is configured alongside the native one.

So all three tasks below sit on top of a bigger, previously-unstated
prerequisite: **reconfiguring Wine's own build to a genuine two-architecture
mode** (`--enable-archs=aarch64,i386`), which this project has never done.
That's real, bounded, well-precedented work (Wine supports it upstream,
verified in `configure.ac` directly), but it's a foundational step, not
a footnote to task 3.

## 1. Exposing the WoW64 CPU backend seam

**The seam exists in source, unmodified, but is dead code today because
nothing ever exercises it.** Traced the actual control flow in
`dlls/ntdll/loader.c`, not guessed:

- `dlls/ntdll/loader.c` has two versions of `init_wow64()`, selected by
  `#ifdef _WIN64` (line ~6121 for the `_WIN64` branch, ~6206 for the
  `#else` branch):
  - **`_WIN64` branch** (line ~6141-6173) -- this is what wine-nx's own
    ARM64 `ntdll.dll` actually compiles today, since this port's ntdll
    build is native 64-bit. It loads `C:\windows\system32\wow64.dll`,
    resolves `Wow64LdrpInitialize`/`Wow64PrepareForException`/
    `Wow64SuspendLocalThread` from it via `RtlFindExportedRoutineByName`,
    and calls into it. This code is present and compiled into wine-nx's
    ntdll right now -- it's just never invoked, because nothing currently
    creates a WoW64 process (there's no 32-bit target to trigger it, and
    `wow64.dll` itself is never actually built -- see above).
  - **`#else` branch** (`map_wow64cpu()`, line ~6179, and its
    `init_wow64()`, line ~6206) -- this is the 32-bit-ntdll-side code that
    opens `\??\C:\windows\sysnative\wow64cpu.dll`, maps it, and resolves
    the `Wow64Transition` trampoline from it. This is what would run
    inside a *32-bit* ntdll.dll -- which wine-nx does not build at all
    today (see the multi-arch prerequisite above). This is the literal
    code path that, on real Wine, ends up loading whatever CPU-backend DLL
    is registered -- `wow64cpu.dll` normally, `wowbox64.dll` in this
    project's case.
- `dlls/wow64cpu/wow64cpu.spec` already declares the exact `BTCpu*` export
  surface a CPU backend needs (`BTCpuProcessInit`, `BTCpuSimulate`,
  `BTCpuGetContext`, `BTCpuSetContext`, `BTCpuResetToConsistentState`,
  `BTCpuTurboThunkControl`, `BTCpuGetBopCode`,
  `BTCpuIsProcessorFeaturePresent`, `__wine_get_unix_opcode`) -- this
  matches what the earlier Box64 research confirmed `wine/wow64/
  wowbox64.c` actually implements. The ABI contract on the Wine side is
  already correct and doesn't need inventing; it needs *building*.
- `dlls/wow64/syscall.c` is the native-side (64-bit) dispatcher that
  receives simulated syscalls from the CPU backend and forwards them into
  real (native) ntdll -- this is the file that would actually route a
  32-bit guest app's NT calls back into wine-nx's own `horizon.c`-backed
  syscall implementations once the rest of this works.

**What's actually required, concretely:**

1. Reconfigure the Wine PE rebuild (`wine-nx-probe/build-switch.sh`'s
   `configure` invocation, currently `--enable-archs=aarch64`) to
   `--enable-archs=aarch64,i386`, so Wine's own build system generates
   real Makefiles for `wow64`, `wow64win`, and `wow64cpu`, and produces a
   genuine 32-bit `ntdll.dll`/`kernel32.dll`/etc. DLL set -- not just a
   config flag flip; this means the PE build tree now produces *two*
   parallel DLL sets (64-bit and 32-bit) instead of one, which touches how
   `wine-nx-probe/build-switch.sh` stages `system32` today (see task 3).
2. Confirm `wow64.dll` actually builds and does the right thing once
   multi-arch is on -- this project has never linked or run it, so
   "builds cleanly" needs to be verified, not assumed, the same way this
   session verified the JIT primitive before trusting it.
3. Substitute `wowbox64.dll` for `wow64cpu.dll` at the path
   `map_wow64cpu()` opens (`C:\windows\sysnative\wow64cpu.dll`) -- either
   by staging Box64's output under that exact name, or by patching the
   path/lookup in `loader.c` if a different name is preferred. The
   simpler option (stage it as `wow64cpu.dll`) needs no ntdll patch at
   all.
4. **Not yet scoped, flagged as a real open question**: wine-nx's process/
   thread bookkeeping. WoW64 processes have a *dual* TEB/PEB (a 64-bit one
   and a 32-bit one, linked together) -- grepped `dlls/ntdll/unix/
   horizon.c` (this project's from-scratch wineserver reimplementation,
   ~9000 lines) for any WoW64/TEB64/PEB64 awareness: **zero hits.**
   Enabling WoW64 isn't just "load a different CPU backend DLL" -- it's
   asking a wineserver substrate that has never modeled a dual-architecture
   process to now do so. This is very likely the largest single piece of
   work in the whole plan, and it's currently completely unscoped; a real
   next pass should audit exactly which `horizon.c` window/process/thread
   paths assume "one TEB per thread, one architecture per process" before
   any of the above is attempted.
5. **Also not yet scoped**: the `system32`/`syswow64`/`sysnative` path
   redirection real WoW64 depends on (`map_wow64cpu()`'s own path,
   `\??\C:\windows\sysnative\wow64cpu.dll`, already shows this -- a 32-bit
   process needs to see `sysnative` as an alias back to the real
   `system32`). wine-nx's current SD-card staging
   (`sdmc:/switch/wine/drive_c/windows/system32`) is flat and
   single-architecture; this redirection scheme doesn't exist yet.

## 2. Wiring the verified JIT allocator into Box64

Researched against Box64's real source tree (`github.com/ptitSeb/box64`
`main`, commit `c5215cb`) -- there's no code in this repo to point at yet
since Box64 itself isn't vendored here; file:line references below are
against that upstream checkout.

**The allocation seam itself is clean.** A single entry point,
`AllocDynarecMap()` (`src/custommem.c:1734`), manages a two-level
allocator: a list of large *chunks*, each carved into sub-blocks by an
intrusive-freelist allocator (`getFirstBlock`/`allocBlock`). When no chunk
has room, it maps a new one via `InternalMmap` (`custommem.c:1801`), not
a raw `mmap` call -- the platform is selected purely by *which `.c` file
provides `InternalMmap` at link time*: `src/custommmap.c` for Linux,
`src/os/os_wine.c` for the WowBox64 build. Confirmed directly against
`wine/wow64/CMakeLists.txt`: the WowBox64 target compiles `custommem.c`
and links `os_wine.c`'s `InternalMmap`; `custommmap.c` is not in that
build's source list. So the hook point already exists and is exactly one
function pair (`InternalMmap`/`InternalMunmap`) -- no new abstraction
needs inventing on the Box64 side.

**But there's a real architectural mismatch, not just a platform-shim
gap.** Box64 maps each chunk **permanently RWX in one shot** and never
toggles it -- new translated code is written into the live RWX arena
*while other blocks in the same arena are executing*. That's the design;
there is no writable/executable transition step on Linux to retarget,
because Linux gives it single-address RWX and it just... keeps it. Horizon
can't offer that (strict W^X, no single-address RWX ever), which forces a
genuine **dual-alias** design -- and that breaks an assumption baked
deeper into the dynarec than just the allocator: Box64 uses **one pointer**
as the emit cursor, the executed entry, the jump-table target, *and* the
address of co-located `dynablock_t` metadata that must stay writable at
runtime for its own atomics (`tick`/`in_used`). Splitting write-alias from
exec-alias means touching:

1. `custommem.c` -- `AllocDynarecMap`/`FreeDynarecMap`/`DelMmaplist` and
   the chunk-tracking structs, to carry a per-chunk RW/RX pointer pair
   instead of one pointer.
2. A new `os_horizon.c` (a peer of `os_wine.c`) providing `InternalMmap`/
   `InternalMunmap` built on `jitCreate`/`jitGetRwAddr`/`jitGetRxAddr`.
3. `dynarec_native.c` (`FillBlock64`, `CreateEmptyBlock`) -- drive all
   *writes* off the RW alias while storing the RX alias everywhere an
   absolute native address needs to be *executed from or jumped to*
   (`block->block`, `jmpnext`, jump-table entries, inter-block links).
4. Metadata-lookup paths that receive a runtime (RX) address and need to
   reach the RW-alias-side bookkeeping (`FindDynablockFromNativeAddress`,
   signal handling in `libtools/signals.c`).

**One genuine piece of good news**: this project's own JIT-primitive
finding from the earlier hardware test directly confirmed a specific
worry here. Box64's chunks are **2MB each** (`DYNMMAPSZ`), sub-carved
across many independently-live blocks -- meaning a naive
`jitTransitionToWritable`/`ToExecutable` cycle on a single-alias `Jit`
object would apply to a whole 2MB chunk full of *other* blocks currently
executing on another thread. That's exactly the whole-arena conflict this
project's own phase-2 concurrent JIT test was built to catch. The fix
isn't "transition more carefully" -- it's **one dual-alias `Jit` per 2MB
chunk, mapped permanently, never toggled**, which sidesteps the conflict
entirely because it matches what Box64 already assumes (permanently-live,
write-while-execute memory) rather than fighting it.

**Cache invalidation is already handled, just needs retargeting.** Box64
doesn't rely on `mmap(PROT_EXEC)` implicit coherency -- it has its own
explicit `ClearCache()` (`dynarec_native.c:349`) that hand-rolls the exact
`dc cvau`/`dsb ish`/`ic ivau`/`dsb ish`/`isb` sequence libnx's
`jitTransitionToExecutable()` already does internally. No new flush needs
adding; because ARM64 D-caches are physically-tagged (PIPT), flushing via
the RX alias should hit the same physical lines the RW-alias write
dirtied. That specific hardware-behavior claim is the one thing worth
confirming empirically (this project's own established discipline: verify
on real hardware, don't just trust the reasoning) rather than trusting
source analysis alone, given the JIT smoke test infrastructure to do
exactly that already exists.

**Verdict for this task specifically**: not a one-file, handful-of-functions
change. The allocation hook is trivial; the dual-alias requirement it
forces is a focused-but-multi-site patch across `custommem.c`, a new
`os_horizon.c`, and `dynarec_native.c`, plus a couple of metadata-lookup
touch-ups. Mitigating factor: Box64's intra-block code is already
PC-relative (offsets from block start, not absolute addresses), so only
the points where an address *escapes* a block -- jump tables, `jmpnext`,
inter-block links -- need the RX/RW distinction threaded through, which
bounds the actual churn well short of "rewrite the dynarec."

## 3. The PE cross-compilation toolchain

**Better news than expected: no new toolchain fetch needed.** Checked the
actual toolchain directory this project already downloads and pins
(`wine-nx-probe/toolchains/llvm-mingw-*`, per the README's own restore
instructions), not assumed:

- It's a full llvm-mingw distribution, not an aarch64-only trim. It
  already contains `i686-w64-mingw32-*`, `x86_64-w64-mingw32-*`,
  `armv7-w64-mingw32-*`, `aarch64-w64-mingw32-*`, **and**
  `arm64ec-w64-mingw32-*` target wrappers (clang's unified-driver naming,
  not separate per-arch GCC binaries -- an earlier, narrower check of this
  toolchain missed these by grepping for a GCC-style naming convention
  that doesn't apply here).
- `arm64ec-w64-mingw32` existing is notable beyond this task: the earlier
  Box64 architecture research found Box64's own ARM64EC support (needed
  for 64-bit x86 guests, not just today's 32-bit-only WowBox64) isn't
  upstream yet. The *toolchain* side of that future work is already
  covered by what's already pinned here; only Box64's own code is the
  blocker there, not this project's build environment.
- wine-nx's existing PE rebuild path already knows how to drive this exact
  toolchain (`LLVM_MINGW_DIR`/`WINE_NX_PROGRAM_BUILD_DIR` env vars,
  `--with-mingw=llvm-mingw` in the configure invocation the README
  documents) -- building `wowbox64.dll` with `aarch64-w64-mingw32-gcc`
  from this same directory is a natural extension of a path that already
  works, not a new integration.

**What's actually required, concretely:**

1. Box64's own build system (`wine/wow64/CMakeLists.txt`, per the earlier
   Box64 research) needs to be pointed at this project's pinned
   `aarch64-w64-mingw32` toolchain rather than a system-installed mingw --
   likely a `CMAKE_C_COMPILER`/toolchain-file override at Box64
   `cmake -DWOW64=ON` invocation time, analogous to how
   `cmake/switch-devkitA64.cmake` already does this for the devkitPro
   side of this project. Not yet verified against Box64's actual
   `CMakeLists.txt` internals -- reasonable next step once the allocator
   research (task 2) is back and Box64's build system is being read
   anyway.
2. This is a **third, independent build environment** alongside the two
   this project already juggles (devkitPro/Docker for the `.nro` runtime,
   host llvm-mingw for the aarch64 Wine PE rebuild) -- it doesn't share
   CMake cache or output directories with either, so keeping it from
   polluting the devkitPro environment is mostly "don't add it to the
   same CMakeLists.txt or build directory," which is already this
   project's existing convention (the Docker-based `.nro` build and the
   host-side PE rebuild are already fully separate invocations,
   `build-switch.sh` vs. the manual `configure`/`make` steps the README
   documents). A third `build-wowbox64/` sibling directory, driven by its
   own script, follows the same pattern.
3. Once Wine's own build goes multi-arch (task 1, item 1), the *guest*
   32-bit Windows DLL set (`ntdll.dll`, `kernel32.dll`, `user32.dll`,
   etc. -- real x86 PE binaries, not just the CPU-backend DLL) also needs
   to be cross-compiled for `i686-w64-mingw32` and staged into a
   `syswow64`-equivalent directory. This toolchain has that target too,
   but it's additional build-system work beyond just compiling
   `wowbox64.dll` itself -- the CPU backend alone doesn't provide a 32-bit
   Windows API surface, only instruction translation.

## Summary verdict

All three tasks turned out different in scope than framed, in both
directions:

- **Task 3 (toolchain) is smaller than framed.** The compiler is already
  pinned and working; the hard part is driving Box64's own CMake with it,
  a contained, well-precedented piece of work.
- **Task 1 (the Wine-side seam) is larger than framed.** The CPU-backend
  ABI is already correct in Wine's source and needs no invention, but it
  currently sits on top of a multi-architecture process model wine-nx has
  never built, and `horizon.c`'s own WoW64-awareness is at zero.
- **Task 2 (the allocator patch) is more surgical than "point mmap at
  jitCreate," but bounded.** The hook point is one clean function pair;
  the dual-alias requirement it forces touches three files in a
  well-understood, PC-relative-code-shaped way, not an open-ended rewrite.

Realistic order of operations, given all three findings together:

1. Get Wine's own build to multi-arch (`--enable-archs=aarch64,i386`) and
   confirm `wow64.dll` builds and loads at all -- a real, standalone,
   hardware-testable milestone, entirely before Box64 enters the picture.
2. Audit and extend `horizon.c` for dual-TEB/PEB process state -- almost
   certainly the single largest unknown in this whole plan, and currently
   the least scoped.
3. Prototype the Horizon `os_horizon.c` + dual-alias `custommem.c`/
   `dynarec_native.c` patch against Box64 standalone (buildable and
   testable independent of wine-nx's own multi-arch work, since it only
   needs libnx's JIT primitive, already hardware-verified) -- this can
   happen in parallel with (1)/(2) rather than waiting on them.
4. Only once (1) and (2) land does substituting `wowbox64.dll` for the
   CPU backend at `map_wow64cpu()`'s lookup path become the last
   integration step rather than the first.

## Phase 1 execution: step (1), attempted and partially verified on this host

Item (1) above -- "get Wine's own build to multi-arch and confirm
`wow64.dll` builds and loads at all" -- was actually run, not just
reasoned about further, since this project's Wine PE toolchain runs
directly on this host machine (no Docker needed for this part).

**Build system: done, host-verified.** Reconfigured a scratch build tree
(`wine-nx-probe/build-wine-arm64-pe-wow64-test/`, gitignored, disposable)
with `--enable-archs=aarch64,i386` in place of `aarch64` alone -- the user
request's original wording (`i686`) was corrected to `i386`, the only
value `configure.ac` actually accepts (`{i386,x86_64,arm,aarch64}`).
Configure failed once on unrelated host tooling (macOS's stock bison 2.3;
Wine's grammar files need 3.0+), fixed with `brew install bison` ahead on
`PATH`. With that, configure completed cleanly (`configure: Finished. Do
'make' to compile Wine.`), and:

- `make -C dlls/wow64` produced a real `dlls/wow64/aarch64-windows/
  wow64.dll` -- 1.3MB, confirmed via `file`: `PE32+ executable (DLL)
  (console) Aarch64, for MS Windows`.
- `make -C dlls/wow64win` likewise produced a real `wow64win.dll`
  (1.4MB), same target.
- `dlls/wow64cpu` still produces no build output -- traced this precisely
  in `configure.ac` (line 2451): `enable_wow64cpu=${enable_wow64cpu:-
  x86_64}`, hardcoded regardless of what guest architectures are
  enabled. This isn't a gap multi-arch was supposed to close -- it's Wine
  correctly recognizing that an aarch64-host/i386-guest pairing needs an
  emulator-supplied CPU backend (WowBox64, in this project's case), not
  its own native-CPU-only `wow64cpu.dll`. README updated with the
  corrected, host-verified configure invocation and this same context.

**Not yet done**: actually loading `wow64.dll` inside a running wine-nx
process (compiling doesn't prove it initializes correctly against this
project's own `horizon.c` substrate) -- that depends on item (2) below
existing first, since `wow64.dll`'s own `Wow64LdrpInitialize` path
assumes a process that already has WoW64 state to initialize.

## Phase 1 execution: step (2), audited further -- deliberately not patched yet

The request asked for `horizon.c` patches implementing dual-TEB/PEB
tracking. Audited real Wine's own wineserver source (`server/process.h`,
`server/process.c`, `server/thread.c`, `server/thread.h` -- vendored in
this repo even though `horizon.c` replaces it at runtime, so it's readable
as ground truth) before writing anything, and found the shape of the real
problem is different from -- not simply larger than -- what "dual-TEB
patches" suggests:

**Good news: the wineserver side doesn't track two full structures.**
`struct process` has one `peb` field, `struct thread` has one `teb`
field -- not `peb32`/`peb64` pairs. WoW64-ness is a single per-process
`machine` field (`process->machine`), and `is_wow64_process()` is just
`native_machine is 64-bit && process->machine isn't`. The genuine
dual-tracking that exists is at the *thread CPU context* level
(`CTX_NATIVE`/`CTX_WOW` -- a WoW64 thread has two register-state
snapshots server-side, not two TEBs). The real "two TEBs in memory" split
is handled entirely client-side, inside the process's own address space,
by `dlls/ntdll/loader.c`'s existing (currently-dead, per task 1 above)
WoW64 init code -- not something `horizon.c` needs to replicate at all.

**The actual blocker: `process->machine` has no wine-nx-shaped place to
come from.** Traced exactly where real Wine sets it --
`server/process.c:1352`, `process->machine = req->machine`, inside the
handler for a *process creation* request: a parent process asking the
server to spawn a child running the target executable, with the target's
architecture passed in at that moment. wine-nx has no equivalent of this
flow anywhere. It doesn't fork or exec -- `wine-nx-runtime.nro` loads the
target PE (`gui_smoke.exe`, `notepad.exe`, ...) directly into its own
single process (confirmed throughout this entire project's history: one
`.nro`, one loaded PE, no `NtCreateUserProcess`, no child process). The
moment where real Wine's architecture gets decided -- at child-process-
creation time -- simply doesn't exist in this port's control flow.

This is why no patch is included with this pass. Writing `process->
machine`-equivalent tracking into `horizon.c` without first finding (or
building) wine-nx's own equivalent decision point -- where in `runtime.c`
or `horizon.c`'s existing PE-loading path the target's architecture could
actually be known and threaded through -- would mean guessing at the
integration shape for the single highest-blast-radius file in this whole
project, with no way to test the guess before it ships. That's exactly
the kind of thing this session's own track record (three separate
mid-flight corrections tonight alone, each caught by checking real
behavior before trusting reasoning) argues against doing blind.

**Concrete next step, not yet started**: audit `wine-nx-probe/source/
runtime.c`'s and `horizon.c`'s existing PE-loading/process-init call
chain (the code that currently loads a single target PE into the
process, e.g. wherever it reads the PE header's `Machine` field today for
its own existing aarch64-only purposes) to find where a 32-bit-target
detection could naturally hook in, *before* attempting to add
`machine`-tag tracking to `horizon.c`'s window/process/thread structs.
That audit is scoped but not yet done.

## Follow-up audit: the actual hook point, traced precisely

Done as a direct follow-up to the above. Two findings, both grounded in
exact line numbers, not inferred:

**`runtime.c` already reads the target's `Machine` field -- but hard-
rejects anything that isn't 64-bit PE before it gets there.**
`runtime_describe_image()` (`runtime.c:1769`) reads `nt->FileHeader.Machine`
into `main_image_info.Machine` (`SECTION_IMAGE_INFORMATION`, a real NT
structure -- declared in `runtime_platform.c:241`, not invented for this
port) at line 1794. But it's gated by a hard check three lines earlier
(`:1774-1775`): `nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC`
fails and returns 0 for anything that isn't PE32+. Worse, the caller
(`runtime_nt_headers()`, `:1430`) unconditionally casts the mapped image
to `IMAGE_NT_HEADERS64*` regardless of actual bitness -- a real 32-bit PE
has a differently-laid-out `IMAGE_NT_HEADERS32`, so this code can't even
correctly *read* a 32-bit header today, let alone accept one.

**The actual blocker is sequencing, not missing plumbing.** Traced
`main()`'s real call order:
- `runtime.c:1861` -- target filename selected (a string; nothing opened).
- `runtime.c:1896` -- `server_init_process()`, which is what sends
  `HORIZON_REQ_INIT_FIRST_THREAD` to `horizon.c`.
- `runtime.c:1904` -- `map_pe_image()` opens and maps the target **for
  the first time**.
- `runtime.c:1911` -- `runtime_describe_image()` finally reads
  `main_image_info.Machine` from the now-mapped image.

`horizon_server_handle_init_first_thread()` (`dlls/ntdll/unix/
horizon.c:3873`) is the server-side handler for that first request, and
it's the mirror image of what got looked for: not a place the *client*
tells the *server* the target's architecture, but the reverse -- the
server tells the client what machine(s) it supports, via a hardcoded
`unsigned short machine = HORIZON_IMAGE_FILE_MACHINE_ARM64;`
(`horizon.c:3878`), sent back in the reply. Real Wine's equivalent
protocol carries a *list* of supported machines (native plus whatever
WoW64 guests are available); this port's reimplementation collapsed that
down to one hardcoded value, which is exactly correct for a single-arch
port and exactly what would need to become conditional for WowBox64.

**Putting the two together: by the time the handshake that would carry a
machine tag fires (`server_init_process()`, line 1896), the target's
architecture genuinely isn't known yet** -- the file that would reveal it
doesn't get opened until line 1904, eight lines later. This isn't a case
of the plumbing being missing; the information doesn't exist yet at that
point in the sequence. Real Wine doesn't hit this problem because a
*parent* process (running `NtCreateUserProcess`) already knows the
target's architecture before the child's wineserver connection is even
established -- a process model wine-nx doesn't have (see above).

**The fix this points to is small and mechanical, not a redesign**: a
lightweight pre-check -- reading just the target file's DOS header
(`e_lfanew`) plus the COFF `FileHeader.Machine` field directly off disk,
maybe ~24-30 bytes, no `NtCreateSection`/virtual-memory machinery needed
-- moved to before `server_init_process()` at line 1896, so the
architecture is known in time to extend `horizon_init_first_thread_request`
(currently `unix_pid`/`unix_tid`/`debug_level`/`reply_fd`/`wait_fd`, no
machine field) with it. That closes the loop this audit was scoped to
find: `runtime.c` would gain one new, small, self-contained function; the
`HORIZON_REQ_INIT_FIRST_THREAD` request/reply structs and handler in
`horizon.c` would gain one field each to carry and honor it instead of
the hardcoded constant. Still not attempted -- this audit's job was
finding the hook point, not writing the patch -- but the shape of the
patch is now concrete rather than open-ended.
