# PalSchema Linux Port — Current State

**Date**: 2026-07-31
**Branch**: `linux-port` (latest commit: `cc402eb` — static efsw)
**Docker image**: `palschema-linux-v41` (latest deployed to NAS test container)
**Test container**: `palserver-test` on TrueNAS NAS (`nas` SSH host)

---

## 1. Primary Objective

Restore PalSchema feature parity on the native Linux UE4SS/Palworld server by resolving the remaining game-specific signatures and validating the hooks in `palserver-test`.

**Status: In progress — v41 builds successfully and is deployed to the isolated test container. PalSchema reaches the end of `on_unreal_init()` without a native crash, and UE4SS accepts the deferred `ProcessEvent` registration. The idle dedicated server has not emitted that callback, so item-hook installation and full feature parity are not yet runtime-proven.**

---

## 2. Current Runtime Finding

The v41 test container starts PalSchema successfully. `start_mod()`, the constructor, `on_program_start()`, and `on_unreal_init()` all complete without a PalSchema signal 11. The deployed library hash is `6818898f70f8067c3f68b0afb3b327ae90f299c84dfa86b489bd18f0a30b6fea`, matching the extracted v41 build artifact.

`PalSchema_Addresses.ini` now supplies:

```ini
FPakPlatformFile::GetPakFolders=0x9DC96D0
UDataTable::Serialize=0xA2FA660
UPalDynamicItemWorldSubsystem::ApplyWorldSaveData=0x7184800
UPalDynamicItemWorldSubsystem::Create_ServerInternal=0x7184B20
```

The ordinary-item pair is now statically corroborated for Palworld v1.0.2.101103:

```ini
UPalItemContainer::ApplySaveData=0x729F8E7
UPalItemSlot::UpdateItem_ServerInternal=0x72B2F20
```

`ApplySaveData` is intentionally the return address immediately after the
save-loop call at `0x729F8E2`, because the detour compares `_ReturnAddress()`;
it is not the function entry. The call target has the expected item-slot
argument handling and prologue. This has passed static disassembly checks but
has not yet fired in a live item-save path.

`UWorld::CleanupWorld` remains unset. No address is being filled from a name,
vtable slot, or init-sequence assumption.

### 2.1 Evidence and analysis boundary

- The checked-in UE4SS Linux source explicitly skips broad UObject iteration
  and documents that some Unreal lifecycle hooks are unavailable on Linux;
  this explains why reflection probes are not being used to guess addresses.
- Ghidra 12.1.2 `-noanalysis` import was used against the exact Palworld
  v1.0.2.101103 ELF. It recovered RTTI/vtable locations and independently
  corroborated the dynamic-item address through a vtable slot.
- The ordinary-item target was recovered from the item-container save-loop
  call at `0x729F8E2`; the configured `0x729F8E7` value is the return address
  required by the existing `_ReturnAddress()` guard. Static disassembly checks
  passed for the call encoding, sentinel instruction, and target prologue.
- The generic [UE4SS C++ mod guide](https://docs.ue4ss.com/guides/creating-a-c%2B%2B-mod.html)
  documents the mod contract, but it does not provide Palworld's stripped
  Linux internal addresses. The [Unreal `CleanupWorld` API reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Engine/UWorld/CleanupWorld?application_version=5.5)
  documents the function signature, not this binary's address.

### 2.2 CleanupWorld investigation checkpoint

The external DWARF file advertised by `.gnu_debuglink`
(`PalServer-Linux-Shipping.debug`) was searched for locally and under
`/palworld` and `/tmp` on the NAS; it is absent. No symbol-assisted recovery
is therefore available.

Static disassembly found a strong but incomplete lead at `0x47F1D10`:

- it writes `UWorld::CleanupWorldTag` at `this + 0x70C` to `3`;
- several world-array cleanup paths call it with `rdi = UWorld*` and `esi = 1`;
- its prologue is `push rbx; mov rbx,rdi` and it performs world-cleanup state
  transitions.

It is **not configured** because the routine only consumes the first boolean
argument, while PalSchema's existing hook expects
`(UWorld*, bool, bool, UWorld*)`. The nearby large routine at `0x47F1D80`
also consumes only one boolean. Treat these as investigation leads, not
addresses. Do not deploy either value until the ABI and hook semantics are
proven, or the Linux hook is deliberately adapted and runtime-tested.

These were derived from the fixed-address Palworld ELF. The unsafe reflected UFunction lookup is no longer used.

The v30-v34 experiments established that startup callbacks are not a reliable
Linux initialization trigger: the game has already done much of its pak/table
work before UE4SS loads native mods. Eagerly calling the full core loader from
`on_unreal_init()` caused recoverable native-mod SIGSEGVs, so it remains disabled
until the loader dependencies are made safe at that lifecycle point.

---

## 3. Build Environment

### Architecture
- **Host**: macOS (arm64)
- **NAS**: TrueNAS x86-64, Docker with buildx
- **Container**: `palserver-test` — Palworld dedicated server with UE4SS loaded via `LD_PRELOAD`
- **Cross-compilation**: Docker `--platform linux/amd64` on NAS

### Build Pipeline
1. Edit source locally on macOS
2. `rtk rsync` individual files to NAS at `/tmp/palschema-build/`
3. `rtk ssh nas "cd /tmp/palschema-build && docker buildx build --no-cache --platform linux/amd64 -f Dockerfile --tag palschema-linux-vN ."`
4. Extract `.so` from Docker image
5. `docker cp` into `palserver-test` container
6. `docker restart palserver-test`

### Key Paths
- **Local source**: `/Users/lhw/src/PalSchema/`
- **NAS build context**: `/tmp/palschema-build/` (on `nas`)
- **Container UE4SS root**: `/palworld/UE4SS/`
- **Container mod path**: `/palworld/UE4SS/Mods/PalSchema/libs/libPalSchema.so`
- **Container UE4SS log**: `/palworld/UE4SS/UE4SS.log`
- **Game binary**: `/palworld/Pal/Binaries/Linux/PalServer-Linux-Shipping` (187MB, stripped, ET_EXEC)
- **UE4SS library**: `/palworld/UE4SS/libUE4SS.so`

### NAS Build Context Sync
The NAS build context at `/tmp/palschema-build/` must be synced with local source. Only individual files are synced via `rtk rsync` — the full tree was set up once previously. Key files that have local modifications beyond the committed state:

```
# Modified locally (not committed):
src/dllmain.cpp                                     — diag_log diagnostics
deps/ue4ss-linux/UE4SS/src/Mod/CppMod.cpp          — fprintf diagnostics in start_mod()
deps/ue4ss-linux/UE4SS/src/UE4SSProgram.cpp        — crash recovery wraps + debug messages
deps/ue4ss-linux/UE4SS/src/main_linux.cpp           — signal handler chain
deps/ue4ss-linux/deps/first/Unreal/src/UnrealInitializer.cpp — ScanOverrides with signal recovery
```

### Working tree / analysis artifacts

The worktree is intentionally dirty and contains the Linux-port implementation
plus local diagnostics. Preserve all existing changes. Notable untracked
artifacts are `CURRENT_STATE.md`, `UE4SS_Addresses.ini`, `UE4SS_Signatures/`,
and `ghidra/QueryPalworld.java`. The Ghidra script is a small reproducible
query helper; it does not modify the game binary or enter the Docker build.

`git diff --check` passes. No commit or production deployment was made.

---

## 4. UE4SS Crash Analysis (v29-v36)

With the diagnostic UE4SS build deployed, the UE4SS boot sequence shows:

```
[12:34:20] Linux: full mode — GUObjectArray has 439 elements. Starting mods with UE API support.
[12:34:20] Caught signal 11 during mod execution, recovering...
[12:34:20] Recovered from signal 11 during mod execution, continuing to next mod.
[12:34:20] LuaMod::on_program_start() crashed, continuing without Lua hooks.
[12:34:20] Caught signal 11 during mod execution, recovering...
[12:34:20] Recovered from signal 11 during mod execution, continuing to next mod.
[12:34:20] Linux: full mode enabled (MemberVariableLayout.ini + FName constructor resolved).
[12:34:20] Caught signal 11 during mod execution, recovering...
[12:34:20] Recovered from signal 11 during mod execution, continuing to next mod.
[12:34:20] setup_unreal_properties() crashed, continuing.
[12:34:20] Linux: full mode post-init done, starting event loop.
```

### Three UE4SS Signal 11 Crashes (All Caught by Recovery)

| # | What Crashed | Wrapped In | Consequence |
|---|---|---|---|
| 1 | `LuaMod::on_program_start()` | `ue4ss_with_crash_recovery` | Lua hooks unavailable |
| 2 | `fire_program_start_for_cpp_mods()` | `ue4ss_with_crash_recovery` | older v29 path; v36 C++ startup is confirmed |
| 3 | `setup_unreal_properties()` | `ue4ss_with_crash_recovery` | Lua property setup incomplete |

### Code Flow (UE4SSProgram.cpp)

```
start_cpp_mods()              → TRY block (C++ try-catch, NOT crash recovery)
LuaMod::on_program_start()    → ue4ss_with_crash_recovery → CRASHES (recovered)
fire_program_start_for_cpp_mods() → ue4ss_with_crash_recovery → CRASHES (recovered)
start_lua_mods()              → ue4ss_with_crash_recovery → succeeds
fire_unreal_init_for_cpp_mods() → ue4ss_with_crash_recovery → SUCCEEDS
setup_unreal_properties()     → ue4ss_with_crash_recovery → CRASHES (recovered)
```

### Important: `fire_unreal_init_for_cpp_mods()` SUCCEEDS

This is the function that calls PalSchema's `on_unreal_init()`. v36 logs confirm that `start_mod()`, `on_program_start()`, and `on_unreal_init()` are invoked; the remaining gap is later feature initialization, not C++ mod loading.

### All Three Crashes Are in UE4SS Internal Code

None of these UE4SS recovery events are the reason PalSchema fails to load. They are in:
1. UE4SS's internal Lua hook registration (`LuaMod::on_program_start`)
2. UE4SS's internal C++ mod notification (`fire_program_start_for_cpp_mods`)
3. UE4SS's internal Lua property setup (`setup_unreal_properties`)

These are **expected** on a stripped Linux binary where UE4SS can't resolve certain UE4 internals. The crash recovery system handles them gracefully.

---

## 5. UE4SS Startup Flow (Linux Full Mode)

```
1.  LD_PRELOAD loads libUE4SS.so into PalServer process
2.  UE4SSProgram::Initialize()
3.    ├── ScanGame() — finds GUObjectArray (439 elements), ProcessEvent, etc.
4.    ├── VerifyFNameConstructor() — skip on Linux (use address as-is)
5.    ├── FireModFunctions() for post-scan init
6.    ├── install_cpp_mods() — finds PalSchema directory, creates CppMod object
7.    │     └── CppMod constructor: dlopen(libPalSchema.so), dlsym("start_mod")
8.    ├── install_lua_mods() — finds check_hooks, UE4SSStatus
9.    └── setup_unreal() block:
10.       ├── [full mode check: GUObjectArray && MemberVariableLayout && elements > 0]
11.       ├── start_cpp_mods() — calls start_mods<CppMod>()
12.       │     ├── mods.txt pass: check_hooks, UE4SSStatus (Lua mods, skipped)
13.       │     └── enabled.txt pass: no enabled.txt mods found
14.       │     └── PalSchema `start_mod()` / constructor run successfully (v36)
15.       ├── LuaMod::on_program_start() → CRASH (signal 11)
16.       ├── fire_program_start_for_cpp_mods() → CRASH (signal 11)
17.       ├── start_lua_mods() → succeeds
18.       │     ├── check_hooks loads, hooks fire
19.       │     └── UE4SSStatus loads
20.       ├── fire_unreal_init_for_cpp_mods() → SUCCEEDS (PalSchema `on_unreal_init()` runs)
21.       └── setup_unreal_properties() → CRASH (signal 11)
22.  Event loop starts
23.  REST API becomes available
```

---

## 6. Files Modified (Uncommitted Changes)

### `src/dllmain.cpp` — Diagnostic logging
- Added `diag_log()` function that writes to `/palworld/UE4SS/Mods/PalSchema/diag.log`
- All `fprintf(stderr, ...)` replaced with `diag_log(...)` calls
- Added `on_program_start()` and `on_unreal_init()` overrides with diagnostics

### `deps/ue4ss-linux/UE4SS/src/Mod/CppMod.cpp` — fprintf diagnostics
- Added `fprintf(stderr, ...)` calls in `start_mod()` before/after `m_start_mod_func()` call

### `deps/ue4ss-linux/UE4SS/src/UE4SSProgram.cpp` — Crash recovery wraps
- `LuaMod::on_program_start()` wrapped in `ue4ss_with_crash_recovery` (line 3289)
- `fire_program_start_for_cpp_mods()` wrapped in `ue4ss_with_crash_recovery` (line 3300)
- `start_lua_mods()` wrapped in `ue4ss_with_crash_recovery` (line 3313)
- `fire_unreal_init_for_cpp_mods()` and `setup_unreal_properties()` wrapped in `ue4ss_with_crash_recovery` (lines 704-706)
- `VerifyFNameConstructor` skip on Linux (lines 715-722)
- Various `fprintf(stderr, ...)` debug messages added
- `UE4SS_DBG` and `UE4SS_ERR` debug messages added throughout

### `deps/ue4ss-linux/UE4SS/src/main_linux.cpp` — Signal handler chain
- `s_scan_jmpbuf` / `s_has_scan_jmpbuf` extern declarations at file scope
- `ue4ss_with_crash_recovery` function with `sigsetjmp`/`siglongjmp`

### `deps/ue4ss-linux/deps/first/Unreal/src/UnrealInitializer.cpp` — ScanOverrides
- File-scope extern declarations for `s_scan_jmpbuf`/`s_has_scan_jmpbuf`
- `safe_call` lambda wrapping each ScanOverrides function pointer call
- `ps_scan` skip on Linux (lines 492-506)

---

## 7. UE4SS Settings (Container)

### `UE4SS-settings.ini`
```ini
[Debug]
DebugConsoleEnabled=false
SimpleConsoleEnabled=true

[DebugGame]
DebugGameModeEnabled=false

[EngineVersionOverride]
OverrideEngineVersion=5.1
```

### `mods.txt`
```
check_hooks : 1
UE4SSStatus : 1
```

### `UE4SS_Addresses.ini`
```ini
[GlobalOffsets]
GUObjectArray=0x7FA291598950  # STALE — overridden by heuristic scan
GMalloc=0xC137000              # Stable (BSS segment)

[ScanOverrides]
StaticConstructObjectInternal=0x7FA5266B4240
FNameInit=0x7FA524D016D0
ProcessEvent=0x7FA527041960
ProcessInternal=0x7FA5270432D0
ProcessLocalScriptFunction=0x7FA527043590
CallFunctionByNameWithArguments=0x7FA527054A20
```

---

## 8. Server Boot Status

The Palworld dedicated server **boots and runs successfully**:
- UE4SS loads and initializes in full mode (439 GUObjectArray elements)
- Event loop starts
- REST API responds at port 8212
- Lua mods (check_hooks, UE4SSStatus) load and run
- Server runs for 3+ hours without issues

The 3 signal 11 crashes are all caught by `ue4ss_with_crash_recovery` and are non-fatal.

PalSchema now reaches `on_unreal_init()` and returns. UE4SS still emits non-fatal stripped-binary diagnostics around Lua/property setup.

---

## 9. Technical Details

### Game Binary
- Path: `/palworld/Pal/Binaries/Linux/PalServer-Linux-Shipping`
- Size: 187MB
- Type: ET_EXEC (fixed load addresses, no ASLR)
- Symbols: stripped (`.symtab=0`)
- GUObjectArray: found via heuristic scan (changes every run, ~0x7FDD04BB6DF0)
- GMalloc: found via dlsym/heuristic (0xC137000, stable in BSS)

### Build System
- CMake build type: `Game__Shipping__Linux64`
- GUI disabled: `UE4SS_GUI_ENABLED=OFF`
- efsw: static build (no runtime dependency)
- Docker image base: `ubuntu:24.04`
- Compiler: GCC (Ubuntu 24.04)

### Signal Handler Chain
```
s_has_mod_jmpbuf (per-mod) → s_has_scan_jmpbuf (heap scan) → s_has_jmpbuf (init-level, aborts UE4SS)
```

### PalSchema C++ Mod Contract
- Exports: `start_mod()` → `CppUserModBase*`, `uninstall_mod(CppUserModBase*)`
- Library location: `Mods/PalSchema/libs/libPalSchema.so`
- Loaded by: `CppMod` constructor via `dlopen()` + `dlsym()`

---

## 10. Exact Handoff / Future Direction

Continue in this order:

1. **Do not change the four existing dynamic/table/pak addresses.** They are
   deployed and startup-safe. Keep `UWorld::CleanupWorld=0x0` until proven.
2. **Recover the cleanup ABI and target.** Compare the `0x47F1D10` lead with
   the Palworld/UE version's actual `UWorld::CleanupWorld` declaration and
   inspect its callers. If the Linux binary really exposes a two-argument
   cleanup routine, adapt `PalSpawnLoader` under `#ifdef PLATFORM_LINUX`
   rather than forcing the Windows four-argument signature. Validate with a
   world transition or server shutdown in the isolated container.
3. **Find a normal `ProcessEvent` trigger.** The idle dedicated server does
   not emit the deferred callback. REST is enabled but the test container's
   admin password authentication returned `401`; no password was printed or
   changed. RCON and a real client connection are possible future triggers,
   but do not invent commands or touch production.
4. **Build v42 only after a source/config change.** Sync only changed files to
   `/tmp/palschema-build`, build on NAS, extract only `libPalSchema.so`, copy
   it to `palserver-test`, restart, and compare hashes before reading logs.
5. **Runtime-proof the ordinary-item pair.** A live save/update event must
   show the item hook installed and its callback/log marker firing. Static
   disassembly alone is insufficient for feature parity.
6. **Then test dynamic-item, datatable, pak, and spawn behavior separately.**
   Keep baseline server-health checks and do not replace them with mod-only
   smoke tests.

### Remaining UE4SS Crashes
The 3 signal 11 crashes are in UE4SS internals, not PalSchema. They are:
1. Expected on stripped binaries
2. Caught by crash recovery
3. Non-fatal (server boots and runs)

These could be investigated separately but are not blocking PalSchema functionality.

### Production Deployment
After PalSchema works on `palserver-test`:
1. Deploy to `games-palserver-1` (production container)
2. Ensure proper volume mounts for mod persistence
3. Test with real game clients

---

## 11. Docker Image History

| Version | Date | Changes | Status |
|---------|------|---------|--------|
| v7 | Jul 31 | Crash recovery wraps, static efsw | Deployed, tested |
| v8 | Jul 31 | fprintf(stderr) diagnostics | Deployed, stderr not visible in docker logs |
| v29 | Jul 31 | ELF-derived UDataTable and pak-folder targets | Stable lifecycle baseline |
| v30-v34 | Jul 31 | Hook timing, core-init, and UE4SS-native helper experiments | Eager full init rejected after recoverable SIGSEGVs |
| v35-v36 | Jul 31 | Deferred `ProcessEvent` core-init registration and boundary validation | Deployed to test container; registration succeeds, idle callback not observed |
| v40 | Jul 31 | Removed obsolete validation detour dependencies; split item/dynamic hook setup; added ELF-derived dynamic-item pair | Built and deployed to test container; deferred core init still not triggered |
| v41 | Jul 31 | Added statically corroborated ordinary-item call target and `_ReturnAddress()` sentinel | Built and deployed to test container; container healthy, item callback not yet observed |

---

## 12. Known Issues

1. **Full feature parity not yet proven** — container health and deferred registration are verified, but core hook installation, gameplay-triggered datatable/pak behavior, ordinary-item behavior, dynamic-item behavior, and spawn cleanup still need assertions.
2. **fprintf(stderr) invisible in docker logs** — Game server process may redirect stderr. Use file-based diagnostics instead.
3. **NAS build context staleness** — Only individual files synced via `rtk rsync`. Full tree sync needed if many files change.
4. **UE4SS internal crashes** — 3 signal 11s in UE4SS internals (LuaMod hooks, setup_unreal_properties). Expected on stripped binary, caught by recovery.
5. **PalSchema_Addresses.ini** — Six usable entries are populated, including the ordinary-item call target and return sentinel; `UWorld::CleanupWorld` remains unresolved. The cleanup lead at `0x47F1D10` is intentionally not present in the INI.
6. **Linux core initialization timing** — Calling the full loader from `on_unreal_init()` can trigger native-mod SIGSEGV recovery; do not enable that path without a narrower dependency test.
7. **No external debug symbols** — The ELF's referenced DWARF file is absent locally and on the NAS, so static RE must use call sites, RTTI/vtables, field offsets, and control flow.

### Useful verification evidence

- v41 Docker build completed successfully on the NAS as
  `palschema-linux-v41`; the build emitted only the existing Dockerfile
  `FROM --platform` warnings.
- Extracted v41 library and the deployed container library both hash to
  `6818898f70f8067c3f68b0afb3b327ae90f299c84dfa86b489bd18f0a30b6fea`.
- `palserver-test` is running after restart. `diag.log` shows successful
  `start_mod`, constructor, `on_program_start`, and `on_unreal_init` sequences;
  no `InitCore` or item callback marker exists yet.
- The REST probe used the configured admin password without printing it and
  returned `401`; the password was not changed. The test container remains
  isolated and production was untouched.
