// plugin.cpp - PrintScreen v3.0.0 with Menu Event Handler support
#include "PCH.h"

#include "Config.h"
#include "logger.h"
#include "PapyrusInterface.h"

using namespace std::literals;

namespace {
    constexpr std::string_view PLUGIN_NAME = "Printscreen"sv;
    constexpr REL::Version PLUGIN_VERSION{3, 0, 0};
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    // Initialize SKSE first
    SKSE::Init(a_skse);
// Initialize state early to set main thread ID
   PrintScreenPapyrus::InitializeState();
    // 1) Load logging configuration
    Config::Initialize();

    // 2) Set up logger sinks & level
    logger::SetupLog();

    logger::info("Printscreen: SKSEPlugin_Load starting");

    // 3) Warn about unknown INI keys
    const auto& unknownKeys = Config::GetUnknownKeys();
    if (!unknownKeys.empty()) {
        logger::warn("Unknown keys found in INI configuration file:");
        for (const auto& key : unknownKeys) {
            logger::warn("  - Unknown key: '{}'", key);
        }
        logger::warn("Valid keys are: LogLevel, ConsoleOutput, FileOutput, ShowTimestamps, "
                     "MaxLogFileSizeMB, ParallelCompression, CompressionThreads, "
                     "LogCaptureProgress, LogTimingInfo (under sections [Logging], [Performance], [Capture])");
        logger::warn("Legacy flat format also supported: Level, File, Console, Timestamps, LogPath");
    }

    // Log runtime version
    const auto rt = a_skse->RuntimeVersion();
    logger::info("Runtime version: {}", rt.string());

    // Verify minimum requirements
    if (a_skse->IsEditor()) {
        logger::error("Plugin does not support Creation Kit");
        return false;
    }

    const auto runtimeVersion = a_skse->RuntimeVersion();
    if (runtimeVersion < SKSE::RUNTIME_SSE_1_5_39) {
        logger::error("Unsupported runtime version {}", runtimeVersion.string());
        return false;
    }

    logger::info("Runtime version check passed");

    // Get Papyrus interface
    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus) {
        logger::error("Failed to get Papyrus interface");
        return false;
    }

    logger::info("Papyrus interface obtained successfully");

    // Register Papyrus functions
    logger::info("Attempting to register Papyrus functions...");

    if (!papyrus->Register(PrintScreenPapyrus::RegisterFunctions)) {
        logger::error("Failed to register Papyrus functions");
        return false;
    }

    logger::info("Papyrus functions registered successfully");

    // No message handler needed - Papyrus scripts handle all UI/menu logic

    // Log success with configuration summary
logger::info("===============================================");
logger::info("Printscreen Plugin v3.0.0 LOADED SUCCESSFULLY");
logger::info("Features:");
logger::info("- TakePhoto: Capture screenshots in multiple formats");
logger::info("- Animated captures: GIF and APNG with differential encoding");
logger::info("Configuration Summary:");
logger::info("- Log Level: {}", Config::LogLevelToString(Config::g_settings.GetLogLevel()));
logger::info("- Console Output: {}", Config::g_settings.IsConsoleOutputEnabled() ? "Enabled" : "Disabled");
logger::info("- File Output: {}", Config::g_settings.IsFileOutputEnabled() ? "Enabled" : "Disabled");
logger::info("Supported Formats: PNG, JPEG, BMP, TIFF, DDS, GIF, APNG");
logger::info("===============================================");
    return true;
}

/**
 * Required for CommonLibSSE-NG 3.6+ - Plugin version data
 */
extern "C" __declspec(dllexport) constinit auto SKSEPlugin_Version = []() noexcept {
    SKSE::PluginVersionData data{};
    
    data.PluginName(PLUGIN_NAME);
    data.PluginVersion(PLUGIN_VERSION);
    data.AuthorName("William G Lea");
    data.UsesAddressLibrary(true);
    data.UsesStructsPost629(true);
    data.HasNoStructUse(false);
    
    data.CompatibleVersions({
        SKSE::RUNTIME_SSE_1_5_39,
        SKSE::RUNTIME_SSE_1_5_97,
        SKSE::RUNTIME_SSE_1_6_318,
        SKSE::RUNTIME_SSE_1_6_353,
        SKSE::RUNTIME_SSE_1_6_629,
        SKSE::RUNTIME_SSE_1_6_640,
        SKSE::RUNTIME_SSE_1_6_659,
        SKSE::RUNTIME_SSE_1_6_678,
        REL::Version{1, 6, 1130, 0},
        REL::Version{1, 6, 1170, 0}
    });
    
    return data;
}();

/**
 * Plugin query function for compatibility checking
 */
extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info) {
    
    // Set plugin info
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = PLUGIN_NAME.data();
    a_info->version = PLUGIN_VERSION.pack();

    // Check if we're in the editor (not supported)
    if (a_skse->IsEditor()) {
        return false;
    }

    // Check minimum runtime version
    const auto ver = a_skse->RuntimeVersion();
    
    if (ver < SKSE::RUNTIME_SSE_1_5_39) {
        return false;
    }
 
    return true;
}
