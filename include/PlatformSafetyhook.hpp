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
          m_hook(other.m_hook), m_original(other.m_original),
          m_original_ptr(other.m_original_ptr), m_enabled(other.m_enabled) {
        other.m_funchook = nullptr;
        other.m_enabled = false;
    }
    InlineHook& operator=(InlineHook&& other) noexcept {
        if (this != &other) {
            disable();
            m_funchook = other.m_funchook;
            m_target = other.m_target;
            m_hook = other.m_hook;
            m_original = other.m_original;
            m_original_ptr = other.m_original_ptr;
            m_enabled = other.m_enabled;
            other.m_funchook = nullptr;
            other.m_enabled = false;
        }
        return *this;
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

    template<typename Ret = void, typename... Args>
    Ret call(Args... args) {
        using FuncPtr = Ret(*)(std::remove_reference_t<Args>...);
        auto func = reinterpret_cast<FuncPtr>(m_original);
        return func(args...);
    }

    template<typename T1, typename T2>
    static InlineHook create_from(T1 target, T2 hook) {
        InlineHook ih;
        ih.m_target = reinterpret_cast<void*>(target);
        ih.m_hook = reinterpret_cast<void*>(hook);

        ih.m_funchook = funchook_create();
        if (!ih.m_funchook) return ih;

        void* target_copy = ih.m_target;
        funchook_prepare(ih.m_funchook, &target_copy, ih.m_hook);

        ih.m_original_ptr = target_copy;
        ih.m_original = &ih.m_original_ptr;

        return ih;
    }

private:
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
    return InlineHook::create_from(target, hook);
}

template<typename T1, typename T2>
inline InlineHook create_inline(T1 target, T2 hook) {
    return InlineHook::create_from(target, hook);
}

} // namespace safetyhook

// Bring SafetyHookInline into global scope (matches Windows safetyhook behavior)
using safetyhook::SafetyHookInline;
using safetyhook::InlineHook;
