#include "Config.h"
#include <iostream>

namespace Config {
    
    // Global settings instance definition
    Settings g_settings;
    
    // Initialize configuration system
    bool Initialize() {
        std::cout << "[CONFIG] Initializing configuration system..." << std::endl;
        
        bool loaded = g_settings.Load();
        
        if (loaded) {
            std::cout << "[CONFIG] Configuration loaded from: " << g_settings.GetConfigPath() << std::endl;
            std::cout << "[CONFIG] Log Level: " << Settings::LogLevelToString(g_settings.GetLogLevel()) << std::endl;
            std::cout << "[CONFIG] Console Output: " << (g_settings.IsConsoleOutputEnabled() ? "Enabled" : "Disabled") << std::endl;
            std::cout << "[CONFIG] File Output: " << (g_settings.IsFileOutputEnabled() ? "Enabled" : "Disabled") << std::endl;
        } else {
            std::cout << "[CONFIG] Failed to load configuration, using defaults" << std::endl;
        }
        
        return loaded;
    }
    
} // namespace Config