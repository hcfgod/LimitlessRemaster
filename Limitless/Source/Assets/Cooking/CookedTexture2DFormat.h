#pragma once

#include "Core/Error.h"
#include "Graphics/Texture.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Limitless::Assets::Cooking
{
    struct CookedTexture2DMipLevelView
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        const uint8_t* PixelsRGBA8 = nullptr;
        uint32_t SizeBytes = 0;
    };

    struct CookedTexture2DView
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        TextureSpecification Specification{};
        std::vector<CookedTexture2DMipLevelView> MipLevels; // Views into the provided blob bytes.
    };

    // Serializes a cooked Texture2D (RGBA8 + mip chain + spec) into bytes.
    Result<std::vector<uint8_t>> CookTexture2DFromRGBA8(
        uint32_t width,
        uint32_t height,
        const uint8_t* rgbaPixels,
        const TextureSpecification& specification);

    // Parses a cooked blob and returns views into the passed-in byte buffer.
    Result<CookedTexture2DView> ParseCookedTexture2DView(const uint8_t* bytes, size_t byteCount);
}

