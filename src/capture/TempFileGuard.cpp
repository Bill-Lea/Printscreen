#include "PCH.h"
#include "TempFileGuard.h"
#include "logger.h"
#include <regex>
#include <chrono>

namespace {

// Temp directory prefixes used by MakeTempDir
constexpr const wchar_t* kGifTempPrefix = L"gif_temp";
constexpr const wchar_t* kApngTempPrefix = L"apng_temp";

// Pattern: prefix_YYYYMMDD_HHMMSS_mmm
bool IsStaleTempDir(const std::filesystem::path& path) {
    const std::wstring name = path.wstring();
    
    // Must start with known prefix
    if (name.find(kGifTempPrefix) != 0 && name.find(kApngTempPrefix) != 0) {
        return false;
    }
    
    // Must contain timestamp pattern _YYYYMMDD_HHMMSS_mmm
    const std::wregex timestampPattern(L"_[0-9]{8}_[0-9]{6}_[0-9]{3}$");
    return std::regex_search(name, timestampPattern);
}

} // namespace

// --- Global registry for orphaned temp directories ---
std::mutex TempFileGuard::s_registryMutex;
std::set<std::wstring> TempFileGuard::s_registeredDirs;

TempFileGuard::TempFileGuard(std::wstring tempDir) : m_dir(std::move(tempDir)) {
    if (!m_dir.empty()) {
        RegisterDirectory(m_dir);
    }
}

TempFileGuard::TempFileGuard(TempFileGuard&& other) noexcept {
    m_dir = std::move(other.m_dir);
    m_released = other.m_released;
    other.m_released = true;  // moved-from won't clean up
}

TempFileGuard& TempFileGuard::operator=(TempFileGuard&& other) noexcept {
    if (this != &other) {
        Cleanup();
        m_dir = std::move(other.m_dir);
        m_released = other.m_released;
        other.m_released = true;
    }
    return *this;
}

void TempFileGuard::Release() noexcept {
    if (!m_dir.empty() && !m_released) {
        UnregisterDirectory(m_dir);
    }
    m_released = true;
}

void TempFileGuard::Cleanup() noexcept {
    if (m_released || m_dir.empty()) return;
    
    // Unregister before cleanup
    UnregisterDirectory(m_dir);
    
    std::error_code ec;
    // Remove all files in the directory first
    if (std::filesystem::exists(m_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
            std::filesystem::remove(entry.path(), ec);
        }
        std::filesystem::remove(m_dir, ec);
    }
    m_released = true;  // Don't try again in destructor
}

void TempFileGuard::RegisterDirectory(const std::wstring& dir) {
    if (dir.empty()) return;
    std::lock_guard<std::mutex> lock(s_registryMutex);
    s_registeredDirs.insert(dir);
}

void TempFileGuard::UnregisterDirectory(const std::wstring& dir) {
    if (dir.empty()) return;
    std::lock_guard<std::mutex> lock(s_registryMutex);
    s_registeredDirs.erase(dir);
}

void TempFileGuard::CleanupAllRegistered() {
    std::lock_guard<std::mutex> lock(s_registryMutex);
    
    int cleanedCount = 0;
    for (const auto& dir : s_registeredDirs) {
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                std::filesystem::remove(entry.path(), ec);
            }
            std::filesystem::remove(dir, ec);
            if (!ec) {
                logger::info("TempFileGuard::CleanupAllRegistered: cleaned orphaned temp dir: {}",
                             util::wstring_to_utf8(dir));
                cleanedCount++;
            } else {
                logger::warn("TempFileGuard::CleanupAllRegistered: failed to remove '{}': {}",
                             util::wstring_to_utf8(dir), ec.message());
            }
        }
    }
    s_registeredDirs.clear();
    
    if (cleanedCount > 0) {
        logger::info("TempFileGuard::CleanupAllRegistered: cleaned {} orphaned temp directories", 
                     cleanedCount);
    }
}

// --- StaleTempCleanup ---

void StaleTempCleanup::ScanAndRemove(const std::wstring& outputDir) {
    std::error_code ec;
    
    if (!std::filesystem::exists(outputDir, ec)) {
        logger::warn("StaleTempCleanup: output directory does not exist: {}", 
                     util::wstring_to_utf8(outputDir));
        return;
    }
    
    int removedCount = 0;
    int failedCount = 0;
    
    for (const auto& entry : std::filesystem::directory_iterator(outputDir, ec)) {
        if (ec) break;
        
        if (!entry.is_directory(ec)) continue;
        if (ec) continue;
        
        const auto dirName = entry.path().filename();
        if (!IsStaleTempDir(dirName)) continue;
        
        // Remove the stale temp directory and all its contents
        std::error_code removeEc;
        std::filesystem::remove_all(entry.path(), removeEc);
        
        if (removeEc) {
            logger::warn("StaleTempCleanup: failed to remove stale directory '{}': {}",
                         util::wstring_to_utf8(entry.path().wstring()), 
                         removeEc.message());
            failedCount++;
        } else {
            logger::info("StaleTempCleanup: removed stale temp directory: {}",
                         util::wstring_to_utf8(entry.path().wstring()));
            removedCount++;
        }
    }
    
    if (removedCount > 0 || failedCount > 0) {
        logger::info("StaleTempCleanup: removed {} stale directories, {} failed", 
                     removedCount, failedCount);
    }
}

bool StaleTempCleanup::IsTempDirectory(const std::filesystem::path& dirName) {
    return IsStaleTempDir(dirName);
}