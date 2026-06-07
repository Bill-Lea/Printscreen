#pragma once
#include <string>
#include <vector>
#include "capture/CancellationToken.h"

namespace DirectX { class ScratchImage; }

// ============================================================
// EncodeResult — returned by every encoder
// ============================================================
struct EncodeResult {
    bool         success{false};
    std::string  error;
    std::wstring outputPath;

    static EncodeResult Ok(std::wstring path) {
        return {true, {}, std::move(path)};
    }
    static EncodeResult Fail(std::string msg) {
        return {false, std::move(msg), {}};
    }
    static EncodeResult Cancelled() {
        return {false, "Cancelled", {}};
    }
};

// ============================================================
// IEncoder — abstract base for all image encoders
// ============================================================
class IEncoder {
public:
    virtual ~IEncoder() = default;

    // Single-frame encode.  Animated encoders may ignore this.
    virtual EncodeResult Encode(
        const DirectX::ScratchImage& frame,
        const std::wstring&          outputPath,
        CancellationToken::Ptr       token) = 0;

    // Multi-frame encode.  Still encoders use only frames[0].
    virtual EncodeResult EncodeSequence(
        std::vector<DirectX::ScratchImage>& frames,
        const std::wstring&                 outputPath,
        float                               fps,
        int                                 loopCount,
        int                                 deltaMode,
        CancellationToken::Ptr              token) = 0;
};
