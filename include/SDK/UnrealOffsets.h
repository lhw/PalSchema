#pragma once

namespace Palworld::UnrealOffsets {
    void Initialize();

    void InitializeGMalloc();

    void ApplyMemberVariableLayout();

    // On Linux, resolve functions from UE4SS's dlsym-based resolution
    // instead of AOB scanning. Called after UE4SS has fully initialized.
    void ResolveFromUE4SS();
}