# PalSchema Linux Port Plan

## Status: In Progress (branch: `linux-port`)

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
| `include/SDK/PalSignatures.h` | Wrapped Windows AOB patterns in `#ifdef _WIN32`; Linux has empty maps + `LoadManualAddresses()` declaration |
| `src/SDK/PalSignatures.cpp` | Added Linux AOB scanning path (if patterns provided); added `LoadManualAddresses()` implementation using `PalSchema_Addresses.ini` |
| `src/dllmain.cpp` | Uses `Platform.h`; calls `LoadManualAddresses()` on Linux; ImGui GUI guarded with `#ifdef HAS_GUI` |
| `Dockerfile` | **NEW** — Multi-stage build: UE4SS Linux → PalSchema → runtime |
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
13. `ValidateWorldSaveDynamicItemStaticIds`
14. `ValidateDynamicItemSaveData`
15. `FFieldClass::GetNameToFieldClassMap` (call-resolved)
16. `FName::ToString_Wchar` (call-resolved)
17. `GetObjectsOfClass` (call-resolved)
18. `FMemory::Free` (call-resolved) — Used for GMalloc
19. `UDataTable::Serialize` (call-resolved)
20. `UPalDynamicItemWorldSubsystem::Create_ServerInternal` (call-resolved)
21. `UPalItemSlot::UpdateItem_ServerInternal` (call-resolved)
22. `UWorld::CleanupWorld` (call-resolved)

### Linux Approach
- **AOB scanning**: `ScanTarget::MainExe` is populated via `dl_iterate_phdr` (same interface as Windows). If correct Linux AOB patterns are provided, scanning works identically.
- **Manual addresses**: `PalSchema_Addresses.ini` — INI file with `[Signatures]` section, format `FunctionName=0xADDRESS`
- **dlsym**: Only works for exported symbols (unlikely for internal UE4/Palworld functions)

### GMalloc Resolution
- `UnrealOffsets::InitializeGMalloc()` uses Zydis to decode x86 instructions after `FMemory::Free`
- On Linux, the instruction encoding (x86-64) is the same ISA, but the specific byte patterns differ
- The `FMemory::Free` signature is Windows-specific, so this function won't work on Linux without a Linux-specific signature
- **Impact**: `TArray::Add` in `GetPakFolders` hook won't work without GMalloc. This hook is only set up via Windows AOB scan anyway, so it's a cascade failure.

### How to Get Linux AOB Patterns
1. Procure `PalServer-Linux-Shipping` binary from `palworld-server-docker` volume
2. Load in Ghidra/IDA Pro with ELF loader
3. Identify each function by name (if unstripped) or by cross-references
4. Extract byte patterns from function prologues
5. Test patterns with `patternsleuth` or custom scanner

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
/palworld/Pal/Binaries/Linux/Mods/
├── mods.txt                          # "PalSchema : 1"
└── PalSchema/
    ├── libs/
    │   └── libPalSchema.so           # The built mod
    └── PalSchema_Addresses.ini       # Manual addresses (if needed)
```

### Loading
```bash
# UE4SS is loaded via LD_PRELOAD
LD_PRELOAD=/opt/ue4ss/libUE4SS.so ./PalServer-Linux-Shipping

# UE4SS discovers Mods/PalSchema/mods.txt, loads libs/libPalSchema.so
# Calls start_mod() → PalSchema constructor runs
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
- [ ] **Test compilation** — Run `docker build` and verify `libPalSchema.so` is produced

### Core Functionality (to get mod working)
- [ ] **Procure PalServer Linux binary** — Extract from palworld-server-docker volume
- [ ] **Derive Linux AOB patterns** — Scan the ELF binary for each of the 22 functions
- [ ] **Populate PalSchema_Addresses.ini** — Fill in actual addresses
- [ ] **Test GMalloc resolution** — Verify Zydis decoding works on Linux instruction patterns
- [ ] **Test hook setup** — Verify safetyhook works with Linux function addresses

### Polish
- [ ] Guard remaining ImGui code (already done for dllmain.cpp, check other files)
- [ ] Create `.github/workflows/build-linux.yml`
- [ ] Test with palworld-server-docker container
- [ ] Document Linux-specific setup in README

---

## 11. Key Technical Findings

### What Works Cross-Platform (No Changes Needed)
- `std::filesystem` path handling
- `safetyhook::create_inline` (works on Linux via funchook or safetyhook)
- `efsw::FileWatcher` (uses inotify on Linux)
- Zydis disassembler (x86-64 ISA is platform-independent)
- VTable index-based virtual function calls (same C++ ABI for UE4 classes)
- `Ini::Parser`, `nlohmann::json`, `glaze` — all cross-platform
- `std::format`, `STR()` macro usage

### What Requires Platform-Specific Code
- Export macros (`__declspec` vs `visibility`)
- AOB signature patterns (Windows vs Linux machine code)
- GMalloc resolution (same Zydis approach, different instruction patterns)
- ImGui GUI (guarded with `#ifdef HAS_GUI`, disabled for headless)
- `version.rc` (Windows resource, not compiled on Linux)

### UE4SS Linux API Compatibility
- `CppUserModBase` interface is identical
- `UE4SSProgram::get_program().get_working_directory()` works
- `SinglePassScanner::start_scan()` with `ScanTarget::MainExe` works (populated via `dl_iterate_phdr`)
- `ASMHelper::resolve_call()` works (Zydis-based, x86-64)
- `Ini::Parser` available from UE4SS deps
- `File::open()` available from UE4SS deps
- `RC::to_utf8()` for string conversion available

---

## 12. Git History

```
* 486d5bf feat: initial Linux port via ue4ss-linux submodule  (linux-port)
* 9847387 Update copyright year in LICENSE file                (main)
```
