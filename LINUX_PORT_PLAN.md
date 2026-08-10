# PalSchema Linux Port Plan

## Status: Runtime validation in progress ✅ (branch: `linux-port`)

---

## 1. Architecture Overview

### Current State (Windows)
- PalSchema is a C++ mod for UE4SS (Windows, via Okaetsu's `RE-UE4SS` fork)
- Builds as `PalSchema.dll`, loaded by UE4SS through `dlopen`/`LoadLibrary`
- Exports two C functions: `start_mod()` → `CppUserModBase*`, `uninstall_mod(CppUserModBase*)`
- Links against: UE4SS (shared), Zydis, Zycore, safetyhook, nlohmann_json, glaze, efsw
- Uses x86 byte-pattern (AOB) signatures to find functions in Palworld's binary
- Uses Zydis to resolve GMalloc via instruction decoding after `FMemory::Free` signature

### Target State (Linux)
- Builds as `libPalSchema.so`, loaded by UE4SS Linux via `LD_PRELOAD` + `dlopen`
- Same `CppUserModBase` interface — `start_mod()`/`uninstall_mod()` contract identical
- C++ mods go in `libs/` directory (not `dlls/`) per UE4SS Linux convention
- Same `extern "C"` export contract, but uses `__attribute__((visibility("default")))` instead of `__declspec(dllexport)`

### UE4SS Linux (`XarminaEu/ue4ss-linux`, branch `linux-native`)
- Native Linux port of UE4SS for dedicated game servers
- Loaded via `LD_PRELOAD` into the game process (no proxy DLL needed)
- C++23 required, builds with GCC 13+ or Clang 15+
- Key differences from Windows fork:
  - `FORCE_U16` defined: `STR()` produces `char16_t` strings (not `wchar_t`)
  - `funchook` replaces `safetyhook` for inline hooking
  - `RC_UE4SS_API` is `__attribute__((visibility("default")))` on Linux (empty for consumers)
  - Function resolution via `dlsym(RTLD_DEFAULT, ...)` for unstripped binaries
  - Manual address overrides via `UE4SS_Addresses.ini` for stripped binaries
  - GUI uses GLFW3/OpenGL3 backend (disabled for headless servers)
  - No UVTD (depends on Windows-only `raw_pdb`)
- All first-party deps are static libraries linked into `libUE4SS.so`
- Mods link against `UE4SS` target (PUBLIC linkage gives all transitive deps)

---

## 2. Submodule Added

```
[submodule "deps/ue4ss-linux"]
    path = deps/ue4ss-linux
    url = https://github.com/XarminaEu/ue4ss-linux.git
    branch = linux-native
```

Pinned at: `e87756a8dcbd189b7728f6f0ebcb380915b5d4a1` (v3.0.26-linux-dev)

---

## 3. Files Changed

| File | Change |
|------|--------|
| `.gitmodules` | Added `deps/ue4ss-linux` submodule entry |
| `.gitignore` | Added `build_linux/` |
| `deps/CMakeLists.txt` | Platform-conditional: `RE-UE4SS` (Windows) vs `ue4ss-linux` (Linux); `safetyhook` only on Windows |
| `CMakeLists.txt` | Removed direct Zydis/Zycore/safetyhook link (via UE4SS transitively); added Linux link libs (`dl`, `pthread`); added `FORCE_U16`, `PLATFORM_LINUX` defines; set symbol visibility to hidden |
| `include/Platform.h` | **NEW** — Cross-platform `PALSCHEMA_API` export macro |
| `include/PlatformSafetyhook.hpp` | **NEW** — funchook-based `InlineHook` wrapper matching safetyhook API |
| `include/SDK/PalSignatures.h` | Wrapped Windows AOB patterns in `#ifdef _WIN32`; Linux has empty maps + `LoadManualAddresses()` |
| `src/SDK/PalSignatures.cpp` | Added `LoadManualAddresses()` using INI parser; `get_list().for_each()` for section iteration |
| `src/dllmain.cpp` | Uses `Platform.h`; calls `LoadManualAddresses()` on Linux; `RC::` qualified `UE4SSProgram` |
| `src/Loader/*.cpp` | Standardized mod name types to `RC::StringType`; converted `path::native()` at filesystem boundaries |
| `src/SDK/Helper/PropertyHelper.h` | Reordered templates: `IsPropertyA` + `CastProperty` before `GetPropertyByName` |
| `Dockerfile` | **NEW** — Multi-stage build: ubuntu:24.04 builder → runtime with `libstdc++6` |
| `build_scripts/build_linux.sh` | **NEW** — Local dev build script |
| `PalSchema_Addresses.ini` | **NEW** — Template for Linux manual address overrides |

---

## 4. Build System Details

### deps/CMakeLists.txt (Linux path)
```cmake
add_subdirectory("ue4ss-linux")  # Builds libUE4SS.so + all deps
add_subdirectory("json")          # nlohmann_json (FetchContent)
add_subdirectory("glaze")         # glaze (FetchContent)
add_subdirectory("efsw")          # efsw (cross-platform, inotify on Linux)
# safetyhook NOT added on Linux — funchook comes from UE4SS deps transitively
```

### Root CMakeLists.txt (Linux-specific)
```cmake
target_link_libraries(${TARGET} PRIVATE
    nlohmann_json::nlohmann_json glaze::glaze efsw
    UE4SS   # Provides: fmt, funchook, dl, pthread, Zydis, Zycore,
            # File, DynamicOutput, Unreal, SinglePassSigScanner,
            # LuaMadeSimple, Function, IniParser, JSON, Input,
            # Constructs, Helpers, MProgram, ScopedTimer, Profiler
)
# Linux extras:
target_link_libraries(${TARGET} PRIVATE dl pthread)
target_compile_definitions(${TARGET} PRIVATE FORCE_U16 PLATFORM_LINUX)
```

### UE4SS Linux build (from CI workflow)
```bash
cmake -B build_linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Game__Shipping__Linux64 \
  -DUE4SS_GUI_ENABLED=OFF \
  -DUE4SS_INPUT_ENABLED=OFF \
  -DUE4SS_PROFILERS=OFF
cmake --build build_linux --target UE4SS
# Output: build_linux/Game__Shipping__Linux64/lib/libUE4SS.so
```

---

## 5. Signature Scanning — The Critical Blocker

### Windows AOB Patterns (16 functions)
All patterns in `PalSignatures.h` are x86-64 machine code sequences specific to the Windows Palworld binary. They **will not match** the Linux binary compiled with GCC/Clang.

Functions requiring signatures:
1. `UBlueprintGeneratedClass::PostLoadDefaultObject` — Blueprint loader
2. `FPakPlatformFile::GetPakFolders` — Raw table/pak path extension
3. `AsyncTask` — Game thread task dispatch
4. `AGameModeBase::InitGameState` — Loader initialization trigger
5. `UClass::AssembleReferenceTokenStream`
6. `UClass::GetDefaultObject`
7. `UStruct::StaticLink`
8. `UObjectGlobals::StaticFindObject` — Early object lookup
9. `FField::IsA` — Type checking
10. `FName::Constructor` — Early init
11. `UPalItemContainer::ApplySaveData`
12. `UPalDynamicItemWorldSubsystem::ApplyWorldSaveData`
13. `FFieldClass::GetNameToFieldClassMap` (call-resolved)
14. `FName::ToString_Wchar` (call-resolved)
15. `GetObjectsOfClass` (call-resolved)
16. `FMemory::Free` (call-resolved) — Used for GMalloc
17. `UDataTable::Serialize` (call-resolved)
18. `UPalDynamicItemWorldSubsystem::Create_ServerInternal` (call-resolved)
19. `UPalItemSlot::UpdateItem_ServerInternal` (call-resolved)
20. `UWorld::CleanupWorld` (call-resolved)

### Linux Approach (3 Methods)

**Method 1: VTable + UFunction Path (no AOB needed — 5 hooks)**
These hooks use `StaticFindObject` to find the class CDO, then read the vtable pointer and index into a specific slot. They work automatically without any signatures:
- `UPalGameInstance::Init` — VTable[90]
- `UBlueprintGeneratedClass::PostLoad` — VTable[20]
- `AActor::PostInitializeComponents` — VTable[159]
- `OnLevelShown` / `OnLevelHidden` — UFunction path lookup via `StaticFindObject<UFunction*>`

**Method 2: UE4SS `RegisterHook` Lua API (not usable in the current stripped server)**
UE4SS documents name-based UFUNCTION hooks, but the current Linux runtime crashes while setting up Lua/reflection and while calling the Lua reflection probes. C++ mod startup and `on_unreal_init()` do work; this is a reflection/runtime limitation, not an unknown init sequence. Do not treat the `_ServerInternal` suffix as proof that a hook is safe.

**Method 3: AOB Patterns / Manual Addresses**
These require function addresses found via reverse engineering:
- `UDataTable::Serialize` (call-resolved)
- `FPakPlatformFile::GetPakFolders` (direct)
- `UPalItemContainer::ApplySaveData` (post-call return sentinel used by the detour)
- `UPalItemSlot::UpdateItem_ServerInternal` (direct call target)
- `UWorld::CleanupWorld` (call-resolved)
- `UPalDynamicItemWorldSubsystem::Create_ServerInternal` (direct call target)

The current cleanup investigation has a rejected candidate at `0x47F1D10`:
it writes `UWorld::CleanupWorldTag` and is called on world pointers, but the
observed ABI consumes only one boolean while the existing PalSchema hook takes
three post-`this` arguments. It must remain unset until the ABI is resolved.

### GMalloc Resolution
- **On Linux**: `UnrealOffsets::ResolveFromUE4SS()` resolves `GMalloc` via `UnrealInitializer::LoadExport("GMalloc")` (dlsym) — this works without any AOB patterns
- Fallback: Zydis-based `FMemory::Free` scan (Windows-specific, won't work on Linux)
- **The `GetPakFolders` hook** is needed to add the mod pak directory, not for GMalloc

### How to Get Linux Function Addresses

**Option A: Ghidra (most reliable)**
1. Copy `PalServer-Linux-Shipping` from `palworld-server-docker` volume
2. Load in Ghidra with ELF loader
3. Binary is **stripped** (no symbols) — functions must be identified by cross-references or known UE4 patterns
4. Search for UFunction names in reflection data (FName entries)
5. Extract function addresses

**Option B: UE4SS runtime reflection (currently blocked)**
- UE4SS's documented Lua/reflection path crashes during setup on this stripped binary.
- A C++ `ForEachUObject`/`UFunction` probe also failed inside the same recovered callback before producing stable metadata.
- Runtime reflection is therefore evidence for a future UE4SS fix, not a source of current Linux addresses.

**Option C: Community resources**
- Check if Linux Palworld AOB patterns exist in UE4SS community
- Check `find_ps_scan.sh` in UE4SS repo for known patterns

### UE4SS Reflection Limitation (Known Issue)
UE4SS Linux reaches native C++ mod loading on the stripped PalServer binary,
but its broad UObject iteration and some Lua/property setup paths can signal
11. Crash recovery keeps the server running. The documented name-based
reflection route is therefore not a reliable source of addresses here;
external static analysis is being used instead. This is a runtime API
limitation, not an unknown PalSchema init sequence.

Ghidra `-noanalysis` import independently identified the Palworld vtables for
`UPalItemContainer`, `UPalItemSlot`, `UPalDynamicItemWorldSubsystem`, and
`UWorld`. The dynamic-item vtable slot agrees with the existing
`ApplyWorldSaveData` address. The ordinary-item target was recovered from the
item-container save-loop call site and its post-call return sentinel. No
`UWorld::CleanupWorld` address has met the same evidence standard.

---

## 6. Hook Dependencies (What Won't Work Without Signatures)

| Hook | Depends On | Impact if Missing |
|------|-----------|-------------------|
| `HookDatatableSerialize` | `UDataTable::Serialize` | Core datatable modification won't work |
| `HookGameInstanceInit` | `StaticFindObject` + VTable[90] | Loader initialization won't trigger |
| `GetPakFolders` hook | `FPakPlatformFile::GetPakFolders` | Extra pak path not added |
| `PostLoad` hook (Blueprint) | `StaticFindObject` + VTable[20] | Blueprint mod loading won't work |
| `PostInitializeComponents` hook | VTable[159] | Actor init hook won't fire |
| `UpdateItem_ServerInternal` hook | Signature | Item modification won't work |
| `DynamicItem` hook | Signature | Dynamic item handling won't work |
| `WorldCleanup` hook | `UWorld::CleanupWorld` | Cleanup logic won't fire |

**Without any of these signatures, PalSchema on Linux will compile and load but do nothing functional.**

---

## 7. Docker Build Environment

### Dockerfile (multi-stage)
```dockerfile
FROM ubuntu:24.04 AS builder
# Installs: cmake, ninja, gcc, g++, git, rust
# Stage 1: Build UE4SS Linux from submodule
# Stage 2: Build PalSchema against UE4SS
# Runtime: Ubuntu 24.04 with just libstdc++6 + built .so files
```

### Build Script (`build_scripts/build_linux.sh`)
```bash
# 1. Init submodules
# 2. Build UE4SS Linux (if not cached)
# 3. Build PalSchema against UE4SS
# Output: build_linux/libPalSchema.so
```

### Docker Build Command
```bash
docker build -t palschema-linux .
# Output: container with /output/libPalSchema.so and /output/libUE4SS.so
```

---

## 8. Runtime Deployment

### Mod Directory Structure (Linux)
```
/palworld/UE4SS/
├── UE4SS-settings.ini                # UE4SS config (ModsFolderPath=/palworld/UE4SS/Mods)
├── libUE4SS.so                       # UE4SS library
└── Mods/
    ├── mods.txt                      # "PalSchema : 1"
    └── PalSchema/
        └── libs/
            └── libPalSchema.so       # The built mod
```

### Loading
```bash
# UE4SS is loaded via LD_PRELOAD
LD_PRELOAD=/palworld/UE4SS/libUE4SS.so ./PalServer-Linux-Shipping

# UE4SS reads UE4SS-settings.ini, finds ModsFolderPath
# Discovers PalSchema mod (C++ mod in libs/), loads libs/libPalSchema.so
# Calls start_mod() → PalSchema constructor runs
```

### UE4SSSettings.ini
```ini
[Debug]
DebugConsoleEnabled=false
SimpleConsoleEnabled=true

[Overrides]
ModsFolderPath=/palworld/UE4SS/Mods
```

---

## 9. docker-compose Integration

### palworld-server-docker integration
```yaml
# In compose.yaml, mount the built mod:
volumes:
  - ./palschema-mods:/palworld/Pal/Binaries/Linux/Mods
# Where palschema-mods/ contains:
#   mods.txt
#   PalSchema/libs/libPalSchema.so
#   PalSchema/PalSchema_Addresses.ini
```

---

## 10. Remaining Work

### Immediate (to get compilation working)
- [x] Submodule added
- [x] Build system updated
- [x] Platform abstraction created
- [x] Export macros fixed
- [x] Signature scanning made cross-platform
- [x] Docker build environment created
- [x] **Test compilation** — `docker build` produces `libPalSchema.so` (0 errors, 0 warnings from our code)

### Core Functionality (to get mod working)
- [x] **Procure PalServer Linux binary** — Extract from palworld-server-docker volume
- [~] **Derive Linux AOB patterns** — Static ELF/Ghidra evidence recovered the ordinary-item pair; world cleanup remains
- [~] **Populate PalSchema_Addresses.ini** — Six usable ELF-derived entries are populated; `UWorld::CleanupWorld` remains unset
- [x] **Test GMalloc resolution** — UE4SS-side Linux resolution supplies `GMalloc`; the Windows Zydis fallback remains unused
- [~] **Test hook setup** — v41 loads the manual entries and returns from `on_unreal_init()` without a PalSchema crash; deferred core init has not yet exercised the item loader
- [~] **Test deferred core initialization** — v36 registers the UE4SS `ProcessEvent` callback successfully; an idle dedicated server has not fired it yet
- [x] **Use UE4SS-native field helpers where safe** — Linux field-class lookup and `FField::IsA` no longer require guessed Palworld addresses

### Exact continuation point
- [~] Recover `UWorld::CleanupWorld` with an ABI-matched target; do not use the `0x47F1D10` lead yet
- [ ] Obtain a safe normal server/client event that fires the deferred `ProcessEvent` callback
- [ ] Runtime-prove ordinary-item update/save and dynamic-item callbacks
- [ ] Runtime-prove datatable, pak-path, and spawn-cleanup behavior independently
- [ ] Remove or isolate temporary diagnostics before any production deployment

### Polish
- [ ] Guard remaining ImGui code (already done for dllmain.cpp, check other files)
- [ ] Create `.github/workflows/build-linux.yml`
- [ ] Test with palworld-server-docker container
- [ ] Document Linux-specific setup in README

---

## 11. Key Technical Findings

### What Works Cross-Platform (No Changes Needed)
- `efsw::FileWatcher` (uses inotify on Linux)
- Zydis disassembler (x86-64 ISA is platform-independent)
- VTable index-based virtual function calls (same C++ ABI for UE4 classes)
- `Ini::Parser`, `nlohmann::json`, `glaze` — all cross-platform
- `CppUserModBase` interface is identical
- `UE4SSProgram::get_program().get_working_directory()` works
- `SinglePassScanner::start_scan()` with `ScanTarget::MainExe` works
- `ASMHelper::resolve_call()` works (Zydis-based, x86-64)

### What Requires Platform-Specific Code (and What We Fixed)
- **Export macros** — `__declspec(dllexport)` → `__attribute__((visibility("default")))` via `PALSCHEMA_API`
- **AOB signature patterns** — Windows x86-64 byte sequences won't match Linux ELF; need Linux patterns
- **safetyhook → funchook** — Created `PlatformSafetyhook.hpp` wrapper with `InlineHook` class matching safetyhook's API (`create_inline`, `call<Ret>`, `enable/disable`). `funchook_prepare` + `funchook_install` for hooking.
- **ImGui GUI** — Guarded with `#ifdef HAS_GUI`, disabled for headless (`UE4SS_GUI_ENABLED=OFF`)
- **`_ReturnAddress`** — Mapped to `__builtin_return_address(0)`
- **`static_cast<void*>` to function pointer** — Changed to `reinterpret_cast` (GCC disallows `static_cast` from `void*` to function pointer)
- **`std::filesystem::path::string_type`** — On Linux this is `char`, not `wchar_t`. All mod name parameters standardized to `RC::StringType` (`char16_t`). Added `RC::to_generic_string(path::native())` conversions at filesystem boundaries.
- **`fmt::format` / `PS::Log`** — `STR()` produces `char16_t` format strings; all arguments must be `char16_t`. Fixed `std::to_wstring` → `fmt::format(STR("{}"), val)` and wrapped `std::string` args with `RC::to_generic_string()`.
- **`std::regex_replace` with char16_t** — Not supported by libstdc++. Replaced with manual string parsing.
- **`FString` construction** — `FString(const char*)` doesn't exist on Linux; must pass `char16_t*`. Converted via `RC::to_generic_string(path.native())`.
- **`Ini::Parser::get_section`** — Doesn't exist; use `get_list(section).for_each(callback)`.
- **`RC::to_utf8`** — Doesn't exist; use `RC::to_utf8_string()` or `RC::to_string()`.
- **`UE4SSProgram` namespace** — On Linux it's `RC::UE4SSProgram`, not bare `UE4SSProgram`.
- **`RC::Function::get_address`** — Doesn't exist; use `get_function_address()`.
- **Explicit qualification in `using namespace`** — Inside `namespace UECustom::BPGeneratedClassHelper`, functions can't have `UECustom::BPGeneratedClassHelper::` prefix.
- **`std::format` with char16_t** — Not supported by GCC's libstdc++; use `fmt::format` instead.
- **Forward declarations** — `AActor` needs explicit forward declaration in headers.

### UE4SS Linux Build Quirks
- Build type must be UE4-style: `Game__Shipping__Linux64` (parsed by `__` delimiter in `setup_build_configuration()`)
- `UE4SS_GUI_ENABLED` must be `OFF` for headless servers (GLFW requires X11)
- `UE4SS_INPUT_ENABLED` and `UE4SS_PROFILERS` should be `OFF`
- Docker build log is truncated at 2MiB; build output goes to `build_palschema/libPalSchema.so` (not `Game__Shipping__Linux64/lib/`)

---

## 12. Git History

```
* bfddfda build: fix Dockerfile COPY path for libPalSchema.so          (linux-port)
* 98aa328 build: fix explicit qualification, Ini::Parser API, RC::to_utf8
* 601360f build: fix get_function_address, UE4SSProgram namespace
* e5cd3b1 build: move LoadManualAddresses to public
* cb081fb build: fix static_cast to reinterpret_cast for function pointers
* 3e90599 build: fix CastProperty forward decl, AActor forward decl
* 493b0cb build: fix InlineHook private access, convert .native()
* 588e9bb build: fix type mismatches - standardize RC::StringType
* d0eb1c0 build: fix InlineHook move assignment, call<T> template
* 5616f88 build: fix SafetyHookInline scope, TCHAR qualification
* a047697 build: fix PlatformSafetyhook stub
* b8cce77 build: fix Linux compilation - safetyhook guards, TEXT() format
* e41dc06 build: add PSFormat.h, -Wno-changes-meaning
* 713558f build: fix char16_t formatting
* ed4ee64 build: exclude build_palschema from docker context
* 605f401 build: fix CMAKE_BUILD_TYPE to UE4-style
* 56146b3 build: simplify Dockerfile
* 40ddfc2 deps: disable UE4SS GUI
* 66b9f61 feat: Linux port improvements
* 5577f87 docs: add comprehensive Linux port plan
* 486d5bf feat: initial Linux port via ue4ss-linux submodule  (linux-port)
```
