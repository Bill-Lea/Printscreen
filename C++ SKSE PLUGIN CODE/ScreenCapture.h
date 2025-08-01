#pragma once

#include <string>
#include <atomic>
#include <vector>
#include <wrl/client.h>

// Forward declarations to avoid including DirectX headers in header
namespace DirectX {
    class ScratchImage;
}

// Forward declare D3D11 types
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGIOutputDuplication;

namespace ScreenCapture {
    
    // Image format enumeration
    enum class ImageFormat {
        PNG = 0,
        JPEG,
        BMP,
        TIF,
        GIF,
        DDS
    };
    
    // TIFF compression modes
    enum class TiffMode {
        NONE = 0,
        LZW,
        CCITT1D,
        CCITT4,
        RLE,
        ZIP
    };
    
    // DDS compression modes
    enum class DDSCompression {
        BC1 = 0,
        BC2,
        BC3,
        BC4,
        BC5,
        BC6H,
        BC7_SLOW,
        BC7_NORMAL,
        BC7_FAST
    };
    
    // Helper functions
    ImageFormat StringToImageFormat(const std::string& format);
    TiffMode StringToTiffCompression(const std::string& compression);
    DDSCompression StringToDDSCompression(const std::string& compression);
    
    // ADDED: Cancellation helper function
    bool CheckCancellation(std::atomic<bool>* cancelFlag, const std::string& operation);
    
    // Capture parameters structure
    struct CaptureParams {
        std::wstring basePath;
        ImageFormat format = ImageFormat::PNG;
        float jpegQuality = 95.0f;
        float gifDuration = 3.0f;
        TiffMode tiffMode = TiffMode::NONE;
        DDSCompression ddsMode = DDSCompression::BC1;
        std::atomic<bool>* cancelFlag = nullptr;
    };
    
    // Result structure
    struct CaptureResult {
        bool success = false;
        std::string message = "Unknown error";
        std::wstring filepath;
    };
    
    // Main public interface
    CaptureResult CaptureScreen(const CaptureParams& params);
    CaptureResult CaptureGIF(const CaptureParams& params);
    
    // Threading initialization
    void InitializeDirectXTexThreading();
    
    // IMPROVED: Internal functions with cancellation support
    HRESULT SaveToWIC(const DirectX::ScratchImage& image, const CaptureParams& params, const std::wstring& filepath);
    HRESULT SaveToDDS(const DirectX::ScratchImage& image, const CaptureParams& params, const std::wstring& filepath);
    
    // IMPROVED: Internal helper functions with cancellation parameters
    std::wstring GenerateFilename(const std::wstring& basePath, ImageFormat format);
    
    HRESULT SetupDesktopDuplication(
        Microsoft::WRL::ComPtr<ID3D11Device>& device,
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context,
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication>& duplication,
        std::atomic<bool>* cancelFlag = nullptr
    );
    
    HRESULT CaptureSingleFrame(
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        IDXGIOutputDuplication* pDuplication,
        DirectX::ScratchImage& outImage,
        std::atomic<bool>* cancelFlag = nullptr
    );
    
    HRESULT CreateAnimatedGIF(
        const std::vector<DirectX::ScratchImage>& frames, 
        const std::wstring& outputPath, 
        float frameDelay,
        std::atomic<bool>* cancelFlag = nullptr
    );
    
} // namespace ScreenCapture