#include "PCH.h"
#include "StillEncoder.h"
#include "stringutils.h"
#include <DirectXTex.h>
#include <wincodec.h>
#include <propvarutil.h>

StillEncoder::StillEncoder(const CaptureRequest& req)
    : format_(req.format), jpegQuality_(req.jpegQuality),
      tiffMode_(req.tiffMode), ddsMode_(req.ddsMode) {}

// ============================================================
// Public interface
// ============================================================

EncodeResult StillEncoder::Encode(
    const DirectX::ScratchImage& frame,
    const std::wstring& outputPath,
    CancellationToken::Ptr token)
{
    if (token) token->ThrowIfCancelled("StillEncoder::Encode start");

    HRESULT hr = (format_ == ImageFormat::DDS)
        ? SaveDDS(frame, outputPath, token)
        : SaveWIC(frame, outputPath, token);

    if (hr == E_ABORT)     return EncodeResult::Cancelled();
    if (FAILED(hr))        return EncodeResult::Fail(
        "Save failed: HRESULT 0x" + [&]{ char buf[16]; sprintf_s(buf, "%08X", hr); return std::string(buf); }());

    return EncodeResult::Ok(outputPath);
}

EncodeResult StillEncoder::EncodeSequence(
    std::vector<DirectX::ScratchImage>& frames,
    const std::wstring& outputPath,
    float, int, int,
    CancellationToken::Ptr token)
{
    if (frames.empty()) return EncodeResult::Fail("No frames provided");
    return Encode(frames[0], outputPath, token);
}

// ============================================================
// WIC save (PNG / JPEG / BMP / TIF / GIF single-frame)
// ============================================================
HRESULT StillEncoder::SaveWIC(
    const DirectX::ScratchImage& img,
    const std::wstring& path,
    CancellationToken::Ptr token)
{
    logger::info("StillEncoder: WIC save, format={}", static_cast<int>(format_));
    if (token) token->ThrowIfCancelled("StillEncoder::SaveWIC start");

    GUID containerFmt{};
    switch (format_) {
        case ImageFormat::PNG:  containerFmt = GUID_ContainerFormatPng;  break;
        case ImageFormat::JPEG: containerFmt = GUID_ContainerFormatJpeg; break;
        case ImageFormat::BMP:  containerFmt = GUID_ContainerFormatBmp;  break;
        case ImageFormat::TIF:  containerFmt = GUID_ContainerFormatTiff; break;
        case ImageFormat::GIF:  containerFmt = GUID_ContainerFormatGif;  break;
        default:
            logger::error("StillEncoder::SaveWIC: unhandled format {}", static_cast<int>(format_));
            return E_INVALIDARG;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInited = SUCCEEDED(hr);

    auto guard = [&](HRESULT result) {
        if (comInited) CoUninitialize();
        return result;
    };

    if (token) token->ThrowIfCancelled("StillEncoder::SaveWIC before DirectXTex call");

    const DirectX::Image* srcImg = img.GetImage(0, 0, 0);
    if (!srcImg) return guard(E_FAIL);

    if (format_ == ImageFormat::JPEG) {
        hr = DirectX::SaveToWICFile(
            *srcImg, DirectX::WIC_FLAGS_NONE,
            containerFmt, path.c_str(), nullptr,
            [&](IPropertyBag2* props) -> HRESULT {
                if (!props) return S_OK;
                PROPBAG2 opt{};
                opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
                VARIANT v{}; VariantInit(&v);
                v.vt = VT_R4;
                v.fltVal = std::clamp(jpegQuality_, 0.f, 100.f) / 100.0f;
                return props->Write(1, &opt, &v);
            });
    } else if (format_ == ImageFormat::TIF) {
        hr = DirectX::SaveToWICFile(
            *srcImg, DirectX::WIC_FLAGS_NONE,
            containerFmt, path.c_str(), nullptr,
            [&](IPropertyBag2* props) -> HRESULT {
                if (!props) return S_OK;
                PROPBAG2 opt{};
                opt.pstrName = const_cast<LPOLESTR>(L"TiffCompressionMethod");
                VARIANT v{}; VariantInit(&v);
                v.vt = VT_UI1;
                switch (tiffMode_) {
                    case TiffMode::LZW:     v.bVal = static_cast<BYTE>(WICTiffCompressionLZW);    break;
                    case TiffMode::ZIP:     v.bVal = static_cast<BYTE>(WICTiffCompressionZIP);    break;
                    case TiffMode::RLE:     v.bVal = static_cast<BYTE>(WICTiffCompressionRLE);    break;
                    case TiffMode::CCITT1D: v.bVal = static_cast<BYTE>(WICTiffCompressionCCITT3); break;
                    case TiffMode::CCITT4:  v.bVal = static_cast<BYTE>(WICTiffCompressionCCITT4); break;
                    default:                v.bVal = static_cast<BYTE>(WICTiffCompressionNone);   break;
                }
                return props->Write(1, &opt, &v);
            });
    } else {
        hr = DirectX::SaveToWICFile(*srcImg, DirectX::WIC_FLAGS_NONE,
                                    containerFmt, path.c_str());
    }

    if (FAILED(hr)) logger::error("StillEncoder::SaveWIC failed: 0x{:08X}", static_cast<uint32_t>(hr));
    else             logger::info("StillEncoder::SaveWIC saved: {}", util::wstring_to_utf8(path));

    return guard(hr);
}

// ============================================================
// DDS save (BC1–BC7 via DirectXTex)
// ============================================================
HRESULT StillEncoder::SaveDDS(
    const DirectX::ScratchImage& img,
    const std::wstring& path,
    CancellationToken::Ptr token)
{
    logger::info("StillEncoder: DDS save, mode={}", static_cast<int>(ddsMode_));
    if (token) token->ThrowIfCancelled("StillEncoder::SaveDDS start");

    DXGI_FORMAT compFmt{};
    DirectX::TEX_COMPRESS_FLAGS compFlags = DirectX::TEX_COMPRESS_DEFAULT;

    switch (ddsMode_) {
        case DDSMode::BC1:       compFmt = DXGI_FORMAT_BC1_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC2:       compFmt = DXGI_FORMAT_BC2_UNORM; break;
        case DDSMode::BC3:       compFmt = DXGI_FORMAT_BC3_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC4:       compFmt = DXGI_FORMAT_BC4_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC5:       compFmt = DXGI_FORMAT_BC5_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC6H:      compFmt = DXGI_FORMAT_BC6H_UF16; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC7_SLOW:  compFmt = DXGI_FORMAT_BC7_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC7_NORMAL:
        case DDSMode::BC7_FAST:
            compFmt   = DXGI_FORMAT_BC7_UNORM;
            compFlags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                DirectX::TEX_COMPRESS_PARALLEL | DirectX::TEX_COMPRESS_BC7_QUICK);
            break;
        default:
            compFmt   = DXGI_FORMAT_BC1_UNORM;
            compFlags = DirectX::TEX_COMPRESS_PARALLEL;
            break;
    }

    if (token) token->ThrowIfCancelled("StillEncoder::SaveDDS before compression");

    const DirectX::Image* srcImg = img.GetImage(0, 0, 0);
    if (!srcImg) return E_FAIL;

    const auto t0 = std::chrono::high_resolution_clock::now();

    DirectX::ScratchImage compressed;
    HRESULT hr = DirectX::Compress(*srcImg, compFmt, compFlags,
                                   DirectX::TEX_THRESHOLD_DEFAULT, compressed);

    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - t0);

    if (token) token->ThrowIfCancelled("StillEncoder::SaveDDS after compression");

    const DirectX::ScratchImage* toSave = SUCCEEDED(hr) ? &compressed : &img;

    if (FAILED(hr)) {
        logger::warn("StillEncoder::SaveDDS: BC compression failed ({} ms), saving uncompressed", dur.count());
    } else {
        float ratio = static_cast<float>(srcImg->slicePitch) /
                      static_cast<float>(compressed.GetImage(0,0,0)->slicePitch);
        logger::info("StillEncoder::SaveDDS: compressed in {} ms, ratio {:.2f}:1", dur.count(), ratio);
    }

    hr = DirectX::SaveToDDSFile(toSave->GetImages(), toSave->GetImageCount(),
                                 toSave->GetMetadata(), DirectX::DDS_FLAGS_NONE,
                                 path.c_str());
    if (FAILED(hr))
        logger::error("StillEncoder::SaveDDS: SaveToDDSFile failed: 0x{:08X}", static_cast<uint32_t>(hr));
    else
        logger::info("StillEncoder::SaveDDS saved: {}", util::wstring_to_utf8(path));

    return hr;
}
