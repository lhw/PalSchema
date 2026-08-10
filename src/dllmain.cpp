#include <cstdio>
#include <filesystem>

#include "Mod/CppUserModBase.hpp"
#include "UE4SSProgram.hpp"
#include "Loader/PalMainLoader.h"
#include "Generator/JsonSchema/JsonSchemaGenerator.h"
#include "Utility/Config.h"
#include "Utility/Logging.h"
#include "SDK/PalSignatures.h"
#include "SDK/Classes/Async.h"
#include "SDK/UnrealOffsets.h"
#include "../version.h"
#include "Platform.h"

using namespace RC;
using namespace RC::Unreal;

static void diag_log(const char* msg) {
    try {
        FILE* f = fopen("/palworld/UE4SS/Mods/PalSchema/diag.log", "a");
        if (f) { fprintf(f, "%s\n", msg); fclose(f); }
    } catch (...) {}
}

class PalSchema : public RC::CppUserModBase
{
public:
    PalSchema() : CppUserModBase()
    {
        diag_log("[PalSchema] constructor: START");
        auto Version = fmt::format(STR("{}.{}.{}"), VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);

        ModName = STR("PalSchema");
        ModVersion = Version;
        ModDescription = STR("Allows modifying of Palworld's assets dynamically.");
        ModAuthors = STR("Okaetsu");

        diag_log("[PalSchema] constructor: has_member_variable_layout check");
        if (!has_member_variable_layout())
        {
            diag_log("[PalSchema] constructor: END (no MemberVariableLayout)");
            PS::Log<LogLevel::Error>(STR("MemberVariableLayout.ini is missing, unable to start PalSchema. Please ensure you are using UE4SS from https://github.com/Okaetsu/RE-UE4SS/releases/tag/experimental-palworld which comes with MemberVariableLayout.ini\n"));
            return;
        }

        diag_log("[PalSchema] constructor: PSConfig::Get()");
        auto config = PS::PSConfig::Get();
        diag_log("[PalSchema] constructor: config->Load()");
        config->Load();

        diag_log("[PalSchema] constructor: SignatureManager::Initialize()");
        PS::Log<LogLevel::Verbose>(STR("Initializing SignatureManager...\n"));
        Palworld::SignatureManager::Initialize();

#ifndef _WIN32
        diag_log("[PalSchema] constructor: LoadManualAddresses()");
        PS::Log<LogLevel::Verbose>(STR("Loading manual addresses for Linux...\n"));
        Palworld::SignatureManager::LoadManualAddresses("/palworld/UE4SS");
#endif

        diag_log("[PalSchema] constructor: UnrealOffsets::Initialize()");
        PS::Log<LogLevel::Verbose>(STR("Initializing UnrealOffsets...\n"));
        Palworld::UnrealOffsets::Initialize();

        diag_log("[PalSchema] constructor: MainLoader.PreInitialize()");
        PS::Log<LogLevel::Verbose>(STR("Preparing to pre-initialize PalSchema...\n"));
        MainLoader.PreInitialize();

        diag_log("[PalSchema] constructor: END (loaded)");
        PS::Log<RC::LogLevel::Normal>(STR("{} v{} by {} loaded.\n"), ModName, ModVersion, ModAuthors);
    }

    ~PalSchema() override
    {
    }

    auto has_member_variable_layout() -> bool
    {
#ifndef _WIN32
        return true;
#else
        namespace fs = std::filesystem;
        auto MemberVariableLayoutFile = fs::path(UE4SSProgram::get_program().get_working_directory()) / "MemberVariableLayout.ini";
        return fs::exists(MemberVariableLayoutFile);
#endif
    }

#ifdef HAS_GUI
    auto render_schema_generator()
    {
        static bool bGeneratingSchemas = false;
        if (ImGui::Button("Generate JSON Schema Files"))
        {
            if (!bGeneratingSchemas)
            {
                bGeneratingSchemas = true;
                UECustom::AsyncTask(UECustom::ENamedThreads::GameThread, [&]() {
                    PS::JsonSchemaGenerator::GenerateSchemaFiles();
                    bGeneratingSchemas = false;
                });
            }
        }

        if (bGeneratingSchemas)
        {
            ImGui::ProgressBar(-0.5f * (float)ImGui::GetTime(), ImVec2(0.0f, 0.0f), "Generating...");
        }
    }

    auto on_ui_init() -> void override
    {
        register_tab(STR("Pal Schema"), [](CppUserModBase* instance) {
            UE4SS_ENABLE_IMGUI()

            auto mod = dynamic_cast<PalSchema*>(instance);
            if (!mod)
            {
                return;
            }

            ImGui::SeparatorText("Generators");
            mod->render_schema_generator();
        });

        PS::Log<LogLevel::Verbose>(STR("Finished registering Pal Schema tab for GUI Console.\n"));
    }
#endif

    auto on_update() -> void override
    {
    }

    auto on_program_start() -> void override
    {
        diag_log("[PalSchema] on_program_start: called");
    }

    auto on_unreal_init() -> void override
    {
        diag_log("[PalSchema] on_unreal_init: START");
#ifndef _WIN32
        diag_log("[PalSchema] on_unreal_init: ResolveFromUE4SS");
        PS::Log<LogLevel::Verbose>(STR("Resolving functions from UE4SS on Linux...\n"));
        Palworld::UnrealOffsets::ResolveFromUE4SS();
        diag_log("[PalSchema] on_unreal_init: MainLoader.Initialize");
        MainLoader.Initialize();
#endif

        diag_log("[PalSchema] on_unreal_init: END");
    }
private:
    Palworld::PalMainLoader MainLoader;
};


extern "C"
{
    PALSCHEMA_API RC::CppUserModBase* start_mod()
    {
        diag_log("[PalSchema] start_mod: entered");
        return new PalSchema();
    }

    PALSCHEMA_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
