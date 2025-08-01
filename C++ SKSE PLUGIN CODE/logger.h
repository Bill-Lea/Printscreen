#pragma once

#include "Config.h"
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <mutex>

namespace logger {
    
    class Logger {
    private:
        static std::mutex s_logMutex;
        static std::string s_logFilePath;
        static bool s_initialized;
        
        static std::string GetTimestamp() {
            if (!Config::g_settings.ShowTimestamps()) {
                return "";
            }
            
            auto now = std::time(nullptr);
            auto tm = *std::localtime(&now);
            
            std::stringstream ss;
            ss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] ";
            return ss.str();
        }
        
        static bool ShouldLog(int level) {
            return level <= Config::g_settings.GetLogLevel();
        }
        
        static std::string GetLevelPrefix(int level) {
            if (level == LOG_LEVEL_ERROR) return "[ERROR] ";
            if (level == LOG_LEVEL_WARN) return "[WARN]  ";
            if (level == LOG_LEVEL_INFO) return "[INFO]  ";
            if (level == LOG_LEVEL_DEBUG) return "[DEBUG] ";
            if (level == LOG_LEVEL_TRACE) return "[TRACE] ";
            return "[LOG]   ";
        }
        
        static void WriteToConsole(const std::string& message) {
            if (Config::g_settings.IsConsoleOutputEnabled()) {
                std::cout << message << std::endl;
            }
        }
        
        static void WriteToFile(const std::string& message) {
            if (!Config::g_settings.IsFileOutputEnabled()) {
                return;
            }
            
            try {
                std::ios_base::openmode mode = s_initialized ? std::ios::app : std::ios::trunc;
                
                std::ofstream logFile(s_logFilePath, mode);
                if (logFile.is_open()) {
                    logFile << message << std::endl;
                    logFile.flush();
                    
                    if (!s_initialized) {
                        s_initialized = true;
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "[LOGGER ERROR] Failed to write to log file: " << e.what() << std::endl;
                std::cout << message << std::endl;
            }
        }
        
        static void LogMessage(int level, const std::string& message) {
            if (!ShouldLog(level)) {
                return;
            }
            
            std::lock_guard<std::mutex> lock(s_logMutex);
            
            std::string fullMessage = GetTimestamp() + GetLevelPrefix(level) + message;
            
            WriteToConsole(fullMessage);
            WriteToFile(fullMessage);
        }
        
    public:
        static std::string ReplaceFirst(std::string str, const std::string& from, const std::string& to) {
            size_t start_pos = str.find(from);
            if (start_pos == std::string::npos) return str;
            str.replace(start_pos, from.length(), to);
            return str;
        }
        
        template<typename T>
        static std::string ToString(const T& value) {
            std::stringstream ss;
            ss << value;
            return ss.str();
        }
        
        static std::string FormatMessage(const std::string& format) {
            return format;
        }
        
        template<typename T, typename... Args>
        static std::string FormatMessage(const std::string& format, const T& first, const Args&... rest) {
            std::string result = ReplaceFirst(format, "{}", ToString(first));
            return FormatMessage(result, rest...);
        }
        
        static void Error(const std::string& message) {
            LogMessage(LOG_LEVEL_ERROR, message);
        }
        
        template<typename... Args>
        static void Error(const std::string& format, const Args&... args) {
            Error(FormatMessage(format, args...));
        }
        
        static void Warn(const std::string& message) {
            LogMessage(LOG_LEVEL_WARN, message);
        }
        
        template<typename... Args>
        static void Warn(const std::string& format, const Args&... args) {
            Warn(FormatMessage(format, args...));
        }
        
        static void Info(const std::string& message) {
            LogMessage(LOG_LEVEL_INFO, message);
        }
        
        template<typename... Args>
        static void Info(const std::string& format, const Args&... args) {
            Info(FormatMessage(format, args...));
        }
        
        static void Debug(const std::string& message) {
            LogMessage(LOG_LEVEL_DEBUG, message);
        }
        
        template<typename... Args>
        static void Debug(const std::string& format, const Args&... args) {
            Debug(FormatMessage(format, args...));
        }
        
        static void Trace(const std::string& message) {
            LogMessage(LOG_LEVEL_TRACE, message);
        }
        
        template<typename... Args>
        static void Trace(const std::string& format, const Args&... args) {
            Trace(FormatMessage(format, args...));
        }
        
        static void Progress(const std::string& message) {
            if (Config::g_settings.IsLogCaptureProgressEnabled()) {
                LogMessage(LOG_LEVEL_INFO, "[PROGRESS] " + message);
            }
        }
        
        template<typename... Args>
        static void Progress(const std::string& format, const Args&... args) {
            Progress(FormatMessage(format, args...));
        }
        
        static void Timing(const std::string& message) {
            if (Config::g_settings.IsLogTimingInfoEnabled()) {
                LogMessage(LOG_LEVEL_DEBUG, "[TIMING] " + message);
            }
        }
        
        template<typename... Args>
        static void Timing(const std::string& format, const Args&... args) {
            Timing(FormatMessage(format, args...));
        }
        
        static void Initialize() {
            char* userProfile = nullptr;
            size_t len = 0;
            if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile != nullptr) {
                std::string documentsPath = std::string(userProfile) + "/Documents/My Games/Skyrim Special Edition/SKSE";
                s_logFilePath = documentsPath + "/printscreen.log";
                free(userProfile);
            } else {
                s_logFilePath = "C:/temp/printscreen.log";
            }
            
            std::filesystem::path logDirPath = std::filesystem::path(s_logFilePath).parent_path();
            std::error_code ec;
            std::filesystem::create_directories(logDirPath, ec);
            
            if (ec) {
                std::cout << "[LOGGER ERROR] Failed to create log directory: " << ec.message() << std::endl;
                s_logFilePath = "C:/temp/printscreen.log";
                std::filesystem::create_directories("C:/temp", ec);
            }
            
            Info("=== PrintScreen Plugin Logger Initialized ===");
            Info("Log file: {}", s_logFilePath);
            Info("Log level: {}", Config::Settings::LogLevelToString(Config::g_settings.GetLogLevel()));
        }
        
        static const std::string& GetLogFilePath() {
            return s_logFilePath;
        }
    };
    
    inline void error(const std::string& message) { Logger::Error(message); }
    template<typename... Args> void error(const std::string& format, const Args&... args) { Logger::Error(format, args...); }
    
    inline void warn(const std::string& message) { Logger::Warn(message); }
    template<typename... Args> void warn(const std::string& format, const Args&... args) { Logger::Warn(format, args...); }
    
    inline void info(const std::string& message) { Logger::Info(message); }
    template<typename... Args> void info(const std::string& format, const Args&... args) { Logger::Info(format, args...); }
    
    inline void debug(const std::string& message) { Logger::Debug(message); }
    template<typename... Args> void debug(const std::string& format, const Args&... args) { Logger::Debug(format, args...); }
    
    inline void trace(const std::string& message) { Logger::Trace(message); }
    template<typename... Args> void trace(const std::string& format, const Args&... args) { Logger::Trace(format, args...); }
    
    inline void progress(const std::string& message) { Logger::Progress(message); }
    template<typename... Args> void progress(const std::string& format, const Args&... args) { Logger::Progress(format, args...); }
    
    inline void timing(const std::string& message) { Logger::Timing(message); }
    template<typename... Args> void timing(const std::string& format, const Args&... args) { Logger::Timing(format, args...); }
    
    inline void SetupLog() {
        Logger::Initialize();
    }
    
} // namespace logger