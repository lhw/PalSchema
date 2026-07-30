#pragma once

// Linux safetyhook stub — wraps funchook (provided by ue4ss-linux)
// safetyhook is Windows-only; on Linux we use funchook for inline hooking.

#ifdef _WIN32
#error "This file should not be included on Windows"
#endif

#include <funchook.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

// MSVC _ReturnAddress intrinsic → GCC/Clang equivalent
#ifndef _ReturnAddress
#define _ReturnAddress() __builtin_return_address(0)
#endif

namespace safetyhook {

class InlineHook {
public:
    InlineHook() = default;
    ~InlineHook() { disable(); }

    InlineHook(const InlineHook&) = delete;
    InlineHook& operator=(const InlineHook&) = delete;
    InlineHook(InlineHook&& other) noexcept
        : m_funchook(other.m_funchook), m_target(other.m_target),
          m_hook(other.m_hook), m_original(other.m_original), m_enabled(other.m_enabled) {
        other.m_funchook = nullptr;
        other.m_enabled = false;
    }

    bool enable() {
        if (m_enabled || !m_funchook) return false;
        funchook_install(m_funchook, 0);
        m_enabled = true;
        return true;
    }

    bool disable() {
        if (!m_enabled || !m_funchook) return false;
        funchook_uninstall(m_funchook, 0);
        m_enabled = false;
        return true;
    }

    void* original() const { return m_original; }

    template<typename Ret, typename... Args>
    Ret call(Args... args) {
        using FuncPtr = Ret(*)(Args...);
        auto func = reinterpret_cast<FuncPtr>(m_original);
        return func(args...);
    }

private:
    friend InlineHook create_inline(void* target, void* hook, void** original);
    friend InlineHook create_inline(void* target, void* hook);

    funchook_t* m_funchook = nullptr;
    void* m_target = nullptr;
    void* m_hook = nullptr;
    void** m_original = nullptr;
    void* m_original_ptr = nullptr;
    bool m_enabled = false;
};

using SafetyHookInline = InlineHook;

// funchook_prepare: funchook_prepare(funchook, &target_func, hook_func)
// It modifies target_func to point to the trampoline (original function).
inline InlineHook create_inline(void* target, void* hook, void** original) {
    InlineHook ih;
    ih.m_target = target;
    ih.m_hook = hook;
    ih.m_original = original;

    ih.m_funchook = funchook_create();
    if (!ih.m_funchook) return ih;

    // funchook_prepare modifies the target pointer to point to the trampoline
    void* target_copy = target;
    funchook_prepare(ih.m_funchook, &target_copy, hook);

    // The trampoline (original function) is now in target_copy
    if (original) {
        *original = target_copy;
        ih.m_original = original;
    } else {
        ih.m_original_ptr = target_copy;
        ih.m_original = &ih.m_original_ptr;
    }

    return ih;
}

inline InlineHook create_inline(void* target, void* hook) {
    InlineHook ih;
    ih.m_target = target;
    ih.m_hook = hook;

    ih.m_funchook = funchook_create();
    if (!ih.m_funchook) return ih;

    void* target_copy = target;
    funchook_prepare(ih.m_funchook, &target_copy, hook);

    ih.m_original_ptr = target_copy;
    ih.m_original = &ih.m_original_ptr;

    return ih;
}

} // namespace safetyhook
