// PapyrusInterface.cpp
// Implementation of Papyrus interface functions for PrintScreen SKSE Plugin

#include "PCH.h"
#include "PapyrusInterface.h"
#include "ScreenCapture.h"
#include "logger.h"
#include "stringutils.h"

#include <thread>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cstring>

namespace PrintScreenPapyrus {

// ============================================================================
// GLOBAL STATE
// ============================================================================

std::atomic<bool> g_initialized{false};
std::atomic<bool> g_isStarting{false};
std::atomic<bool> g_isRunning{false};
std::atomic<bool> g_cancelFlag{false};
std::atomic<bool> g_resultReady{false};
std::atomic<bool> g_operationInProgress{false};
std::atomic<DWORD> g_mainThreadId{0};

std::mutex g_resultMutex;
std::string g_result = "Ready";

// ============================================================================
// INITIALIZATION
// ============================================================================

void InitializeState() {
    // Capture main thread ID - this is called from RegisterFunctions on the main thread
    if (g_mainThreadId.load(std::memory_order_acquire) == 0) {
        g_mainThreadId.store(::GetCurrentThreadId(), std::memory_order_release);
        logger::info("InitializeState: main thread id = {}", g_mainThreadId.load());
    }

    g_isStarting.store(false);
    g_isRunning.store(false);
    g_cancelFlag.store(false);
    g_resultReady.store(false);
    g_operationInProgress.store(false);

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Ready";
    }

    g_initialized.store(true);
    logger::info("PrintScreen state initialized");
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

std::string ToLowerCase(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
}

// Normalize path separators to backslash (the native Windows separator).
// This prevents mixed-slash paths like "C:/Pictures/Test\file.gif" that can
// cause ERROR_PATH_NOT_FOUND when passed to Windows file APIs.
static std::wstring NormalizePath(const std::wstring& path) {
    std::wstring normalized = path;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    return normalized;
}

bool IsValidPath(const std::string& pathStr) {
    if (pathStr.empty()) {
        logger::debug("IsValidPath: empty string");
        return false;
    }
    if (pathStr.length() < 3) {
        logger::debug("IsValidPath: path too short (< 3 chars)");
        return false;
    }

    // ----------------------------------------------------------------
    // Step 1: Require a rooted path and reject illegal characters
    // ----------------------------------------------------------------

    // Must be either a drive-letter path (C:\...) or a UNC path (\\...)
    bool isDrivePath = (pathStr.length() >= 3 && std::isalpha(static_cast<unsigned char>(pathStr[0]))
                        && pathStr[1] == ':' && (pathStr[2] == '\\' || pathStr[2] == '/'));
    bool isUNCPath   = (pathStr.length() >= 3 && pathStr[0] == '\\' && pathStr[1] == '\\');

    if (!isDrivePath && !isUNCPath) {
        logger::debug("IsValidPath: not a rooted drive or UNC path");
        return false;
    }

    // Characters that are illegal anywhere in a Windows path component.
    // Note: backslash, forward-slash and colon are legal as separators /
    // drive letter so we skip them here and only check path component chars.
    static constexpr const char* illegalChars = "<>\"|?*";

    // Walk the string past the root prefix and check each character
    std::size_t startIdx = isDrivePath ? 3 : 2;  // skip "C:\" or "\\"
    for (std::size_t i = startIdx; i < pathStr.size(); ++i) {
        char c = pathStr[i];
        // Reject non-printable / control characters (ASCII 0-31)
        if (static_cast<unsigned char>(c) < 32) {
            logger::debug("IsValidPath: control character at index {}", i);
            return false;
        }
        // Reject explicitly illegal characters
        if (std::strchr(illegalChars, c) != nullptr) {
            logger::debug("IsValidPath: illegal character '{}' at index {}", c, i);
            return false;
        }
    }

    // ----------------------------------------------------------------
    // Step 2: Ensure the directory exists — create it if it doesn't
    // ----------------------------------------------------------------

    try {
        std::filesystem::path dirPath(pathStr);

        // If the path looks like it ends with a file name (has an extension),
        // operate on the parent directory instead.  If it ends with a separator
        // or has no extension treat it as a directory itself.
        if (dirPath.has_extension() && !pathStr.empty() &&
            pathStr.back() != '\\' && pathStr.back() != '/') {
            dirPath = dirPath.parent_path();
        }

        if (dirPath.empty()) {
            logger::debug("IsValidPath: resolved directory is empty");
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::exists(dirPath, ec)) {
            logger::debug("IsValidPath: directory '{}' does not exist, attempting to create",
                          dirPath.string());
            if (!std::filesystem::create_directories(dirPath, ec) && !std::filesystem::exists(dirPath, ec)) {
                logger::debug("IsValidPath: failed to create directory — {}", ec.message());
                return false;
            }
            logger::debug("IsValidPath: directory created successfully");
        }

        // ----------------------------------------------------------------
        // Step 3: Verify read/write access with a temporary probe file
        // ----------------------------------------------------------------

        // Build a unique probe file name inside the target directory
        std::filesystem::path probeFile = dirPath / ".printscreen_probe_test.tmp";

        // Write test
        {
            std::ofstream ofs(probeFile, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                logger::debug("IsValidPath: cannot open probe file for writing");
                return false;
            }
            ofs << "probe";
            if (ofs.fail()) {
                logger::debug("IsValidPath: write to probe file failed");
                ofs.close();
                std::filesystem::remove(probeFile, ec);  // best-effort cleanup
                return false;
            }
            ofs.close();
        }

        // Read test
        {
            std::ifstream ifs(probeFile, std::ios::binary);
            if (!ifs.is_open()) {
                logger::debug("IsValidPath: cannot open probe file for reading");
                std::filesystem::remove(probeFile, ec);
                return false;
            }
            std::string contents;
            ifs >> contents;
            ifs.close();
            if (contents != "probe") {
                logger::debug("IsValidPath: probe read-back mismatch");
                std::filesystem::remove(probeFile, ec);
                return false;
            }
        }

        // Cleanup — remove the probe file
        if (!std::filesystem::remove(probeFile, ec)) {
            // Not fatal — directory is still usable, just log it
            logger::warn("IsValidPath: could not remove probe file — {}", ec.message());
        }

        logger::debug("IsValidPath: '{}' passed all checks", pathStr);
        return true;

    } catch (const std::exception& e) {
        logger::error("IsValidPath exception: {}", e.what());
        return false;
    } catch (...) {
        logger::error("IsValidPath: unknown exception");
        return false;
    }
}

// ============================================================================
// PAPYRUS-EXPOSED FUNCTIONS
// ============================================================================

bool CheckPath(RE::StaticFunctionTag*, std::string path) {
    bool valid = IsValidPath(path);
    logger::debug("CheckPath: '{}' -> {}", path, valid ? "valid" : "invalid");
    return valid;
}

std::string Get_Result(RE::StaticFunctionTag*) {
    std::string currentResult;
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        currentResult = g_result;
    }

    // If a callback result is ready, consume it and reset state
    if (g_resultReady.load()) {
        g_resultReady.store(false);
        g_operationInProgress.store(false);
        
        // Reset to Ready after consuming callback
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            std::string consumed = g_result;
            g_result = "Ready";
            logger::debug("Get_Result: Consumed callback result: {}", consumed);
            return consumed;
        }
    }

    logger::debug("Get_Result: {}", currentResult);
    return currentResult;
}

std::string Cancel(RE::StaticFunctionTag*) {
    logger::info("=== Cancel CALLED ===");
    
    // Always set the cancel flag - let the worker thread handle it
    g_cancelFlag.store(true);
    
    // If nothing is running, just acknowledge
    if (!g_isRunning.load() && !g_isStarting.load() && !g_operationInProgress.load()) {
        logger::info("Cancel: No active operation");
        return "No active operation";
    }
    
    logger::info("Cancel: Cancellation requested");
    return "Cancelling";
}

std::string ForceReset(RE::StaticFunctionTag*) {
    logger::info("=== ForceReset CALLED ===");

    g_isStarting.store(false);
    g_isRunning.store(false);
    g_cancelFlag.store(false);
    g_resultReady.store(false);
    g_operationInProgress.store(false);

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Ready";
    }

    // Release any idle memory pool blocks to reclaim OS memory
    ScreenCapture::ResetMemoryPool();

    logger::info("ForceReset: State reset complete");
    return "Reset";
}

std::string ResetState(RE::StaticFunctionTag*) {
    return ForceReset(nullptr);
}

// ============================================================================
// CAPTURE IMPLEMENTATIONS
// ============================================================================

std::string PerformImageCapture(const std::string& basePath, const std::string& imageType,
    float jpgCompression, const std::string& compressionMode) {
    
    logger::info("PerformImageCapture: basePath='{}', type='{}', jpgCompression={}, compressionMode='{}'",
                 basePath, imageType, jpgCompression, compressionMode);

    try {
        std::wstring wBasePath = NormalizePath(util::utf8_to_wstring(basePath));

        ScreenCapture::CaptureParams params;
        params.basePath = wBasePath;
        params.format = ScreenCapture::StringToImageFormat(imageType);
        params.jpegQuality = jpgCompression;
        params.cancelFlag = &g_cancelFlag;

        // Set compression mode for TIFF
        if (params.format == ScreenCapture::ImageFormat::TIF) {
            params.tiffMode = ScreenCapture::StringToTiffCompression(compressionMode);
        }
        // Set compression mode for DDS
        else if (params.format == ScreenCapture::ImageFormat::DDS) {
            params.ddsMode = ScreenCapture::StringToDDSCompression(compressionMode);
        }

        ScreenCapture::InitializeDirectXTexThreading();

        ScreenCapture::CaptureResult result = ScreenCapture::CaptureScreen(params);

        if (!result.success) {
            logger::error("PerformImageCapture: Failed - {}", result.message);
            return "CALLBACK_ERROR: " + result.message;
        }

        std::string resultPath = util::wstring_to_utf8(result.filepath);
        logger::info("PerformImageCapture: Success - {}", resultPath);
        return "CALLBACK_SAVED: " + resultPath;

    } catch (const std::exception& e) {
        logger::error("PerformImageCapture exception: {}", e.what());
        return "CALLBACK_ERROR: " + std::string(e.what());
    } catch (...) {
        logger::error("PerformImageCapture: Unknown exception");
        return "CALLBACK_ERROR: Unknown error";
    }
}

std::string PerformGifCapture(const std::string& basePath, const std::string& imageType,
    float jpgCompression, const std::string& compressionMode, float gifDuration,
    float gifFPS, int gifLoopCount, int gifCompression, int optimize) {

    logger::info("GIF capture settings:");
    logger::info("  Duration: {}s, FPS: {}, LoopCount: {}, Compression: {}",
                 gifDuration, gifFPS, gifLoopCount, gifCompression);
    logger::info("  Base path: {}", basePath);

    try {
        std::wstring wBasePath = NormalizePath(util::utf8_to_wstring(basePath));

        ScreenCapture::CaptureParams params;
        params.basePath = wBasePath;
        params.format = ScreenCapture::ImageFormat::GIF;
        params.gifDuration = gifDuration;
        params.gifFPS = gifFPS;
        params.gifLoopCount = gifLoopCount;
        params.gifCompression = gifCompression;
        params.gifOptimize = optimize;
        params.cancelFlag = &g_cancelFlag;

        ScreenCapture::InitializeDirectXTexThreading();

        logger::info("Setting up desktop duplication for GIF capture");

        ScreenCapture::CaptureResult result = ScreenCapture::CaptureGIF(params);

        if (!result.success) {
            logger::error("GIF capture failed: {}", result.message);

            if (result.message.find("Cancelled") != std::string::npos ||
                result.message.find("cancelled") != std::string::npos) {
                return "CALLBACK_CANCELLED";
            }

            return "CALLBACK_ERROR: " + result.message;
        }

        std::string resultPath = util::wstring_to_utf8(result.filepath);

        // Verify file exists and has content
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(result.filepath, ec);
        if (ec || fileSize == 0) {
            logger::error("GIF file appears empty or missing: {}", resultPath);
        } else {
            logger::info("GIF saved successfully: {} ({} bytes)", resultPath, fileSize);
        }

        return "CALLBACK_SAVED: " + resultPath;

    } catch (const std::exception& e) {
        logger::error("Exception during GIF capture: {}", e.what());
        return "CALLBACK_ERROR: " + std::string(e.what());
    } catch (...) {
        logger::error("Unknown exception during GIF capture");
        return "CALLBACK_ERROR: Unknown error";
    }
}

std::string PerformAPNGCapture(const std::string& basePath, const std::string& imageType,
                               float apngFPS, float apngDuration, int apngLoopCount,
                               int apngCompression, int optimize) {
    logger::info("APNG capture settings:");
    logger::info("  Duration: {}s, FPS: {}, LoopCount: {}, Compression: {}, Optimize: {}",
                 apngDuration, apngFPS, apngLoopCount, apngCompression, optimize);
    logger::info("  Base path: {}", basePath);

    try {
        std::wstring wBasePath = NormalizePath(util::utf8_to_wstring(basePath));

        ScreenCapture::CaptureParams params;
        params.basePath = wBasePath;
        params.format = ScreenCapture::ImageFormat::APNG;
        params.apngFPS = apngFPS;
        params.apngDuration = apngDuration;
        params.apngLoopCount = apngLoopCount;
        params.apngCompression = apngCompression;
        params.apngOptimize = optimize;
        params.cancelFlag = &g_cancelFlag;

        ScreenCapture::InitializeDirectXTexThreading();

        logger::info("Setting up desktop duplication for APNG capture");

        ScreenCapture::CaptureResult result = ScreenCapture::CaptureAPNG(params);

        if (!result.success) {
            logger::error("APNG capture failed: {}", result.message);

            if (result.message.find("Cancelled") != std::string::npos ||
                result.message.find("cancelled") != std::string::npos) {
                return "CALLBACK_CANCELLED";
            }

            return "CALLBACK_ERROR: " + result.message;
        }

        std::string resultPath = util::wstring_to_utf8(result.filepath);

        // Verify file exists and has content
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(result.filepath, ec);
        if (ec || fileSize == 0) {
            logger::error("APNG file appears empty or missing: {}", resultPath);
        } else {
            logger::info("APNG saved successfully: {} ({} bytes)", resultPath, fileSize);
        }

        return "CALLBACK_SAVED: " + resultPath;

    } catch (const std::exception& e) {
        logger::error("Exception during APNG capture: {}", e.what());
        return "CALLBACK_ERROR: " + std::string(e.what());
    } catch (...) {
        logger::error("Unknown exception during APNG capture");
        return "CALLBACK_ERROR: Unknown error";
    }
}

// ============================================================================
// WORKER THREAD
// ============================================================================

void CaptureWorkerThread(std::string basePath, std::string imageType, float jpgCompression,
    std::string compressionMode, float gifMultiFrameDuration, float gifFPS,
    int gifLoopCount, int gifCompression, int optimize) {

    logger::info("CaptureWorkerThread: Starting");
    logger::info("  basePath='{}', imageType='{}', jpgCompression={}", basePath, imageType, jpgCompression);
    logger::info("  compressionMode='{}', duration={}, fps={}", compressionMode, gifMultiFrameDuration, gifFPS);
    logger::info("  loopCount={}, compression={}, optimize={}", gifLoopCount, gifCompression, optimize);

    std::string completionResult = "CALLBACK_ERROR: Unknown error";

    try {
        // Normalize image type
        std::string normalizedImageType = ToLowerCase(imageType);
        logger::info("CaptureWorkerThread: Processing format '{}'", normalizedImageType);

        // Route to appropriate capture function
        if (normalizedImageType == "agif" || normalizedImageType == "gif_multiframe" ||
            normalizedImageType == "animated_gif" || normalizedImageType == "gifanim") {
            // Animated GIF
            logger::info("CaptureWorkerThread: Routing to GIF capture");
            completionResult = PerformGifCapture(basePath, normalizedImageType, jpgCompression,
                compressionMode, gifMultiFrameDuration, gifFPS, gifLoopCount, gifCompression, optimize);
        }
        else if (normalizedImageType == "apng" || normalizedImageType == "animated_png" ||
                 normalizedImageType == "png_animated") {
            // Animated PNG
            logger::info("CaptureWorkerThread: Routing to APNG capture");
            completionResult = PerformAPNGCapture(basePath, normalizedImageType, gifFPS,
                gifMultiFrameDuration, gifLoopCount, gifCompression, optimize);
        }
        else {
            // Single frame capture (PNG, JPG, BMP, TIF, DDS, static GIF)
            logger::info("CaptureWorkerThread: Routing to single-frame capture");
            completionResult = PerformImageCapture(basePath, normalizedImageType, jpgCompression, compressionMode);
        }

    } catch (const std::exception& e) {
        logger::error("CaptureWorkerThread exception: {}", e.what());
        completionResult = "CALLBACK_ERROR: " + std::string(e.what());
    } catch (...) {
        logger::error("CaptureWorkerThread: Unknown exception");
        completionResult = "CALLBACK_ERROR: Unknown error";
    }

    // Update state
    g_isRunning.store(false);
    g_cancelFlag.store(false);
    g_resultReady.store(true);

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = completionResult;
    }

    logger::info("CaptureWorkerThread: Complete - {}", completionResult);
}

// ============================================================================
// MAIN TAKEPHOTO FUNCTION
// ============================================================================

std::string TakePhoto(RE::StaticFunctionTag*, std::string basePath, std::string imageType,
    float jpgCompression, std::string compressionMode, float gifMultiFrameDuration,
    float gifFPS, int gifLoopCount, int gifCompression, int optimize) {

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

    // Check if already running
    if (g_isRunning.load() || g_isStarting.load() || g_operationInProgress.load()) {
        logger::warn("TakePhoto: Already running");
        return "Already running";
    }

    // Validate path
    if (!IsValidPath(basePath)) {
        logger::error("TakePhoto: Invalid path '{}'", basePath);
        return "Error: Invalid path";
    }

    // Set state - CRITICAL: Set result to "Running" before starting thread
    g_isStarting.store(true);
    g_operationInProgress.store(true);
    g_cancelFlag.store(false);
    g_resultReady.store(false);

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Running";  // IMPORTANT: Set to Running, not Ready
    }

    // Start worker thread
    try {
        std::thread worker(CaptureWorkerThread,
            basePath, imageType, jpgCompression, compressionMode,
            gifMultiFrameDuration, gifFPS, gifLoopCount, gifCompression, optimize);
        worker.detach();

        g_isStarting.store(false);
        g_isRunning.store(true);

        logger::info("TakePhoto: Worker thread started");
        return "Capture started";

    } catch (const std::exception& e) {
        logger::error("TakePhoto: Failed to start thread - {}", e.what());
        g_isStarting.store(false);
        g_isRunning.store(false);
        g_operationInProgress.store(false);

        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            g_result = "Ready";
        }

        return "Error: Thread start failed";
    }
}

// ============================================================================
// REGISTRATION
// ============================================================================

bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm) {
    if (!vm) {
        logger::error("RegisterFunctions: VM is null");
        return false;
    }

    logger::info("=== REGISTERING PAPYRUS FUNCTIONS ===");

    const char* scriptName = "Printscreen_Formula_script";

    try {
        vm->RegisterFunction("CheckPath", scriptName, CheckPath);
        logger::info("  Registered: CheckPath");

        vm->RegisterFunction("TakePhoto", scriptName, TakePhoto);
        logger::info("  Registered: TakePhoto");

        vm->RegisterFunction("Get_Result", scriptName, Get_Result);
        logger::info("  Registered: Get_Result");

        vm->RegisterFunction("Cancel", scriptName, Cancel);
        logger::info("  Registered: Cancel");

        vm->RegisterFunction("ForceReset", scriptName, ForceReset);
        logger::info("  Registered: ForceReset");

        vm->RegisterFunction("ResetState", scriptName, ResetState);
        logger::info("  Registered: ResetState");

        // Initialize state on the main thread
        InitializeState();

        logger::info("=== {} FUNCTIONS REGISTERED (0 failed) ===", 6);
        return true;

    } catch (const std::exception& e) {
        logger::error("Exception during registration: {}", e.what());
        return false;
    } catch (...) {
        logger::error("Unknown exception during registration");
        return false;
    }
}

} // namespace PrintScreenPapyrus
