// plugin.cpp — PrintScreen v4.0 (refactored)
#include "PCH.h"
#include "Config.h"
#include "logger.h"
#include "papyrus/Bindings.h"
#include "capture/TempFileGuard.h"

namespace {
    constexpr std::string_view kPluginName    = "Printscreen"sv;
    constexpr REL::Version     kPluginVersion { 4, 0, 0 };
}

extern "C" __declspec(dllexport)
bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    Config::Initialize();
    logger::SetupLog();

    logger::info("Printscreen: SKSEPlugin_Load starting (v4.0 refactored)");

    const auto& unknownKeys = Config::GetUnknownKeys();
    for (const auto& k : unknownKeys)
        logger::warn("Unknown INI key: '{}'", k);

    logger::info("Runtime: {}", a_skse->RuntimeVersion().string());

    if (a_skse->IsEditor()) {
        logger::error("Creation Kit not supported");
        return false;
    }
    if (a_skse->RuntimeVersion() < SKSE::RUNTIME_SSE_1_5_39) {
        logger::error("Unsupported runtime version");
        return false;
    }

    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus) { logger::error("Failed to get Papyrus interface"); return false; }

    if (!papyrus->Register(PapyrusBindings::Register)) {
        logger::error("Failed to register Papyrus functions");
        return false;
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
            switch (msg->type) {
                case SKSE::MessagingInterface::kDataLoaded:
                    PapyrusBindings::OnDataLoaded();
                    break;
                case SKSE::MessagingInterface::kPostLoadGame:
                case SKSE::MessagingInterface::kNewGame:
                    PapyrusBindings::OnPostLoadGame();
                    break;
            }
        });
    }

    logger::info("Printscreen v4.0 loaded successfully");
    return true;
}

extern "C" __declspec(dllexport)
constinit auto SKSEPlugin_Version = []() noexcept {
    SKSE::PluginVersionData data{};
    data.PluginName(kPluginName);
    data.PluginVersion(kPluginVersion);
    data.AuthorName("William G Lea");
    data.UsesAddressLibrary(true);
    data.UsesStructsPost629(true);
    data.HasNoStructUse(false);
    data.CompatibleVersions({
        SKSE::RUNTIME_SSE_1_5_39, SKSE::RUNTIME_SSE_1_5_97,
        SKSE::RUNTIME_SSE_1_6_318, SKSE::RUNTIME_SSE_1_6_353,
        SKSE::RUNTIME_SSE_1_6_629, SKSE::RUNTIME_SSE_1_6_640,
        SKSE::RUNTIME_SSE_1_6_659, SKSE::RUNTIME_SSE_1_6_678,
        REL::Version{1,6,1130,0}, REL::Version{1,6,1170,0}
    });
    return data;
}();

extern "C" __declspec(dllexport)
bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info) {
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name        = kPluginName.data();
    a_info->version     = kPluginVersion.pack();
    if (a_skse->IsEditor()) return false;
    if (a_skse->RuntimeVersion() < SKSE::RUNTIME_SSE_1_5_39) return false;
    return true;
}
