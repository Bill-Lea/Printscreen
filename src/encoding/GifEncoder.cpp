#include "PCH.h"
#include "GifEncoder.h"
#include "stringutils.h"
#include <DirectXTex.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <algorithm>

// ============================================================
// Internal helpers (file scope)
// ============================================================

namespace {

// Find the palette index used least often — used as the transparency index.
static BYTE FindTransparentIndex(const uint8_t* pixels, size_t count) {
    uint32_t usage[256]{};
    for (size_t i = 0; i < count; ++i) usage[pixels[i]]++;
    BYTE best = 0;
    uint32_t minUsage = UINT32_MAX;
    for (int i = 0; i < 256; ++i) {
        if (usage[i] < minUsage) { minUsage = usage[i]; best = static_cast<BYTE>(i); }
    }
    return best;
}

} // namespace

// ============================================================
// IEncoder overrides
// ============================================================

EncodeResult GifEncoder::Encode(
    const DirectX::ScratchImage& frame,
    const std::wstring& outputPath,
    CancellationToken::Ptr token)
{
    std::vector<DirectX::ScratchImage> frames;
    // ScratchImage is move-only; make a one-element sequence.
    // We encode at 1 fps single-loop.
    DirectX::ScratchImage copy;
    const DirectX::Image* img = frame.GetImage(0, 0, 0);
    if (!img) return EncodeResult::Fail("Null image");
    copy.Initialize2D(img->format, img->width, img->height, 1, 1);
    const DirectX::Image* dst = copy.GetImage(0, 0, 0);
    if (!dst) return EncodeResult::Fail("Copy alloc failed");
    std::memcpy(dst->pixels, img->pixels, img->slicePitch);
    frames.push_back(std::move(copy));
    return EncodeSequence(frames, outputPath, 1.0f, 1, 0, token);
}

EncodeResult GifEncoder::EncodeSequence(
    std::vector<DirectX::ScratchImage>& frames,
    const std::wstring& outputPath,
    float fps, int loopCount, int deltaMode,
    CancellationToken::Ptr token)
{
    if (frames.empty()) return EncodeResult::Fail("No frames");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInited = SUCCEEDED(hr);

    auto cleanup = [&](EncodeResult r) -> EncodeResult {
        if (comInited) CoUninitialize();
        return r;
    };

    if (token) { try { token->ThrowIfCancelled("GifEncoder::EncodeSequence"); }
                 catch (...) { return cleanup(EncodeResult::Cancelled()); } }

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return cleanup(EncodeResult::Fail("WIC factory creation failed"));

    Microsoft::WRL::ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE);

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    wic->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    // Write NETSCAPE2.0 loop extension
    Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> meta;
    encoder->GetMetadataQueryWriter(&meta);
    if (meta) {
        const BYTE netscape[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
        PROPVARIANT app{}; PropVariantInit(&app);
        app.vt = VT_VECTOR | VT_UI1;
        app.caub.pElems = const_cast<BYTE*>(netscape);
        app.caub.cElems = 11;
        meta->SetMetadataByName(L"/appext/Identification", &app);
        app.caub.pElems = nullptr; app.caub.cElems = 0; app.vt = VT_EMPTY;

        BYTE loopData[4] = { 3, 1,
            static_cast<BYTE>(loopCount & 0xFF),
            static_cast<BYTE>((loopCount >> 8) & 0xFF) };
        PROPVARIANT ld{}; PropVariantInit(&ld);
        ld.vt = VT_VECTOR | VT_UI1;
        ld.caub.pElems = loopData;
        ld.caub.cElems = 4;
        meta->SetMetadataByName(L"/appext/Data", &ld);
        ld.caub.pElems = nullptr; ld.caub.cElems = 0; ld.vt = VT_EMPTY;
    }

    const double frameInterval = 1.0 / std::max(0.1, static_cast<double>(fps));
    const USHORT delayHundredths = static_cast<USHORT>(frameInterval * 100);

    for (size_t i = 0; i < frames.size(); ++i) {
        if (token) { try { token->ThrowIfCancelled("GifEncoder frame loop"); }
                     catch (...) { return cleanup(EncodeResult::Cancelled()); } }

        const DirectX::Image* img = frames[i].GetImage(0, 0, 0);
        if (!img) { logger::warn("GifEncoder: null frame {}, skipping", i); continue; }

        const WICPixelFormatGUID srcFmt = (img->format == DXGI_FORMAT_R8G8B8A8_UNORM)
            ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppBGRA;

        Microsoft::WRL::ComPtr<IWICBitmap> wicBmp;
        hr = wic->CreateBitmapFromMemory(
            static_cast<UINT>(img->width), static_cast<UINT>(img->height),
            srcFmt, static_cast<UINT>(img->rowPitch),
            static_cast<UINT>(img->slicePitch), img->pixels, &wicBmp);
        if (FAILED(hr)) { logger::warn("GifEncoder: bitmap create failed for frame {}", i); continue; }

        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEnc;
        Microsoft::WRL::ComPtr<IPropertyBag2> bag;
        encoder->CreateNewFrame(&frameEnc, &bag);
        frameEnc->Initialize(bag.Get());

        // Set frame metadata (delay + disposal)
        Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> fmeta;
        if (SUCCEEDED(frameEnc->GetMetadataQueryWriter(&fmeta)) && fmeta) {
            PROPVARIANT pv{}; PropVariantInit(&pv);
            pv.vt = VT_UI2; pv.uiVal = delayHundredths;
            fmeta->SetMetadataByName(L"/grctlext/Delay", &pv);
            PropVariantClear(&pv);
            pv.vt = VT_UI1; pv.bVal = 2; // restore to background
            fmeta->SetMetadataByName(L"/grctlext/Disposal", &pv);
            PropVariantClear(&pv);
        }

        frameEnc->SetSize(static_cast<UINT>(img->width), static_cast<UINT>(img->height));
        WICPixelFormatGUID outFmt = GUID_WICPixelFormat32bppBGRA;
        frameEnc->SetPixelFormat(&outFmt);
        frameEnc->WriteSource(wicBmp.Get(), nullptr);
        frameEnc->Commit();
    }

    encoder->Commit();
    return cleanup(EncodeResult::Ok(outputPath));
}

// ============================================================
// EncodeFromFiles — disk-based, memory-efficient path
// Called by CaptureSession after all frames are on disk as BMP.
// ============================================================
EncodeResult GifEncoder::EncodeFromFiles(
    const std::vector<std::wstring>& framePaths,
    const std::wstring& outputPath,
    float fps, int loopCount,
    int deltaMode, int optimize,
    CancellationToken::Ptr token)
{
    logger::info("GifEncoder::EncodeFromFiles: {} frames, fps={}, loopCount={}, delta={}, opt={}",
                 framePaths.size(), fps, loopCount, deltaMode, optimize);

    if (framePaths.empty()) return EncodeResult::Fail("No frame files");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInited = (hr == S_OK || hr == S_FALSE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return EncodeResult::Fail("COM init failed: HRESULT 0x" +
                                  [&]{ char b[16]; sprintf_s(b, "%08X", static_cast<uint32_t>(hr)); return std::string(b); }());

    auto cleanup = [&](EncodeResult r) -> EncodeResult {
        if (comInited) CoUninitialize();
        return r;
    };

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return cleanup(EncodeResult::Fail("WIC factory failed"));

    Microsoft::WRL::ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE)))
        return cleanup(EncodeResult::Fail("Cannot open output: " + util::wstring_to_utf8(outputPath)));

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    wic->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    // Write NETSCAPE2.0 extension
    Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> meta;
    encoder->GetMetadataQueryWriter(&meta);
    if (meta) {
        const BYTE netscape[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
        PROPVARIANT app{}; PropVariantInit(&app);
        app.vt = VT_VECTOR | VT_UI1;
        app.caub.pElems = const_cast<BYTE*>(netscape);
        app.caub.cElems = 11;
        meta->SetMetadataByName(L"/appext/Identification", &app);
        app.caub.pElems = nullptr; app.caub.cElems = 0; app.vt = VT_EMPTY;

        BYTE loopData[4] = { 3, 1,
            static_cast<BYTE>(loopCount & 0xFF),
            static_cast<BYTE>((loopCount >> 8) & 0xFF) };
        PROPVARIANT ld{}; PropVariantInit(&ld);
        ld.vt = VT_VECTOR | VT_UI1;
        ld.caub.pElems = loopData;
        ld.caub.cElems = 4;
        meta->SetMetadataByName(L"/appext/Data", &ld);
        ld.caub.pElems = nullptr; ld.caub.cElems = 0; ld.vt = VT_EMPTY;
    }

    const double frameInterval = 1.0 / std::clamp(static_cast<double>(fps), 1.0, 30.0);
    const USHORT delayHundredths = static_cast<USHORT>(std::round(frameInterval * 100.0));

    const bool useDiff  = (deltaMode == 1);
    const bool useDelta = (optimize  == 1);

    std::vector<uint8_t>                prevQuantizedPixels;
    UINT                                prevQuantizedStride = 0;
    size_t                              prevWidth = 0, prevHeight = 0;
    Microsoft::WRL::ComPtr<IWICPalette> prevPalette;

    for (size_t i = 0; i < framePaths.size(); ++i) {
        if (token) { try { token->ThrowIfCancelled("GifEncoder frame encode"); }
                     catch (...) { return cleanup(EncodeResult::Cancelled()); } }

        // Load frame from disk
        DirectX::ScratchImage frameImg;
        if (FAILED(DirectX::LoadFromWICFile(framePaths[i].c_str(), DirectX::WIC_FLAGS_NONE, nullptr, frameImg))) {
            logger::warn("GifEncoder: failed to load frame {}", i);
            continue;
        }

        const DirectX::Image* img = frameImg.GetImage(0, 0, 0);
        if (!img) { logger::warn("GifEncoder: null image for frame {}", i); continue; }

        const WICPixelFormatGUID srcFmt = (img->format == DXGI_FORMAT_R8G8B8A8_UNORM)
            ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppBGRA;

        UINT fLeft = 0, fTop = 0;
        UINT fWidth  = static_cast<UINT>(img->width);
        UINT fHeight = static_cast<UINT>(img->height);
        Microsoft::WRL::ComPtr<IWICBitmap> wicBmp;
        bool useTransparency = false;
        BYTE transparentIdx  = 0;

        // ---- Differential path ----
        if (useDiff && i > 0 && !prevQuantizedPixels.empty()
            && prevWidth == img->width && prevHeight == img->height)
        {
            Microsoft::WRL::ComPtr<IWICBitmap> fullBmp;
            hr = wic->CreateBitmapFromMemory(
                fWidth, fHeight, srcFmt,
                static_cast<UINT>(img->rowPitch),
                static_cast<UINT>(img->slicePitch),
                img->pixels, &fullBmp);

            if (SUCCEEDED(hr)) {
                Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
                wic->CreateFormatConverter(&conv);
                hr = conv->Initialize(fullBmp.Get(), GUID_WICPixelFormat8bppIndexed,
                                      WICBitmapDitherTypeErrorDiffusion,
                                      prevPalette.Get(), 0.0, WICBitmapPaletteTypeMedianCut);

                if (SUCCEEDED(hr)) {
                    Microsoft::WRL::ComPtr<IWICBitmap> qBmp;
                    wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnDemand, &qBmp);

                    WICRect lockRc = { 0, 0, static_cast<INT>(fWidth), static_cast<INT>(fHeight) };
                    Microsoft::WRL::ComPtr<IWICBitmapLock> currLock;
                    if (SUCCEEDED(qBmp->Lock(&lockRc, WICBitmapLockRead, &currLock))) {
                        UINT currStride = 0, currBufSz = 0;
                        BYTE* currData = nullptr;
                        currLock->GetDataPointer(&currBufSz, &currData);
                        currLock->GetStride(&currStride);

                        // Find bounding box of changed pixels
                        UINT minX = fWidth, minY = fHeight, maxX = 0, maxY = 0;
                        bool hasChanges = false;
                        for (UINT y = 0; y < fHeight; ++y) {
                            const uint8_t* cr = currData + y * currStride;
                            const uint8_t* pr = prevQuantizedPixels.data() + y * prevQuantizedStride;
                            for (UINT x = 0; x < fWidth; ++x) {
                                if (cr[x] != pr[x]) {
                                    hasChanges = true;
                                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                                }
                            }
                        }

                        if (hasChanges && maxX >= minX && maxY >= minY) {
                            fLeft = minX; fTop = minY;
                            fWidth  = maxX - minX + 1;
                            fHeight = maxY - minY + 1;

                            std::vector<uint8_t> diffPx(fWidth * fHeight);

                            if (useDelta) {
                                // True delta: unchanged pixels → transparent index
                                transparentIdx = FindTransparentIndex(currData, img->width * img->height);
                                for (UINT dy = 0; dy < fHeight; ++dy) {
                                    const uint8_t* cr2 = currData + (fTop + dy) * currStride;
                                    const uint8_t* pr2 = prevQuantizedPixels.data() + (fTop + dy) * prevQuantizedStride;
                                    for (UINT dx = 0; dx < fWidth; ++dx) {
                                        UINT sx = fLeft + dx;
                                        diffPx[dy * fWidth + dx] = (cr2[sx] == pr2[sx])
                                            ? transparentIdx : cr2[sx];
                                    }
                                }
                                useTransparency = true;
                            } else {
                                // Region only: copy changed region
                                for (UINT dy = 0; dy < fHeight; ++dy) {
                                    const uint8_t* cr2 = currData + (fTop + dy) * currStride;
                                    for (UINT dx = 0; dx < fWidth; ++dx)
                                        diffPx[dy * fWidth + dx] = cr2[fLeft + dx];
                                }
                            }

                            // Store current for next comparison
                            prevQuantizedStride = currStride;
                            prevQuantizedPixels.assign(currData, currData + img->height * currStride);
                            prevWidth = img->width; prevHeight = img->height;
                            currLock.Reset();

                            // Build modified palette
                            Microsoft::WRL::ComPtr<IWICPalette> pal;
                            wic->CreatePalette(&pal);
                            qBmp->CopyPalette(pal.Get());

                            if (useDelta && useTransparency) {
                                UINT cnt = 0; pal->GetColorCount(&cnt);
                                std::vector<WICColor> clrs(cnt); UINT actual = 0;
                                pal->GetColors(cnt, clrs.data(), &actual);
                                if (transparentIdx < actual) clrs[transparentIdx] = 0x00000000;
                                pal->InitializeCustom(clrs.data(), actual);
                            }

                            // Create the differential bitmap
                            hr = wic->CreateBitmap(fWidth, fHeight,
                                                   GUID_WICPixelFormat8bppIndexed,
                                                   WICBitmapCacheOnDemand, &wicBmp);
                            if (SUCCEEDED(hr)) {
                                wicBmp->SetPalette(pal.Get());
                                WICRect wr = {0, 0, static_cast<INT>(fWidth), static_cast<INT>(fHeight)};
                                Microsoft::WRL::ComPtr<IWICBitmapLock> wl;
                                wicBmp->Lock(&wr, WICBitmapLockWrite, &wl);
                                UINT ws = 0, wbsz = 0; BYTE* wd = nullptr;
                                wl->GetDataPointer(&wbsz, &wd); wl->GetStride(&ws);
                                for (UINT y = 0; y < fHeight; ++y)
                                    std::memcpy(wd + y * ws, diffPx.data() + y * fWidth, fWidth);
                            }
                        } else {
                            // No change — encode full quantized frame
                            wicBmp = qBmp;
                            fLeft = 0; fTop = 0;
                            fWidth  = static_cast<UINT>(img->width);
                            fHeight = static_cast<UINT>(img->height);
                            prevQuantizedStride = currStride;
                            prevQuantizedPixels.assign(currData, currData + img->height * currStride);
                            prevWidth = img->width; prevHeight = img->height;
                            currLock.Reset();
                        }
                    }
                }
            }
        }

        // ---- Full frame path (first frame, non-differential, or fallback) ----
        if (!wicBmp) {
            fLeft = 0; fTop = 0;
            fWidth  = static_cast<UINT>(img->width);
            fHeight = static_cast<UINT>(img->height);
            useTransparency = false;

            Microsoft::WRL::ComPtr<IWICBitmap> fullBmp;
            if (FAILED(wic->CreateBitmapFromMemory(fWidth, fHeight, srcFmt,
                    static_cast<UINT>(img->rowPitch),
                    static_cast<UINT>(img->slicePitch), img->pixels, &fullBmp))) {
                logger::warn("GifEncoder: full bitmap create failed for frame {}", i);
                continue;
            }

            if (useDiff) {
                // Quantize and store for next comparison
                Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
                wic->CreateFormatConverter(&conv);
                if (SUCCEEDED(conv->Initialize(fullBmp.Get(), GUID_WICPixelFormat8bppIndexed,
                                               WICBitmapDitherTypeErrorDiffusion,
                                               prevPalette.Get(), 0.0, WICBitmapPaletteTypeMedianCut))) {
                    Microsoft::WRL::ComPtr<IWICBitmap> qBmp;
                    wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnDemand, &qBmp);

                    WICRect lr = {0, 0, static_cast<INT>(fWidth), static_cast<INT>(fHeight)};
                    Microsoft::WRL::ComPtr<IWICBitmapLock> lk;
                    if (SUCCEEDED(qBmp->Lock(&lr, WICBitmapLockRead, &lk))) {
                        UINT stride = 0, bsz = 0; BYTE* data = nullptr;
                        lk->GetDataPointer(&bsz, &data); lk->GetStride(&stride);
                        prevQuantizedStride = stride;
                        prevQuantizedPixels.assign(data, data + img->height * stride);
                        prevWidth = img->width; prevHeight = img->height;
                        lk.Reset();

                        // Store palette for consistency in later frames
                        wic->CreatePalette(&prevPalette);
                        qBmp->CopyPalette(prevPalette.Get());
                    }
                    wicBmp = qBmp;
                }
            }

            if (!wicBmp) wicBmp = fullBmp;
        }

        // ---- Write this frame into the GIF ----
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEnc;
        Microsoft::WRL::ComPtr<IPropertyBag2> bag;
        encoder->CreateNewFrame(&frameEnc, &bag);
        frameEnc->Initialize(bag.Get());

        // Metadata: delay + disposal + (optional) transparency
        Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> fmeta;
        if (SUCCEEDED(frameEnc->GetMetadataQueryWriter(&fmeta)) && fmeta) {
            PROPVARIANT pv{}; PropVariantInit(&pv);
            pv.vt = VT_UI2; pv.uiVal = delayHundredths;
            fmeta->SetMetadataByName(L"/grctlext/Delay", &pv); PropVariantClear(&pv);
            pv.vt = VT_UI1; pv.bVal = 2;
            fmeta->SetMetadataByName(L"/grctlext/Disposal", &pv); PropVariantClear(&pv);
            if (useTransparency) {
                pv.vt = VT_UI1; pv.bVal = 1;
                fmeta->SetMetadataByName(L"/grctlext/TransparencyFlag", &pv); PropVariantClear(&pv);
                pv.vt = VT_UI1; pv.bVal = transparentIdx;
                fmeta->SetMetadataByName(L"/grctlext/TransparentColorIndex", &pv); PropVariantClear(&pv);
            }

            // Frame offset within the canvas
            pv.vt = VT_UI2; pv.uiVal = static_cast<USHORT>(fLeft);
            fmeta->SetMetadataByName(L"/imgdesc/Left", &pv); PropVariantClear(&pv);
            pv.vt = VT_UI2; pv.uiVal = static_cast<USHORT>(fTop);
            fmeta->SetMetadataByName(L"/imgdesc/Top", &pv); PropVariantClear(&pv);
        }

        frameEnc->SetSize(fWidth, fHeight);
        WICPixelFormatGUID outFmt = GUID_WICPixelFormat8bppIndexed;
        frameEnc->SetPixelFormat(&outFmt);

        WICRect srcRect = { static_cast<INT>(fLeft), static_cast<INT>(fTop),
                            static_cast<INT>(fWidth), static_cast<INT>(fHeight) };
        frameEnc->WriteSource(wicBmp.Get(), useDiff ? nullptr : &srcRect);
        frameEnc->Commit();

        logger::debug("GifEncoder: encoded frame {}/{}", i + 1, framePaths.size());
    }

    encoder->Commit();
    logger::info("GifEncoder::EncodeFromFiles: complete -> {}", util::wstring_to_utf8(outputPath));
    return cleanup(EncodeResult::Ok(outputPath));
}
