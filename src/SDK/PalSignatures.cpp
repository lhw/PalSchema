#include "SDK/PalSignatures.h"
#include "Signatures.hpp"
#include "SigScanner/SinglePassSigScanner.hpp"
#include "Utility/Logging.h"
#include "Helpers/String.hpp"
#include "IniParser/Ini.hpp"
#include "ASMHelper/ASMHelper.hpp"

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
    void SignatureManager::Initialize()
    {
#ifdef _WIN32
        // Windows: use AOB signature scanning
        std::vector<SignatureContainer> SigContainerBox;
        SinglePassScanner::SignatureContainerMap SigContainerMap;

        for (auto& [ClassAndName, Signature] : Signatures)
        {
            SignatureContainer SigContainer = [=]() -> SignatureContainer {
                return {
                    {{Signature}},
                    [=](SignatureContainer& self) {
                        void* FunctionPointer = static_cast<void*>(self.get_match_address());

                        SignatureMap.emplace(ClassAndName, FunctionPointer);
                        PS::Log<LogLevel::Normal>(STR("Found {}: {}\n"), RC::to_generic_string(ClassAndName), FunctionPointer);

                        self.get_did_succeed() = true;

                        return true;
                    },
                    [=](const SignatureContainer& self) {
                        if (!self.get_did_succeed())
                        {
                            PS::Log<RC::LogLevel::Error>(STR("Failed to find signature for {}.\n"), RC::to_generic_string(ClassAndName));
                        }
                    }
                };
            }();
            SigContainerBox.emplace_back(SigContainer);
        }

        for (auto& [ClassAndName, Signature] : SignaturesCallResolve)
        {
            SignatureContainer SigContainer = [=]() -> SignatureContainer {
                return {
                    {{Signature}},
                    [=](SignatureContainer& self) {
                        void* FunctionPointer = static_cast<void*>(self.get_match_address());
                        void* FinalAddress = ASM::resolve_call(FunctionPointer);

                        SignatureMap.emplace(ClassAndName, FinalAddress);
                        PS::Log<LogLevel::Normal>(STR("Found {}: {}\n"), RC::to_generic_string(ClassAndName), FinalAddress);

                        self.get_did_succeed() = true;

                        return true;
                    },
                    [=](const SignatureContainer& self) {
                        if (!self.get_did_succeed())
                        {
                            PS::Log<RC::LogLevel::Error>(STR("Failed to find signature for {}.\n"), RC::to_generic_string(ClassAndName));
                        }
                    }
                };
            }();
            SigContainerBox.emplace_back(SigContainer);
        }

        SigContainerMap.emplace(ScanTarget::MainExe, SigContainerBox);
        SinglePassScanner::start_scan(SigContainerMap);
#else
        // Linux: try AOB scanning first (if patterns are provided), then fall back to manual addresses
        if (!Signatures.empty() || !SignaturesCallResolve.empty())
        {
            std::vector<SignatureContainer> SigContainerBox;
            SinglePassScanner::SignatureContainerMap SigContainerMap;

            for (auto& [ClassAndName, Signature] : Signatures)
            {
                SignatureContainer SigContainer = [=]() -> SignatureContainer {
                    return {
                        {{Signature}},
                        [=](SignatureContainer& self) {
                            void* FunctionPointer = static_cast<void*>(self.get_match_address());
                            SignatureMap.emplace(ClassAndName, FunctionPointer);
                            PS::Log<LogLevel::Normal>(STR("Found {} via AOB: {}\n"), RC::to_generic_string(ClassAndName), FunctionPointer);
                            self.get_did_succeed() = true;
                            return true;
                        },
                        [=](const SignatureContainer& self) {
                            if (!self.get_did_succeed())
                            {
                                PS::Log<RC::LogLevel::Verbose>(STR("AOB scan failed for {} (expected on Linux without patterns).\n"), RC::to_generic_string(ClassAndName));
                            }
                        }
                    };
                }();
                SigContainerBox.emplace_back(SigContainer);
            }

            for (auto& [ClassAndName, Signature] : SignaturesCallResolve)
            {
                SignatureContainer SigContainer = [=]() -> SignatureContainer {
                    return {
                        {{Signature}},
                        [=](SignatureContainer& self) {
                            void* FunctionPointer = static_cast<void*>(self.get_match_address());
                            void* FinalAddress = ASM::resolve_call(FunctionPointer);
                            SignatureMap.emplace(ClassAndName, FinalAddress);
                            PS::Log<LogLevel::Normal>(STR("Found {} via AOB: {}\n"), RC::to_generic_string(ClassAndName), FinalAddress);
                            self.get_did_succeed() = true;
                            return true;
                        },
                        [=](const SignatureContainer& self) {
                            if (!self.get_did_succeed())
                            {
                                PS::Log<RC::LogLevel::Verbose>(STR("AOB call-resolve failed for {} (expected on Linux without patterns).\n"), RC::to_generic_string(ClassAndName));
                            }
                        }
                    };
                }();
                SigContainerBox.emplace_back(SigContainer);
            }

            SigContainerMap.emplace(ScanTarget::MainExe, SigContainerBox);
            SinglePassScanner::start_scan(SigContainerMap);
        }
#endif
    }

    void* SignatureManager::GetSignature(const std::string& ClassAndFunction)
    {
        auto It = SignatureMap.find(ClassAndFunction);
        if (It != SignatureMap.end())
        {
            return It->second;
        }

        return nullptr;
    }

    void SignatureManager::LoadManualAddresses(const std::filesystem::path& working_directory)
    {
        auto addresses_file = working_directory / "PalSchema_Addresses.ini";
        if (!std::filesystem::exists(addresses_file))
        {
            PS::Log<LogLevel::Verbose>(STR("No PalSchema_Addresses.ini found, skipping manual address loading.\n"));
            return;
        }

        PS::Log<LogLevel::Normal>(STR("Loading manual addresses from PalSchema_Addresses.ini...\n"));

        auto FileBuffer = File::open(addresses_file);
        auto FileContents = FileBuffer.read_all();
        FileBuffer.close();

        if (FileContents.empty())
        {
            PS::Log<LogLevel::Error>(STR("PalSchema_Addresses.ini is empty.\n"));
            return;
        }

        Ini::Parser parser;
        parser.parse(FileContents);

        // Read addresses from [Signatures] section
        // Format: FunctionName=0xADDRESS
        auto entries = parser.get_section(STR("Signatures"));
        for (auto& [key, value] : entries)
        {
            std::string func_name = RC::to_utf8(key);
            std::string addr_str = RC::to_utf8(value);

            // Parse hex address (0x prefix)
            void* addr = nullptr;
            if (addr_str.starts_with("0x") || addr_str.starts_with("0X"))
            {
                addr = reinterpret_cast<void*>(std::stoull(addr_str, nullptr, 16));
            }
            else
            {
                addr = reinterpret_cast<void*>(std::stoull(addr_str, nullptr, 10));
            }

            if (addr)
            {
                SignatureMap[func_name] = addr;
                PS::Log<LogLevel::Normal>(STR("Loaded {} = {} from INI\n"), RC::to_generic_string(func_name), addr);
            }
        }
    }
}
