#pragma once

#include "PCH.h"
#include <string>
#include <atomic>
#include <mutex>

namespace PrintScreenPapyrus {

    // ============================================================
    // Core capture functions (Papyrus-exposed)
    // ============================================================

    std::string TakePhoto(RE::StaticFunctionTag*, std::string basePath, std::string imageType,
        float jpgCompression, std::string compressionMode, float gifMultiFrameDuration,
        float gifFPS, int gifLoopCount, int gifCompression, int optimize);
    
    std::string Get_Result(RE::StaticFunctionTag*);
    std::string Cancel(RE::StaticFunctionTag*);
    bool CheckPath(RE::StaticFunctionTag*, std::string path);
    std::string ForceReset(RE::StaticFunctionTag*);
    std::string ResetState(RE::StaticFunctionTag*);

    // ============================================================
    // Worker threads (internal)
    // ============================================================

    void CaptureWorkerThread(std::string basePath, std::string imageType, float jpgCompression,
        std::string compressionMode, float gifMultiFrameDuration, float gifFPS,
        int gifLoopCount, int gifCompression, int optimize);

    // ============================================================
    // Utilities (internal)
    // ============================================================

    void InitializeState();
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);
    std::string ToLowerCase(const std::string& str);
    bool IsValidPath(const std::string& pathStr);

    // ============================================================
    // Capture implementations (internal)
    // ============================================================

    std::string PerformGifCapture(const std::string& basePath, const std::string& imageType,
        float jpgCompression, const std::string& compressionMode, float gifDuration,
        float gifFPS, int gifLoopCount, int gifCompression, int optimize);
    std::string PerformAPNGCapture(const std::string& basePath, const std::string& imageType,
        float apngFPS, float apngDuration, int apngLoopCount, int apngCompression, int optimize);
    std::string PerformImageCapture(const std::string& basePath, const std::string& imageType,
        float jpgCompression, const std::string& compressionMode);

    // ============================================================
    // External state access
    // ============================================================

    extern std::atomic<bool> g_cancelFlag;
}
