#include "ScreenCapture.h"
#include "logger.h"
#include "PCH.h"
#include "stringutils.h"
#include "memory_pool/MemoryPool.h"
#include <DirectXTex.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace ScreenCapture {

    // Global memory pool for reusable frame buffers during animated captures.
    // Avoids repeated OS-level alloc/free for large BGRA frame buffers (~8MB each at 1080p).
    static MemoryPool g_memoryPool;

    MemoryPool& GetMemoryPool() { return g_memoryPool; }

    void ResetMemoryPool() {
        g_memoryPool.trim();  // Release unused blocks; safe even if something is still held
        logger::debug("ScreenCapture: memory pool trimmed");
    }

    // Exception-based cancellation helper (cooperative)
    struct Cancelled final : public std::exception {
        const char* what() const noexcept override { return "Cancelled"; }
    };

    inline void CancelIfRequested(std::atomic<bool>* flag, const char* where) {
        if (flag && flag->load(std::memory_order_relaxed)) {
            logger::info("Cancellation requested during: {}", where);
            throw Cancelled{};
        }
    }
    
    
    // ADDED: Cancellation helper function
    bool CheckCancellation(std::atomic<bool>* cancelFlag, const std::string& operation) {
        if (cancelFlag && cancelFlag->load()) {
            logger::info("Cancellation requested during: {}", operation);
            return true;
        }
        return false;
    }
    // Helper to format HRESULT as readable hex string with error description
    std::string FormatHRESULT(HRESULT hr) {
        char buffer[128];
        
        // Common HRESULT codes with descriptions
        // Note: E_ACCESSDENIED == 0x80070005, so we only use the symbolic name
        const char* desc = nullptr;
        switch (hr) {
            case E_INVALIDARG:      desc = "E_INVALIDARG (Invalid parameter)"; break;
            case E_FAIL:            desc = "E_FAIL (General failure)"; break;
            case E_ACCESSDENIED:    desc = "E_ACCESSDENIED (Access denied)"; break;
            case E_OUTOFMEMORY:     desc = "E_OUTOFMEMORY (Out of memory)"; break;
            case E_POINTER:         desc = "E_POINTER (Invalid pointer)"; break;
            case E_ABORT:           desc = "E_ABORT (Operation aborted)"; break;
            case E_UNEXPECTED:      desc = "E_UNEXPECTED (Unexpected error)"; break;
            case E_NOTIMPL:         desc = "E_NOTIMPL (Not implemented)"; break;
            case 0x80070002:        desc = "ERROR_FILE_NOT_FOUND"; break;
            case 0x80070003:        desc = "ERROR_PATH_NOT_FOUND"; break;
            // 0x80070005 is E_ACCESSDENIED, already handled above
            case 0x8007006B:        desc = "ERROR_SHARING_VIOLATION (File in use)"; break;
            case 0x800700B7:        desc = "ERROR_ALREADY_EXISTS"; break;
            case 0x88982F04:        desc = "WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT"; break;
            case 0x88982F07:        desc = "WINCODEC_ERR_CODECNOTHUMBNAIL"; break;
            case 0x88982F0C:        desc = "WINCODEC_ERR_FRAMEMISSING"; break;
            case 0x88982F8C:        desc = "WINCODEC_ERR_BADIMAGE"; break;
            default:                desc = nullptr; break;
        }
        
        if (desc) {
            snprintf(buffer, sizeof(buffer), "0x%08X (%s)", static_cast<uint32_t>(hr), desc);
        } else {
            snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<uint32_t>(hr));
        }
        
        return std::string(buffer);
    }

    
    // Setup optimal threading for DirectXTex
    void InitializeDirectXTexThreading() {
        unsigned int numThreads = std::thread::hardware_concurrency();
        
        if (numThreads > 4) {
            numThreads = numThreads / 2;
        }
        
        numThreads = std::max(2u, std::min(16u, numThreads));
        
        logger::info("DirectXTex threading setup:");
        logger::info("  - Hardware concurrency: {}", std::thread::hardware_concurrency());
        logger::info("  - Using {} threads for compression", numThreads);
    }

// Structure for differential frame with transparency
struct DifferentialFrameData {
    std::vector<uint8_t> pixels;     // 8-bit indexed pixels with transparency
    UINT left, top, width, height;    // Bounding box
    BYTE transparentIndex;            // Index used for unchanged pixels
    bool hasTransparency;             // Whether this frame uses transparency
};

// Helper to find least-used color index for transparency
static BYTE FindTransparentIndex(const uint8_t* quantizedPixels, size_t pixelCount) {
    std::vector<uint32_t> colorUsage(256, 0);
    for (size_t i = 0; i < pixelCount; ++i) {
        colorUsage[quantizedPixels[i]]++;
    }

    // Find least-used color index
    BYTE leastUsedIndex = 0;
    uint32_t minUsage = UINT32_MAX;
    for (int i = 0; i < 256; ++i) {
        if (colorUsage[i] < minUsage) {
            minUsage = colorUsage[i];
            leastUsedIndex = static_cast<BYTE>(i);
        }
    }
    return leastUsedIndex;
}

// APNG Differential Frame Data structure
struct APNGDiffFrame {
    std::vector<uint8_t> pngData;     // Encoded PNG data for this frame/region
    uint32_t left, top;               // Offset within full image
    uint32_t width, height;           // Size of this frame region
    bool isDifferential;              // True if this is a diff frame (not first frame)
    bool isSkipped;                   // True if frame is identical to previous
};

// Compute bounding box of changed pixels between two BGRA frames
// Returns false if frames are identical
static bool ComputeAPNGDiffRect(
    const uint8_t* prevPixels, size_t prevRowPitch,
    const uint8_t* currPixels, size_t currRowPitch,
    uint32_t width, uint32_t height,
    uint32_t& outLeft, uint32_t& outTop,
    uint32_t& outRight, uint32_t& outBottom,
    uint32_t threshold = 2)  // Per-channel threshold for noise tolerance
{
    uint32_t minX = width, minY = height;
    uint32_t maxX = 0, maxY = 0;
    bool hasChanges = false;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* prevRow = prevPixels + y * prevRowPitch;
        const uint8_t* currRow = currPixels + y * currRowPitch;

        for (uint32_t x = 0; x < width; ++x) {
            // BGRA format: compare each channel
            const uint8_t* p0 = prevRow + x * 4;
            const uint8_t* p1 = currRow + x * 4;

            bool pixelChanged = false;
            for (int c = 0; c < 4; ++c) {
                int diff = std::abs(static_cast<int>(p0[c]) - static_cast<int>(p1[c]));
                if (diff > static_cast<int>(threshold)) {
                    pixelChanged = true;
                    break;
                }
            }

            if (pixelChanged) {
                hasChanges = true;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (!hasChanges) {
        return false;  // Frames are identical
    }

    outLeft = minX;
    outTop = minY;
    outRight = maxX;
    outBottom = maxY;
    return true;
}

// Extract a sub-region from BGRA image
static void ExtractSubRegion(
    const uint8_t* srcPixels, size_t srcRowPitch,
    uint32_t srcWidth, uint32_t srcHeight,
    uint32_t left, uint32_t top, uint32_t regionWidth, uint32_t regionHeight,
    std::vector<uint8_t>& outPixels, size_t& outRowPitch)
{
    outRowPitch = regionWidth * 4;  // BGRA
    outPixels.resize(regionHeight * outRowPitch);

    for (uint32_t y = 0; y < regionHeight; ++y) {
        const uint8_t* srcRow = srcPixels + (top + y) * srcRowPitch + left * 4;
        uint8_t* dstRow = outPixels.data() + y * outRowPitch;
        memcpy(dstRow, srcRow, regionWidth * 4);
    }
}

// ============================================================================
// TRUE DELTA ENCODING FUNCTIONS
// These functions compute actual pixel differences, not just region extraction
// ============================================================================

// Extract a sub-region with TRUE DELTA encoding (XOR-based difference)
// Pixels that are identical become fully transparent (alpha=0)
// Pixels that differ contain the NEW pixel value with full alpha
// This enables much better PNG compression since transparent regions compress extremely well
static void ExtractDeltaSubRegion(
    const uint8_t* prevPixels, size_t prevRowPitch,
    const uint8_t* currPixels, size_t currRowPitch,
    uint32_t fullWidth, uint32_t fullHeight,
    uint32_t left, uint32_t top, uint32_t regionWidth, uint32_t regionHeight,
    std::vector<uint8_t>& outPixels, size_t& outRowPitch,
    uint32_t threshold = 2)  // Per-channel noise tolerance
{
    outRowPitch = regionWidth * 4;  // BGRA with alpha
    outPixels.resize(regionHeight * outRowPitch);

    size_t transparentPixels = 0;
    size_t changedPixels = 0;

    for (uint32_t y = 0; y < regionHeight; ++y) {
        const uint32_t srcY = top + y;
        const uint8_t* prevRow = prevPixels + srcY * prevRowPitch;
        const uint8_t* currRow = currPixels + srcY * currRowPitch;
        uint8_t* dstRow = outPixels.data() + y * outRowPitch;

        for (uint32_t x = 0; x < regionWidth; ++x) {
            const uint32_t srcX = left + x;
            const uint8_t* prevPixel = prevRow + srcX * 4;  // BGRA
            const uint8_t* currPixel = currRow + srcX * 4;  // BGRA
            uint8_t* dstPixel = dstRow + x * 4;

            // Check if pixel changed beyond threshold
            bool pixelChanged = false;
            for (int c = 0; c < 3; ++c) {  // Check B, G, R channels (not alpha)
                int diff = std::abs(static_cast<int>(prevPixel[c]) - static_cast<int>(currPixel[c]));
                if (diff > static_cast<int>(threshold)) {
                    pixelChanged = true;
                    break;
                }
            }

            if (pixelChanged) {
                // Pixel changed: write the NEW pixel with full alpha
                dstPixel[0] = currPixel[0];  // B
                dstPixel[1] = currPixel[1];  // G
                dstPixel[2] = currPixel[2];  // R
                dstPixel[3] = 255;           // Full alpha (opaque)
                changedPixels++;
            } else {
                // Pixel unchanged: make it fully transparent
                dstPixel[0] = 0;    // B
                dstPixel[1] = 0;    // G
                dstPixel[2] = 0;    // R
                dstPixel[3] = 0;    // Fully transparent
                transparentPixels++;
            }
        }
    }

    // Log compression stats for this region
    size_t totalPixels = regionWidth * regionHeight;
    float transparentPercent = (static_cast<float>(transparentPixels) / static_cast<float>(totalPixels)) * 100.0f;
    logger::debug("Delta region {}x{}: {:.1f}% transparent ({} unchanged, {} changed)",
                 regionWidth, regionHeight, transparentPercent, transparentPixels, changedPixels);
}

// Encode a BGRA buffer WITH ALPHA to PNG in memory (for delta frames)
// This version preserves the alpha channel for transparency-based delta encoding
static HRESULT EncodeRegionToPNGWithAlpha(
    IWICImagingFactory* wicFactory,
    const uint8_t* pixels, size_t rowPitch,
    uint32_t width, uint32_t height,
    std::vector<uint8_t>& pngData)
{
    HRESULT hr = S_OK;

    // Create memory stream
    Microsoft::WRL::ComPtr<IStream> memStream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &memStream);
    if (FAILED(hr)) return hr;

    // Create PNG encoder
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return hr;

    hr = encoder->Initialize(memStream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;

    // Create frame
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> propBag;
    hr = encoder->CreateNewFrame(&frame, &propBag);
    if (FAILED(hr)) return hr;

    hr = frame->Initialize(propBag.Get());
    if (FAILED(hr)) return hr;

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) return hr;

    // IMPORTANT: Use 32bppBGRA to preserve alpha channel for delta encoding
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) return hr;

    // Create bitmap from memory - preserving alpha
    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
    hr = wicFactory->CreateBitmapFromMemory(
        width, height,
        GUID_WICPixelFormat32bppBGRA,
        static_cast<UINT>(rowPitch),
        static_cast<UINT>(height * rowPitch),
        const_cast<BYTE*>(pixels),
        &wicBitmap);
    if (FAILED(hr)) return hr;

    hr = frame->WriteSource(wicBitmap.Get(), nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Commit();
    if (FAILED(hr)) return hr;

    hr = encoder->Commit();
    if (FAILED(hr)) return hr;

    // Read data from memory stream
    STATSTG stat;
    hr = memStream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) return hr;

    LARGE_INTEGER zero = {};
    hr = memStream->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;

    pngData.resize(static_cast<size_t>(stat.cbSize.QuadPart));
    ULONG bytesRead = 0;
    hr = memStream->Read(pngData.data(), static_cast<ULONG>(pngData.size()), &bytesRead);

    return hr;
}

// Encode a BGRA buffer to PNG in memory (helper for APNG differential frames)
static HRESULT EncodeRegionToPNG(
    IWICImagingFactory* wicFactory,
    const uint8_t* pixels, size_t rowPitch,
    uint32_t width, uint32_t height,
    std::vector<uint8_t>& pngData)
{
    HRESULT hr = S_OK;

    // Create memory stream
    Microsoft::WRL::ComPtr<IStream> memStream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &memStream);
    if (FAILED(hr)) return hr;

    // Create PNG encoder
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return hr;

    hr = encoder->Initialize(memStream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;

    // Create frame
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) return hr;

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) return hr;

    // Create bitmap from memory
    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
    hr = wicFactory->CreateBitmapFromMemory(
        width, height,
        GUID_WICPixelFormat32bppBGRA,
        static_cast<UINT>(rowPitch),
        static_cast<UINT>(height * rowPitch),
        const_cast<BYTE*>(pixels),
        &wicBitmap);
    if (FAILED(hr)) return hr;

    hr = frame->WriteSource(wicBitmap.Get(), nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Commit();
    if (FAILED(hr)) return hr;

    hr = encoder->Commit();
    if (FAILED(hr)) return hr;

    // Read data from memory stream
    STATSTG stat;
    hr = memStream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) return hr;

    LARGE_INTEGER zero = {};
    hr = memStream->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;

    pngData.resize(static_cast<size_t>(stat.cbSize.QuadPart));
    ULONG bytesRead = 0;
    hr = memStream->Read(pngData.data(), static_cast<ULONG>(pngData.size()), &bytesRead);

    return hr;
}

// Helper function implementations
ImageFormat StringToImageFormat(const std::string& format) {
    std::string lower = format;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "png") return ImageFormat::PNG;
    if (lower == "jpg" || lower == "jpeg") return ImageFormat::JPEG;
    if (lower == "bmp") return ImageFormat::BMP;
    if (lower == "tif" || lower == "tiff") return ImageFormat::TIF;
     if (lower == "gif" || lower == "agif" || lower == "animated_gif" || lower == "gif_animated") return ImageFormat::GIF;
    if (lower == "apng" || lower == "animated_png" || lower == "png_animated" || lower == "animatedpng") return ImageFormat::APNG;
    if (lower == "dds") return ImageFormat::DDS;
	
    logger::error("StringToImageFormat: unrecognized image format '{}'", format);
    throw std::invalid_argument("Unrecognized image format: " + format);
}

TiffMode StringToTiffCompression(const std::string& compression) {
    std::string lower = compression;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "lzw") return TiffMode::LZW;
    if (lower == "ccitt1d") return TiffMode::CCITT1D;
    if (lower == "ccitt4") return TiffMode::CCITT4;
    if (lower == "rle") return TiffMode::RLE;
    if (lower == "zip") return TiffMode::ZIP;
    
    return TiffMode::NONE; // Default
}

DDSCompression StringToDDSCompression(const std::string& compression) {
    std::string lower = compression;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "bc1") return DDSCompression::BC1;
    if (lower == "bc2") return DDSCompression::BC2;
    if (lower == "bc3") return DDSCompression::BC3;
    if (lower == "bc4") return DDSCompression::BC4;
    if (lower == "bc5") return DDSCompression::BC5;
    if (lower == "bc6h") return DDSCompression::BC6H;
    if (lower == "bc7_slow") return DDSCompression::BC7_SLOW;
    if (lower == "bc7_normal") return DDSCompression::BC7_NORMAL;
    if (lower == "bc7_fast") return DDSCompression::BC7_FAST;
    
    return DDSCompression::BC1; // Default
}

std::wstring GenerateFilename(const std::wstring& basePath, ImageFormat format) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::filesystem::path dirPath(basePath);
    
    std::wstringstream filename;
    filename << L"SS_";
    
    std::tm* tm_info = std::localtime(&time_t);
    filename << std::put_time(tm_info, L"%Y%m%d_%H%M%S");
    filename << L"_" << std::setfill(L'0') << std::setw(3) << ms.count();
    
    switch (format) {
        case ImageFormat::PNG: filename << L".png"; break;
        case ImageFormat::JPEG: filename << L".jpg"; break;
        case ImageFormat::BMP: filename << L".bmp"; break;
        case ImageFormat::TIF: filename << L".tif"; break;
        case ImageFormat::GIF: filename << L".gif"; break;
        case ImageFormat::APNG: filename << L".png"; break;  // APNG uses .png extension
        case ImageFormat::DDS: filename << L".dds"; break;
        default:
            logger::error("GenerateFilename: unhandled ImageFormat value {}", static_cast<int>(format));
            throw std::invalid_argument("Unhandled ImageFormat in GenerateFilename");
    }
    
    std::filesystem::path fullPath = dirPath / filename.str();
    return fullPath.wstring();
}

// IMPROVED: Desktop duplication with cancellation checks
HRESULT SetupDesktopDuplication(
    Microsoft::WRL::ComPtr<ID3D11Device>& device,
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context,
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication>& duplication,
    std::atomic<bool>* cancelFlag) {
    
    logger::info("Setting up desktop duplication");
    
    HRESULT hr = S_OK;
    
    // ADDED: Check cancellation before starting
    if (CheckCancellation(cancelFlag, "desktop duplication setup start")) {
        return E_ABORT;
		
    }
    
    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context
    );
    
    if (FAILED(hr)) {
        logger::error("Failed to create D3D11 device: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // ADDED: Check cancellation after device creation
    if (CheckCancellation(cancelFlag, "D3D11 device creation")) {
        return E_ABORT;
    }
    
    logger::debug("D3D11 device created successfully");
    
    // Get DXGI device
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = device.As(&dxgiDevice);
    if (FAILED(hr)) {
        logger::error("Failed to get DXGI device: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // ADDED: Check cancellation after DXGI device
    if (CheckCancellation(cancelFlag, "DXGI device acquisition")) {
        return E_ABORT;
    }
    
    // Get DXGI adapter
    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        logger::error("Failed to get DXGI adapter: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // ADDED: Check cancellation after adapter
    if (CheckCancellation(cancelFlag, "DXGI adapter acquisition")) {
        return E_ABORT;
    }
    
    // Get primary output
    Microsoft::WRL::ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) {
        logger::error("Failed to get primary output: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // ADDED: Check cancellation after output enumeration
    if (CheckCancellation(cancelFlag, "primary output enumeration")) {
        return E_ABORT;
    }
    
    // Get output1 interface
    Microsoft::WRL::ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) {
        logger::error("Failed to get IDXGIOutput1: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // Create desktop duplication
    hr = dxgiOutput1->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(hr)) {
        logger::error("Failed to create desktop duplication: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // ADDED: Final cancellation check
    if (CheckCancellation(cancelFlag, "desktop duplication creation")) {
        return E_ABORT;
    }
    
    logger::info("Desktop duplication setup completed successfully");
    return S_OK;
}

// IMPROVED: Frame capture with cancellation checks
HRESULT CaptureSingleFrame(
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    IDXGIOutputDuplication* pDuplication,
    DirectX::ScratchImage& outImage,
    std::atomic<bool>* cancelFlag) {
    
    HRESULT hr = S_OK;
    
    // ADDED: Check cancellation before starting
    if (CheckCancellation(cancelFlag, "frame capture start")) {
        return E_ABORT;
    }
    
    // Get frame info
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;
    
    // Try to acquire frame with timeout and cancellation checks
    const int maxRetries = 5;
    for (int retry = 0; retry < maxRetries; ++retry) {
        // ADDED: Check cancellation before each retry
        if (CheckCancellation(cancelFlag, "frame acquisition retry " + std::to_string(retry))) {
            return E_ABORT;
        }
        
        hr = pDuplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
        
        if (SUCCEEDED(hr)) {
            break;
        } else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            logger::debug("Frame acquisition timeout, retry {}/{}", retry + 1, maxRetries);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        } else {
            logger::error("Failed to acquire frame: 0x{:08X}", static_cast<uint32_t>(hr));
            return hr;
        }
    }
    
    if (FAILED(hr)) {
        logger::error("Failed to acquire frame after {} retries", maxRetries);
        return hr;
    }
    
    // ADDED: Check cancellation after frame acquisition
    if (CheckCancellation(cancelFlag, "frame acquisition complete")) {
        pDuplication->ReleaseFrame();
        return E_ABORT;
    }
    
    // Get texture interface
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        pDuplication->ReleaseFrame();
        logger::error("Failed to get texture interface: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // Get texture description
    D3D11_TEXTURE2D_DESC textureDesc;
    desktopTexture->GetDesc(&textureDesc);
    
    // ADDED: Check cancellation before staging texture creation
    if (CheckCancellation(cancelFlag, "staging texture preparation")) {
        pDuplication->ReleaseFrame();
        return E_ABORT;
    }
    
    // Create staging texture for CPU access
    D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
    hr = pDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        pDuplication->ReleaseFrame();
        logger::error("Failed to create staging texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // ADDED: Check cancellation before copy operation
    if (CheckCancellation(cancelFlag, "texture copy operation")) {
        pDuplication->ReleaseFrame();
        return E_ABORT;
    }
    
    // Copy desktop texture to staging texture
    pContext->CopyResource(stagingTexture.Get(), desktopTexture.Get());
    
    // ADDED: Check cancellation before mapping
    if (CheckCancellation(cancelFlag, "texture mapping")) {
        pDuplication->ReleaseFrame();
        return E_ABORT;
    }
    
    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = pContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        pDuplication->ReleaseFrame();
        logger::error("Failed to map staging texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // Initialize DirectX image
    hr = outImage.Initialize2D(
        textureDesc.Format,
        textureDesc.Width,
        textureDesc.Height,
        1,
        1
    );
    
    if (FAILED(hr)) {
        pContext->Unmap(stagingTexture.Get(), 0);
        pDuplication->ReleaseFrame();
        logger::error("Failed to initialize DirectX image: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }
    
    // ADDED: Check cancellation before pixel data copy
    if (CheckCancellation(cancelFlag, "pixel data copy")) {
        pContext->Unmap(stagingTexture.Get(), 0);
        pDuplication->ReleaseFrame();
        return E_ABORT;
    }
    
    // Copy pixel data
    const DirectX::Image* img = outImage.GetImage(0, 0, 0);
    if (!img) {
        pContext->Unmap(stagingTexture.Get(), 0);
        pDuplication->ReleaseFrame();
        logger::error("Failed to get DirectX image pointer");
        return E_FAIL;
    }
    
    const uint8_t* srcData = static_cast<const uint8_t*>(mappedResource.pData);
    uint8_t* dstData = img->pixels;
    
    size_t srcRowPitch = mappedResource.RowPitch;
    size_t dstRowPitch = img->rowPitch;
    size_t rowSize = std::min(srcRowPitch, dstRowPitch);
    
    // ADDED: Check cancellation periodically during pixel copy for large images
    for (size_t y = 0; y < textureDesc.Height; ++y) {
        // Check cancellation every 100 rows for very large images
        if (y % 100 == 0 && CheckCancellation(cancelFlag, "pixel copy row " + std::to_string(y))) {
            pContext->Unmap(stagingTexture.Get(), 0);
            pDuplication->ReleaseFrame();
            return E_ABORT;
        }
        memcpy(dstData + y * dstRowPitch, srcData + y * srcRowPitch, rowSize);
    }
    
    // Cleanup
    pContext->Unmap(stagingTexture.Get(), 0);
    pDuplication->ReleaseFrame();
    
    logger::debug("Frame captured successfully ({}x{})", textureDesc.Width, textureDesc.Height);
    return S_OK;
}

// IMPROVED: WIC save with cancellation checks
HRESULT SaveToWIC(const DirectX::ScratchImage& image, const CaptureParams& params, const std::wstring& filepath) {
    logger::info("Saving to WIC format: {}", static_cast<int>(params.format));
    
    // ADDED: Check cancellation before starting
    if (CheckCancellation(params.cancelFlag, "WIC save start")) {
        return E_ABORT;
    }
    
    HRESULT hr = S_OK;
    
    // Determine WIC container format
    GUID containerFormat;
    switch (params.format) {
        case ImageFormat::PNG:
            containerFormat = GUID_ContainerFormatPng;
            break;
        case ImageFormat::JPEG:
            containerFormat = GUID_ContainerFormatJpeg;
            break;
        case ImageFormat::BMP:
            containerFormat = GUID_ContainerFormatBmp;
            break;
        case ImageFormat::TIF:
            containerFormat = GUID_ContainerFormatTiff;
            break;
        case ImageFormat::GIF:
            containerFormat = GUID_ContainerFormatGif;
            break;
        default:
            logger::error("SaveToWIC: unhandled ImageFormat value {}", static_cast<int>(params.format));
            throw std::invalid_argument("Unhandled ImageFormat in SaveToWIC");
    }
    
    // Initialize COM
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInitialized = SUCCEEDED(hr);
    
    try {
        // ADDED: Check cancellation before WIC factory creation
        if (CheckCancellation(params.cancelFlag, "WIC factory creation")) {
            if (comInitialized) CoUninitialize();
            return E_ABORT;
        }
        
        // Create WIC factory
        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, 
                             IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) {
            logger::error("Failed to create WIC factory: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        // ADDED: Check cancellation before save operation
        if (CheckCancellation(params.cancelFlag, "WIC save operation")) {
            if (comInitialized) CoUninitialize();
            return E_ABORT;
        }
        
        // Save using DirectXTex WIC functions with format-specific settings
        if (params.format == ImageFormat::JPEG) {
            hr = DirectX::SaveToWICFile(
                *image.GetImage(0, 0, 0),
                DirectX::WIC_FLAGS_NONE,
                containerFormat,
                filepath.c_str(),
                nullptr,
                [&](IPropertyBag2* props) -> HRESULT {
                    // ADDED: Check cancellation in property callback
                    if (CheckCancellation(params.cancelFlag, "JPEG property setting")) {
                        return E_ABORT;
                    }
                    if (props) {
                        PROPBAG2 option = {};
                        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
                        VARIANT varValue;
                        VariantInit(&varValue);
                        varValue.vt = VT_R4;
                        varValue.fltVal = params.jpegQuality / 100.0f;
                        return props->Write(1, &option, &varValue);
                    }
                    return S_OK;
                }
            );
        } else if (params.format == ImageFormat::TIF) {
            hr = DirectX::SaveToWICFile(
                *image.GetImage(0, 0, 0),
                DirectX::WIC_FLAGS_NONE,
                GUID_ContainerFormatTiff,
                filepath.c_str(),
                nullptr,
                [&](IPropertyBag2* props) -> HRESULT {
                    // ADDED: Check cancellation in property callback
                    if (CheckCancellation(params.cancelFlag, "TIFF property setting")) {
                        return E_ABORT;
                    }
                    if (props) {
                        PROPBAG2 option = {};
                        option.pstrName = const_cast<LPOLESTR>(L"TiffCompressionMethod");
                        VARIANT varValue;
                        VariantInit(&varValue);
                        varValue.vt = VT_UI1;
                        
                        switch (params.tiffMode) {
                            case TiffMode::LZW:
                                varValue.bVal = static_cast<BYTE>(WICTiffCompressionLZW);
                                break;
                            case TiffMode::ZIP:
                                varValue.bVal = static_cast<BYTE>(WICTiffCompressionZIP);
                                break;
                            case TiffMode::RLE:
                                varValue.bVal = static_cast<BYTE>(WICTiffCompressionRLE);
                                break;
                            case TiffMode::CCITT1D:
                                varValue.bVal = static_cast<BYTE>(WICTiffCompressionCCITT3);
                                break;
                            case TiffMode::CCITT4:
                                varValue.bVal = static_cast<BYTE>(WICTiffCompressionCCITT4);
                                break;
                            case TiffMode::NONE:
                            default:
                                varValue.bVal = static_cast<BYTE>(WICTiffCompressionNone);
                                break;
                        }
                        
                        return props->Write(1, &option, &varValue);
                    }
                    return S_OK;
                }
            );
        } else if (params.format == ImageFormat::PNG) {
            // Use manual WIC encoding for PNG to ensure correct color handling
            // This matches how APNG encodes frames and avoids washed-out colors
            const DirectX::Image* img = image.GetImage(0, 0, 0);
            if (!img) {
                logger::error("Failed to get image for PNG encoding");
                if (comInitialized) CoUninitialize();
                return E_FAIL;
            }
            
            // Determine source format based on DXGI format
            WICPixelFormatGUID srcFormat = GUID_WICPixelFormat32bppBGRA;
            if (img->format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                srcFormat = GUID_WICPixelFormat32bppRGBA;
            }
            
            // Create stream for output file
            Microsoft::WRL::ComPtr<IWICStream> stream;
            hr = wicFactory->CreateStream(&stream);
            if (FAILED(hr)) {
                logger::error("Failed to create WIC stream for PNG: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            hr = stream->InitializeFromFilename(filepath.c_str(), GENERIC_WRITE);
            if (FAILED(hr)) {
                logger::error("Failed to initialize stream from filename: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            // Create PNG encoder
            Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
            hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
            if (FAILED(hr)) {
                logger::error("Failed to create PNG encoder: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (FAILED(hr)) {
                logger::error("Failed to initialize PNG encoder: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            // Create frame
            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
            hr = encoder->CreateNewFrame(&frame, nullptr);
            if (FAILED(hr)) {
                logger::error("Failed to create PNG frame: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            hr = frame->Initialize(nullptr);
            if (FAILED(hr)) {
                logger::error("Failed to initialize PNG frame: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            hr = frame->SetSize(static_cast<UINT>(img->width), static_cast<UINT>(img->height));
            if (FAILED(hr)) {
                logger::error("Failed to set PNG frame size: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            // Set pixel format - request BGRA output
            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
            hr = frame->SetPixelFormat(&pixelFormat);
            if (FAILED(hr)) {
                logger::error("Failed to set PNG pixel format: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            // Create bitmap from memory with correct source format
            Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
            hr = wicFactory->CreateBitmapFromMemory(
                static_cast<UINT>(img->width),
                static_cast<UINT>(img->height),
                srcFormat,
                static_cast<UINT>(img->rowPitch),
                static_cast<UINT>(img->slicePitch),
                img->pixels,
                &wicBitmap);
            if (FAILED(hr)) {
                logger::error("Failed to create WIC bitmap from memory: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            hr = frame->WriteSource(wicBitmap.Get(), nullptr);
            if (FAILED(hr)) {
                logger::error("Failed to write PNG frame source: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            hr = frame->Commit();
            if (FAILED(hr)) {
                logger::error("Failed to commit PNG frame: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            hr = encoder->Commit();
            if (FAILED(hr)) {
                logger::error("Failed to commit PNG encoder: 0x{:08X}", static_cast<uint32_t>(hr));
                if (comInitialized) CoUninitialize();
                return hr;
            }
            
            logger::info("PNG saved with manual WIC encoding (correct color handling)");
        } else {
            // BMP and static GIF use DirectXTex
            hr = DirectX::SaveToWICFile(
                *image.GetImage(0, 0, 0),
                DirectX::WIC_FLAGS_NONE,
                containerFormat,
                filepath.c_str()
            );
        }
        
        if (FAILED(hr)) {
            logger::error("Failed to save WIC file: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        logger::info("WIC file saved successfully");
        
    } catch (const std::exception& e) {
        logger::error("Exception during WIC save: {}", e.what());
        hr = E_FAIL;
    } catch (...) {
        logger::error("Unknown exception during WIC save");
        hr = E_FAIL;
    }
    
    if (comInitialized) {
        CoUninitialize();
    }
    
    return hr;
}

// IMPROVED: DDS save with cancellation checks during compression
HRESULT SaveToDDS(const DirectX::ScratchImage& image, const CaptureParams& params, const std::wstring& filepath) {
    logger::info("Saving to DDS format with compression: {}", static_cast<int>(params.ddsMode));
    
    // ADDED: Check cancellation before starting
    if (CheckCancellation(params.cancelFlag, "DDS save start")) {
        return E_ABORT;
    }
    
    HRESULT hr = S_OK;
    
    try {
        DirectX::ScratchImage compressedImage;
        const DirectX::ScratchImage* imageToSave = &image;
        
        // Determine compression format and flags
        DXGI_FORMAT compressFormat;
        DirectX::TEX_COMPRESS_FLAGS compressFlags = DirectX::TEX_COMPRESS_DEFAULT;
        bool needsCompression = true;
        
        switch (params.ddsMode) {
            case DDSCompression::BC1: 
                compressFormat = DXGI_FORMAT_BC1_UNORM;
                compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                break;
            case DDSCompression::BC2: 
                compressFormat = DXGI_FORMAT_BC2_UNORM;
                compressFlags = DirectX::TEX_COMPRESS_DEFAULT;
                break;
            case DDSCompression::BC3: 
                compressFormat = DXGI_FORMAT_BC3_UNORM;
                compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                break;
            case DDSCompression::BC4: 
                compressFormat = DXGI_FORMAT_BC4_UNORM;
                compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                break;
            case DDSCompression::BC5: 
                compressFormat = DXGI_FORMAT_BC5_UNORM;
                compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                break;
            case DDSCompression::BC6H: 
                compressFormat = DXGI_FORMAT_BC6H_UF16;
                compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                break;
            case DDSCompression::BC7_SLOW: 
                compressFormat = DXGI_FORMAT_BC7_UNORM;
                compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                logger::info("Using BC7 SLOW mode (highest quality, parallel processing)");
                break;
            case DDSCompression::BC7_NORMAL:
                compressFormat = DXGI_FORMAT_BC7_UNORM;
                compressFlags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                    DirectX::TEX_COMPRESS_PARALLEL | DirectX::TEX_COMPRESS_BC7_QUICK);
                logger::info("Using BC7 NORMAL mode (balanced quality/speed, parallel processing)");
                break;
            case DDSCompression::BC7_FAST:
                compressFormat = DXGI_FORMAT_BC7_UNORM;
                compressFlags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                    DirectX::TEX_COMPRESS_PARALLEL | DirectX::TEX_COMPRESS_BC7_QUICK);
                logger::info("Using BC7 FAST mode (fastest BC7, parallel processing)");
                break;
            default: 
                logger::warn("Unknown DDS compression mode: {}, using BC1", static_cast<int>(params.ddsMode));
                compressFormat = DXGI_FORMAT_BC1_UNORM;
                compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                break;
        }
        
        logger::info("DDS Compression Details:");
        logger::info("  - Format: 0x{:08X}", static_cast<uint32_t>(compressFormat));
        logger::info("  - Flags: 0x{:08X}", static_cast<uint32_t>(compressFlags));
        logger::info("  - Parallel processing: {}", 
                    (compressFlags & DirectX::TEX_COMPRESS_PARALLEL) != 0 ? "ENABLED" : "DISABLED");
        
        if (needsCompression) {
            // ADDED: Check cancellation before compression
            if (CheckCancellation(params.cancelFlag, "DDS compression start")) {
                return E_ABORT;
            }
            
            auto compressionStart = std::chrono::high_resolution_clock::now();
            logger::info("Starting DDS compression with parallel processing...");
            
            const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
            if (!srcImage) {
                logger::error("Source image is null");
                return E_FAIL;
            }
            
            logger::debug("Source image: {}x{}, format: 0x{:08X}", 
                         srcImage->width, srcImage->height, static_cast<uint32_t>(srcImage->format));
            
            // IMPROVED: Custom compression with periodic cancellation checks
            // Note: DirectXTex doesn't provide built-in cancellation, but we can check before/after
            
            // Start compression - this is the longest operation for DDS
            hr = DirectX::Compress(
                *srcImage,
                compressFormat,
                compressFlags,
                DirectX::TEX_THRESHOLD_DEFAULT,
                compressedImage
            );
            
            // ADDED: Check cancellation immediately after compression
            if (CheckCancellation(params.cancelFlag, "DDS compression complete")) {
                return E_ABORT;
            }
            
            auto compressionEnd = std::chrono::high_resolution_clock::now();
            auto compressionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                compressionEnd - compressionStart);
            
            if (FAILED(hr)) {
                logger::error("DDS Compression failed: 0x{:08X} after {}ms", 
                             static_cast<uint32_t>(hr), compressionDuration.count());
                logger::info("Falling back to uncompressed DDS");
                imageToSave = &image;
            } else {
                imageToSave = &compressedImage;
                logger::info("DDS compression completed successfully in {}ms", compressionDuration.count());
                
                size_t originalSize = srcImage->slicePitch;
                size_t compressedSize = compressedImage.GetImage(0, 0, 0)->slicePitch;
                float ratio = static_cast<float>(originalSize) / static_cast<float>(compressedSize);
                logger::info("Compression ratio: {:.2f}:1 ({} -> {} bytes)", 
                           ratio, originalSize, compressedSize);
            }
        }
        
        // ADDED: Check cancellation before file save
        if (CheckCancellation(params.cancelFlag, "DDS file save")) {
            return E_ABORT;
        }
        
        logger::debug("Saving DDS file to: {}", util::wstring_to_utf8(filepath));
        
        auto saveStart = std::chrono::high_resolution_clock::now();
        
        // Save DDS file
        hr = DirectX::SaveToDDSFile(
            imageToSave->GetImages(),
            imageToSave->GetImageCount(),
            imageToSave->GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            filepath.c_str()
        );
        
        auto saveEnd = std::chrono::high_resolution_clock::now();
        auto saveDuration = std::chrono::duration_cast<std::chrono::milliseconds>(saveEnd - saveStart);
        
        if (FAILED(hr)) {
            logger::error("Failed to save DDS file: 0x{:08X} after {}ms", 
                         static_cast<uint32_t>(hr), saveDuration.count());
            return hr;
        }
        
        // ADDED: Check cancellation after file save
        if (CheckCancellation(params.cancelFlag, "DDS file verification")) {
            return E_ABORT;
        }
        
        // Verify file was created and get size
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(filepath, ec);
        if (ec) {
            logger::error("Failed to get DDS file size: {}", ec.message());
        } else {
            logger::info("DDS file saved successfully in {}ms ({} bytes)", 
                        saveDuration.count(), fileSize);
        }
        
    } catch (const std::exception& e) {
        logger::error("Exception during DDS save: {}", e.what());
        hr = E_FAIL;
    } catch (...) {
        logger::error("Unknown exception during DDS save");
        hr = E_FAIL;
    }
    
    return hr;
}

// IMPROVED: Main capture functions with cancellation support

CaptureResult CaptureScreen(const CaptureParams& params) {
    logger::info("=== Starting single frame capture ===");
    logger::info("Format: {}, Base path: {}", static_cast<int>(params.format),
                 util::wstring_to_utf8(params.basePath));

    CaptureResult result{};
    try {
        CancelIfRequested(params.cancelFlag, "capture start");

        // Setup D3D + duplication
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;

        HRESULT hr = SetupDesktopDuplication(device, context, duplication, params.cancelFlag);
        if (hr == E_ABORT) {
            result.success = false;
            result.message = "Cancelled during desktop duplication setup";
            return result;
        }
        if (FAILED(hr)) {
            result.success = false;
            result.message = "Failed to setup desktop duplication: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        // Capture one frame
        DirectX::ScratchImage image;
        hr = CaptureSingleFrame(device.Get(), context.Get(), duplication.Get(), image, params.cancelFlag);
        if (hr == E_ABORT) {
            result.success = false;
            result.message = "Cancelled during frame capture";
            return result;
        }
        if (FAILED(hr)) {
            result.success = false;
            result.message = "Failed to capture frame: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        // Fire early callback after frame capture (before save/compress) so callers can restore UI promptly.
        // NOTE: The callback must enqueue UI work on the main thread.
        if (params.onFramesCaptured) {
            params.onFramesCaptured();
        }

        CancelIfRequested(params.cancelFlag, "before file save");

        // Generate output path
        std::wstring outputPath = GenerateFilename(params.basePath, params.format);

        // Save according to format
        if (params.format == ImageFormat::DDS) {
            hr = SaveToDDS(image, params, outputPath);
        } else {
            hr = SaveToWIC(image, params, outputPath);
        }

        if (hr == E_ABORT) {
            result.success = false;
            result.message = "Cancelled during image save";
            return result;
        }
        if (FAILED(hr)) {
            result.success = false;
            result.message = "Failed to save image: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        CancelIfRequested(params.cancelFlag, "file verification");

        // Verify file exists and is non-empty
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(outputPath, ec);
        if (ec || fileSize == 0) {
            result.success = false;
            result.message = "Image file creation failed - file is empty or doesn't exist";
            return result;
        }

        result.success = true;
        result.message = "Single frame capture completed successfully";
        result.filepath = outputPath;

        logger::info("Image saved: {} ({} bytes)",
                     util::wstring_to_utf8(outputPath), fileSize);
    } catch (const Cancelled&) {
        result.success = false;
        result.message = "Cancelled";
    } catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("Exception during capture: ") + e.what();
        logger::error("{}", result.message);
    } catch (...) {
        result.success = false;
        result.message = "Unknown exception during capture";
        logger::error("{}", result.message);
    }
    return result;
}


// Helper function to create temp directory for GIF frames
static std::wstring CreateGifTempDirectory(const std::wstring& basePath) {
    // Create a unique temp directory based on timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
    localtime_s(&tm_buf, &time_t_now);

    wchar_t timestamp[64];
    swprintf_s(timestamp, L"gif_temp_%04d%02d%02d_%02d%02d%02d_%03d",
               tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
               static_cast<int>(ms.count()));

    std::wstring tempDir = basePath + L"\\" + timestamp;

    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    if (ec) {
        logger::error("Failed to create temp directory: {}", ec.message());
        return L"";
    }

    logger::info("Created temp directory for GIF frames: {}", util::wstring_to_utf8(tempDir));
    return tempDir;
}

// Helper function to save a single frame to disk as BMP (fast read/write)
static HRESULT SaveTempFrame(const DirectX::ScratchImage& image, const std::wstring& filepath) {
    const DirectX::Image* img = image.GetImage(0, 0, 0);
    if (!img) {
        return E_FAIL;
    }

    // Save as BMP for fast I/O (no compression overhead)
    HRESULT hr = DirectX::SaveToWICFile(*img, DirectX::WIC_FLAGS_NONE,
                                         GUID_ContainerFormatBmp, filepath.c_str());
    return hr;
}

// Helper function to load a frame from disk
static HRESULT LoadTempFrame(const std::wstring& filepath, DirectX::ScratchImage& image) {
    HRESULT hr = DirectX::LoadFromWICFile(filepath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);
    return hr;
}

// Helper function to clean up temp directory and files
static void CleanupTempDirectory(const std::wstring& tempDir, const std::vector<std::wstring>& tempFiles) {
    std::error_code ec;

    // Delete all temp frame files
    for (const auto& file : tempFiles) {
        std::filesystem::remove(file, ec);
        if (ec) {
            logger::warn("Failed to delete temp file {}: {}", util::wstring_to_utf8(file), ec.message());
        }
    }

    // Remove the temp directory
    if (!tempDir.empty()) {
        std::filesystem::remove(tempDir, ec);
        if (ec) {
            logger::warn("Failed to remove temp directory {}: {}", util::wstring_to_utf8(tempDir), ec.message());
        } else {
            logger::debug("Cleaned up temp directory: {}", util::wstring_to_utf8(tempDir));
        }
    }
}

// IMPROVED: GIF capture with disk-based frame storage to prevent memory overflow

CaptureResult CaptureGIF(const CaptureParams& params) {
    logger::info("=== Starting GIF capture (disk-based frame storage) ===");
    logger::info("Duration: {}s, FPS: {}, LoopCount: {}, Compression: {}, Optimize: {}, Base path: {}",
                 params.gifDuration, params.gifFPS, params.gifLoopCount, params.gifCompression,
                 params.gifOptimize, util::wstring_to_utf8(params.basePath));

    CaptureResult result{};
    std::wstring tempDir;
    std::vector<std::wstring> tempFrameFiles;

    try {
        CancelIfRequested(params.cancelFlag, "GIF capture start");

        // Create temp directory for intermediate frames
        tempDir = CreateGifTempDirectory(params.basePath);
        if (tempDir.empty()) {
            result.success = false;
            result.message = "Failed to create temp directory for GIF frames";
            return result;
        }

        // Setup D3D + duplication
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;

        HRESULT hr = SetupDesktopDuplication(device, context, duplication, params.cancelFlag);
        if (hr == E_ABORT) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "Cancelled during GIF desktop duplication setup";
            return result;
        }
        if (FAILED(hr)) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "Failed to setup desktop duplication for GIF: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        // Determine frame count based on FPS parameter (clamp to 1-30 fps range)
        const double fps = std::clamp(static_cast<double>(params.gifFPS), 1.0, 30.0);
        const int totalFrames = std::max(1, static_cast<int>(std::round(params.gifDuration * fps)));
        const double frameInterval = 1.0 / fps;

        logger::info("Capturing {} frames at {} fps (storing to disk)", totalFrames, fps);

        const auto startTime = std::chrono::high_resolution_clock::now();

        // Capture frames and save to disk immediately to prevent memory overflow
        for (int frame = 0; frame < totalFrames; ++frame) {
            CancelIfRequested(params.cancelFlag, "GIF frame loop");

            DirectX::ScratchImage img;
            hr = CaptureSingleFrame(device.Get(), context.Get(), duplication.Get(), img, params.cancelFlag);
            if (hr == E_ABORT) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                result.success = false;
                result.message = "Cancelled during GIF frame capture";
                return result;
            }
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                result.success = false;
                result.message = "Failed to capture GIF frame: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            // Save frame to disk immediately and release memory
            wchar_t frameFilename[MAX_PATH];
            swprintf_s(frameFilename, L"%s\\frame_%04d.bmp", tempDir.c_str(), frame);
            std::wstring framePath = frameFilename;

            hr = SaveTempFrame(img, framePath);
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                result.success = false;
                result.message = "Failed to save temp frame to disk: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            tempFrameFiles.push_back(framePath);

            // Release image memory immediately after saving to disk
            img.Release();

            logger::debug("Captured and saved frame {}/{} to disk", frame + 1, totalFrames);

            // Sleep until next frame while allowing cancellation
            // Use (frame + 1) to ensure proper timing: after capturing frame N, wait until time for frame N+1
            auto target = startTime + std::chrono::duration<double>((frame + 1) * frameInterval);
            while (std::chrono::high_resolution_clock::now() < target) {
                CancelIfRequested(params.cancelFlag, "GIF frame timing wait");
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }

   if (tempFrameFiles.empty()) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "No frames captured for GIF";
            return result;
        }
		
        if (params.onFramesCaptured) {
            // Fire early callback to restore UI before encoding starts
            params.onFramesCaptured();
        }

        logger::info("All {} frames captured to disk, starting GIF encoding...", tempFrameFiles.size());
        CancelIfRequested(params.cancelFlag, "before GIF encode");

        // Build output path
        std::wstring outputPath = GenerateFilename(params.basePath, ImageFormat::GIF);

        // Encode GIF using WIC
        bool comInitialized = false;
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            // Already initialized differently; treat as OK
            comInitialized = false;
        } else {
            result.success = false;
            result.message = "COM initialization failed for GIF encoder: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to create WIC factory: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        Microsoft::WRL::ComPtr<IWICStream> stream;
        hr = wicFactory->CreateStream(&stream);
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to create WIC stream: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        hr = stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to open output file for GIF: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        hr = wicFactory->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder);
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to create GIF encoder: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to initialize GIF encoder: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> metadata;
        encoder->GetMetadataQueryWriter(&metadata);
        if (metadata) {
            // Set up NETSCAPE2.0 application extension for animation looping
            PROPVARIANT val;
            PropVariantInit(&val);
            val.vt = VT_UI1;
            val.bVal = 0;
            metadata->SetMetadataByName(L"/appext/Application", &val);
            PropVariantClear(&val);

            // Application identifier
            const BYTE netscape2_0[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
            PROPVARIANT appId; PropVariantInit(&appId);
            appId.vt = VT_VECTOR | VT_UI1;
            appId.caub.pElems = const_cast<BYTE*>(netscape2_0);
            appId.caub.cElems = 11;
            metadata->SetMetadataByName(L"/appext/Identification", &appId);
            appId.caub.pElems = nullptr;
            appId.caub.cElems = 0;
            appId.vt = VT_EMPTY;

            // Set loop count data: sub-block ID (1) + loop count (2 bytes little-endian)
            // Loop count: 0 = infinite, 1+ = specific number of loops
            BYTE loopData[4];
            loopData[0] = 3;  // Sub-block size
            loopData[1] = 1;  // Sub-block ID for looping
            loopData[2] = static_cast<BYTE>(params.gifLoopCount & 0xFF);         // Low byte
            loopData[3] = static_cast<BYTE>((params.gifLoopCount >> 8) & 0xFF);  // High byte

            PROPVARIANT loopVar; PropVariantInit(&loopVar);
            loopVar.vt = VT_VECTOR | VT_UI1;
            loopVar.caub.pElems = loopData;
            loopVar.caub.cElems = 4;
            metadata->SetMetadataByName(L"/appext/Data", &loopVar);
            loopVar.caub.pElems = nullptr;
            loopVar.caub.cElems = 0;
            loopVar.vt = VT_EMPTY;

            logger::debug("Set GIF loop count to: {}", params.gifLoopCount);
        }

        const int delayHundredths = static_cast<int>(std::round(frameInterval * 100.0));

        // For differential encoding (gifCompression == 1), we need to track the previous frame
        // gifOptimize controls whether to use true delta encoding (transparency) or just region extraction
        const bool useDifferentialEncoding = (params.gifCompression == 1);
        const bool useTrueDeltaEncoding = (params.gifOptimize == 1);
        std::vector<uint8_t> prevQuantizedPixels;  // 8-bit indexed pixels from previous frame
        UINT prevQuantizedStride = 0;
        size_t prevWidth = 0, prevHeight = 0;
        Microsoft::WRL::ComPtr<IWICPalette> prevPalette;  // Store palette for consistency

        if (useDifferentialEncoding) {
            if (useTrueDeltaEncoding) {
                logger::info("Using TRUE DELTA encoding (transparency-based pixel differences)");
            } else {
                logger::info("Using DIFFERENTIAL frame encoding (region extraction only)");
            }
        } else {
            logger::info("Using full frame encoding");
        }

        // Encode frames by loading each one from disk (one at a time to save memory)
        for (size_t i = 0; i < tempFrameFiles.size(); ++i) {
            CancelIfRequested(params.cancelFlag, "GIF encode frame");

            // Load frame from disk
            DirectX::ScratchImage frameImage;
            hr = LoadTempFrame(tempFrameFiles[i], frameImage);
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Failed to load temp frame from disk: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            const DirectX::Image* img = frameImage.GetImage(0, 0, 0);
            if (!img) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "GIF frame image is null";
                return result;
            }

            // Determine source pixel format based on the loaded image format
            WICPixelFormatGUID srcFormat = GUID_WICPixelFormat32bppBGRA;
            if (img->format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                srcFormat = GUID_WICPixelFormat32bppBGRA;
            } else if (img->format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                srcFormat = GUID_WICPixelFormat32bppRGBA;
            }

            // Variables for the region to encode (full frame or differential)
            UINT frameLeft = 0, frameTop = 0;
            UINT frameWidth = static_cast<UINT>(img->width);
            UINT frameHeight = static_cast<UINT>(img->height);
            Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;

            // Track transparency for this frame
            bool useTransparency = false;
            BYTE transparentIdx = 0;

            // FIXED: Differential encoding - quantize first, then compare in 8-bit space
            if (useDifferentialEncoding && i > 0 && !prevQuantizedPixels.empty() &&
                prevWidth == img->width && prevHeight == img->height) {

                // First, create bitmap from current frame
                Microsoft::WRL::ComPtr<IWICBitmap> fullBitmap;
                hr = wicFactory->CreateBitmapFromMemory(
                    static_cast<UINT>(img->width),
                    static_cast<UINT>(img->height),
                    srcFormat,
                    static_cast<UINT>(img->rowPitch),
                    static_cast<UINT>(img->slicePitch),
                    img->pixels,
                    &fullBitmap);

                if (FAILED(hr)) {
                    logger::warn("Failed to create full bitmap for differential, using full frame");
                    goto full_frame_encoding;
                }

                // Convert to 8-bit indexed using WIC format converter
                Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
                hr = wicFactory->CreateFormatConverter(&converter);
                if (FAILED(hr)) goto full_frame_encoding;

                hr = converter->Initialize(
                    fullBitmap.Get(),
                    GUID_WICPixelFormat8bppIndexed,
                    WICBitmapDitherTypeErrorDiffusion,
                    prevPalette.Get(),  // Use previous frame's palette for consistency
                    0.0,
                    WICBitmapPaletteTypeMedianCut);
                if (FAILED(hr)) goto full_frame_encoding;

                // Create bitmap from converted source to get access to pixels
                Microsoft::WRL::ComPtr<IWICBitmap> quantizedBitmap;
                hr = wicFactory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnDemand, &quantizedBitmap);
                if (FAILED(hr)) goto full_frame_encoding;

                // Lock current quantized frame
                WICRect lockRect = { 0, 0, static_cast<INT>(img->width), static_cast<INT>(img->height) };
                Microsoft::WRL::ComPtr<IWICBitmapLock> currLock;
                hr = quantizedBitmap->Lock(&lockRect, WICBitmapLockRead, &currLock);
                if (FAILED(hr)) goto full_frame_encoding;

                UINT currStride = 0;
                UINT currBufferSize = 0;
                BYTE* currData = nullptr;
                hr = currLock->GetDataPointer(&currBufferSize, &currData);
                if (FAILED(hr)) goto full_frame_encoding;
                currLock->GetStride(&currStride);

                // Find bounding box of changed pixels (comparing quantized 8-bit values)
                UINT minX = static_cast<UINT>(img->width), minY = static_cast<UINT>(img->height);
                UINT maxX = 0, maxY = 0;
                bool hasChanges = false;

                for (UINT y = 0; y < static_cast<UINT>(img->height); ++y) {
                    const uint8_t* currRow = currData + y * currStride;
                    const uint8_t* prevRow = prevQuantizedPixels.data() + y * prevQuantizedStride;

                    for (UINT x = 0; x < static_cast<UINT>(img->width); ++x) {
                        if (currRow[x] != prevRow[x]) {
                            hasChanges = true;
                            minX = std::min(minX, x);
                            maxX = std::max(maxX, x);
                            minY = std::min(minY, y);
                            maxY = std::max(maxY, y);
                        }
                    }
                }

                if (hasChanges && maxX >= minX && maxY >= minY) {
                    frameLeft = minX;
                    frameTop = minY;
                    frameWidth = maxX - minX + 1;
                    frameHeight = maxY - minY + 1;

                    // Create differential frame data
                    std::vector<uint8_t> diffPixels(frameWidth * frameHeight);
                    size_t unchangedPixelCount = 0;  // Track transparent pixels for stats

                    if (useTrueDeltaEncoding) {
                        // TRUE DELTA ENCODING: Mark unchanged pixels as transparent
                        // Find a transparent color index (least used in current frame)
                        transparentIdx = FindTransparentIndex(currData, img->width * img->height);

                        for (UINT dy = 0; dy < frameHeight; ++dy) {
                            UINT srcY = frameTop + dy;
                            const uint8_t* currRow = currData + srcY * currStride;
                            const uint8_t* prevRow = prevQuantizedPixels.data() + srcY * prevQuantizedStride;

                            for (UINT dx = 0; dx < frameWidth; ++dx) {
                                UINT srcX = frameLeft + dx;
                                if (currRow[srcX] == prevRow[srcX]) {
                                    // Unchanged pixel - mark as transparent
                                    diffPixels[dy * frameWidth + dx] = transparentIdx;
                                    useTransparency = true;
                                    unchangedPixelCount++;
                                } else {
                                    // Changed pixel - use actual color
                                    diffPixels[dy * frameWidth + dx] = currRow[srcX];
                                }
                            }
                        }

                        // Log transparency stats for this frame
                        size_t totalDiffPixels = frameWidth * frameHeight;
                        size_t fullFramePixels = img->width * img->height;
                        float transparencyPercent = (totalDiffPixels > 0) ?
                            (unchangedPixelCount * 100.0f / totalDiffPixels) : 0.0f;
                        float regionPercent = (fullFramePixels > 0) ?
                            (totalDiffPixels * 100.0f / fullFramePixels) : 100.0f;

                        logger::debug("Frame {}: TRUE DELTA region {}x{} ({:.1f}% of full), {}/{} pixels transparent ({:.1f}%)",
                                     i, frameWidth, frameHeight, regionPercent,
                                     unchangedPixelCount, totalDiffPixels, transparencyPercent);
                    } else {
                        // REGION EXTRACTION ONLY: Copy all pixels in the changed region (no transparency)
                        for (UINT dy = 0; dy < frameHeight; ++dy) {
                            UINT srcY = frameTop + dy;
                            const uint8_t* currRow = currData + srcY * currStride;

                            for (UINT dx = 0; dx < frameWidth; ++dx) {
                                UINT srcX = frameLeft + dx;
                                diffPixels[dy * frameWidth + dx] = currRow[srcX];
                            }
                        }

                        // No transparency for region-only extraction
                        useTransparency = false;

                        float regionPercent = (static_cast<float>(frameWidth * frameHeight) /
                                              static_cast<float>(img->width * img->height)) * 100.0f;
                        logger::debug("Frame {}: REGION extraction {}x{} at ({},{}) - {:.1f}% of full frame",
                                     i, frameWidth, frameHeight, frameLeft, frameTop, regionPercent);
                    }

                    // Store quantized current frame for next comparison
                    prevQuantizedStride = currStride;
                    prevQuantizedPixels.resize(img->height * currStride);
                    memcpy(prevQuantizedPixels.data(), currData, img->height * currStride);
                    prevWidth = img->width;
                    prevHeight = img->height;

                    currLock.Reset();

                    // Get the palette from the quantized bitmap
                    Microsoft::WRL::ComPtr<IWICPalette> palette;
                    hr = wicFactory->CreatePalette(&palette);
                    if (SUCCEEDED(hr)) {
                        hr = quantizedBitmap->CopyPalette(palette.Get());
                    }

                    // For true delta encoding, modify palette to make transparent index fully transparent
                    if (SUCCEEDED(hr) && useTrueDeltaEncoding && useTransparency) {
                        UINT colorCount = 0;
                        palette->GetColorCount(&colorCount);

                        std::vector<WICColor> colors(colorCount);
                        UINT actualColorCount = 0;
                        palette->GetColors(colorCount, colors.data(), &actualColorCount);

                        // Set the transparent index color to fully transparent (ARGB: 0x00000000)
                        if (transparentIdx < actualColorCount) {
                            colors[transparentIdx] = 0x00000000;  // Fully transparent black
                            logger::debug("Frame {}: Set palette index {} to transparent (0x00000000)", i, transparentIdx);
                        }

                        // Re-initialize palette with modified colors
                        hr = palette->InitializeCustom(colors.data(), actualColorCount);
                    }

                    if (SUCCEEDED(hr)) {
                        // Create the differential frame bitmap
                        hr = wicFactory->CreateBitmap(
                            frameWidth, frameHeight,
                            GUID_WICPixelFormat8bppIndexed,
                            WICBitmapCacheOnDemand,
                            &wicBitmap);

                        if (SUCCEEDED(hr)) {
                            hr = wicBitmap->SetPalette(palette.Get());
                        }

                        if (SUCCEEDED(hr)) {
                            WICRect writeRect = { 0, 0, static_cast<INT>(frameWidth), static_cast<INT>(frameHeight) };
                            Microsoft::WRL::ComPtr<IWICBitmapLock> writeLock;
                            hr = wicBitmap->Lock(&writeRect, WICBitmapLockWrite, &writeLock);

                            if (SUCCEEDED(hr)) {
                                UINT writeStride = 0;
                                UINT writeSize = 0;
                                BYTE* writeData = nullptr;
                                writeLock->GetDataPointer(&writeSize, &writeData);
                                writeLock->GetStride(&writeStride);

                                for (UINT y = 0; y < frameHeight; ++y) {
                                    memcpy(writeData + y * writeStride,
                                           diffPixels.data() + y * frameWidth,
                                           frameWidth);
                                }
                            }
                        }
                    }

                    if (FAILED(hr) || !wicBitmap) {
                        logger::warn("Failed to create differential bitmap, falling back to full frame");
                        frameLeft = 0;
                        frameTop = 0;
                        frameWidth = static_cast<UINT>(img->width);
                        frameHeight = static_cast<UINT>(img->height);
                        useTransparency = false;
                        wicBitmap.Reset();
                        goto full_frame_encoding;
                    }

                    logger::debug("Differential frame {}: region ({},{}) {}x{} with {} transparency",
                                 i, frameLeft, frameTop, frameWidth, frameHeight,
                                 useTransparency ? "enabled" : "no");
                } else {
                    // No changes detected - use the already quantized bitmap as-is
                    // IMPORTANT: Do NOT goto full_frame_encoding as that would re-quantize
                    // and overwrite prevQuantizedPixels, breaking the comparison chain
                    wicBitmap = quantizedBitmap;
                    frameLeft = 0;
                    frameTop = 0;
                    frameWidth = static_cast<UINT>(img->width);
                    frameHeight = static_cast<UINT>(img->height);
                    useTransparency = false;

                    // Update stored data for next frame comparison
                    prevQuantizedStride = currStride;
                    prevQuantizedPixels.resize(img->height * currStride);
                    memcpy(prevQuantizedPixels.data(), currData, img->height * currStride);
                    prevWidth = img->width;
                    prevHeight = img->height;
                    currLock.Reset();

                    logger::debug("Frame {} identical to previous, using full quantized frame", i);
                }
            } else {
            full_frame_encoding:
                // Create full frame bitmap (for first frame or non-differential or fallback)
                if (!wicBitmap) {
                    Microsoft::WRL::ComPtr<IWICBitmap> fullBitmap;
                    hr = wicFactory->CreateBitmapFromMemory(
                        static_cast<UINT>(img->width),
                        static_cast<UINT>(img->height),
                        srcFormat,
                        static_cast<UINT>(img->rowPitch),
                        static_cast<UINT>(img->slicePitch),
                        img->pixels,
                        &fullBitmap);
                    if (FAILED(hr)) {
                        CleanupTempDirectory(tempDir, tempFrameFiles);
                        if (comInitialized) CoUninitialize();
                        result.success = false;
                        result.message = "Failed to create WIC bitmap from frame: HRESULT " + FormatHRESULT(hr);
                        return result;
                    }

                    // For differential encoding, quantize and store for next frame comparison
                    if (useDifferentialEncoding) {
                        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
                        hr = wicFactory->CreateFormatConverter(&converter);
                        if (SUCCEEDED(hr)) {
                            // Use existing palette for consistency, or generate new for first frame
                            // FIXED: Previously always used nullptr, breaking palette consistency
                            hr = converter->Initialize(
                                fullBitmap.Get(),
                                GUID_WICPixelFormat8bppIndexed,
                                WICBitmapDitherTypeErrorDiffusion,
                                prevPalette.Get(),  // Use existing palette if available (nullptr for first frame)
                                0.0,
                                WICBitmapPaletteTypeMedianCut);
                        }

                        if (SUCCEEDED(hr)) {
                            Microsoft::WRL::ComPtr<IWICBitmap> quantizedBitmap;
                            hr = wicFactory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnDemand, &quantizedBitmap);

                            if (SUCCEEDED(hr)) {
                                // Store palette for future frames
                                if (!prevPalette) {
                                    wicFactory->CreatePalette(&prevPalette);
                                }
                                if (prevPalette) {
                                    quantizedBitmap->CopyPalette(prevPalette.Get());
                                }

                                // Store quantized pixels for comparison
                                WICRect lockRect = { 0, 0, static_cast<INT>(img->width), static_cast<INT>(img->height) };
                                Microsoft::WRL::ComPtr<IWICBitmapLock> lock;
                                hr = quantizedBitmap->Lock(&lockRect, WICBitmapLockRead, &lock);
                                if (SUCCEEDED(hr)) {
                                    UINT stride = 0;
                                    UINT bufSize = 0;
                                    BYTE* data = nullptr;
                                    lock->GetDataPointer(&bufSize, &data);
                                    lock->GetStride(&stride);

                                    prevQuantizedStride = stride;
                                    prevQuantizedPixels.resize(img->height * stride);
                                    memcpy(prevQuantizedPixels.data(), data, img->height * stride);
                                    prevWidth = img->width;
                                    prevHeight = img->height;
                                }

                                // Use quantized bitmap for encoding
                                wicBitmap = quantizedBitmap;
                            }
                        }

                        // If quantization failed, fall back to original
                        if (FAILED(hr) || !wicBitmap) {
                            wicBitmap = fullBitmap;
                        }
                    } else {
                        wicBitmap = fullBitmap;
                    }

                    // Full frame encoding
                    frameLeft = 0;
                    frameTop = 0;
                    frameWidth = static_cast<UINT>(img->width);
                    frameHeight = static_cast<UINT>(img->height);
                    useTransparency = false;
                }
            }

            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEnc;
            Microsoft::WRL::ComPtr<IPropertyBag2> props;
            hr = encoder->CreateNewFrame(&frameEnc, &props);
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Failed to create GIF frame encoder: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            hr = frameEnc->Initialize(nullptr);
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Failed to initialize GIF frame: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            // Set frame metadata: delay and position via metadata query writer
            Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> metaWriter;
            hr = frameEnc->GetMetadataQueryWriter(&metaWriter);
            if (SUCCEEDED(hr) && metaWriter) {
                PROPVARIANT v; PropVariantInit(&v);
                v.vt = VT_UI2;
                v.uiVal = static_cast<USHORT>(delayHundredths);
                metaWriter->SetMetadataByName(L"/grctlext/Delay", &v);
                PropVariantClear(&v);

                // Set disposal method: 1 = do not dispose (keep previous frame) for differential encoding
                PropVariantInit(&v);
                v.vt = VT_UI1;
                v.bVal = useDifferentialEncoding ? 1 : 2; // 1=leave, 2=restore to background
                metaWriter->SetMetadataByName(L"/grctlext/Disposal", &v);
                PropVariantClear(&v);

                // Set frame position for differential encoding
                if (useDifferentialEncoding && (frameLeft > 0 || frameTop > 0)) {
                    PropVariantInit(&v);
                    v.vt = VT_UI2;
                    v.uiVal = static_cast<USHORT>(frameLeft);
                    metaWriter->SetMetadataByName(L"/imgdesc/Left", &v);
                    PropVariantClear(&v);

                    PropVariantInit(&v);
                    v.vt = VT_UI2;
                    v.uiVal = static_cast<USHORT>(frameTop);
                    metaWriter->SetMetadataByName(L"/imgdesc/Top", &v);
                    PropVariantClear(&v);
                }

                // FIXED: Set transparency flag and index for differential frames
                if (useTransparency) {
                    // Enable transparency
                    PropVariantInit(&v);
                    v.vt = VT_BOOL;
                    v.boolVal = VARIANT_TRUE;
                    metaWriter->SetMetadataByName(L"/grctlext/TransparencyFlag", &v);
                    PropVariantClear(&v);

                    // Set transparent color index
                    PropVariantInit(&v);
                    v.vt = VT_UI1;
                    v.bVal = transparentIdx;
                    metaWriter->SetMetadataByName(L"/grctlext/TransparentColorIndex", &v);
                    PropVariantClear(&v);

                    logger::debug("Frame {} transparency enabled, transparent index: {}", i, transparentIdx);
                }
            }

            // Set frame size
            hr = frameEnc->SetSize(frameWidth, frameHeight);
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Failed to set GIF frame size";
                return result;
            }

            // Let the encoder choose the appropriate pixel format for GIF (8-bit indexed)
            WICPixelFormatGUID pf = GUID_WICPixelFormat8bppIndexed;
            hr = frameEnc->SetPixelFormat(&pf);
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Failed to set GIF pixel format";
                return result;
            }

            // CRITICAL FIX: For differential frames with transparency, use WritePixels for direct control
            // This prevents WIC from re-quantizing and losing our transparency setup
            if (useTransparency) {
                // Set palette on frame encoder BEFORE writing pixels
                Microsoft::WRL::ComPtr<IWICPalette> encoderPalette;
                hr = wicFactory->CreatePalette(&encoderPalette);
                if (SUCCEEDED(hr)) {
                    hr = wicBitmap->CopyPalette(encoderPalette.Get());
                }
                if (SUCCEEDED(hr)) {
                    hr = frameEnc->SetPalette(encoderPalette.Get());
                    if (FAILED(hr)) {
                        logger::warn("Failed to set frame encoder palette: 0x{:08X}", static_cast<uint32_t>(hr));
                    }
                }

                // Lock bitmap and use WritePixels for direct control
                WICRect lockRect = { 0, 0, static_cast<INT>(frameWidth), static_cast<INT>(frameHeight) };
                Microsoft::WRL::ComPtr<IWICBitmapLock> readLock;
                hr = wicBitmap->Lock(&lockRect, WICBitmapLockRead, &readLock);

                if (SUCCEEDED(hr)) {
                    UINT stride = 0;
                    UINT bufferSize = 0;
                    BYTE* pixelData = nullptr;
                    readLock->GetDataPointer(&bufferSize, &pixelData);
                    readLock->GetStride(&stride);

                    // Write pixels directly - bypasses WIC re-quantization
                    hr = frameEnc->WritePixels(frameHeight, stride, bufferSize, pixelData);

                    readLock.Reset();

                    if (FAILED(hr)) {
                        logger::warn("WritePixels failed: 0x{:08X}, falling back to WriteSource", static_cast<uint32_t>(hr));
                        hr = frameEnc->WriteSource(wicBitmap.Get(), nullptr);
                    }
                } else {
                    logger::warn("Failed to lock bitmap for WritePixels, using WriteSource");
                    hr = frameEnc->WriteSource(wicBitmap.Get(), nullptr);
                }
            } else {
                // Non-differential frames: use WriteSource for format conversion
                hr = frameEnc->WriteSource(wicBitmap.Get(), nullptr);
            }

            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Failed to write GIF frame: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            hr = frameEnc->Commit();
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Failed to commit GIF frame: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            // Release frame memory after encoding (memory efficient)
            frameImage.Release();
            logger::debug("Encoded frame {}/{} from disk", i + 1, tempFrameFiles.size());
        }

        CancelIfRequested(params.cancelFlag, "finalize GIF");

        hr = encoder->Commit();
        if (FAILED(hr)) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to commit GIF encoder: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        if (comInitialized) CoUninitialize();

        // Verify file exists and size
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(outputPath, ec);
        if (ec || fileSize == 0) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "GIF file creation failed - file is empty or doesn't exist";
            return result;
        }

        // Clean up temp files on success
        logger::info("Cleaning up {} temp frame files...", tempFrameFiles.size());
        CleanupTempDirectory(tempDir, tempFrameFiles);

        result.success = true;
        result.message = "GIF capture completed successfully";
        result.filepath = outputPath;

        logger::info("GIF saved: {} ({} bytes)", util::wstring_to_utf8(outputPath), fileSize);
        if (useDifferentialEncoding) {
            // Calculate bytes per frame for reference
            float bytesPerFrame = static_cast<float>(fileSize) / tempFrameFiles.size();
            logger::info("  Differential encoding: {} frames, {:.1f} bytes/frame average",
                        tempFrameFiles.size(), bytesPerFrame);
        }

        // Log memory pool stats after encoding completes
        g_memoryPool.logStats("GIF capture complete");

    } catch (const Cancelled&) {
        // Clean up temp files on cancellation
        CleanupTempDirectory(tempDir, tempFrameFiles);
        result.success = false;
        result.message = "Cancelled";
    } catch (const std::exception& e) {
        // Clean up temp files on exception
        CleanupTempDirectory(tempDir, tempFrameFiles);
        result.success = false;
        result.message = std::string("Exception during GIF capture: ") + e.what();
        logger::error("{}", result.message);
    } catch (...) {
        // Clean up temp files on unknown exception
        CleanupTempDirectory(tempDir, tempFrameFiles);
        result.success = false;
        result.message = "Unknown exception during GIF capture";
        logger::error("{}", result.message);
    }

    // Trim unused pool blocks after capture
    g_memoryPool.trim();

    return result;
}


// IMPROVED: Animated GIF creation with cancellation checks
HRESULT CreateAnimatedGIF(const std::vector<DirectX::ScratchImage>& frames, 
                         const std::wstring& outputPath, 
                         float frameDelay,
                         std::atomic<bool>* cancelFlag) {
    logger::info("Creating animated GIF with {} frames", frames.size());
    
    // ADDED: Check cancellation before starting
    if (CheckCancellation(cancelFlag, "animated GIF creation start")) {
        return E_ABORT;
    }
    
    HRESULT hr = S_OK;
    
    // Initialize COM
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInitialized = SUCCEEDED(hr);
    
    try {
        // ADDED: Check cancellation before WIC factory creation
        if (CheckCancellation(cancelFlag, "GIF WIC factory creation")) {
            if (comInitialized) CoUninitialize();
            return E_ABORT;
        }
        
        // Create WIC factory
        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, 
                             IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) {
            logger::error("Failed to create WIC factory: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        // Create file stream
        Microsoft::WRL::ComPtr<IWICStream> stream;
        hr = wicFactory->CreateStream(&stream);
        if (FAILED(hr)) {
            logger::error("Failed to create WIC stream: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        hr = stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            logger::error("Failed to initialize file stream: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        // ADDED: Check cancellation before encoder creation
        if (CheckCancellation(cancelFlag, "GIF encoder creation")) {
            if (comInitialized) CoUninitialize();
            return E_ABORT;
        }
        
        // Create GIF encoder
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        hr = wicFactory->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder);
        if (FAILED(hr)) {
            logger::error("Failed to create GIF encoder: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            logger::error("Failed to initialize GIF encoder: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        // Convert frame delay to centiseconds (GIF standard)
        USHORT delayTime = static_cast<USHORT>(frameDelay * 100);
        
        // Add each frame to the GIF with cancellation checks
        for (size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
            // ADDED: Check cancellation before processing each frame
            if (CheckCancellation(cancelFlag, "GIF frame encoding " + std::to_string(frameIndex))) {
                if (comInitialized) CoUninitialize();
                return E_ABORT;
            }
            
            const auto& frame = frames[frameIndex];
            const DirectX::Image* img = frame.GetImage(0, 0, 0);
            
            if (!img) {
                logger::warn("Frame {} is null, skipping", frameIndex);
                continue;
            }
            
            // Create frame encoder with correct parameters
            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEncode;
            Microsoft::WRL::ComPtr<IPropertyBag2> framePropertyBag;
            
            hr = encoder->CreateNewFrame(&frameEncode, &framePropertyBag);
            if (FAILED(hr)) {
                logger::error("Failed to create frame {}: 0x{:08X}", frameIndex, static_cast<uint32_t>(hr));
                continue;
            }
            
            hr = frameEncode->Initialize(framePropertyBag.Get());
            if (FAILED(hr)) {
                logger::error("Failed to initialize frame {}: 0x{:08X}", frameIndex, static_cast<uint32_t>(hr));
                continue;
            }
            
            // Set frame delay using metadata query writer
            Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> frameMetadata;
            hr = frameEncode->GetMetadataQueryWriter(&frameMetadata);
            if (SUCCEEDED(hr) && frameMetadata) {
                PROPVARIANT propValue;
                PropVariantInit(&propValue);
                propValue.vt = VT_UI2;
                propValue.uiVal = delayTime;
                hr = frameMetadata->SetMetadataByName(L"/grctlext/Delay", &propValue);
                PropVariantClear(&propValue);
                
                // Set disposal method (restore to background)
                PropVariantInit(&propValue);
                propValue.vt = VT_UI1;
                propValue.bVal = 2; // Restore to background color
                hr = frameMetadata->SetMetadataByName(L"/grctlext/Disposal", &propValue);
                PropVariantClear(&propValue);
            }
            
            // Set frame size
            hr = frameEncode->SetSize(static_cast<UINT>(img->width), static_cast<UINT>(img->height));
            if (FAILED(hr)) {
                logger::error("Failed to set frame {} size: 0x{:08X}", frameIndex, static_cast<uint32_t>(hr));
                continue;
            }
            
            // Convert format if necessary
            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
            
            // Handle different source formats
            WICPixelFormatGUID sourceFormat;
            if (img->format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                sourceFormat = GUID_WICPixelFormat32bppRGBA;
            } else if (img->format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                sourceFormat = GUID_WICPixelFormat32bppBGRA;
            } else {
                sourceFormat = GUID_WICPixelFormat32bppBGRA; // Default
            }
            
            hr = frameEncode->SetPixelFormat(&pixelFormat);
            if (FAILED(hr)) {
                logger::error("Failed to set pixel format for frame {}: 0x{:08X}", frameIndex, static_cast<uint32_t>(hr));
                continue;
            }
            
            // ADDED: Check cancellation before bitmap creation
            if (CheckCancellation(cancelFlag, "GIF bitmap creation " + std::to_string(frameIndex))) {
                if (comInitialized) CoUninitialize();
                return E_ABORT;
            }
            
            // Create WIC bitmap from DirectX image
            Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
            hr = wicFactory->CreateBitmapFromMemory(
                static_cast<UINT>(img->width),
                static_cast<UINT>(img->height),
                sourceFormat,
                static_cast<UINT>(img->rowPitch),
                static_cast<UINT>(img->slicePitch),
                img->pixels,
                &wicBitmap
            );
            
            if (FAILED(hr)) {
                logger::error("Failed to create WIC bitmap for frame {}: 0x{:08X}", frameIndex, static_cast<uint32_t>(hr));
                continue;
            }
            
            // Write the frame
            hr = frameEncode->WriteSource(wicBitmap.Get(), nullptr);
            if (FAILED(hr)) {
                logger::error("Failed to write frame {}: 0x{:08X}", frameIndex, static_cast<uint32_t>(hr));
                continue;
            }
            
            hr = frameEncode->Commit();
            if (FAILED(hr)) {
                logger::error("Failed to commit frame {}: 0x{:08X}", frameIndex, static_cast<uint32_t>(hr));
                continue;
            }
            
            logger::debug("Successfully added frame {}/{}", frameIndex + 1, frames.size());
        }
        
        // ADDED: Check cancellation before final commit
        if (CheckCancellation(cancelFlag, "GIF final commit")) {
            if (comInitialized) CoUninitialize();
            return E_ABORT;
        }
        
        // Finalize the GIF
        hr = encoder->Commit();
        if (FAILED(hr)) {
            logger::error("Failed to commit GIF encoder: 0x{:08X}", static_cast<uint32_t>(hr));
            if (comInitialized) CoUninitialize();
            return hr;
        }
        
        logger::info("Animated GIF creation completed successfully");
        
    } catch (const std::exception& e) {
        logger::error("Exception during GIF creation: {}", e.what());
        hr = E_FAIL;
    } catch (...) {
        logger::error("Unknown exception during GIF creation");
        hr = E_FAIL;
    }
    
    if (comInitialized) {
        CoUninitialize();
    }
    
    return hr;
}

// ============================================================================
// APNG (Animated PNG) Implementation
// ============================================================================

// PNG chunk helper functions for APNG encoding
namespace APNGHelper {
    // Calculate CRC32 for PNG chunks
    static uint32_t crc_table[256];
    static bool crc_table_computed = false;

    static void make_crc_table() {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) {
                if (c & 1)
                    c = 0xedb88320L ^ (c >> 1);
                else
                    c = c >> 1;
            }
            crc_table[n] = c;
        }
        crc_table_computed = true;
    }

    static uint32_t update_crc(uint32_t crc, const uint8_t* buf, size_t len) {
        if (!crc_table_computed)
            make_crc_table();
        uint32_t c = crc;
        for (size_t n = 0; n < len; n++) {
            c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
        }
        return c;
    }

    static uint32_t png_crc(const uint8_t* buf, size_t len) {
        return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
    }

    // Write a 32-bit big-endian value
    static void write_be32(std::vector<uint8_t>& out, uint32_t val) {
        out.push_back((val >> 24) & 0xFF);
        out.push_back((val >> 16) & 0xFF);
        out.push_back((val >> 8) & 0xFF);
        out.push_back(val & 0xFF);
    }

    // Write a 16-bit big-endian value
    static void write_be16(std::vector<uint8_t>& out, uint16_t val) {
        out.push_back((val >> 8) & 0xFF);
        out.push_back(val & 0xFF);
    }

    // Write a PNG chunk
    static void write_chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
        // Length (4 bytes, big-endian)
        write_be32(out, static_cast<uint32_t>(data.size()));

        // Type (4 bytes)
        size_t typeStart = out.size();
        for (int i = 0; i < 4; i++) {
            out.push_back(static_cast<uint8_t>(type[i]));
        }

        // Data
        size_t dataStart = out.size();
        out.insert(out.end(), data.begin(), data.end());

        // CRC (4 bytes) - calculated over type + data
        std::vector<uint8_t> crcData(out.begin() + typeStart, out.end());
        uint32_t crc = png_crc(crcData.data(), crcData.size());
        write_be32(out, crc);
    }

    // Write acTL (animation control) chunk
    static void write_acTL(std::vector<uint8_t>& out, uint32_t num_frames, uint32_t num_plays) {
        std::vector<uint8_t> data;
        write_be32(data, num_frames);   // Number of frames
        write_be32(data, num_plays);    // Number of times to loop (0 = infinite)
        write_chunk(out, "acTL", data);
    }

    // Write fcTL (frame control) chunk
    static void write_fcTL(std::vector<uint8_t>& out, uint32_t sequence_number,
                           uint32_t width, uint32_t height,
                           uint32_t x_offset, uint32_t y_offset,
                           uint16_t delay_num, uint16_t delay_den,
                           uint8_t dispose_op, uint8_t blend_op) {
        std::vector<uint8_t> data;
        write_be32(data, sequence_number);
        write_be32(data, width);
        write_be32(data, height);
        write_be32(data, x_offset);
        write_be32(data, y_offset);
        write_be16(data, delay_num);
        write_be16(data, delay_den);
        data.push_back(dispose_op);     // 0=none, 1=background, 2=previous
        data.push_back(blend_op);       // 0=source, 1=over
        write_chunk(out, "fcTL", data);
    }

    // Write fdAT (frame data) chunk
    static void write_fdAT(std::vector<uint8_t>& out, uint32_t sequence_number,
                           const uint8_t* data, size_t dataLen) {
        std::vector<uint8_t> chunkData;
        write_be32(chunkData, sequence_number);
        chunkData.insert(chunkData.end(), data, data + dataLen);
        write_chunk(out, "fdAT", chunkData);
    }

    // Read a 32-bit big-endian value
    static uint32_t read_be32(const uint8_t* p) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) |
               static_cast<uint32_t>(p[3]);
    }

    // Structure to hold parsed PNG chunk info
    struct PNGChunk {
        std::string type;
        std::vector<uint8_t> data;
    };

    // Parse PNG chunks from a PNG file data
    static bool parse_png_chunks(const std::vector<uint8_t>& pngData,
                                 std::vector<PNGChunk>& chunks) {
        // PNG signature is 8 bytes
        if (pngData.size() < 8) return false;

        // Verify PNG signature
        const uint8_t pngSig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        if (memcmp(pngData.data(), pngSig, 8) != 0) return false;

        size_t pos = 8;
        while (pos + 12 <= pngData.size()) {
            uint32_t length = read_be32(&pngData[pos]);
            if (pos + 12 + length > pngData.size()) break;

            PNGChunk chunk;
            chunk.type = std::string(reinterpret_cast<const char*>(&pngData[pos + 4]), 4);
            chunk.data.assign(&pngData[pos + 8], &pngData[pos + 8 + length]);
            chunks.push_back(chunk);

            pos += 12 + length; // length(4) + type(4) + data(length) + crc(4)

            if (chunk.type == "IEND") break;
        }

        return true;
    }
}

// Helper function to encode a single frame to PNG in memory
static HRESULT EncodePNGToMemory(IWICImagingFactory* wicFactory,
                                  const DirectX::Image* img,
                                  std::vector<uint8_t>& pngData) {
    HRESULT hr = S_OK;

    // Create memory stream
    Microsoft::WRL::ComPtr<IStream> memStream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &memStream);
    if (FAILED(hr)) return hr;

    // Create PNG encoder
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return hr;

    hr = encoder->Initialize(memStream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;

    // Create frame
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->SetSize(static_cast<UINT>(img->width), static_cast<UINT>(img->height));
    if (FAILED(hr)) return hr;

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) return hr;

    // Determine source format
    WICPixelFormatGUID srcFormat = GUID_WICPixelFormat32bppBGRA;
    if (img->format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        srcFormat = GUID_WICPixelFormat32bppRGBA;
    }

    // Create bitmap from memory
    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
    hr = wicFactory->CreateBitmapFromMemory(
        static_cast<UINT>(img->width),
        static_cast<UINT>(img->height),
        srcFormat,
        static_cast<UINT>(img->rowPitch),
        static_cast<UINT>(img->slicePitch),
        img->pixels,
        &wicBitmap);
    if (FAILED(hr)) return hr;

    hr = frame->WriteSource(wicBitmap.Get(), nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Commit();
    if (FAILED(hr)) return hr;

    hr = encoder->Commit();
    if (FAILED(hr)) return hr;

    // Read data from memory stream
    STATSTG stat;
    hr = memStream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) return hr;

    LARGE_INTEGER zero = {};
    hr = memStream->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;

    pngData.resize(static_cast<size_t>(stat.cbSize.QuadPart));
    ULONG bytesRead = 0;
    hr = memStream->Read(pngData.data(), static_cast<ULONG>(pngData.size()), &bytesRead);
    if (FAILED(hr)) return hr;

    return S_OK;
}

// Helper function to create temp directory for APNG frames
static std::wstring CreateAPNGTempDirectory(const std::wstring& basePath) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
    localtime_s(&tm_buf, &time_t_now);

    wchar_t timestamp[64];
    swprintf_s(timestamp, L"apng_temp_%04d%02d%02d_%02d%02d%02d_%03d",
               tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
               static_cast<int>(ms.count()));

    std::wstring tempDir = basePath + L"\\" + timestamp;

    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    if (ec) {
        logger::error("Failed to create APNG temp directory: {}", ec.message());
        return L"";
    }

    return tempDir;
}

    CaptureResult CaptureAPNG(const CaptureParams& params) {
    logger::info("=== Starting APNG capture ===");
    logger::info("Duration: {}s, FPS: {}, LoopCount: {}, Base path: {}",
                 params.apngDuration, params.apngFPS, params.apngLoopCount,
                 util::wstring_to_utf8(params.basePath));

    // Log compression mode - now with optimize parameter for true delta
    const bool useDifferentialEncoding = (params.apngCompression == 1);
    const bool useTrueDeltaEncoding = (params.apngOptimize == 1);
    
    if (useDifferentialEncoding) {
        if (useTrueDeltaEncoding) {
            logger::info("Using TRUE DELTA encoding (transparency-based pixel differences)");
        } else {
            logger::info("Using DIFFERENTIAL frame encoding (region extraction only)");
        }
    } else {
        logger::info("Using FULL frame encoding");
    }

    CaptureResult result{};
    std::wstring tempDir;
    std::vector<std::wstring> tempFrameFiles;

    try {
        CancelIfRequested(params.cancelFlag, "APNG capture start");

        // Create temp directory for intermediate frames
        tempDir = CreateAPNGTempDirectory(params.basePath);
        if (tempDir.empty()) {
            result.success = false;
            result.message = "Failed to create temp directory for APNG frames";
            return result;
        }

        // Setup D3D + duplication
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;

        HRESULT hr = SetupDesktopDuplication(device, context, duplication, params.cancelFlag);
        if (hr == E_ABORT) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "Cancelled during APNG desktop duplication setup";
            return result;
        }
        if (FAILED(hr)) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "Failed to setup desktop duplication for APNG: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        // Determine frame count based on FPS parameter (clamp to 1-60 fps range for APNG)
        const double fps = std::clamp(static_cast<double>(params.apngFPS), 1.0, 60.0);
        const int totalFrames = std::max(1, static_cast<int>(std::round(params.apngDuration * fps)));
        const double frameInterval = 1.0 / fps;

        logger::info("Capturing {} frames at {} fps (storing to disk)", totalFrames, fps);

        const auto startTime = std::chrono::high_resolution_clock::now();

        // Capture frames and save to disk immediately to prevent memory overflow
        for (int frame = 0; frame < totalFrames; ++frame) {
            CancelIfRequested(params.cancelFlag, "APNG frame loop");

            DirectX::ScratchImage img;
            hr = CaptureSingleFrame(device.Get(), context.Get(), duplication.Get(), img, params.cancelFlag);
            if (hr == E_ABORT) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                result.success = false;
                result.message = "Cancelled during APNG frame capture";
                return result;
            }
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                result.success = false;
                result.message = "Failed to capture APNG frame: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            // Save frame to disk immediately as BMP and release memory
            wchar_t frameFilename[MAX_PATH];
            swprintf_s(frameFilename, L"%s\\frame_%04d.bmp", tempDir.c_str(), frame);
            std::wstring framePath = frameFilename;

            hr = SaveTempFrame(img, framePath);
            if (FAILED(hr)) {
                CleanupTempDirectory(tempDir, tempFrameFiles);
                result.success = false;
                result.message = "Failed to save temp APNG frame to disk: HRESULT " + FormatHRESULT(hr);
                return result;
            }

            tempFrameFiles.push_back(framePath);
            img.Release();

            logger::debug("Captured and saved APNG frame {}/{} to disk", frame + 1, totalFrames);

            // Sleep until next frame while allowing cancellation
            auto target = startTime + std::chrono::duration<double>((frame + 1) * frameInterval);
            while (std::chrono::high_resolution_clock::now() < target) {
                CancelIfRequested(params.cancelFlag, "APNG frame timing wait");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        if (tempFrameFiles.empty()) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
        // ADDED: Fire the early callback to restore UI before encoding starts
            result.success = false;
            result.message = "No frames captured for APNG";
            return result;
        }
		
        // ADDED: Fire the early callback to restore UI before encoding starts
        // ADDED: Fire the early callback to restore UI before encoding starts
		                // Fire early callback to restore UI before encoding starts
        if (params.onFramesCaptured) {
            params.onFramesCaptured();
        }

logger::info("All {} frames captured to disk, starting APNG encoding...", tempFrameFiles.size());
        CancelIfRequested(params.cancelFlag, "before APNG encode");
		        // ADDED: Fire the early callback to restore UI before encoding starts
        // Build output path
        std::wstring outputPath = GenerateFilename(params.basePath, ImageFormat::APNG);

        // Initialize COM for WIC
        bool comInitialized = false;
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized = true;
        } else if (hr != RPC_E_CHANGED_MODE) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "COM initialization failed for APNG encoder: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        // Create WIC factory
        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to create WIC factory for APNG: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        // ====================================================================
        // DIFFERENTIAL ENCODING IMPLEMENTATION
        // ====================================================================

        // Load first frame to get dimensions and encode as reference
        DirectX::ScratchImage firstFrameImage;
        hr = LoadTempFrame(tempFrameFiles[0], firstFrameImage);
        if (FAILED(hr)) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to load first APNG frame: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        const DirectX::Image* firstImg = firstFrameImage.GetImage(0, 0, 0);
        if (!firstImg) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "First APNG frame image is null";
            return result;
        }

        const uint32_t fullWidth = static_cast<uint32_t>(firstImg->width);
        const uint32_t fullHeight = static_cast<uint32_t>(firstImg->height);

        // Encode first frame to PNG to get chunks
        std::vector<uint8_t> firstPngData;
        hr = EncodePNGToMemory(wicFactory.Get(), firstImg, firstPngData);
        if (FAILED(hr)) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to encode first frame as PNG: HRESULT " + FormatHRESULT(hr);
            return result;
        }

        // Store previous frame pixels for differential comparison
        // Using PoolBuffer for RAII-safe pool allocation - avoids repeated OS alloc/free
        // if multiple APNG captures happen in a session (pool reuses the block).
        PoolBuffer prevFrameBuffer;
        size_t prevRowPitch = 0;

        if (useDifferentialEncoding) {
            prevRowPitch = firstImg->rowPitch;
            size_t bufferSize = fullHeight * prevRowPitch;
            prevFrameBuffer = g_memoryPool.allocateBuffer(bufferSize);
            if (!prevFrameBuffer) {
                logger::error("Failed to allocate pool buffer for prev frame ({} bytes)", bufferSize);
                CleanupTempDirectory(tempDir, tempFrameFiles);
                if (comInitialized) CoUninitialize();
                result.success = false;
                result.message = "Memory allocation failed for differential encoding buffer";
                return result;
            }
            memcpy(prevFrameBuffer.data(), firstImg->pixels, bufferSize);
        }

        // Parse PNG chunks
        std::vector<APNGHelper::PNGChunk> firstFrameChunks;
        if (!APNGHelper::parse_png_chunks(firstPngData, firstFrameChunks)) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to parse PNG chunks from first frame";
            return result;
        }

        // Build APNG output
        std::vector<uint8_t> apngData;
        apngData.reserve(firstPngData.size() * tempFrameFiles.size() / 2);  // Estimate

        // PNG signature
        const uint8_t pngSig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        apngData.insert(apngData.end(), pngSig, pngSig + 8);

        // Calculate frame delay in fractional seconds
        uint16_t delay_num = static_cast<uint16_t>(std::round(1000.0 / fps));
        uint16_t delay_den = 1000;

        uint32_t sequenceNumber = 0;

        // Stats for differential encoding
        size_t totalFullFrameBytes = 0;
        size_t totalDiffFrameBytes = 0;
        size_t skippedFrames = 0;
        size_t diffFrames = 0;

        // Write chunks from first frame, inserting acTL and fcTL at appropriate positions
        bool wroteAcTL = false;
        bool wroteFirstFcTL = false;

        for (const auto& chunk : firstFrameChunks) {
            if (chunk.type == "IHDR") {
                // Write IHDR first
                APNGHelper::write_chunk(apngData, "IHDR", chunk.data);

                // Write acTL right after IHDR
                APNGHelper::write_acTL(apngData, static_cast<uint32_t>(tempFrameFiles.size()),
                                       static_cast<uint32_t>(params.apngLoopCount));
                wroteAcTL = true;
            }
            else if (chunk.type == "IDAT") {
                // Before first IDAT, write fcTL for first frame
                if (!wroteFirstFcTL) {
                    APNGHelper::write_fcTL(apngData, sequenceNumber++,
                                           fullWidth, fullHeight,
                                           0, 0,  // x_offset, y_offset
                                           delay_num, delay_den,
                                           0, 0);  // dispose_op=none, blend_op=source
                    wroteFirstFcTL = true;
                }
                // Write IDAT chunk
                APNGHelper::write_chunk(apngData, "IDAT", chunk.data);
                totalFullFrameBytes += chunk.data.size();
            }
            else if (chunk.type != "IEND") {
                // Write other chunks (except IEND, we'll write it at the end)
                APNGHelper::write_chunk(apngData, chunk.type.c_str(), chunk.data);
            }
        }

        firstFrameImage.Release();

        // Process remaining frames with differential encoding
        for (size_t i = 1; i < tempFrameFiles.size(); ++i) {
            CancelIfRequested(params.cancelFlag, "APNG encode frame");

            // Load frame from disk
            DirectX::ScratchImage frameImage;
            hr = LoadTempFrame(tempFrameFiles[i], frameImage);
            if (FAILED(hr)) {
                logger::warn("Failed to load APNG frame {}, skipping", i);
                continue;
            }

            const DirectX::Image* img = frameImage.GetImage(0, 0, 0);
            if (!img) {
                logger::warn("APNG frame {} image is null, skipping", i);
                continue;
            }

            // Variables for this frame
            uint32_t frameLeft = 0, frameTop = 0;
            uint32_t frameWidth = fullWidth, frameHeight = fullHeight;
            std::vector<uint8_t> framePngData;
            bool frameSkipped = false;

            // ================================================================
            // DIFFERENTIAL ENCODING LOGIC
            // When useTrueDeltaEncoding is enabled (optimize=1), we use
            // transparency-based delta encoding where unchanged pixels
            // become fully transparent, enabling much better PNG compression
            // ================================================================
            if (useDifferentialEncoding && prevFrameBuffer.valid()) {
                uint32_t diffLeft, diffTop, diffRight, diffBottom;

                bool hasChanges = ComputeAPNGDiffRect(
                    prevFrameBuffer.data(), prevRowPitch,
                    img->pixels, img->rowPitch,
                    fullWidth, fullHeight,
                    diffLeft, diffTop, diffRight, diffBottom,
                    2);  // Threshold of 2 for noise tolerance

                if (!hasChanges) {
                    // Frame is identical - we can skip it by extending previous frame duration
                    // For now, we'll emit a minimal 1x1 transparent frame
                    // A more advanced implementation would merge frame durations
                    logger::debug("Frame {} identical to previous, marking as skipped", i);
                    skippedFrames++;
                    frameSkipped = true;

                    // Still need to write something for APNG compliance
                    // Write a 1x1 transparent region at 0,0 with blend=over (won't change anything)
                    frameLeft = 0;
                    frameTop = 0;
                    frameWidth = 1;
                    frameHeight = 1;

                    // Create 1x1 transparent BGRA pixel
                    std::vector<uint8_t> tinyPixels = { 0, 0, 0, 0 };  // Fully transparent
                    hr = EncodeRegionToPNGWithAlpha(wicFactory.Get(), tinyPixels.data(), 4, 1, 1, framePngData);
                }
                else {
                    // Calculate diff region dimensions
                    frameLeft = diffLeft;
                    frameTop = diffTop;
                    frameWidth = diffRight - diffLeft + 1;
                    frameHeight = diffBottom - diffTop + 1;

                    std::vector<uint8_t> regionPixels;
                    size_t regionRowPitch;

                    if (useTrueDeltaEncoding) {
                        // TRUE DELTA ENCODING: Extract region with transparency for unchanged pixels
                        // Unchanged pixels become fully transparent (alpha=0)
                        // Changed pixels retain their new values with full alpha
                        // This compresses MUCH better than simple region extraction
                        ExtractDeltaSubRegion(
                            prevFrameBuffer.data(), prevRowPitch,
                            img->pixels, img->rowPitch,
                            fullWidth, fullHeight,
                            frameLeft, frameTop, frameWidth, frameHeight,
                            regionPixels, regionRowPitch,
                            2);  // Threshold

                        // Encode with alpha preservation for delta transparency
                        hr = EncodeRegionToPNGWithAlpha(wicFactory.Get(), regionPixels.data(), regionRowPitch,
                                              frameWidth, frameHeight, framePngData);

                        logger::debug("Frame {}: TRUE DELTA region {}x{} at ({},{})",
                                     i, frameWidth, frameHeight, frameLeft, frameTop);
                    }
                    else {
                        // SIMPLE REGION EXTRACTION: Extract the changed region pixels as-is
                        // This is the old behavior - just extracts the bounding box
                        ExtractSubRegion(
                            img->pixels, img->rowPitch,
                            fullWidth, fullHeight,
                            frameLeft, frameTop, frameWidth, frameHeight,
                            regionPixels, regionRowPitch);

                        // Encode the region (no special alpha handling needed)
                        hr = EncodeRegionToPNG(wicFactory.Get(), regionPixels.data(), regionRowPitch,
                                              frameWidth, frameHeight, framePngData);

                        // Calculate compression stats
                        float regionPercent = (static_cast<float>(frameWidth * frameHeight) /
                                              static_cast<float>(fullWidth * fullHeight)) * 100.0f;
                        logger::debug("Frame {}: diff region {}x{} at ({},{}) - {:.1f}% of full frame",
                                     i, frameWidth, frameHeight, frameLeft, frameTop, regionPercent);
                    }

                    diffFrames++;
                }

                // Update previous frame pixels for next comparison
                memcpy(prevFrameBuffer.data(), img->pixels, fullHeight * img->rowPitch);
            }
            else {
                // Full frame encoding (no differential)
                hr = EncodePNGToMemory(wicFactory.Get(), img, framePngData);
            }

            if (FAILED(hr)) {
                logger::warn("Failed to encode APNG frame {} as PNG, skipping", i);
                frameImage.Release();
                continue;
            }

            // Parse frame chunks
            std::vector<APNGHelper::PNGChunk> frameChunks;
            if (!APNGHelper::parse_png_chunks(framePngData, frameChunks)) {
                logger::warn("Failed to parse PNG chunks from frame {}, skipping", i);
                frameImage.Release();
                continue;
            }

            // Write fcTL for this frame
            // dispose_op: 0=none (keep previous), 1=background, 2=previous
            // blend_op: 0=source (replace), 1=over (alpha composite)
            uint8_t dispose_op = 0;  // APNG_DISPOSE_OP_NONE - don't clear anything
            uint8_t blend_op = useDifferentialEncoding ? 1 : 0;  // APNG_BLEND_OP_OVER for diff frames

            APNGHelper::write_fcTL(apngData, sequenceNumber++,
                                   frameWidth, frameHeight,
                                   frameLeft, frameTop,
                                   delay_num, delay_den,
                                   dispose_op, blend_op);

            // Write IDAT chunks as fdAT chunks
            for (const auto& chunk : frameChunks) {
                if (chunk.type == "IDAT") {
                    APNGHelper::write_fdAT(apngData, sequenceNumber++,
                                           chunk.data.data(), chunk.data.size());

                    if (useDifferentialEncoding) {
                        totalDiffFrameBytes += chunk.data.size();
                    } else {
                        totalFullFrameBytes += chunk.data.size();
                    }
                }
            }

            frameImage.Release();
            logger::debug("Encoded APNG frame {}/{}", i + 1, tempFrameFiles.size());
        }

        // Write IEND chunk
        std::vector<uint8_t> emptyData;
        APNGHelper::write_chunk(apngData, "IEND", emptyData);

        // Write APNG data to file
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open()) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to open output file for APNG";
            return result;
        }

        outFile.write(reinterpret_cast<const char*>(apngData.data()), apngData.size());
        outFile.close();

        if (outFile.fail()) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            if (comInitialized) CoUninitialize();
            result.success = false;
            result.message = "Failed to write APNG data to file";
            return result;
        }

        if (comInitialized) CoUninitialize();

        // Verify file exists and size
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(outputPath, ec);
        if (ec || fileSize == 0) {
            CleanupTempDirectory(tempDir, tempFrameFiles);
            result.success = false;
            result.message = "APNG file creation failed - file is empty or doesn't exist";
            return result;
        }

        // Clean up temp files on success
        logger::info("Cleaning up {} temp frame files...", tempFrameFiles.size());
        CleanupTempDirectory(tempDir, tempFrameFiles);

        result.success = true;
        result.message = "APNG capture completed successfully";
        result.filepath = outputPath;

        // Log compression stats
        if (useDifferentialEncoding) {
            if (useTrueDeltaEncoding) {
                logger::info("APNG TRUE DELTA encoding stats:");
            } else {
                logger::info("APNG differential encoding stats:");
            }
            logger::info("  - Encoding mode: {}", useTrueDeltaEncoding ? "TRUE DELTA (transparency-based)" : "Region extraction");
            logger::info("  - Full frame bytes: {} KB", totalFullFrameBytes / 1024);
            logger::info("  - Diff frame bytes: {} KB", totalDiffFrameBytes / 1024);
            logger::info("  - Diff frames: {}, Skipped frames: {}", diffFrames, skippedFrames);
            if (totalFullFrameBytes > 0) {
                float compressionRatio = 1.0f - (static_cast<float>(totalDiffFrameBytes) /
                    static_cast<float>((tempFrameFiles.size() - 1) * totalFullFrameBytes / 1));
                logger::info("  - Estimated compression improvement: {:.1f}%", compressionRatio * 100.0f);
            }
            logger::info("  - Final file size: {} KB ({} bytes)", fileSize / 1024, fileSize);
        }

        logger::info("APNG saved: {} ({} bytes, {} frames)",
                     util::wstring_to_utf8(outputPath), fileSize, tempFrameFiles.size());

        // Log memory pool stats after encoding completes
        g_memoryPool.logStats("APNG capture complete");

    } catch (const Cancelled&) {
        CleanupTempDirectory(tempDir, tempFrameFiles);
        result.success = false;
        result.message = "Cancelled";
    } catch (const std::exception& e) {
        CleanupTempDirectory(tempDir, tempFrameFiles);
        result.success = false;
        result.message = std::string("Exception during APNG capture: ") + e.what();
        logger::error("{}", result.message);
    } catch (...) {
        CleanupTempDirectory(tempDir, tempFrameFiles);
        result.success = false;
        result.message = "Unknown exception during APNG capture";
        logger::error("{}", result.message);
    }

    // Trim unused pool blocks after capture (keeps in-use blocks, frees idle ones)
    g_memoryPool.trim();

    return result;
}


} // namespace ScreenCapture
