// config.cpp
#include "pch.h"
#include "Config.h"
#include <Windows.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <set>
#include <cwctype>
#include "stringutils.h"

namespace {
    std::wstring skse_folder() {
        wchar_t* up = nullptr; size_t len = 0;
        _wdupenv_s(&up, &len, L"USERPROFILE");
        std::wstring home = up ? up : L"";
        free(up);
        return (std::filesystem::path(home) /
                L"Documents" / L"My Games" / L"Skyrim Special Edition" / L"SKSE").wstring();
    }
    std::wstring ini_candidate_legacy() {
        return (std::filesystem::path(skse_folder()) / L"Plugins" / L"Printscreen_Log.ini").wstring();
    }
    std::wstring ini_candidate_simple() {
        return (std::filesystem::path(skse_folder()) / L"PrintScreen.ini").wstring();
    }
    bool exists(const std::wstring& p) {
        return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    std::wstring wget(const wchar_t* sec, const wchar_t* key,
                      const std::wstring& def, const std::wstring& ini) {
        wchar_t buf[1024]{};
        DWORD n = GetPrivateProfileStringW(sec, key, def.c_str(), buf, 1024, ini.c_str());
        return std::wstring(buf, n);
    }
    uint32_t wget_u32(const wchar_t* s, const wchar_t* k, uint32_t d, const std::wstring& ini) {
        wchar_t def[32]; _snwprintf_s(def, _TRUNCATE, L"%u", d);
        std::wstring v = wget(s, k, def, ini);
        return v.empty() ? d : static_cast<uint32_t>(std::wcstoul(v.c_str(), nullptr, 10));
    }
    bool wget_bool(const wchar_t* s, const wchar_t* k, bool d, const std::wstring& ini) {
        std::wstring v = wget(s, k, d ? L"1" : L"0", ini);
        std::string l = util::wstring_to_utf8(v);
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return (l == "1" || l == "true" || l == "yes" || l == "on");
    }

    // Write helpers
    void wput(const wchar_t* sec, const wchar_t* key, 
              const std::wstring& val, const std::wstring& ini) {
        WritePrivateProfileStringW(sec, key, val.c_str(), ini.c_str());
    }
    void wput_u32(const wchar_t* sec, const wchar_t* key, 
                  uint32_t val, const std::wstring& ini) {
        wchar_t buf[32];
        _snwprintf_s(buf, _TRUNCATE, L"%u", val);
        WritePrivateProfileStringW(sec, key, buf, ini.c_str());
    }
    void wput_bool(const wchar_t* sec, const wchar_t* key, 
                   bool val, const std::wstring& ini) {
        WritePrivateProfileStringW(sec, key, val ? L"true" : L"false", ini.c_str());
    }

    std::wstring trim_ws(const std::wstring& s) {
        size_t start = 0, end = s.size();
        while (start < end && std::iswspace(s[start])) ++start;
        while (end > start && std::iswspace(s[end - 1])) --end;
        return s.substr(start, end - start);
    }

    std::wstring to_lower_ws(std::wstring s) {
        for (auto& ch : s) ch = std::towlower(ch);
        return s;
    }

    const std::set<std::wstring> g_validKeys = {
        L"loglevel", L"consoleoutput", L"fileoutput", L"showtimestamps", L"maxlogfilesizemb",
        L"parallelcompression", L"compressionthreads",
        L"logcaptureprogress", L"logtiminginfo",
        L"level", L"file", L"console", L"timestamps", L"logpath"
    };

    std::vector<std::string> validateIniKeys(const std::wstring& iniPath) {
        std::vector<std::string> unknownKeys;
        std::wifstream fin(iniPath);
        if (!fin.is_open()) return unknownKeys;

        std::wstring line;
        while (std::getline(fin, line)) {
            auto commentPos = line.find(L';');
            if (commentPos != std::wstring::npos) line = line.substr(0, commentPos);
            commentPos = line.find(L'#');
            if (commentPos != std::wstring::npos) line = line.substr(0, commentPos);

            line = trim_ws(line);
            if (line.empty()) continue;
            if (line.front() == L'[' && line.back() == L']') continue;

            auto eqPos = line.find(L'=');
            if (eqPos == std::wstring::npos) continue;

            std::wstring key = trim_ws(line.substr(0, eqPos));
            std::wstring keyLower = to_lower_ws(key);

            if (g_validKeys.find(keyLower) == g_validKeys.end()) {
                unknownKeys.push_back(util::wstring_to_utf8(key));
            }
        }
        return unknownKeys;
    }
} // end anonymous namespace

namespace Config {

Settings g_settings{};
std::vector<std::string> g_unknownKeys{};

static int to_level(std::wstring v) {
    std::string s = util::wstring_to_utf8(v);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "none")  return 0;
    if (s == "error") return 1;
    if (s == "warn")  return 2;
    if (s == "info")  return 3;
    if (s == "debug") return 4;
    if (s == "trace") return 5;
    try { return std::clamp(std::stoi(s), 0, 5); } catch (...) { return 3; }
}

static std::wstring wget_legacy(const wchar_t* key, const std::wstring& ini) {
    wchar_t buf[1024]{};
    const wchar_t* sentinel = L"\x01\x02_NOTFOUND_\x02\x01";
    DWORD n = GetPrivateProfileStringW(nullptr, key, sentinel, buf, 1024, ini.c_str());
    std::wstring result(buf, n);
    if (result == sentinel) return L"";
    return result;
}

bool Initialize()
{
    const std::wstring legacy = ini_candidate_legacy();
    const std::wstring simple = ini_candidate_simple();
    const std::wstring path   = exists(legacy) ? legacy : (exists(simple) ? simple : legacy);
    g_settings.iniPath = path;

    if (!exists(path)) {
        return false;
    }

    g_unknownKeys = validateIniKeys(path);
    std::wstring logLevelVal = wget(L"Logging", L"LogLevel", L"", path);
    bool useLegacyFormat = logLevelVal.empty();

    if (useLegacyFormat) {
        std::wstring levelVal = wget_legacy(L"Level", path);
        if (!levelVal.empty()) g_settings.logLevel = to_level(levelVal);

        std::wstring fileVal = wget_legacy(L"File", path);
        if (!fileVal.empty()) {
            std::string l = util::wstring_to_utf8(fileVal);
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            g_settings.fileOutput = (l == "1" || l == "true" || l == "yes" || l == "on");
        }

        std::wstring consoleVal = wget_legacy(L"Console", path);
        if (!consoleVal.empty()) {
            std::string l = util::wstring_to_utf8(consoleVal);
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            g_settings.consoleOutput = (l == "1" || l == "true" || l == "yes" || l == "on");
        }

        std::wstring timestampsVal = wget_legacy(L"Timestamps", path);
        if (!timestampsVal.empty()) {
            std::string l = util::wstring_to_utf8(timestampsVal);
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            g_settings.showTimestamps = (l == "1" || l == "true" || l == "yes" || l == "on");
        }
    } else {
        g_settings.logLevel         = to_level(logLevelVal);
        g_settings.consoleOutput    = wget_bool(L"Logging", L"ConsoleOutput",    g_settings.consoleOutput,    path);
        g_settings.fileOutput       = wget_bool(L"Logging", L"FileOutput",       g_settings.fileOutput,       path);
        g_settings.showTimestamps   = wget_bool(L"Logging", L"ShowTimestamps",   g_settings.showTimestamps,   path);
        g_settings.maxLogFileSizeMB = wget_u32 (L"Logging", L"MaxLogFileSizeMB", g_settings.maxLogFileSizeMB, path);

        g_settings.parallelCompression = wget_bool(L"Performance", L"ParallelCompression", g_settings.parallelCompression, path);
        g_settings.compressionThreads  = wget_u32 (L"Performance", L"CompressionThreads",  g_settings.compressionThreads,  path);

        g_settings.logCaptureProgress  = wget_bool(L"Capture", L"LogCaptureProgress", g_settings.logCaptureProgress, path);
        g_settings.logTimingInfo       = wget_bool(L"Capture", L"LogTimingInfo",      g_settings.logTimingInfo,      path);
    }

    return true;
}

bool Save() {
    const std::wstring path = g_settings.iniPath.empty() 
        ? ini_candidate_simple() 
        : g_settings.iniPath;
    
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    
    wput(L"Logging", L"LogLevel", util::utf8_to_wstring(LogLevelToString(g_settings.logLevel)), path);
    wput_bool(L"Logging", L"ConsoleOutput", g_settings.consoleOutput, path);
    wput_bool(L"Logging", L"FileOutput", g_settings.fileOutput, path);
    wput_bool(L"Logging", L"ShowTimestamps", g_settings.showTimestamps, path);
    wput_u32(L"Logging", L"MaxLogFileSizeMB", g_settings.maxLogFileSizeMB, path);
    
    wput_bool(L"Performance", L"ParallelCompression", g_settings.parallelCompression, path);
    wput_u32(L"Performance", L"CompressionThreads", g_settings.compressionThreads, path);
    
    wput_bool(L"Capture", L"LogCaptureProgress", g_settings.logCaptureProgress, path);
    wput_bool(L"Capture", L"LogTimingInfo", g_settings.logTimingInfo, path);
    
    return true;
}

bool SaveDefaults() {
    g_settings = Settings{};  // Reset to defaults
    g_settings.iniPath = ini_candidate_simple();
    return Save();
}

const std::vector<std::string>& GetUnknownKeys() {
    return g_unknownKeys;
}

} // namespace Config