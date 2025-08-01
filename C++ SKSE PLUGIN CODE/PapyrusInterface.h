#pragma once

#include "PCH.h"
#include <string>
#include <atomic>
#include <mutex>

namespace PrintScreenPapyrus {
    
    // Function declarations for Papyrus registration
    std::string TakePhoto(
        RE::StaticFunctionTag*,
        std::string basePath,
        std::string imageType,
        float jpgCompression,
        std::string compressionMode,
        float gifMultiFrameDuration
    );
    
    std::string Get_Result(RE::StaticFunctionTag*);
    std::string Cancel(RE::StaticFunctionTag*);
    bool CheckPath(RE::StaticFunctionTag*, std::string path);
    std::string ForceReset(RE::StaticFunctionTag*);
    std::string ResetState(RE::StaticFunctionTag*);
    
    // Worker thread function
    void CaptureWorkerThread(
        std::string basePath,
        std::string imageType,
        float jpgCompression,
        std::string compressionMode,
        float gifMultiFrameDuration
    );
    
    // State management functions
    void InitializeState();
    
    // Registration function
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);
    
    // Helper functions
    std::string ToLowerCase(const std::string& str);
    bool IsValidPath(const std::string& pathStr);
    std::string GenerateTimestampedFilename(const std::string& basePath, const std::string& extension);
    
    // Capture implementation functions
    std::string PerformGifCapture(const std::string& basePath, const std::string& imageType, 
                                 float jpgCompression, const std::string& compressionMode, float gifDuration);
    std::string PerformImageCapture(const std::string& basePath, const std::string& imageType, 
                                   float jpgCompression, const std::string& compressionMode);
    
}