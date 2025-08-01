#include "PCH.h"
#include "PapyrusInterface.h"
#include "ScreenCapture.h"  // ADDED: Include the real screen capture functionality
#include "logger.h"

namespace PrintScreenPapyrus {

// Global state variables - THREAD SAFE with race condition fix
std::atomic<bool> g_isStarting(false);
std::atomic<bool> g_isRunning(false);
std::atomic<bool> g_cancelFlag(false);
std::atomic<bool> g_resultReady(false);
std::atomic<bool> g_initialized(false);
std::mutex g_resultMutex;
std::string g_result("Ready");

// RACE CONDITION FIX: Add global operation flag to prevent double calls
std::atomic<bool> g_operationInProgress(false);

void InitializeState() {
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
    logger::info("PrintScreen Papyrus state initialized");
}

// UNCHANGED: Keep original path validation as requested - only used by Papyrus CheckPath function
bool IsValidPath(const std::string& pathStr) {
    if (pathStr.empty()) return false;
    
    // Basic path validation - check if it looks like a Windows path
    if (pathStr.length() < 3) return false;
    
    // Check for drive letter format (C:\ or C:/) or UNC path (\\)
    if (!((pathStr[1] == ':' && (pathStr[2] == '\\' || pathStr[2] == '/')) || 
          (pathStr[0] == '\\' && pathStr[1] == '\\'))) {
        logger::debug("Path format validation failed: {}", pathStr);
        return false;
    }
    
    logger::debug("Path format validation passed, testing filesystem access: {}", pathStr);
    
    try {
        // Convert to filesystem path (handles both / and \ separators)
        std::filesystem::path fsPath(pathStr);
        
        // Check if the path exists
        std::error_code ec;
        bool pathExists = std::filesystem::exists(fsPath, ec);
        
        if (ec) {
            logger::debug("Error checking path existence: {} ({})", ec.message(), pathStr);
            return false;
        }
        
        if (!pathExists) {
            // Try to create the directory if it doesn't exist
            logger::debug("Path doesn't exist, attempting to create: {}", pathStr);
            bool created = std::filesystem::create_directories(fsPath, ec);
            
            if (ec || !created) {
                logger::debug("Failed to create directory: {} ({})", ec.message(), pathStr);
                return false;
            }
            
            logger::debug("Successfully created directory: {}", pathStr);
        }
        
        // Test write access by creating a temporary file
        std::filesystem::path testFile = fsPath / "printscreen_test_write.tmp";
        
        // Try to create and write to a test file
        std::ofstream testStream(testFile, std::ios::binary);
        if (!testStream.is_open()) {
            logger::debug("Failed to open test file for writing: {}", testFile.string());
            return false;
        }
        
        // Write some test data
        testStream.write("test", 4);
        testStream.close();
        
        if (testStream.fail()) {
            logger::debug("Failed to write to test file: {}", testFile.string());
            // Try to clean up the failed file
            std::filesystem::remove(testFile, ec);
            return false;
        }
        
        // Test read access
        std::ifstream readStream(testFile, std::ios::binary);
        if (!readStream.is_open()) {
            logger::debug("Failed to open test file for reading: {}", testFile.string());
            // Clean up and return false
            std::filesystem::remove(testFile, ec);
            return false;
        }
        
        char buffer[4];
        readStream.read(buffer, 4);
        readStream.close();
        
        if (readStream.fail() || std::string(buffer, 4) != "test") {
            logger::debug("Failed to read from test file: {}", testFile.string());
            // Clean up and return false
            std::filesystem::remove(testFile, ec);
            return false;
        }
        
        // Clean up the test file
        bool removed = std::filesystem::remove(testFile, ec);
        if (ec || !removed) {
            logger::warn("Failed to remove test file: {} ({})", testFile.string(), ec.message());
            // This is not a critical failure for path validation
        }
        
        logger::debug("Path validation successful - read/write access confirmed: {}", pathStr);
        return true;
        
    } catch (const std::exception& e) {
        logger::error("Exception during path validation: {} ({})", e.what(), pathStr);
        return false;
    } catch (...) {
        logger::error("Unknown exception during path validation: {}", pathStr);
        return false;
    }
}

std::string TakePhoto(
    RE::StaticFunctionTag*,
    std::string basePath,
    std::string imageType,
    float jpgCompression,
    std::string compressionMode,
    float gifMultiFrameDuration
) {
    logger::info("=== PAPYRUS TAKEPHOTO CALLED ===");
    logger::info("Parameters: basePath='{}', imageType='{}', jpgCompression={}, Mode='{}', GIF_MultiFrame_Duration={}", 
                basePath, imageType, jpgCompression, compressionMode, gifMultiFrameDuration);

    // RACE CONDITION FIX: Atomic check and set - prevents double calls
    bool expected = false;
    if (!g_operationInProgress.compare_exchange_strong(expected, true)) {
        logger::warn("TakePhoto called while capture already running or starting (operationInProgress was already true)");
        return "Already running";
    }

    // Additional safety checks
    if (g_isStarting.load() || g_isRunning.load()) {
        g_operationInProgress.store(false);  // Reset our flag
        logger::warn("TakePhoto: secondary state check failed - starting={}, running={}", g_isStarting.load(), g_isRunning.load());
        return "Already running";
    }

    logger::info("Current state - running: 0, starting: 0, cancelFlag: 0, resultReady: 0, initialized: {}", g_initialized.load());

    // ONLY validate path when called from Papyrus - internal C++ calls bypass this
    if (!IsValidPath(basePath)) {
        g_operationInProgress.store(false);  // Reset on failure
        logger::error("Path validation failed: {}", basePath);
        return "Invalid path";
    }

    logger::info("Path validation successful: {}", basePath);

    // Set starting state
    g_isStarting.store(true);
    g_cancelFlag.store(false);
    g_resultReady.store(false);

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Starting";
    }

    logger::info("Starting capture thread... (starting: 1, running: 0)");

    // Start worker thread
    try {
        std::thread captureThread(CaptureWorkerThread, basePath, imageType, jpgCompression, compressionMode, gifMultiFrameDuration);
        captureThread.detach();
        
        logger::info("Capture thread started successfully");
        return "Started";
        
    } catch (const std::exception& e) {
        logger::error("Failed to start capture thread: {}", e.what());
        
        // Reset state on failure
        g_isStarting.store(false);
        g_operationInProgress.store(false);  // Reset our flag
        
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            g_result = "Ready";
        }
        
        return "Thread start failed";
    }
}

std::string Get_Result(RE::StaticFunctionTag*) {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    
    logger::debug("Get_Result called");
    logger::debug("Current state - running: {}, starting: {}, resultReady: {}, result: '{}'", 
                 g_isRunning.load(), g_isStarting.load(), g_resultReady.load(), g_result);

    // Handle completion callbacks
    if (g_resultReady.load()) {
        std::string result = g_result;
        
        if (result.find("CALLBACK_") == 0) {
            logger::info("Returning {} callback", result);
            
            // Reset state after callback
            g_result = "Ready";
            g_resultReady.store(false);
            g_operationInProgress.store(false);  // RACE CONDITION FIX: Reset operation flag
            
            logger::debug("State reset to Ready after callback");
            return result;
        }
    }

    // Return current state
    if (g_isRunning.load() || g_isStarting.load()) {
        logger::debug("Capture still running");
    } else {
        logger::debug("Returning current result: {}", g_result);
    }

    return g_result;
}

std::string Cancel(RE::StaticFunctionTag*) {
    logger::info("Cancel requested");
    
    bool wasActive = g_isRunning.load() || g_isStarting.load();
    if (!wasActive) {
        logger::info("Cancel called but nothing was running");
        return "Nothing to cancel";
    }

    g_cancelFlag.store(true);
    logger::info("Cancel flag set");

    // Brief wait for thread to respond
    auto startTime = std::chrono::steady_clock::now();
    while ((g_isRunning.load() || g_isStarting.load()) && 
           std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(1000)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (g_isRunning.load() || g_isStarting.load()) {
        logger::warn("Thread didn't respond to cancel - forcing reset");
        ForceReset(nullptr);
        return "Force cancelled";
    }

    logger::info("Cancel completed successfully");
    return "Cancelled";
}

std::string ForceReset(RE::StaticFunctionTag*) {
    logger::info("ForceReset called - performing emergency cleanup");
    
    // Force all flags to false
    g_isStarting.store(false);
    g_isRunning.store(false);
    g_cancelFlag.store(false);
    g_resultReady.store(false);
    g_operationInProgress.store(false);  // RACE CONDITION FIX: Reset operation flag
    
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Ready";
    }
    
    logger::info("Force reset completed");
    return "Reset complete";
}

std::string ResetState(RE::StaticFunctionTag*) {
    logger::info("ResetState called - performing clean initialization");
    
    if (!g_isRunning.load() && !g_isStarting.load()) {
        InitializeState();
        logger::info("PrintScreen state initialized to clean state");
        return "State reset to clean";
    } else {
        logger::info("ResetState called while operation active - ignoring");
        return "Cannot reset while active";
    }
}

bool CheckPath(RE::StaticFunctionTag*, std::string path) {
    logger::debug("CheckPath called with path: '{}'", path);
    logger::debug("Validating path: '{}'", path);
    
    bool isValid = IsValidPath(path);
    
    if (isValid) {
        logger::info("Path validation successful: {}", path);
        logger::info("CheckPath('{}') = 1", path);
    } else {
        logger::error("Path validation failed: {}", path);
        logger::info("CheckPath('{}') = 0", path);
    }
    
    return isValid;
}

void CaptureWorkerThread(
    std::string basePath,
    std::string imageType,
    float jpgCompression,
    std::string compressionMode,
    float gifMultiFrameDuration
) {
    logger::info("=== CAPTURE WORKER THREAD STARTED ===");
    logger::info("Parameters: basePath='{}', imageType='{}', jpgCompression={}, compressionMode='{}', gifDuration={}", 
                basePath, imageType, jpgCompression, compressionMode, gifMultiFrameDuration);

    // Transition from starting to running
    g_isStarting.store(false);
    g_isRunning.store(true);
    
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = "Running";
    }

    std::string completionResult;
    
    try {
        if (g_cancelFlag.load()) {
            completionResult = "CALLBACK_CANCELLED";
            logger::info("=== CAPTURE WORKER THREAD COMPLETED: Cancelled (early) ===");
        } else {
            // Perform the actual capture based on image type
            std::string normalizedImageType = ToLowerCase(imageType);
            
            if (normalizedImageType == "gif_multiframe") {
                completionResult = PerformGifCapture(basePath, normalizedImageType, jpgCompression, compressionMode, gifMultiFrameDuration);
            } else {
                completionResult = PerformImageCapture(basePath, normalizedImageType, jpgCompression, compressionMode);
            }
        }
        
    } catch (const std::exception& e) {
        logger::error("Capture worker exception: {}", e.what());
        completionResult = "CALLBACK_ERROR: " + std::string(e.what());
    } catch (...) {
        logger::error("Unknown exception in capture worker");
        completionResult = "CALLBACK_ERROR: Unknown error";
    }

    // Set completion state
    g_isRunning.store(false);
    g_cancelFlag.store(false);
    g_resultReady.store(true);
    // NOTE: g_operationInProgress will be reset when Get_Result returns the callback

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = completionResult;
    }

    logger::info("=== CAPTURE WORKER THREAD COMPLETED: {} ===", completionResult);
}

// FIXED: Real GIF capture implementation using ScreenCapture
std::string PerformGifCapture(const std::string& basePath, const std::string& imageType, 
                             float jpgCompression, const std::string& compressionMode, float gifDuration) {
    logger::info("GIF multiframe duration set to: {}s", static_cast<int>(gifDuration));
    logger::info("Starting GIF multiframe capture...");
    logger::info("=== Starting GIF capture ===");
    logger::info("Duration: {}s, Base path: {}", static_cast<int>(gifDuration), basePath);
    
    try {
        // Convert string path to wstring for ScreenCapture
        std::wstring wBasePath(basePath.begin(), basePath.end());
        
        // Setup capture parameters
        ScreenCapture::CaptureParams params;
        params.basePath = wBasePath;
        params.format = ScreenCapture::ImageFormat::GIF;
        params.gifDuration = gifDuration;
        params.cancelFlag = &g_cancelFlag;  // Pass the global cancel flag
        
        // Initialize DirectXTex threading for optimal performance
        ScreenCapture::InitializeDirectXTexThreading();
        
        logger::info("Setting up desktop duplication");
        
        // Perform the actual GIF capture using ScreenCapture
        ScreenCapture::CaptureResult result = ScreenCapture::CaptureGIF(params);
        
        if (!result.success) {
            logger::error("GIF capture failed: {}", result.message);
            
            // Check if it was cancelled
            if (result.message.find("Cancelled") != std::string::npos || 
                result.message.find("cancelled") != std::string::npos) {
                return "CALLBACK_CANCELLED";
            }
            
            return "CALLBACK_ERROR: " + result.message;
        }
        
        // Convert result path back to string for logging
        std::string resultPath(result.filepath.begin(), result.filepath.end());
        
        // Get file size for logging
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(result.filepath, ec);
        
        if (ec) {
            logger::warn("Could not get file size for: {}", resultPath);
            logger::info("GIF saved successfully: {}", resultPath);
        } else {
            logger::info("GIF saved successfully: {} ({} bytes)", resultPath, fileSize);
        }
        
        logger::info("Capture completed successfully: GIF capture completed successfully");
        
        return "CALLBACK_SUCCESS";
        
    } catch (const std::exception& e) {
        logger::error("Exception during GIF capture: {}", e.what());
        return "CALLBACK_ERROR: " + std::string(e.what());
    } catch (...) {
        logger::error("Unknown exception during GIF capture");
        return "CALLBACK_ERROR: Unknown error";
    }
}

// FIXED: Real image capture implementation using ScreenCapture
std::string PerformImageCapture(const std::string& basePath, const std::string& imageType, 
                               float jpgCompression, const std::string& compressionMode) {
    logger::info("Starting single image capture: {}", imageType);
    
    try {
        // Convert string path to wstring for ScreenCapture
        std::wstring wBasePath(basePath.begin(), basePath.end());
        
        // Setup capture parameters
        ScreenCapture::CaptureParams params;
        params.basePath = wBasePath;
        params.format = ScreenCapture::StringToImageFormat(imageType);
        params.jpegQuality = jpgCompression;
        params.cancelFlag = &g_cancelFlag;  // Pass the global cancel flag
        
        // Set compression mode based on format
        if (params.format == ScreenCapture::ImageFormat::TIF) {
            params.tiffMode = ScreenCapture::StringToTiffCompression(compressionMode);
        } else if (params.format == ScreenCapture::ImageFormat::DDS) {
            params.ddsMode = ScreenCapture::StringToDDSCompression(compressionMode);
        }
        
        // Initialize DirectXTex threading for optimal performance
        ScreenCapture::InitializeDirectXTexThreading();
        
        logger::info("Setting up desktop duplication");
        
        // Perform the actual screen capture using ScreenCapture
        ScreenCapture::CaptureResult result = ScreenCapture::CaptureScreen(params);
        
        if (!result.success) {
            logger::error("Image capture failed: {}", result.message);
            
            // Check if it was cancelled
            if (result.message.find("Cancelled") != std::string::npos || 
                result.message.find("cancelled") != std::string::npos) {
                return "CALLBACK_CANCELLED";
            }
            
            return "CALLBACK_ERROR: " + result.message;
        }
        
        // Convert result path back to string for logging
        std::string resultPath(result.filepath.begin(), result.filepath.end());
        
        // Get file size for logging
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(result.filepath, ec);
        
        if (ec) {
            logger::warn("Could not get file size for: {}", resultPath);
            logger::info("Image saved successfully: {}", resultPath);
        } else {
            logger::info("Image saved successfully: {} ({} bytes)", resultPath, fileSize);
        }
        
        logger::info("Capture completed successfully: {} capture completed", imageType);
        
        return "CALLBACK_SUCCESS";
        
    } catch (const std::exception& e) {
        logger::error("Exception during image capture: {}", e.what());
        return "CALLBACK_ERROR: " + std::string(e.what());
    } catch (...) {
        logger::error("Unknown exception during image capture");
        return "CALLBACK_ERROR: Unknown error";
    }
}

// Helper functions
std::string ToLowerCase(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
}

std::string GenerateTimestampedFilename(const std::string& basePath, const std::string& extension) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    char timestamp[64];
    std::tm* timeinfo = std::localtime(&time_t);
    std::sprintf(timestamp, "%04d%02d%02d_%02d%02d%02d_%03d",
                timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
                static_cast<int>(ms.count()));
    
    return basePath + "\\SS_" + std::string(timestamp) + "." + extension;
}

bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm) {
    logger::info("===============================================");
    logger::info("=== STARTING PAPYRUS FUNCTION REGISTRATION ===");
    logger::info("===============================================");
    logger::info("Virtual Machine pointer valid: 0x{:x}", reinterpret_cast<uintptr_t>(vm));

    constexpr auto scriptName = "Printscreen_Formula_script";
    logger::info("Target script name: '{}'", scriptName);

    // Initialize state
    InitializeState();

    // Register all functions
    logger::info("Attempting to register CheckPath...");
    vm->RegisterFunction("CheckPath", scriptName, CheckPath);
    logger::info("✓ CheckPath registered successfully");

    logger::info("Attempting to register TakePhoto...");
    vm->RegisterFunction("TakePhoto", scriptName, TakePhoto);
    logger::info("✓ TakePhoto registered successfully");

    logger::info("Attempting to register Get_Result...");
    vm->RegisterFunction("Get_Result", scriptName, Get_Result);
    logger::info("✓ Get_Result registered successfully");

    logger::info("Attempting to register Cancel...");
    vm->RegisterFunction("Cancel", scriptName, Cancel);
    logger::info("✓ Cancel registered successfully");

    logger::info("Attempting to register ForceReset...");
    vm->RegisterFunction("ForceReset", scriptName, ForceReset);
    logger::info("✓ ForceReset registered successfully");

    logger::info("Attempting to register ResetState...");
    vm->RegisterFunction("ResetState", scriptName, ResetState);
    logger::info("✓ ResetState registered successfully");

    logger::info("===============================================");
    logger::info("=== ALL PAPYRUS FUNCTIONS REGISTERED SUCCESSFULLY ===");
    logger::info("===============================================");

    return true;
}

} // namespace PrintScreenPapyrus