# macOS 26 blocks Wine's loader on this machine

This is not a wine-nx-probe bug and not specific to the host-sim work. It
blocks **any** native (non-Docker) Wine build — Switch-related or not — from
running on this specific machine (macOS 26.2, build 25C56, Apple Silicon).
Recorded here because it cost a long debugging session and would otherwise
get rediscovered from scratch.

## Symptom

A freshly built `loader/wine` (or any binary linked with Wine's macOS
`WINELOADER_LDFLAGS`) hangs forever with **zero output**, even with
`WINEDEBUG=+all`, even for the most trivial console `hello.exe`. No crash,
no error message on stdout/stderr -- the process is killed by the OS before
it prints anything.

The actual signal is in the system log, not the process's own output:

```sh
log show --last 2m --predicate 'eventMessage CONTAINS[c] "Security policy"' --info
```

```
kernel: (AppleSystemPolicy) ASP: Security policy would not allow process: <pid>, <path>
```

## Root cause

`configure.ac` sets, for macOS:

```sh
WINELOADER_LDFLAGS="-Wl,-segalign,0x1000,-pagezero_size,0x1000,-sectcreate,__TEXT,__info_plist,loader/wine_info.plist"
```

`-segalign,0x1000` (4KB) and `-pagezero_size,0x1000` shrink macOS's normal,
much larger `__PAGEZERO` segment/alignment. This is Wine's long-standing
trick for reclaiming low memory addresses to match Windows' memory layout
(see "why this is load-bearing" below).

On this machine's macOS version, that non-standard segment alignment is
**hard-blocked at the kernel/AppleSystemPolicy level**, independent of code
signing, entitlements, Developer Mode, or MDM.

## Clean repro (rules out every other explanation)

Two trivial `hello world` binaries, both built and run with **zero manual
`codesign` calls** (both get the same automatic linker signature):

```sh
$ clang -o plainhello3 plainhello.c
$ codesign -dv plainhello3 2>&1 | grep flags
flags=0x20002(adhoc,linker-signed)
$ ./plainhello3
plain hello                    # works

$ clang -o plainhello_segalign2 plainhello.c -Wl,-segalign,0x1000
$ codesign -dv plainhello_segalign2 2>&1 | grep flags
flags=0x20002(adhoc,linker-signed)     # IDENTICAL signature state
$ ./plainhello_segalign2
                                # hangs, killed after timeout, zero output
```

Same source, same compiler, same automatic `adhoc,linker-signed` signature.
The *only* difference is `-Wl,-segalign,0x1000`. That alone is sufficient to
trigger the block. This rules out:

- **MDM**: `profiles status -type enrollment` → `MDM enrollment: No`.
- **Developer Mode**: was enabled, made no difference, even after a reboot
  (which can be needed for some AMFI-adjacent policy changes -- wasn't it,
  here).
- **Code signing / entitlements**: identical signature both times; also
  tried ad-hoc re-signing and a full hardened-runtime entitlements plist
  (`allow-jit`, `disable-library-validation`, `allow-unsigned-executable-memory`,
  etc.) -- no effect either way.
- **The name "wine"**: renaming the exact binary changed nothing.
- **`posix_spawn` with `POSIX_SPAWN_SETEXEC | _POSIX_SPAWN_DISABLE_ASLR`**
  (also used in Wine's macOS re-exec path): tested in isolation, works fine
  on its own.

## Why this is load-bearing for Wine (can't just drop the flags)

Tested removing `-segalign,0x1000,-pagezero_size,0x1000` from
`WINELOADER_LDFLAGS` (keeping only the unrelated `-sectcreate` for the
embedded Info.plist). This *does* avoid the AppleSystemPolicy block -- but
immediately fails differently:

```
err:virtual:map_fixed_area out of memory for 0x7ffe0000-0x7ffe1000
err:virtual:allocate_virtual_memory out of memory for allocation, base 0x7ffe0000 size 00001000
err:virtual:virtual_alloc_first_teb wine: failed to map the shared user data: c0000017
```

Wine needs to map Windows' `KUSER_SHARED_DATA` at the fixed low address
`0x7ffe0000`. That only works because shrinking macOS's normal huge
`__PAGEZERO`/alignment via `-segalign`/`-pagezero_size` frees up that address
range for Wine to claim. Without it, Wine can't even initialize the first
thread's TEB -- this is one of the earliest, most fundamental steps in
process startup, not an edge case.

So on this OS version there is currently no combination of these two
specific flags that both (a) satisfies AppleSystemPolicy and (b) gives Wine
the low address space it needs. That's a real conflict between Wine's
established macOS loader strategy and a very recent macOS security policy
change, not something to route around from this repo.

## Status

- Reverted the flag-removal experiment; `configure.ac`/`configure` and the
  `wine-nx-probe/build-host-sim` build tree are back to the original
  (correct-but-blocked-here) flags.
- Not something wine-nx-probe can fix. Upstream Wine's macOS loader would
  need a different low-address-reservation strategy for whatever changed in
  this macOS version.
- Workaround for *this project's* purposes: run the host-sim in a Linux
  container instead of natively on this Mac -- Linux doesn't use this
  Mach-O-specific mechanism at all, so the whole problem doesn't apply.
  See the Docker path in `wine-nx-probe/switch-shims/README.md`.
