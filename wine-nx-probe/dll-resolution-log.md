# Portable-app DLL resolution -- investigation log

The detailed trail behind fixing bundled-DLL resolution for portable apps
(the "plug and play" problem). Companion to `perf-lab-log.md`; same rules:
this is the *why*, the README carries only current status.

## Symptom

Luanti (first real test target shipping its own DLLs -- `SDL2.dll`,
`OpenAL32.dll`, `libcurl.dll`, etc. next to `luanti.exe`) failed every one
of its bundled DLL loads with `STATUS_DLL_NOT_FOUND` and self-terminated.
Every Wine-provided system DLL loaded fine. OpenTTD and `cube32` never hit
this because neither bundles any DLLs.

## Evidence chain

1. The DllPath handed to the WOW64 guest was traced live in
   `build_wow64_parameters()` (`dlls/ntdll/unix/env.c`) and confirmed
   correct on hardware:
   `C:\luanti\bin;C:\windows\system32;C:\windows;C:\` -- app dir first.
2. The hardware-observed search for each missing DLL was exactly six
   opens: `C:\windows\syswow64` / `C:\windows` / `C:\` as direct DOS
   paths, then the same three directories again as `\??\unix/sdmc:...`
   raw-unix fallbacks. **No open for `C:\luanti\bin` in either form, ever**
   -- confirmed by the absence of any `[NTOPEN]` line for it (that trace
   fires unconditionally at unix-side `NtCreateFile()` entry, before any
   path handling, so a missing line means the syscall genuinely never
   happened).
3. Both loader search functions (`search_dll_file()` and the wine-nx
   `wine_nx_search_sdmc_dll_file()` fallback, `dlls/ntdll/loader.c`) parse
   the semicolon list independently, and both skipped the app dir -- so
   the string itself, not either parser, was the suspect. The six observed
   opens match `wine_nx_search_sdmc_dll_file()`'s hardcoded system-dir
   *tail* exactly, which only runs the loop-free way it did if the paths
   loop walks **zero** entries -- i.e. the DllPath the guest walks is an
   *empty string*, not a list missing its first entry.

## Root cause

`RtlSetCurrentDirectory_U()` (`dlls/ntdll/path.c`, stock Wine) rewrites
the process parameters' `CurrentDirectory.DosPath.Buffer` **in place**,
appending a trailing backslash plus NUL, with no capacity check -- by
upstream contract, because every upstream params builder
(`build_initial_params()`, `RtlCreateProcessParametersEx()`) reserves
`MAX_PATH` WCHARs for exactly this slot.

wine-nx's own params builder (`runtime_create_process_params()`,
`wine-nx-probe/source/runtime.c`) predates that knowledge and packed every
string tight (`MaximumLength = len + 1`), curdir and DllPath adjacent.
That layout survives two faithful copies (`build_wow64_parameters()`'s
32-bit dup, then the guest's `init_user_process_params()` heap copy), and
then the guest's own init calls `RtlSetCurrentDirectory_U("C:\luanti\bin")`:
canonicalized to `C:\luanti\bin\` -- exactly one WCHAR longer than the
packed slot -- whose terminating NUL lands on `DllPath.Buffer[0]`.

`DllPath.Length` still reads 48 chars; `wcslen()` reads 0. Every
semicolon-list walker sees an empty list and falls through to the
hardcoded system-dir fallback. Any curdir not already ending in `\`
triggers it, so **every 32-bit target ever booted (OpenTTD, cube32) ran
with an empty DLL search path** -- invisible until an app actually needed
a non-system DLL.

The mechanism was reproduced byte-for-byte in a host-side harness
(the four-stage copy chain plus `RtlSetCurrentDirectory_U()`'s store
pattern): pre-fix the walked DllPath is empty with `Length` still
claiming 48 chars; post-fix all four entries walk.

## Fix

`runtime_create_process_params()` now reserves `MAX_PATH` WCHARs of real
capacity for the curdir slot and sets `MaximumLength` accordingly --
restoring the exact invariant `build_initial_params()` keeps upstream
(`dlls/ntdll/unix/env.c:2096`). No change to stock `RtlSetCurrentDirectory_U()`
or the loader search functions: both were behaving correctly all along.

The design principle this preserves: an app's bundled DLLs resolve from
its own directory (DllPath entry 0) only -- nothing is ever copied into
shared `system32`/`syswow64`, so same-named DLLs from different portable
apps can't collide, and deleting an app's folder removes everything it
used.

## Validation

- Host harness: passes (see above).
- Hardware: **pending** -- success is `luanti.exe` getting past
  `STATUS_DLL_NOT_FOUND` on its bundled DLL set (whatever comes after
  that -- e.g. the missing OpenGL driver -- is a separate, known issue),
  plus `cube32` still at 60fps as the no-bundled-DLLs regression check.
