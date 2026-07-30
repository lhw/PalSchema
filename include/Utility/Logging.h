#pragma once

#include <HAL/Platform.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include "Utility/Config.h"

namespace PS {
    template <RC::Unreal::int32 optional_arg, typename... FmtArgs>
    auto Log(RC::File::StringViewType content, FmtArgs... fmt_args) -> void
    {
        if (optional_arg == RC::LogLevel::Error)
        {
            RC::Output::send<optional_arg>(STR("[PalSchema] [error] {}"), content, fmt_args...);
        }
        else if (optional_arg == RC::LogLevel::Warning)
        {
            RC::Output::send<optional_arg>(STR("[PalSchema] [warning] {}"), content, fmt_args...);
        }
        else if (optional_arg == RC::LogLevel::Verbose)
        {
            auto config = PS::PSConfig::Get();
            if (!config->IsDebugLoggingEnabled()) return;

            RC::Output::send<optional_arg>(STR("[PalSchema] [debug] {}"), content, fmt_args...);
        }
        else
        {
            RC::Output::send<optional_arg>(STR("[PalSchema] {}"), content, fmt_args...);
        }
    }
}
