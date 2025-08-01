#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>  // ADDED: Required for std::transform
#include <mutex>      // ADDED: For thread safety during reload
#include <iostream>   // ADDED: For debug output
#include <ctime>      // ADDED: For timestamp in debug log
#include <iomanip>    // ADDED: For timestamp formatting

// Log level constants
constexpr int LOG_LEVEL_ERROR = 1;
constexpr int LOG_LEVEL_WARN = 2;
constexpr int LOG_LEVEL_INFO = 3;
constexpr int LOG_LEVEL_DEBUG = 4;
constexpr int LOG_LEVEL_TRACE = 5;

namespace Config {
    
    class Settings {
    private:
        int m_logLevel = LOG_LEVEL_INFO;
        bool m_consoleOutput = true;
        bool m_fileOutput = true;
        bool m_showTimestamps = true;
        bool m_logCaptureProgress = true;
        bool m_logTimingInfo = false;
        std::string m_configPath;
        
        // ADDED: For runtime reloading
        mutable std::mutex m_configMutex;
        mutable std::filesystem::file_time_type m_lastWriteTime;
        
        // ADDED: Debug logging to file
        void DebugLog(const std::string& message) const {
            static std::string debugLogPath;
            if (debugLogPath.empty()) {
                char* userProfile = nullptr;
                size_t len = 0;
                if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile != nullptr) {
                    debugLogPath = std::string(userProfile) + "/Documents/My Games/Skyrim Special Edition/SKSE/Plugins/config_debug.log";
                    free(userProfile);
                } else {
                    debugLogPath = "C:/temp/config_debug.log";
                }
            }
            
            try {
                std::ofstream debugFile(debugLogPath, std::ios::app);
                if (debugFile.is_open()) {
                    auto now = std::time(nullptr);
                    auto tm = *std::localtime(&now);
                    debugFile << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << message << std::endl;
                    debugFile.close();
                }
            } catch (...) {
                // Silent failure for debug logging
            }
        }
        
        // Helper function to trim whitespace
        std::string Trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }
        
        // Helper function to convert string to bool
        bool StringToBool(const std::string& str) {
            std::string lower = str;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return (lower == "true" || lower == "1" || lower == "yes" || lower == "on");
        }
        
        // Parse a single INI line - IMPROVED with debug to file
        void ParseLine(const std::string& line) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                return;
            }
            
            // Skip section headers [Section]
            if (line[0] == '[') {
                return;
            }
            
            // Find the = separator
            size_t equalPos = line.find('=');
            if (equalPos == std::string::npos) {
                return;
            }
            
            std::string key = Trim(line.substr(0, equalPos));
            std::string value = Trim(line.substr(equalPos + 1));
            
            // DEBUG: Print what we're parsing to file
            DebugLog("Parsing: '" + key + "' = '" + value + "'");
            
            // Parse known configuration values
            if (key == "LogLevel") {
                try {
                    int level = std::stoi(value);
                    DebugLog("LogLevel parsed as: " + std::to_string(level));
                    if (level >= LOG_LEVEL_ERROR && level <= LOG_LEVEL_TRACE) {
                        DebugLog("Setting LogLevel from " + std::to_string(m_logLevel) + " to " + std::to_string(level));
                        m_logLevel = level;
                    } else {
                        DebugLog("LogLevel " + std::to_string(level) + " is out of range");
                    }
                } catch (...) {
                    DebugLog("Failed to parse LogLevel: " + value);
                }
            }
            else if (key == "ConsoleOutput") {
                bool newValue = StringToBool(value);
                DebugLog("Setting ConsoleOutput from " + std::to_string(m_consoleOutput) + " to " + std::to_string(newValue));
                m_consoleOutput = newValue;
            }
            else if (key == "FileOutput") {
                bool newValue = StringToBool(value);
                DebugLog("Setting FileOutput from " + std::to_string(m_fileOutput) + " to " + std::to_string(newValue));
                m_fileOutput = newValue;
            }
            else if (key == "ShowTimestamps") {
                bool newValue = StringToBool(value);
                DebugLog("Setting ShowTimestamps from " + std::to_string(m_showTimestamps) + " to " + std::to_string(newValue));
                m_showTimestamps = newValue;
            }
            else if (key == "LogCaptureProgress") {
                bool newValue = StringToBool(value);
                DebugLog("Setting LogCaptureProgress from " + std::to_string(m_logCaptureProgress) + " to " + std::to_string(newValue));
                m_logCaptureProgress = newValue;
            }
            else if (key == "LogTimingInfo") {
                bool newValue = StringToBool(value);
                DebugLog("Setting LogTimingInfo from " + std::to_string(m_logTimingInfo) + " to " + std::to_string(newValue));
                m_logTimingInfo = newValue;
            }
            else {
                DebugLog("Unknown key: " + key);
            }
        }
        
        // IMPROVED: Internal load function with debug to file
        bool LoadInternal() {
            DebugLog("=== CONFIG LOAD START ===");
            
            // Get config path using std::filesystem for proper path handling
            char* userProfile = nullptr;
            size_t len = 0;
            if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile != nullptr) {
                std::filesystem::path userPath(userProfile);
                std::filesystem::path configPath = userPath / "Documents" / "My Games" / "Skyrim Special Edition" / "SKSE" / "Plugins" / "Printscreen_Log.ini";
                m_configPath = configPath.string();
                free(userProfile);
            } else {
                m_configPath = "C:/temp/Printscreen_Log.ini";
            }
            
            DebugLog("Looking for config file: " + m_configPath);
            
            // Check if file exists
            std::error_code ec;
            bool exists = std::filesystem::exists(m_configPath, ec);
            DebugLog("File exists: " + std::string(exists ? "YES" : "NO"));
            
            if (ec) {
                DebugLog("File existence check error: " + ec.message());
            }
            
            if (exists) {
                auto size = std::filesystem::file_size(m_configPath, ec);
                if (!ec) {
                    DebugLog("File size: " + std::to_string(size) + " bytes");
                } else {
                    DebugLog("Could not get file size: " + ec.message());
                }
            }
            
            // Try to read the INI file
            std::ifstream configFile(m_configPath);
            if (!configFile.is_open()) {
                DebugLog("FAILED to open config file!");
                DebugLog("Using default values");
                DebugLog("=== CONFIG LOAD FAILED ===");
                return false;
            }
            
            DebugLog("File opened successfully, parsing...");
            
            std::string line;
            int lineCount = 0;
            while (std::getline(configFile, line)) {
                lineCount++;
                DebugLog("Line " + std::to_string(lineCount) + ": " + line);
                ParseLine(line);
            }
            
            configFile.close();
            DebugLog("Finished parsing " + std::to_string(lineCount) + " lines");
            
            // Report final values
            DebugLog("Final LogLevel: " + std::to_string(m_logLevel));
            DebugLog("Final ConsoleOutput: " + std::to_string(m_consoleOutput));
            DebugLog("Final FileOutput: " + std::to_string(m_fileOutput));
            DebugLog("Final ShowTimestamps: " + std::to_string(m_showTimestamps));
            DebugLog("Final LogCaptureProgress: " + std::to_string(m_logCaptureProgress));
            DebugLog("Final LogTimingInfo: " + std::to_string(m_logTimingInfo));
            
            // ADDED: Update file timestamp for change detection
            m_lastWriteTime = std::filesystem::last_write_time(m_configPath, ec);
            
            DebugLog("=== CONFIG LOAD SUCCESS ===");
            return true;
        }
        
        // ADDED: Check if config file has been modified
        bool HasConfigChanged() const {
            std::error_code ec;
            auto currentWriteTime = std::filesystem::last_write_time(m_configPath, ec);
            if (ec) {
                return false; // File doesn't exist or can't be accessed
            }
            return currentWriteTime != m_lastWriteTime;
        }
        
    public:
        Settings() = default;
        
        // IMPROVED: Getters that check for config changes
        int GetLogLevel() const { 
            CheckAndReload();
            std::lock_guard<std::mutex> lock(m_configMutex);
            return m_logLevel; 
        }
        
        bool IsConsoleOutputEnabled() const { 
            CheckAndReload();
            std::lock_guard<std::mutex> lock(m_configMutex);
            return m_consoleOutput; 
        }
        
        bool IsFileOutputEnabled() const { 
            CheckAndReload();
            std::lock_guard<std::mutex> lock(m_configMutex);
            return m_fileOutput; 
        }
        
        bool ShowTimestamps() const { 
            CheckAndReload();
            std::lock_guard<std::mutex> lock(m_configMutex);
            return m_showTimestamps; 
        }
        
        bool IsLogCaptureProgressEnabled() const { 
            CheckAndReload();
            std::lock_guard<std::mutex> lock(m_configMutex);
            return m_logCaptureProgress; 
        }
        
        bool IsLogTimingInfoEnabled() const { 
            CheckAndReload();
            std::lock_guard<std::mutex> lock(m_configMutex);
            return m_logTimingInfo; 
        }
        
        std::string GetConfigPath() const { 
            std::lock_guard<std::mutex> lock(m_configMutex);
            return m_configPath; 
        }
        
        // Setters
        void SetLogLevel(int level) { 
            std::lock_guard<std::mutex> lock(m_configMutex);
            if (level >= LOG_LEVEL_ERROR && level <= LOG_LEVEL_TRACE) {
                m_logLevel = level; 
            }
        }
        void SetConsoleOutput(bool enabled) { 
            std::lock_guard<std::mutex> lock(m_configMutex);
            m_consoleOutput = enabled; 
        }
        void SetFileOutput(bool enabled) { 
            std::lock_guard<std::mutex> lock(m_configMutex);
            m_fileOutput = enabled; 
        }
        void SetShowTimestamps(bool enabled) { 
            std::lock_guard<std::mutex> lock(m_configMutex);
            m_showTimestamps = enabled; 
        }
        void SetLogCaptureProgress(bool enabled) { 
            std::lock_guard<std::mutex> lock(m_configMutex);
            m_logCaptureProgress = enabled; 
        }
        void SetLogTimingInfo(bool enabled) { 
            std::lock_guard<std::mutex> lock(m_configMutex);
            m_logTimingInfo = enabled; 
        }
        
        // ADDED: Method to check and reload config if changed
        void CheckAndReload() const {
            if (HasConfigChanged()) {
                std::lock_guard<std::mutex> lock(m_configMutex);
                // Double-check after acquiring lock
                if (HasConfigChanged()) {
                    // Need to cast away const since we're modifying state
                    const_cast<Settings*>(this)->LoadInternal();
                }
            }
        }
        
        // ADDED: Manual reload method
        bool Reload() {
            std::lock_guard<std::mutex> lock(m_configMutex);
            return LoadInternal();
        }
        
        // Configuration loading/saving
        bool Load() {
            std::lock_guard<std::mutex> lock(m_configMutex);
            return LoadInternal();
        }
        
        bool Save() const {
            std::lock_guard<std::mutex> lock(m_configMutex);
            std::ofstream configFile(m_configPath);
            if (!configFile.is_open()) {
                return false;
            }
            
            configFile << "; Printscreen Plugin Configuration\n";
            configFile << "[Logging]\n";
            configFile << "LogLevel=" << m_logLevel << "\n";
            configFile << "ConsoleOutput=" << (m_consoleOutput ? "1" : "0") << "\n";
            configFile << "FileOutput=" << (m_fileOutput ? "1" : "0") << "\n";
            configFile << "ShowTimestamps=" << (m_showTimestamps ? "1" : "0") << "\n";
            configFile << "\n";
            configFile << "[Performance]\n";
            configFile << "ParallelCompression=1\n";
            configFile << "CompressionThreads=0\n";
            configFile << "\n";
            configFile << "[Capture]\n";
            configFile << "LogCaptureProgress=" << (m_logCaptureProgress ? "1" : "0") << "\n";
            configFile << "LogTimingInfo=" << (m_logTimingInfo ? "1" : "0") << "\n";
            
            configFile.close();
            
            // ADDED: Update timestamp after save
            std::error_code ec;
            const_cast<Settings*>(this)->m_lastWriteTime = std::filesystem::last_write_time(m_configPath, ec);
            
            return true;
        }
        
        static std::string LogLevelToString(int level) {
            switch (level) {
                case LOG_LEVEL_ERROR: return "ERROR";
                case LOG_LEVEL_WARN: return "WARN";
                case LOG_LEVEL_INFO: return "INFO";
                case LOG_LEVEL_DEBUG: return "DEBUG";
                case LOG_LEVEL_TRACE: return "TRACE";
                default: return "UNKNOWN";
            }
        }
    };
    
    // Global settings instance
    extern Settings g_settings;
    
    // Initialize configuration system
    bool Initialize();
    
} // namespace Config