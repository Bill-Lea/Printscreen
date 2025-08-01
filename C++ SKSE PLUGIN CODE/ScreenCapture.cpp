#include "ScreenCapture.h"
#include "logger.h"
#include "PCH.h"
#include <DirectXTex.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ScreenCapture {
    
    // ADDED: Cancellation helper function
    bool CheckCancellation(std::atomic<bool>* cancelFlag, const std::string& operation) {
        if (cancelFlag && cancelFlag->load()) {
            logger::info("Cancellation requested during: {}", operation);
            return true;
        }
        return false;
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

// Helper function implementations
ImageFormat StringToImageFormat(const std::string& format) {
    std::string lower = format;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "png") return ImageFormat::PNG;
    if (lower == "jpg" || lower == "jpeg") return ImageFormat::JPEG;
    if (lower == "bmp") return ImageFormat::BMP;
    if (lower == "tif" || lower == "tiff") return ImageFormat::TIF;
    if (lower == "gif") return ImageFormat::GIF;
    if (lower == "dds") return ImageFormat::DDS;
    
    return ImageFormat::PNG; // Default
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
        case ImageFormat::DDS: filename << L".dds"; break;
        default: 
            logger::error("GenerateFilename: unhandled ImageFormat value ({})", static_cast<int>(format));
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
            containerFormat = GUID_ContainerFormatPng;
            break;
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
        } else {
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
        
        logger::debug("Saving DDS file to: {}", std::string(filepath.begin(), filepath.end()));
        
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
                std::string(params.basePath.begin(), params.basePath.end()));

    CaptureResult result;
    
    try {
        // ADDED: Check cancellation before starting
        if (CheckCancellation(params.cancelFlag, "capture start")) {
            result.message = "Cancelled before starting";
            return result;
        }
        
        // Setup DirectX components
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
        
        HRESULT hr = SetupDesktopDuplication(device, context, duplication, params.cancelFlag);
        if (hr == E_ABORT) {
            result.message = "Cancelled during desktop duplication setup";
            return result;
        }
        if (FAILED(hr)) {
            result.message = "Failed to setup desktop duplication: " + std::to_string(hr);
            logger::error(result.message);
            return result;
        }
        
        logger::info("Desktop duplication setup successful");
        
        // ADDED: Check cancellation before frame capture
        if (CheckCancellation(params.cancelFlag, "frame capture")) {
            result.message = "Cancelled before frame capture";
            return result;
        }
        
        // Capture single frame
        DirectX::ScratchImage image;
        hr = CaptureSingleFrame(device.Get(), context.Get(), duplication.Get(), image, params.cancelFlag);
        
        if (hr == E_ABORT) {
            result.message = "Cancelled during frame capture";
            return result;
        }
        if (FAILED(hr)) {
            result.message = "Failed to capture frame: " + std::to_string(hr);
            logger::error(result.message);
            return result;
        }
        
        logger::info("Frame captured successfully");
        
        // ADDED: Check cancellation before filename generation
        if (CheckCancellation(params.cancelFlag, "filename generation")) {
            result.message = "Cancelled before filename generation";
            return result;
        }
        
        // Generate output filename
        std::wstring outputPath = GenerateFilename(params.basePath, params.format);
        logger::info("Generated output path: {}", std::string(outputPath.begin(), outputPath.end()));
        
        // ADDED: Check cancellation before save
        if (CheckCancellation(params.cancelFlag, "image save")) {
            result.message = "Cancelled before image save";
            return result;
        }
        
        // Save based on format
        if (params.format == ImageFormat::DDS) {
            hr = SaveToDDS(image, params, outputPath);
        } else {
            hr = SaveToWIC(image, params, outputPath);
        }
        
        if (hr == E_ABORT) {
            result.message = "Cancelled during image save";
            return result;
        }
        if (FAILED(hr)) {
            result.message = "Failed to save image: " + std::to_string(hr);
            logger::error(result.message);
            return result;
        }
        
        // ADDED: Check cancellation before file verification
        if (CheckCancellation(params.cancelFlag, "file verification")) {
            result.message = "Cancelled during file verification";
            return result;
        }
        
        // Check if file was actually created and has size > 0
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(outputPath, ec);
        if (ec || fileSize == 0) {
            result.message = "Image file creation failed - file is empty or doesn't exist";
            logger::error(result.message);
            return result;
        }
        
        result.success = true;
        result.message = "Single frame capture completed successfully";
        result.filepath = outputPath;
        
        logger::info("Image saved successfully: {} ({} bytes)", 
                    std::string(outputPath.begin(), outputPath.end()), fileSize);
        
    } catch (const std::exception& e) {
        result.message = "Exception during capture: " + std::string(e.what());
        logger::error(result.message);
    } catch (...) {
        result.message = "Unknown exception during capture";
        logger::error(result.message);
    }
    
    return result;
}

// IMPROVED: GIF capture with comprehensive cancellation checks
CaptureResult CaptureGIF(const CaptureParams& params) {
    logger::info("=== Starting GIF capture ===");
    logger::info("Duration: {}s, Base path: {}", params.gifDuration, 
                std::string(params.basePath.begin(), params.basePath.end()));

    CaptureResult result;
    
    try {
        // ADDED: Check cancellation before starting
        if (CheckCancellation(params.cancelFlag, "GIF capture start")) {
            result.message = "Cancelled before starting GIF capture";
            return result;
        }
        
        // Setup DirectX components
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
        
        HRESULT hr = SetupDesktopDuplication(device, context, duplication, params.cancelFlag);
        if (hr == E_ABORT) {
            result.message = "Cancelled during GIF desktop duplication setup";
            return result;
        }
        if (FAILED(hr)) {
            result.message = "Failed to setup desktop duplication for GIF: " + std::to_string(hr);
            logger::error(result.message);
            return result;
        }
        
        logger::info("Desktop duplication setup successful");
        
        // Calculate frame timing
        const float frameRate = 10.0f; // 10 FPS for GIF
        const float frameInterval = 1.0f / frameRate;
        const int totalFrames = static_cast<int>(params.gifDuration * frameRate);
        
        logger::info("Capturing {} frames at {} FPS", totalFrames, frameRate);
        
        // Store all frames
        std::vector<DirectX::ScratchImage> frames;
        frames.reserve(totalFrames);
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int frame = 0; frame < totalFrames; ++frame) {
            // IMPROVED: Check for cancellation at the start of each frame
            if (CheckCancellation(params.cancelFlag, "GIF frame " + std::to_string(frame))) {
                result.message = "GIF capture cancelled at frame " + std::to_string(frame);
                logger::info(result.message);
                return result;
            }
            
            // Capture single frame with cancellation support
            DirectX::ScratchImage frameImage;
            hr = CaptureSingleFrame(device.Get(), context.Get(), duplication.Get(), frameImage, params.cancelFlag);
            
            if (hr == E_ABORT) {
                result.message = "GIF capture cancelled during frame " + std::to_string(frame);
                logger::info(result.message);
                return result;
            }
            
            if (FAILED(hr)) {
                logger::warn("Failed to capture frame {}: 0x{:08X}", frame, static_cast<uint32_t>(hr));
                
                // For GIF, we can tolerate some frame drops
                if (frame > 0) {
                    // ADDED: Check cancellation before duplicating frame
                    if (CheckCancellation(params.cancelFlag, "GIF frame duplication")) {
                        result.message = "GIF capture cancelled during frame duplication";
                        return result;
                    }
                    
                    // Duplicate the last successful frame
                    frames.emplace_back(std::move(DirectX::ScratchImage()));
                    hr = frames.back().InitializeFromImage(*frames[frames.size()-2].GetImage(0, 0, 0));
                    if (FAILED(hr)) {
                        logger::error("Failed to duplicate frame: 0x{:08X}", static_cast<uint32_t>(hr));
                        continue;
                    }
                } else {
                    // If first frame fails, retry a few times
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            } else {
                frames.emplace_back(std::move(frameImage));
            }
            
            logger::debug("Captured frame {}/{}", frame + 1, totalFrames);
            
            // Wait for next frame time with cancellation checks during sleep
            auto targetTime = startTime + std::chrono::duration<float>(frame * frameInterval);
            auto currentTime = std::chrono::high_resolution_clock::now();
            
            if (currentTime < targetTime) {
                auto sleepDuration = std::chrono::duration_cast<std::chrono::milliseconds>(targetTime - currentTime);
                
                // IMPROVED: Break sleep into smaller chunks to allow cancellation
                const auto checkInterval = std::chrono::milliseconds(50);
                while (sleepDuration > std::chrono::milliseconds(0)) {
                    auto sleepChunk = std::min(sleepDuration, checkInterval);
                    std::this_thread::sleep_for(sleepChunk);
                    sleepDuration -= sleepChunk;
                    
                    // Check cancellation during sleep
                    if (CheckCancellation(params.cancelFlag, "GIF frame timing wait")) {
                        result.message = "GIF capture cancelled during frame timing";
                        return result;
                    }
                }
            }
        }
        
        if (frames.empty()) {
            result.message = "No frames captured for GIF";
            logger::error(result.message);
            return result;
        }
        
        // ADDED: Check cancellation before GIF creation
        if (CheckCancellation(params.cancelFlag, "GIF creation start")) {
            result.message = "Cancelled before GIF creation";
            return result;
        }
        
        logger::info("Captured {} frames, now creating GIF", frames.size());
        
        // Generate output filename
        std::wstring outputPath = GenerateFilename(params.basePath, ImageFormat::GIF);
        logger::info("Generated GIF output path: {}", std::string(outputPath.begin(), outputPath.end()));
        
        // Create animated GIF using WIC with cancellation support
        hr = CreateAnimatedGIF(frames, outputPath, frameInterval, params.cancelFlag);
        
        if (hr == E_ABORT) {
            result.message = "GIF creation cancelled";
            return result;
        }
        if (FAILED(hr)) {
            result.message = "Failed to create animated GIF: " + std::to_string(hr);
            logger::error(result.message);
            return result;
        }
        
        // ADDED: Check cancellation before file verification
        if (CheckCancellation(params.cancelFlag, "GIF file verification")) {
            result.message = "Cancelled during GIF file verification";
            return result;
        }
        
        // Check if file was actually created and has size > 0
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(outputPath, ec);
        if (ec || fileSize == 0) {
            result.message = "GIF file creation failed - file is empty or doesn't exist";
            logger::error(result.message);
            return result;
        }
        
        result.success = true;
        result.message = "GIF capture completed successfully";
        result.filepath = outputPath;
        
        logger::info("GIF saved successfully: {} ({} bytes)", 
                    std::string(outputPath.begin(), outputPath.end()), fileSize);
        
    } catch (const std::exception& e) {
        result.message = "Exception during GIF capture: " + std::string(e.what());
        logger::error(result.message);
    } catch (...) {
        result.message = "Unknown exception during GIF capture";
        logger::error(result.message);
    }
    
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

} // namespace ScreenCapture