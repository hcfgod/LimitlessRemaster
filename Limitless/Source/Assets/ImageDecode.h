#pragma once

#include "Core/Error.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Limitless::Assets
{
    struct DecodedImageRGBA8
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::vector<uint8_t> Pixels; // RGBA8
    };

    // Decodes an image file from disk into RGBA8 CPU memory.
    Result<DecodedImageRGBA8> DecodeToRGBA8(const std::string& path, bool flipVerticallyOnLoad);

    // Decodes an image already loaded into memory into RGBA8 CPU memory.
    Result<DecodedImageRGBA8> DecodeToRGBA8FromMemory(const uint8_t* bytes, size_t byteCount, const std::string& debugName, bool flipVerticallyOnLoad);

    // Minimal ASCII PPM (P3) loader for small dev/test assets.
    Result<DecodedImageRGBA8> TryDecodePpmP3ToRGBA8(const std::string& path);
    Result<DecodedImageRGBA8> TryDecodePpmP3ToRGBA8FromMemory(const uint8_t* bytes, size_t byteCount, const std::string& debugName);

    // Utility used by loaders/cookers.
    void FlipVerticalRGBA8(DecodedImageRGBA8& img);
}

