// logger.cpp
#include "PCH.h"  // CRITICAL: Must be first include!
#include "logger.h"
#include "Config.h"

#include <vector>
#include <filesystem>
#include <system_error>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>

namespace {

std::filesystem::path GetSKSEFolder() {
    wchar_t* up = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&up, &len, L"USERPROFILE") != 0 || !up) {
        // Fallback if environment variable fails
        OutputDebugStringA("PrintScreen: Failed to get USERPROFILE\n");
        return std::filesystem::path();
    }
    std::wstring home = up;
    free(up);
    return std::filesystem::path(home) / L"Documents" / L"My Games" / L"Skyrim Special Edition" / L"SKSE";
}

inline spdlog::level::level_enum to_spd_level(int n) {
    switch (n) {
        case 0: return spdlog::level::off;
        case 1: return spdlog::level::err;
        case 2: return spdlog::level::warn;
        case 3: return spdlog::level::info;
        case 4: return spdlog::level::debug;
        case 5: return spdlog::level::trace;
        default: return spdlog::level::info;
    }
}

} // anonymous namespace

namespace logger {

void SetupLog()
{
    // Debug output to confirm we reached this function
    OutputDebugStringA("PrintScreen: SetupLog() called\n");

    const auto& s = Config::Get();

    // Debug: Log what config says
    char debugMsg[256];
    sprintf_s(debugMsg, "PrintScreen: Config - FileOutput=%d, ConsoleOutput=%d, LogLevel=%d\n",
              s.IsFileOutputEnabled(), s.IsConsoleOutputEnabled(), s.GetLogLevel());
    OutputDebugStringA(debugMsg);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.reserve(2);

    if (s.IsFileOutputEnabled()) {
        std::filesystem::path skseFolder = GetSKSEFolder();
        
        if (skseFolder.empty()) {
            OutputDebugStringA("PrintScreen: ERROR - Could not determine SKSE folder path\n");
        } else {
            std::filesystem::path logPath = skseFolder / L"Printscreen.log";
            
            // Debug: Show the path we're trying to use
            OutputDebugStringW((L"PrintScreen: Log path = " + logPath.wstring() + L"\n").c_str());
            
            // Create directory if needed
            std::error_code ec;
            std::filesystem::create_directories(logPath.parent_path(), ec);
            
            if (ec) {
                char errMsg[512];
                sprintf_s(errMsg, "PrintScreen: ERROR creating log directory: %s\n", ec.message().c_str());
                OutputDebugStringA(errMsg);
            } else {
                try {
                    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        logPath.string(),  // Use string() for compatibility
                        static_cast<size_t>(s.maxLogFileSizeMB) * 1024ULL * 1024ULL,
                        3);  // Keep 3 rotated files
                    sinks.emplace_back(std::move(fileSink));
                    OutputDebugStringA("PrintScreen: File sink created successfully\n");
                } catch (const std::exception& e) {
                    char errMsg[512];
                    sprintf_s(errMsg, "PrintScreen: ERROR creating file sink: %s\n", e.what());
                    OutputDebugStringA(errMsg);
                }
            }
        }
    } else {
        OutputDebugStringA("PrintScreen: File output is DISABLED in config\n");
    }

    if (s.IsConsoleOutputEnabled()) {
#if defined(_MSC_VER)
        sinks.emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
        OutputDebugStringA("PrintScreen: MSVC debug sink added\n");
#else
        sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif
    }

    // Ensure we have at least one sink
    if (sinks.empty()) {
        OutputDebugStringA("PrintScreen: WARNING - No sinks configured, adding fallback MSVC sink\n");
        sinks.emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    }

    auto lg = std::make_shared<spdlog::logger>("printscreen", sinks.begin(), sinks.end());
    lg->set_level(to_spd_level(s.GetLogLevel()));

    spdlog::set_default_logger(std::move(lg));

    if (s.ShowTimestamps()) {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    } else {
        spdlog::set_pattern("[%^%l%$] %v");
    }

    spdlog::flush_on(spdlog::level::debug);  // Flush more aggressively for debugging
    
    // Test that logging actually works
    spdlog::info("=== PrintScreen Logger Initialized ===");
    spdlog::info("Log file: Documents/My Games/Skyrim Special Edition/SKSE/Printscreen.log");
    spdlog::default_logger()->flush();  // Force immediate flush
    
    OutputDebugStringA("PrintScreen: SetupLog() completed\n");
}

void SetLevel(spdlog::level::level_enum level)
{
    spdlog::set_level(level);
}

} // namespace logger
