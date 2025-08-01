// plugin.cpp - MINIMAL VERSION that should definitely compile
#include "PCH.h"

// Include the minimal config and logger
#include "Config.h"
#include "logger.h"
#include "PapyrusInterface.h"

using namespace std::literals;

namespace {
    constexpr std::string_view PLUGIN_NAME = "Printscreen"sv;
    constexpr REL::Version PLUGIN_VERSION{2, 0, 0};
}

// CRITICAL: Static member definitions for logger (MUST be in exactly one .cpp file)
namespace logger {
    std::mutex Logger::s_logMutex;
    std::string Logger::s_logFilePath;
    bool Logger::s_initialized = false;
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
        SKSE::RUNTIME_SSE_1_6_678
    });
    
    return data;
}();

/**
 * Main plugin load function - SIMPLIFIED
 */
extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse) {
    // Initialize SKSE first
    SKSE::Init(a_skse);
    
    // STEP 1: Initialize configuration (this should work)
    bool configLoaded = Config::Initialize();
    
    // STEP 2: Setup logger (this should work)
    logger::SetupLog();
    
    // STEP 3: Test basic logging (this should work)
    logger::info("PrintScreen Plugin v2.0.0 Loading");
    logger::info("SKSE Version: {}", a_skse->SKSEVersion());
    logger::info("Runtime Version: {}", a_skse->RuntimeVersion().string());
    
    if (configLoaded) {
        logger::info("Configuration loaded successfully");
    } else {
        logger::warn("Configuration failed to load, using defaults");
    }

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
    
    // Log success with configuration summary
    logger::info("===============================================");
    logger::info("Printscreen Plugin v2.0.0 LOADED SUCCESSFULLY");
    logger::info("Configuration Summary:");
    logger::info("- Log Level: {}", Config::Settings::LogLevelToString(Config::g_settings.GetLogLevel()));
    logger::info("- Console Output: {}", Config::g_settings.IsConsoleOutputEnabled() ? "Enabled" : "Disabled");
    logger::info("- File Output: {}", Config::g_settings.IsFileOutputEnabled() ? "Enabled" : "Disabled");
    logger::info("===============================================");
    
    return true;
}

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
