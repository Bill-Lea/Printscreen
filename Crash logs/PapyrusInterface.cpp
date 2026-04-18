#include <Windows.h>
#include "PCH.h"
#include <SKSE/API.h>
#include "PapyrusInterface.h"
#include "ScreenCapture.h"
#include "logger.h"
#include "stringutils.h"

namespace PrintScreenPapyrus {

// ============================================================
// Global State
// ============================================================

std::atomic<bool> g_isStarting(false);
std::atomic<bool> g_isRunning(false);
std::atomic<bool> g_cancelFlag(false);
std::atomic<bool> g_resultReady(false);
std::atomic<bool> g_initialized(false);
std::mutex g_resultMutex;
std::string g_result("Ready");
std::atomic<bool> g_operationInProgress(false);

// Main thread identification
static std::atomic<DWORD> g_mainThreadId{0};

// ============================================================
// INITIALIZATION
// ============================================================

void InitializeState() {
    // Guard against multiple initialization
    static bool alreadyInitialized = false;
    if (alreadyInitialized) {
        logger::debug("InitializeState: Already initialized, skipping");
        return;
    }
    alreadyInitialized = true;
    
    g_mainThreadId.store(::GetCurrentThreadId(), std::memory_order_release);
    g_initialized.store(true, std::memory_order_release);
    logger::info("InitializeState: Main thread ID = {}", g_mainThreadId.load());
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

std::string ToLowerCase(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool IsValidPath(const std::string& pathStr) {
    if (pathStr.empty()) {
        return false;
    }
    try {
        std::filesystem::path p(pathStr);
        return !p.empty();
    } catch (...) {
        return false;
    }
}

// ============================================================
// WORKER THREAD
// ============================================================

void CaptureWorkerThread(std::string basePath, std::string imageType, float jpgCompression,
    std::string compressionMode, float gifMultiFrameDuration, float gifFPS,
    int gifLoopCount, int gifCompression, int optimize)
{
    logger::info("CaptureWorkerThread: Starting");
    logger::info("  basePath='{}', imageType='{}', jpgCompression={}", basePath, imageType, jpgCompression);
    logger::info("  compressionMode='{}', duration={}, fps={}", compressionMode, gifMultiFrameDuration, gifFPS);
    logger::info("  loopCount={}, compression={}, optimize={}", gifLoopCount, gifCompression, optimize);

    std::string result;
    
    try {
        std::string lowerType = ToLowerCase(imageType);
        logger::info("CaptureWorkerThread: Processing format '{}'", lowerType);

        if (lowerType == "gif") {
            result = PerformGifCapture(basePath, imageType, jpgCompression, compressionMode,
                gifMultiFrameDuration, gifFPS, gifLoopCount, gifCompression, optimize);
        }
        else if (lowerType == "apng" || lowerType == "agif") {
            // AGIF is treated as APNG (Animated PNG)
            logger::info("CaptureWorkerThread: Routing '{}' to APNG capture", lowerType);
            result = PerformAPNGCapture(basePath, imageType, gifFPS, gifMultiFrameDuration,
                gifLoopCount, gifCompression, optimize);
        }
        else {
            result = PerformImageCapture(basePath, imageType, jpgCompression, compressionMode);
        }
    }
    catch (const std::exception& e) {
        result = std::string("Error: ") + e.what();
        logger::error("CaptureWorkerThread: Exception - {}", e.what());
    }
    catch (...) {
        result = "Error: Unknown exception during capture";
        logger::error("CaptureWorkerThread: Unknown exception");
    }

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = result;
        g_resultReady.store(true, std::memory_order_release);
    }

    g_isRunning.store(false, std::memory_order_release);
    g_operationInProgress.store(false, std::memory_order_release);
    
    logger::info("CaptureWorkerThread: Complete - {}", result);
}

// ============================================================
// PAPYRUS-EXPOSED FUNCTIONS
// ============================================================

std::string TakePhoto(RE::StaticFunctionTag*, std::string basePath, std::string imageType,
    float jpgCompression, std::string compressionMode, float gifMultiFrameDuration,
    float gifFPS, int gifLoopCount, int gifCompression, int optimize)
{
    logger::info("=== PAPYRUS TakePhoto CALLED ===");
    logger::info("  basePath: '{}'", basePath);
    logger::info("  imageType: '{}'", imageType);
    logger::info("  jpgCompression: {}", jpgCompression);
    logger::info("  compressionMode: '{}'", compressionMode);
    logger::info("  gifMultiFrameDuration: {}", gifMultiFrameDuration);
    logger::info("  gifFPS: {}", gifFPS);
    logger::info("  gifLoopCount: {}", gifLoopCount);
    logger::info("  gifCompression: {}", gifCompression);
    logger::info("  optimize: {}", optimize);

    if (g_isRunning.load(std::memory_order_acquire)) {
        logger::warn("TakePhoto: Already running");
        return "Error: Operation already in progress";
    }

    if (!IsValidPath(basePath)) {
        logger::error("TakePhoto: Invalid path '{}'", basePath);
        return "Error: Invalid file path";
    }

    g_cancelFlag.store(false, std::memory_order_release);
    g_resultReady.store(false, std::memory_order_release);
    g_isStarting.store(true, std::memory_order_release);
    g_isRunning.store(true, std::memory_order_release);
    g_operationInProgress.store(true, std::memory_order_release);

    std::thread worker(CaptureWorkerThread, basePath, imageType, jpgCompression,
        compressionMode, gifMultiFrameDuration, gifFPS, gifLoopCount, gifCompression, optimize);
    worker.detach();

    g_isStarting.store(false, std::memory_order_release);
    logger::info("TakePhoto: Worker thread started");
    return "Capture started";
}

std::string Get_Result(RE::StaticFunctionTag*)
{
    std::lock_guard<std::mutex> lock(g_resultMutex);
    return g_result;
}

std::string Cancel(RE::StaticFunctionTag*)
{
    logger::info("=== PAPYRUS Cancel CALLED ===");
    
    if (!g_isRunning.load(std::memory_order_acquire)) {
        return "Error: No operation in progress";
    }

    g_cancelFlag.store(true, std::memory_order_release);
    logger::info("Cancel: Flag set, waiting for operation to stop...");

    return "Cancelled";
}

bool CheckPath(RE::StaticFunctionTag*, std::string path)
{
    bool valid = IsValidPath(path);
    logger::debug("CheckPath: '{}' -> {}", path, valid ? "valid" : "invalid");
    return valid;
}

std::string ForceReset(RE::StaticFunctionTag*)
{
    logger::warn("=== PAPYRUS ForceReset CALLED ===");
    
    g_cancelFlag.store(false, std::memory_order_release);
    g_isStarting.store(false, std::memory_order_release);
    g_isRunning.store(false, std::memory_order_release);
    g_operationInProgress.store(false, std::memory_order_release);
    g_resultReady.store(false, std::memory_order_release);
    
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Reset";
    }
    
    return "State forcibly reset";
}

std::string ResetState(RE::StaticFunctionTag*)
{
    logger::info("=== PAPYRUS ResetState CALLED ===");
    
    if (g_isRunning.load(std::memory_order_acquire)) {
        return "Error: Cannot reset while operation is running";
    }

    g_cancelFlag.store(false, std::memory_order_release);
    g_resultReady.store(false, std::memory_order_release);
    
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Ready";
    }
    
    return "State reset";
}

// ============================================================
// CAPTURE IMPLEMENTATIONS
// ============================================================

std::string PerformGifCapture(const std::string& basePath, const std::string& imageType,
    float jpgCompression, const std::string& compressionMode, float gifDuration,
    float gifFPS, int gifLoopCount, int gifCompression, int optimize) {

    logger::info("PerformGifCapture: basePath='{}', type='{}', duration={}, fps={}, loops={}, compression={}, optimize={}",
        basePath, imageType, gifDuration, gifFPS, gifLoopCount, gifCompression, optimize);

    // Calculate and log expected frame count
    const double fps = std::clamp(static_cast<double>(gifFPS), 1.0, 30.0);
    const int totalFrames = std::max(1, static_cast<int>(std::round(gifDuration * fps)));
    logger::info("PerformGifCapture: Will capture {} frames (duration {} * fps {})", totalFrames, gifDuration, fps);

    ScreenCapture::CaptureParams params;
    params.basePath = std::wstring(basePath.begin(), basePath.end());
    params.format = ScreenCapture::ImageFormat::GIF;
    params.gifDuration = gifDuration;
    params.gifFPS = gifFPS;
    params.gifLoopCount = gifLoopCount;
    params.gifCompression = gifCompression;
    params.gifOptimize = optimize;
    params.cancelFlag = &g_cancelFlag;

    auto result = ScreenCapture::CaptureGIF(params);

    if (result.success) {
        std::string filepath(result.filepath.begin(), result.filepath.end());
        logger::info("PerformGifCapture: Success - {}", filepath);
        return "CALLBACK_SAVED: " + filepath;
    } else {
        logger::error("PerformGifCapture: Failed - {}", result.message);
        return "CALLBACK_ERROR: " + result.message;
    }
}

std::string PerformAPNGCapture(const std::string& basePath, const std::string& imageType,
    float apngFPS, float apngDuration, int apngLoopCount, int apngCompression, int optimize) {

    logger::info("PerformAPNGCapture: basePath='{}', type='{}', fps={}, duration={}, loops={}, compression={}, optimize={}",
        basePath, imageType, apngFPS, apngDuration, apngLoopCount, apngCompression, optimize);

    // Calculate and log expected frame count
    const double fps = std::clamp(static_cast<double>(apngFPS), 1.0, 60.0);
    const int totalFrames = std::max(1, static_cast<int>(std::round(apngDuration * fps)));
    logger::info("PerformAPNGCapture: Will capture {} frames (duration {} * fps {})", totalFrames, apngDuration, fps);

    ScreenCapture::CaptureParams params;
    params.basePath = std::wstring(basePath.begin(), basePath.end());
    params.format = ScreenCapture::ImageFormat::APNG;
    params.apngFPS = apngFPS;
    params.apngDuration = apngDuration;
    params.apngLoopCount = apngLoopCount;
    params.apngCompression = apngCompression;
    params.apngOptimize = optimize;
    params.cancelFlag = &g_cancelFlag;

    auto result = ScreenCapture::CaptureAPNG(params);

    if (result.success) {
        std::string filepath(result.filepath.begin(), result.filepath.end());
        logger::info("PerformAPNGCapture: Success - {}", filepath);
        return "CALLBACK_SAVED: " + filepath;
    } else {
        logger::error("PerformAPNGCapture: Failed - {}", result.message);
        return "CALLBACK_ERROR: " + result.message;
    }
}

std::string PerformImageCapture(const std::string& basePath, const std::string& imageType,
    float jpgCompression, const std::string& compressionMode) {

    logger::info("PerformImageCapture: basePath='{}', type='{}', jpgCompression={}, compressionMode='{}'",
        basePath, imageType, jpgCompression, compressionMode);

    ScreenCapture::CaptureParams params;
    params.basePath = std::wstring(basePath.begin(), basePath.end());
    params.format = ScreenCapture::StringToImageFormat(imageType);
    params.jpegQuality = jpgCompression;
    params.cancelFlag = &g_cancelFlag;

    if (params.format == ScreenCapture::ImageFormat::TIF) {
        params.tiffMode = ScreenCapture::StringToTiffCompression(compressionMode);
    } else if (params.format == ScreenCapture::ImageFormat::DDS) {
        params.ddsMode = ScreenCapture::StringToDDSCompression(compressionMode);
    }

    auto result = ScreenCapture::CaptureScreen(params);

    if (result.success) {
        std::string filepath(result.filepath.begin(), result.filepath.end());
        logger::info("PerformImageCapture: Success - {}", filepath);
        return "CALLBACK_SAVED: " + filepath;
    } else {
        logger::error("PerformImageCapture: Failed - {}", result.message);
        return "CALLBACK_ERROR: " + result.message;
    }
}

// ============================================================
// PAPYRUS REGISTRATION
// ============================================================

bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm) {
    // Guard against multiple registrations
    static bool alreadyRegistered = false;
    if (alreadyRegistered) {
        logger::warn("RegisterFunctions: Already registered, skipping duplicate call");
        return true;
    }
    alreadyRegistered = true;
    
    logger::info("=== REGISTERING PAPYRUS FUNCTIONS ===");
    constexpr auto scriptName = "Printscreen_Formula_script";
    
    // Note: InitializeState() is called in plugin.cpp, not here
    
    bool allSucceeded = true;
    int successCount = 0;
    int failCount = 0;

    auto registerFunc = [&](const char* funcName, auto func) -> bool {
        try { 
            vm->RegisterFunction(funcName, scriptName, func); 
            logger::info("  Registered: {}", funcName);
            successCount++; 
            return true; 
        }
        catch (const std::exception& e) { 
            logger::error("  FAILED to register {}: {}", funcName, e.what());
            failCount++; 
            return false; 
        }
        catch (...) { 
            logger::error("  FAILED to register {}: unknown exception", funcName);
            failCount++; 
            return false; 
        }
    };

    // Core capture functions
    allSucceeded &= registerFunc("CheckPath", CheckPath);
    allSucceeded &= registerFunc("TakePhoto", TakePhoto);
    allSucceeded &= registerFunc("Get_Result", Get_Result);
    allSucceeded &= registerFunc("Cancel", Cancel);
    allSucceeded &= registerFunc("ForceReset", ForceReset);
    allSucceeded &= registerFunc("ResetState", ResetState);

    logger::info("=== {} FUNCTIONS REGISTERED ({} failed) ===", successCount, failCount);
    
    if (failCount > 0) {
        logger::error("Some Papyrus functions failed to register - check script name matches: '{}'", scriptName);
    }
    
    return allSucceeded;
}

} // namespace PrintScreenPapyrus
