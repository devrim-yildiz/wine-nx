# Wine-NX — Pause-Handover (Stand: 2026-08-21)

Snapshot-Dokument für eine Projektpause. Ziel: eine neue Session (KI oder
Mensch) kann ohne Rückfragen an exakt der Stelle weiterarbeiten, an der die
Arbeit am 2026-07-28 unterbrochen wurde. Alle Aussagen unten sind entweder
aus dem committeten Git-Verlauf, aus den bestehenden In-Repo-Scoping-Docs
(die selbst quellcode-verifiziert sind) oder aus direkter Inspektion des
Working-Trees zum Zeitpunkt dieses Dokuments abgeleitet — nicht geraten.
Wo eine Aussage aus einem älteren Dokument stammt und seither überholt
wurde, ist das explizit vermerkt.

**Kritischster Punkt zuerst:** Ein sehr großer Teil der zuletzt geleisteten
Arbeit (komplette Vulkan-Integration, komplette x64native/Box64-Kernanbindung,
der gesamte neue GUI-Launcher) liegt **uncommitted im Working Tree** — siehe
Abschnitt 7. Vor einer längeren Pause sollte das committed werden, sonst
riskiert ein `git checkout`/`git clean`/Rechner-Wechsel echten Datenverlust.

---

## 1. Architektur-Status

Drei koexistierende Ausführungspfade in einem Prozessmodell ohne
`NtCreateUserProcess`: **eine** `.nro` lädt **ein** Ziel-PE-Image direkt in
ihren eigenen Prozess (kein fork/exec, kein Kind-Prozess — das ist an
mehreren Stellen architektur-bestimmend, siehe unten).

### PE-Loader
- Einstieg: `wine-nx-probe/source/runtime.c` — `runtime_nt_headers()`
  (liest/castet NT-Header), `runtime_describe_image()` (liest
  `FileHeader.Machine`, füllt `SECTION_IMAGE_INFORMATION`), `map_pe_image()`
  (mapped die Ziel-Datei erstmals). Aufrufreihenfolge in `main()`: Zielname
  wählen → `server_init_process()` (Horizon-Handshake) → `map_pe_image()` →
  `runtime_describe_image()`.
- Bug/Limitation (noch offen): Die Architektur des Ziels ist beim
  Horizon-Handshake (`server_init_process()`) noch nicht bekannt, weil die
  Datei erst danach gemappt wird — reine Sequenzierungsfrage, kein
  fehlendes Plumbing. Fix ist umrissen, nicht implementiert (Abschnitt 5).
- Restlicher Loader: Stock-Wine `dlls/ntdll/loader.c`, per `#ifdef
  __SWITCH__` erweitert.

### IAT-Handling / Relocations
- **Nativer ARM64-Pfad**: normaler Stock-Wine-Importfixup, unverändert.
- **WOW64 (32-bit) und x86_64-Gast-Pfad**: Importfixup/DllMain-Aufrufe
  werden für das Gast-Image explizit **umgangen** — der native (aarch64)
  Loader darf ein x86/x86-64-IAT nicht gegen aarch64-DLLs auflösen oder
  fremde DllMains nativ aufrufen. Stattdessen: expliziter, geloggter
  Handoff an `wine_nx_wow64_handoff()` bzw. `wine_nx_x64_handoff()`
  (`dlls/ntdll/loader.c`), die den Gast-Kontext selbst aufbauen und in
  `LdrInitializeThunk` des *Gast*-ntdll springen — der Gast fixt sein
  eigenes IAT dann selbst.
- DLL-Suchpfad-Bug (gefixt, siehe Abschnitt 3/Historie): Der
  Prozessparameter-Builder in `runtime.c` reservierte zu wenig Kapazität
  für `CurrentDirectory.DosPath.Buffer`; `RtlSetCurrentDirectory_U()`
  schrieb dadurch eine NUL über `DllPath.Buffer[0]`, wodurch **jede**
  32-bit-App mit leerem DLL-Suchpfad lief (unsichtbar, bis eine App eigene
  DLLs mitbrachte — Luanti war der erste Fall). Fix:
  `runtime_create_process_params()` reserviert jetzt `MAX_PATH` WCHARs,
  exakt wie Stock-Wines `build_initial_params()`.

### Horizon-OS Memory-Mapping
- `dlls/ntdll/unix/virtual.c`: `get_horizon_page_vprot()` und Verwandte
  implementieren Horizons striktes W^X-Modell (keine einzelne RWX-Mappage
  je erlaubt).
- Dual-Alias-JIT für den 32-bit-Dynarec: `jitCreate()` /
  `jitTransitionToWritable()` / `jitTransitionToExecutable()` (libnx) —
  hardware-verifiziert inkl. konkurrierendem Read-while-Write aus zwei
  Threads (`wine-nx-probe/source/jit_smoke.c`).
- WOW64-Unixlib-Dispatch: `wine_nx_lookup_static_unix_funcs_wow()`
  (`virtual.c`) läuft die **separate** 32-bit-PEB/LDR-Modulliste
  (`wow_peb`, `PEB_LDR_DATA32`/`LDR_DATA_TABLE_ENTRY32`, alle Felder
  `ULONG`) ab, komplett handgeschrieben, kein Stock-Wine-Vorbild dafür
  vorhanden.

### Syscall-Hooks
- `wine_nx_do_syscall()` (`dlls/ntdll/unix/signal_arm64.c`) ist der
  zentrale Dispatch-Punkt für praktisch jeden PE→nativ-Syscall.
- `__wine_syscall_dispatcher` (ARM64-Assembler-Trampolin) überbrückt vom
  PE-ABI-Aufrufkontext in den nativen AAPCS64-Kontext — für den nativen
  Pfad selbst nicht nötig, nur für den Cross-ABI-Sprung.
- `dlls/ntdll/unix/horizon.c` (~9000 Zeilen) ist dieses Projekts
  **selbstgeschriebener Wineserver-Ersatz**, in-process statt als
  separater Prozess. Transport: `pthread_mutex` + `pthread_cond_wait` auf
  einem Ringpuffer. **Harte, gemessene Untergrenze von ~14-15ms pro
  IPC-Roundtrip**, unabhängig vom Request-Typ — kein Bug, sondern echte
  Horizon-Thread-Wake-Latenz; einziger Hebel ist Batching/Caching, nicht
  Beseitigung.
- x86_64-Gast: `EmuX64Syscall()` in
  `wine-nx-probe/third_party/box64/src/os/os_horizon.c` (unter
  `WINE_NX_X64NATIVE`) ist Box64s Hook für die reale x86-64
  `SYSCALL`-Instruktion, liest Args nach echter Windows-x64-Nt*-Konvention
  (Arg1 in R10, nicht RCX — `syscall` zerstört RCX mit der Rücksprung­
  adresse) und ruft `wine_nx_do_syscall()` direkt.

### Exception Dispatcher
- `signal_set_full_context()`/`NtContinue` sind implementiert (waren zu
  Beginn des WOW64-Bring-ups ein No-Op-Stub, `STATUS_NOT_IMPLEMENTED` —
  gefixt).
- Stock-Wines `longjmp` läuft normalerweise über `RtlUnwind()`, das
  `.pdata`/`.xdata`-Metadaten für jeden gequerten nativen Frame braucht —
  Box64s eigene JIT/Dispatch-Frames haben keine. Deshalb: als direkte
  Register-Restaurierung reimplementiert, passend zu `_setjmpex`s
  Save-Sequenz.
- `KeUserModeCallback`-Grenze (native → Gast, z.B. WM_PAINT-Dispatch) ist
  ein **direkter Funktionsaufruf im selben Adressraum** — es gibt kein
  echtes WOW64-Prozess/Adressraum-Splitting und keine `wow64win.dll`-Schicht,
  die Structs zwischen 32-bit-Gast und 64-bit-nativem win32u übersetzen
  würde. Jeder Struct, der roh über diese Grenze geht, wird vom Gast mit
  seinen eigenen (32-bit) Feld-Offsets gelesen, obwohl nativ mit
  (64-bit) Offsets geschrieben wurde — siehe Abschnitt 3, das ist eine
  systemische Bugklasse, keine Einzelinstanz.

### Der neue dritte Pfad: x86_64 nativ (WIP, siehe Abschnitt 2/6/7)
Kein separates PE-DLL/`BTCpu*`-Plugin nötig (ursprünglicher Plan verworfen,
siehe `wine-nx-probe/x86_64-scoping.md`) — `wow64.dll` kann 64-bit-Gäste
grundsätzlich nicht hosten (`current_machine` in
`dlls/wow64/syscall.c` kennt nur I386/ARMNT), also gibt es keinen
`wow64.dll`-Loader, der ein PE-Modul verlangen würde. Stattdessen: Box64s
x86-64-Core wird **direkt nativ ins `wine-nx-runtime`-ELF gelinkt**
(gleiche Linkeinheit wie `wine_nx_do_syscall`), erreichbar über
`wine_nx_x64_handoff()` (schwaches Symbol, siehe unten).

---

## 2. Aktueller Funktionsstand

### Stabil / hardware-verifiziert
| Pfad | Status | Quelle |
|---|---|---|
| Nativer ARM64 (Notepad) | bootet, zeigt Fenster/Menü/Text, ~8fps GDI | README, perf-lab-log.md |
| WOW64 32-bit (OpenTTD, cube32) | bootet, rendert, **60fps** (von 1fps) | perf-lab-log.md Teil 2 |
| deko3d GPU-Compositor (isoliert) | Device/Queue/Swapchain/Present, Textur-Upload, geshadete 3D-Geometrie, 60-62fps | 3d-accel-scoping.md, `wine-nx-deko3d-smoke.nro` |
| JIT-Dual-Alias-Primitive | 8/8 Single-Thread-Runden + 4s Concurrent-Stress, sauber | `jit_smoke.c` |
| Vulkan/NVK, 32-bit-WOW64-Gast | Instance→Surface→Device→Swapchain→**300 präsentierte Frames**, über echte Wine-Vulkan-Treiberkette | project_vulkan_integration-Memory, **komplett uncommitted** |
| Vulkan-Displaytreiber (nativ) | `dlls/win32u/winnx_vulkan.c` — laut eigenem Header-Kommentar "hardware-proven end to end" via `nvk_smoke.c` | Header-Kommentar der Datei, **uncommitted** |

### Grenzfälle / teilweise funktionsfähig
- **Luanti** (32-bit, bringt eigene DLLs mit): lädt inzwischen alle ~35
  DLLs, NLS, Fonts, Fensterklasse — bricht dann mit sauberem
  `NtTerminateProcess(exit_code=3)` ab, Ursache ungeklärt (Abschnitt 3).
- **WOW64-Textrendering**: in manchen Kontexten inkonsistent (Dialog-Body
  blieb leer, Titel/ein Button-Label nicht) — nicht root-caused.

### x86_64-nativer Pfad — Wichtig: Diskrepanz zwischen Doku und Code
`wine-nx-probe/x86_64-scoping.md` (Dateistand 2026-07-23) beschreibt
`EmuX64Syscall()` als "syntax-verified, nicht gelinkt" und den nativen
Build-Target für Box64s Core als "nächster konkreter Schritt". **Der
tatsächliche Code-Stand (2026-07-28, 5 Tage neuer) ist deutlich weiter**
und die Doku wurde nicht nachgezogen:
- `wine-nx-probe/CMakeLists.txt` enthält jetzt (~470 Zeilen, ab Zeile
  ~1323) einen vollständigen `x64native`-Build: 4 Dynarec-Pass-Objektbibliotheken,
  einen Interpreter, `x64native-core` (`x64native.c`,
  `x64native_globals.c`, `x64native_rtl.c/.S`), Dynacache-Hash-Generierung
  — alles hinter `option(WINE_NX_BUILD_X64NATIVE ... OFF)`.
- `include/wine/x64native.h` (neu, uncommitted) definiert den Vertrag:
  `struct wine_nx_amd64_context` (Feld-für-Feld-Spiegel der echten
  AMD64_CONTEXT) plus vier schwach gebundene Funktionen
  (`wine_nx_x64_process_init/thread_init/enter/prepare_for_exception`).
- `wine-nx-probe/source/runtime.c` ruft `wine_nx_x64_handoff()` bereits
  über ein `__attribute__((weak))`-Symbol auf (Zeile ~78, ~2256-2277) —
  Builds ohne `WINE_NX_BUILD_X64NATIVE` fallen sauber auf einen
  "not implemented"-Log zurück, Builds damit rufen den echten Handoff.
- **Verifiziert per Dateisystem, nicht durch erneuten Build in dieser
  Session**: `wine-nx-probe/build-switch-x64native/wine-nx-runtime.nro`
  (9,9MB, gegenüber 3,9MB bei `vulkan-runtime`/`runtime-deko3d` ohne
  Box64-Core) wurde am 2026-07-28 12:23 erfolgreich gebaut — der
  x64native-Build **kompiliert und linkt durch**.
- **Nicht getan**: `wine_nx_x64_handoff()` selbst (Gast-Stack/Kontext-Aufbau,
  Punkt 3 in x86_64-scoping.md) und jeglicher Hardware-Test. Kein
  Log-/Crash-Artefakt für `cube64` existiert im Repo — der Pfad wurde nie
  auf echter Hardware ausgeführt.

### Stub / nicht implementiert
- Reale x86_64-Ausführung selbst (Routing+Erkennung ja, Backend-Aufruf
  neu vorhanden aber ungetestet).
- WGL/Direct3D-Treiber: keiner. DXVK/vkd3d: nicht begonnen (jetzt technisch
  möglich dank NVK, aber nicht geplant/gescoped).
- OpenGL via Zink: nicht begonnen.
- Registry/COM/OLE, shell32/comdlg, IME, Audio (WinMM lief nie in den
  Luanti-Traces an), Clipboard, Multi-Prozess-Verhalten: laut README
  durchgehend "incomplete or untested".
- Neuer GUI-Launcher (`wine-nx-launcher.nro`, siehe Abschnitt 4): kompiliert
  (9,9MB, 2026-07-28 13:31), enthält eine echte On-Device-Diagnose-Suite
  (`nx_diagnostics.c`, 32-bit/64-bit/JIT-Suiten laufen **echt**, Vulkan/D3D/GL-
  Suiten sind **bewusst** `SKIPPED` — reproduzieren sonst den
  vi-Consumer-Konflikt, siehe unten). Nie auf Hardware gestartet.

---

## 3. Bekannte Blocker & Crashes

1. **Natives ARM64: unidentifizierte ~80-90ms/Frame-Lücke.**
   `paint_avg` liegt bei ~200-210ms/Frame, die verfolgten Teilphasen
   summieren sich nur auf ~121ms. Ein gebatchter `get_paint_regions`-Call
   (Protokoll-ID 310, IPC-Trips 3→... reduziert) wurde implementiert und
   funktioniert korrekt, **bewegte die fps aber nicht** — die Lücke ist
   nicht die IPC-Rundenzahl. Offene Frage für den nächsten Einstieg.
   (`wine-nx-probe/perf-lab-log.md`)

2. **WOW64: zweiter, ungelöster Crash nach dem `win_proc_params`-Fix.**
   `STATUS_ACCESS_VIOLATION` (`0xc0000005`) unmittelbar nach
   `[NXPAINT][CALLER] window_surface_flush` (`dlls/win32u/dce.c:828`),
   während/nach dem ersten `WindowPosChanged` fürs Testfenster. Fault-
   Adresse `far=0x1e0` == Register `x20` == 480 dezimal == exakt die
   Testfenster-Höhe → starkes Indiz für **dieselbe Bugklasse** wie
   `win_proc_params` (ein Struct quert `KeUserModeCallback` mit
   inkompatiblen Guest/Native-Feld-Offsets), nur in einem anderen,
   noch nicht identifizierten Struct. Kandidaten (noch nicht auditiert):
   `struct win_hook_params` (`dlls/win32u/hook.c`),
   `struct copy_image_params`/`load_image_params` (`cursoricon.c`),
   `struct draw_text_params`/`draw_text_result` (`font.c`),
   `struct win_event_hook_params`, `struct send_async_params`
   (beide `message.c`). Template für den Fix: `struct
   wine_nx_winproc_wire` (`include/ntuser.h`) — feste `UINT64`-Felder,
   explizite Serialize/Deserialize an der Grenze.
   (`wine-nx-probe/winproc-wire-status.md`, gitignored, nicht committed)

3. **Luanti: sauberer Abort mit `exit_code=3`, Ursache unbekannt.**
   Reproduzierbar identisch über jeden Versuch (gleicher Call-Site,
   gleicher Guest-`Esp`, gleicher 22-DWORD-Stackdump). 5 Hypothesen
   getestet und **verworfen**: Backend-Auswahl (ALSOFT_DRIVERS),
   WinMM-Legacy-Treibersuche, MCI, ACM-Registrierung,
   VirtualProtect/W^X — keine davon feuert überhaupt während des Laufs.
   Schwache, unbestätigte Spur: `libgcc_s_dw2-1.dll` wird gesucht (~19
   Pfade) und nie gefunden — aber `uxtheme.dll` zeigt dasselbe Muster
   folgenlos, also kein verlässliches Signal. Nächste Schritte im
   Originaldokument: `RaiseException`/Exception-Dispatch tracen statt nur
   `NtTerminateProcess` (das ist stromabwärts der eigentlichen
   Entscheidung).

4. **deko3d als Laufzeit-Backend in `wine-nx-runtime` selbst: architektonisch
   blockiert.** `nwindowGetDefault()` wird von libnx' `__nx_win_init()`
   **vor `main()`** aufgerufen, sobald die Funktion irgendwo im Link-Closure
   referenziert wird — und der Pflicht-Fallback (libnx-Framebuffer-Pfad)
   referenziert sie immer. Deshalb kann deko3d nie First-and-only
   `vi`-Consumer sein, solange beide Pfade im selben Binary koexistieren.
   Fallback auf libnx-Framebuffer bei jedem deko3d-Init-Fehler ist
   hardware-verifiziert und bleibt aktiv — kein Regressions-Risiko, nur ein
   geparktes Feature. Lösungsweg: eigenes Binary ohne jede Referenz auf
   `nwindowGetDefault()` (bereits umgesetzt für Vulkan als
   `wine-nx-vulkan-runtime.nro` / `winnx_vulkan.c`, die dieselbe
   "eigene ViLayer statt Default-NWindow"-Lösung verwenden).

5. **Vulkan im neuen GUI-Launcher-Prozess:** würde denselben
   vi-Consumer-Konflikt reproduzieren (Launcher-GUI hält bereits das eine
   NWindow, das ein Vulkan-Swapchain auch bräuchte) — deshalb in
   `nx_diagnostics.c` bewusst als `SKIPPED` markiert statt versucht.
   Kein Bug, sondern eine bestätigte, dokumentierte Scope-Entscheidung.

---

## 4. Dateistruktur & Entry-Points

**Runtime/Packaging (Kern):**
- `wine-nx-probe/source/runtime.c` — Haupt-Bootstrap: PE-Mapping, Prozess-
  parameter, WOW64-/x64-Handoff-Routing, `wine_nx_boot_and_run_target()`
  (extrahiert, damit der Launcher denselben Pfad in-process aufrufen kann).
- `wine-nx-probe/source/runtime_boot.h` (neu) — Kontrakt für
  `wine_nx_boot_and_run_target()`/`wine_nx_ensure_lazy_init()`.
- `wine-nx-probe/source/runtime_deko3d.c` — deko3d-Backend für
  `wine_nx_fb_lock/unlock/present`.
- `wine-nx-probe/source/runtime_platform.c` — Plattform-Init.
- `wine-nx-probe/CMakeLists.txt` — 12 `add_executable`-Targets, 13
  NRO-Outputs (Details Abschnitt 7).
- `wine-nx-probe/build-switch.sh` — Docker/devkitA64-Buildskript,
  `WINE_NX_APP`-gesteuertes Staging.

**Neu (uncommitted): GUI-Launcher-Subsystem**
- `wine-nx-probe/source/launcher_main.c` — Entry-Point des
  Merged-Single-NRO-Launchers (`wine-nx-launcher`).
- `wine-nx-probe/source/nx_arch.c/.h` — In-Process-Sprung vom Launcher in
  `wine_nx_boot_and_run_target()` für GDI-Backend-Spiele (Vulkan-Spiele
  laufen weiter über separate-Binary-Handoff via `envSetNextLoad`, siehe
  `launcher_main.c`s `resolve_runtime_nro()`).
- `wine-nx-probe/source/nx_diagnostics.c/.h` — echte On-Device-Diagnose-
  Suite (32-bit/64-bit/JIT laufen real; Vulkan/D3D/GL bewusst geskippt).
- `wine-nx-probe/source/gui/*` — `nxmenu*` (Bibliothek/Detail/Mod-Manager/
  Quickmenu/Controller), `nxgui*` (Rendering, Font, PNG-Icon), `nxinput.c`,
  `nx_discord.c/.h`, `nxtoast.c/.h`, `nxdebug.c/.h`, `nxtest.c/.h`,
  `nxlib.c/.h`.

**Neu (uncommitted): x86_64-nativer Pfad**
- `include/wine/x64native.h` — Kontrakt (`wine_nx_amd64_context` +
  4 schwache Funktionen) zwischen `dlls/ntdll/loader.c` und Box64.
- `wine-nx-probe/third_party/box64/wine/x64native/*` — Box64-seitige
  Implementierung (`x64native.c/_globals.c/_rtl.c/_rtl.S`).
- `wine-nx-probe/third_party/box64/src/os/os_horizon.c` — `EmuX64Syscall()`.
- `wine-nx-probe/samples/cube64/` — x86_64-Smoke-Test (Pendant zu cube32).

**Neu (uncommitted): Vulkan/NVK**
- `dlls/win32u/vulkan.c` — Wines eigene win32u-Vulkan-Glue,
  `#ifndef __SWITCH__`-gegatete Feature-Forcing-Pfade deaktiviert
  (`VK_EXT_map_memory_placed`, `shaderStorageImageWriteWithoutFormat`-Force).
- `dlls/win32u/winnx_vulkan.c` (neu) — der eigentliche Vulkan-Displaytreiber,
  direkt gegen NVKs `vk_icdGetInstanceProcAddr` (kein dynamischer Loader
  auf Horizon), eigene ViLayer/NWindow (nicht die Default-Instanz).
- `dlls/winevulkan/vulkan_thunks.c` — eigene Unixlib-Registrierung für
  winevulkan.dll (`wine_nx_winevulkan_unix_funcs`/`_wow64_funcs`, umbenannt
  zur Kollisionsvermeidung im statischen Link).
- `dlls/ntdll/unix/virtual.c` — `wine_nx_lookup_static_unix_funcs_wow()`
  (32-bit-PEB-Walk für Unixlib-Dispatch).
- `wine-nx-probe/third_party/nxvk/` — vendorter NVK/Mesa-Fork
  (Submodule, committed als Pointer in `4dec5ee`/`3d8a319`).
- `wine-nx-probe/samples/vulkan_test/` — Smoke-Test-App.

**Switch-Display/USER/GDI-Integration:**
`dlls/win32u/winnx_drv.c`, `window.c`, `dce.c`, `sysparams.c`, `message.c`,
`input.c`, `font.c`.

**Horizon/NTDLL-Substrat:**
`dlls/ntdll/unix/horizon.c` (In-Process-Wineserver-Ersatz), `file.c`,
`process.c`, `thread.c`, `signal_arm64.c`.

**WOW64/Box64-CPU-Backend (klassisch, 32-bit):**
`wine-nx-probe/third_party/box64` (Submodule, inkl. `wine/wow64/wowbox64.c`),
`wine-nx-probe/wowbox64-scoping.md`.

**Host-seitige Entwicklung (ohne Konsole):**
`dlls/win32u/winnx_host_sim.c` (SDL2-Backend), `wine-nx-probe/switch-shims/`,
`wine-nx-probe/docker-host-sim/`.

**Dokumentation (im Repo, quellcode-verifiziert, nicht geraten):**
`wine-nx-probe/perf-lab-log.md`, `wowbox64-scoping.md`, `3d-accel-scoping.md`,
`dll-resolution-log.md`, `x86_64-scoping.md` (5 Tage älter als der aktuelle
Code, siehe Abschnitt 2), `winproc-wire-status.md` (gitignored).

---

## 5. Konkrete nächste Schritte

Reihenfolge nach Abhängigkeit und Risiko, nicht nach Aufwand:

1. **Uncommitted-Risiko beseitigen** (vor allem anderen): den gesamten
   Working-Tree-Stand (Vulkan, x64native, GUI-Launcher, Doku-Updates) in
   sinnvolle Commits aufteilen und committen. Siehe Abschnitt 7 für den
   genauen Umfang. Ohne das ist jede weitere Aktion auf diesem Checkout
   ein Risiko.
2. **`wine_nx_x64_handoff()` real implementieren** (`dlls/ntdll/loader.c`,
   heute nur Routing-Stub): Gast-Stack allozieren, initialen AMD64-Kontext
   bauen (Rip → x86_64-ntdll `LdrInitializeThunk`, Rsp mit Headroom — jede
   Bug-Klasse aus der 32-bit-Boot-Chain in `perf-lab-log.md` Teil 2 hat
   ein 64-bit-Analogon), x86_64-Gast-ntdll laden, in Box64s Run-Loop
   eintreten. Dann `WINE_NX_APP=cube64` erstmals auf Hardware testen —
   erwartete Bug-Kategorien: initialer Kontext, Stack-Headroom, TLS-State,
   NtContinue-Routing, Unwind-vs-JIT-Frames (dieselben Klassen wie beim
   32-bit-Bring-up, andere Details).
3. **Zweiten WOW64-Crash root-causen** (`win_hook_params`/
   `draw_text_params`/`win_event_hook_params`/`send_async_params` gegen
   das `wine_nx_winproc_wire`-Muster aus `include/ntuser.h` prüfen) —
   direkt blockierend für jede reale 32-bit-App mit Fenstern jenseits von
   Testprogrammen.
4. **Luanti-Abort root-causen**: `RaiseException`/Exception-Dispatch-Trace
   statt Stack-Archäologie nach `NtTerminateProcess`, wie im Original-
   dokument vorgeschlagen — settle die `libgcc_s_dw2-1.dll`-Spur zuerst,
   billig zu testen.
5. **x86_64-Gast-DLL-Staging**: dritte staged Directory (Vorschlag:
   `drive_c/windows/sysx64`, analog zu `syswow64`), Loader-Suchpfad-
   Erweiterung, verdrahtet wie `SYSWOW64_DIR` in `build-switch.sh` —
   Voraussetzung dafür, dass ein echtes x86_64-Windows-Programm (nicht nur
   `cube64`) überhaupt DLLs findet.
6. *(danach, nicht blockierend)*: die 80-90ms/Frame-Lücke im nativen
   ARM64-Pfad weiterverfolgen; `x86_64-scoping.md` und
   `winproc-wire-status.md` gegen den tatsächlichen Code-Stand
   nachziehen (beide sind jetzt nachweislich hinter dem Code).

---

## 6. Branches

```
main                     fafde9f  — kanonisch, PR-gemerged
dll-search-and-x86_64    4dec5ee  — AKTUELLER Branch, main + 11 Commits + riesiger uncommitted WIP
pr-1                     e1f672c  — 18 Commits ab einem älteren main-Stand, NIE gemerged
merge-test-pr1           3e99cb7  — lokaler Testmerge von pr-1 in main, "review only, not pushed"
wowbox64-scoping         fafa4da  — [gone] Remote-Branch bereits gelöscht, lokaler Rest von PR #2
```

- **`main`**: aktueller Kopf `fafde9f` ("docs: rewrite README as clean
  public documentation"), entstanden aus Merge von PR #2
  (`wowbox64-scoping`, 32-bit-Boot-Chain) und PR #3
  (`wine-nx-performance`, WOW64-Perf-Fixes).
- **`dll-search-and-x86_64`** (aktueller Branch): zweigt von `main` ab bei
  `fafde9f`, fügt 11 Commits hinzu (DLL-Suchpfad-Fix, x86_64-Routing/
  Erkennung, NVK-Doku-Korrektur, nxvk-Submodule-Vendoring) — **plus** den
  kompletten in Abschnitt 7 beschriebenen uncommitted Working-Tree-Stand.
  Das ist der Branch, auf dem faktisch die gesamte in diesem Dokument
  beschriebene Vulkan/x64native/Launcher-Arbeit sitzt.
- **`pr-1`**: 18 Commits (CI via GitHub Actions, Host-Sim-Wiederbelebung,
  Timing-Instrumentierung, Hardware-Deploy-Logs) — zweigt von einem
  **älteren** `main`-Stand ab (vor dem Merge von PR #2) und wurde nie
  gemerged. Enthält u.a. den Host-Sim-Loop, den PR #2/#3 offenbar nicht
  brauchten, aber CI-Infrastruktur (GitHub Actions Switch-NRO-Build +
  Host-Sim-Smoke-Test) fehlt dadurch heute komplett auf `main` und
  `dll-search-and-x86_64`.
- **`merge-test-pr1`**: ein bereits durchgeführter, aber laut eigener
  Commit-Message **nicht gepushter** Testmerge von `pr-1` in `main`, der
  WOW64-bezogene Konflikte auflöst. Das ist der faktische Nachweis, dass
  ein Merge von `pr-1` möglich ist, aber Konflikte hat — die Entscheidung
  "mergen oder verwerfen" wurde nie final getroffen.
- **`wowbox64-scoping`**: reiner lokaler Rest eines bereits gemergten und
  remote gelöschten Branches (PR #2) — sicher löschbar, keine eigene
  Historie mehr wert.

**Empfehlung vor der Pause**: `pr-1`/`merge-test-pr1` explizit entscheiden
(mergen, um die CI-Infrastruktur zurückzugewinnen, oder bewusst verwerfen
und dokumentieren warum), dann `wowbox64-scoping` löschen. Sonst hinterlässt
das Repo vier tote/halbtote Branches ohne erkennbaren Grund für eine neue
Session.

---

## 7. Build-Landschaft: 5 Build-Pipelines, 13 NRO-Outputs

**Ja — die Wahrnehmung stimmt: mehrere, sich überschneidende Build-Setups.**
Zum aktuellen Stand kein Chaos (jedes Setup hat einen klaren, dokumentierten
Zweck), aber definitiv erklärungsbedürftig für eine neue Session:

### Die 5 Pipelines
1. **Switch-NRO-Build** (`wine-nx-probe/build-switch.sh` → Docker,
   `devkitpro/devkita64`-Image, CMake) — DER Hauptbuild. Ein CMake-Configure
   baut **alle** 12 `add_executable`-Targets auf einmal; `WINE_NX_APP`
   steuert nur, welches Sample fürs SD-Karten-Staging ausgewählt wird, nicht
   was kompiliert wird. Ausgabeverzeichnis: `wine-nx-probe/build-switch/`
   (gitignored).
2. **x64native-Scratch-Build** (`wine-nx-probe/build-switch-x64native/`,
   untracked) — dieselbe CMakeLists.txt, aber mit
   `-DWINE_NX_BUILD_X64NATIVE=ON`, in einem separaten Verzeichnis, um den
   Hauptbuild nicht zu berühren. **Letzter erfolgreicher Voll-Build:
   2026-07-28**, alle 13 Artefakte vorhanden inkl. `wine-nx-runtime.nro`
   (9,9MB, Box64-Core mitgelinkt).
3. **Host-Sim-Build** (`build-host-sim/`, `build-host-sim-linux/`) —
   natives macOS/Linux-Binary desselben `win32u`-Treibers gegen SDL2
   (`dlls/win32u/winnx_host_sim.c`), für Rendering-/Input-Korrektheit ohne
   Konsole. Auf macOS 26 wegen einer Host-Loader-Inkompatibilität nur via
   `wine-nx-probe/docker-host-sim/` (Linux-Container) nutzbar, siehe
   `switch-shims/macos26-loader-incompatibility.md`.
4. **Wine-PE-Cross-Rebuild** (Host-`llvm-mingw`, `configure && make`,
   **kein** Docker) — erzeugt die eigentlichen Wine-DLLs
   (ntdll/kernel32/user32/gdi32/win32u/...) als echte PE-Dateien.
   Produktionsbaum: `build-wine-arm64-pe-clean/` (aarch64 only). Daneben
   drei/vier **Scratch-Bäume**, alle gitignored, alle mit unterschiedlichem
   `--enable-archs`-Wert zum Ausprobieren erzeugt:
   - `build-wine-arm64-pe-3arch-test/` — `aarch64,i386,x86_64` (Beweis: ein
     Baum kann alle drei Archs gleichzeitig bauen).
   - `build-wine-arm64-pe-wow64-test/` — `aarch64,i386` (Beweis: `wow64.dll`/
     `wow64win.dll` bauen echt, 1,3MB/1,4MB PE32+, host-verifiziert).
   - `build-wine-arm64-pe-x64native/` — liefert das x86_64-windows-DLL-Set
     für `cube64`/den x64native-Pfad.
   - `build-wine-arm64-pe/` — ohne erkennbares Suffix, vermutlich ein
     weiterer Zwischenstand; nicht separat verifiziert in dieser Session.
5. **WowBox64-Build** (`build-wowbox64/`, opt-in via
   `WINE_NX_BUILD_WOWBOX64`) — cross-compiled Box64s **klassisches**
   32-bit-`wowbox64.dll` (der `BTCpu*`-Ansatz) über `aarch64-w64-mingw32`,
   eigene Toolchain-Invocation, komplett unabhängig vom neuen
   nativ-gelinkten x64native-Pfad (Pipeline 1/2). Diese Doppelung ist
   architektonisch bewusst, nicht redundant: WowBox64 bedient den
   32-bit-Gast (über den echten `wow64.dll`-Loader-Pfad, sobald der mal
   aktiviert wird — aktuell nutzt der 32-bit-Pfad noch den handgebauten
   `wine_nx_wow64_handoff()`, nicht `wow64.dll`), x64native bedient den
   neuen 64-bit-Gast direkt ohne `wow64.dll` überhaupt.

### Die 13 NRO-Outputs (ein CMake-Configure, Pipeline 1/2)
`wine-nx-probe.nro`, `wine-nx-pe-smoke.nro`, `wine-nx-pe-real-report.nro`,
`wine-nx-pe-real-run.nro`, `wine-nx-pe-ntdll-smoke.nro`,
`wine-nx-ntdll-smoke.nro`, `wine-nx-jit-smoke.nro`,
`wine-nx-ntdll-file-smoke.nro`, `wine-nx-deko3d-smoke.nro`,
`wine-nx-runtime.nro`, `wine-nx-runtime-deko3d.nro`,
`wine-nx-vulkan-runtime.nro` (wrapt `wine-nx-runtime-deko3d.elf` neu, kein
eigenes `add_executable`), `wine-nx-launcher.nro` (neu). Die ersten 8 sind
reine Diagnose-/Smoke-Test-Binaries aus der frühen Bring-up-Phase — nie
aufgeräumt, aber auch nicht störend, da sie unabhängig kompilieren.

### Ist der Build-Verlauf clean?
**Statisch konsistent, aber seit 3,5 Wochen nicht erneut verifiziert.**
Konkret, per Dateisystem-Zeitstempel geprüft (kein Build in dieser Session
selbst ausgeführt):
- Alle Quelländerungen (`CMakeLists.txt`, `runtime.c`,
  `nx_diagnostics.c`, `launcher_main.c`, `nx_arch.*`, `x64native.h`) liegen
  zwischen 2026-07-28 11:52 und 13:31.
- Der letzte erfolgreiche Voll-Build (`build-switch-x64native/`, alle 13
  Artefakte inkl. `wine-nx-launcher.nro`) datiert auf 2026-07-28 13:31 —
  **nach** allen Quelländerungen außer einer.
- `wine-nx-probe/build-switch.sh` wurde um 13:51 editiert — **20 Minuten
  nach** diesem letzten Build. Diese Änderung wurde nie erneut gebaut/
  verifiziert. Da es sich um das Staging-Skript (kein kompilierter Code)
  handelt, ist das Risiko klein, aber nicht null.
- Seit 2026-07-28 wurde nichts mehr angefasst — der Working Tree ist exakt
  im Zustand der letzten Arbeitssitzung eingefroren.

**Empfehlung**: vor der eigentlichen Pause einmal `WINE_NX_APP=cube64
./wine-nx-probe/build-switch.sh` sowohl mit als auch ohne
`-DWINE_NX_BUILD_X64NATIVE=ON` frisch laufen lassen, um zu bestätigen, dass
der aktuelle (identische) Stand noch baut, dann committen (Abschnitt 6/hier).
Diese Session hat das nicht selbst getan — nur den vorhandenen Artefakt-
und Zeitstempel-Stand ausgewertet.

---

## Offene Meta-Frage für die nächste Session

Dieses Dokument selbst liegt aktuell **nicht** im Git-Index (neu, untracked,
wie `winproc-wire-status.md` bewusst gitignored ist). Falls gewünscht, vor
dem Pausieren zusammen mit dem restlichen WIP committen oder gezielt aus
der Versionierung heraushalten — beides ist eine bewusste Entscheidung, die
hier offen gelassen wird.
