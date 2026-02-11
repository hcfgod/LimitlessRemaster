#include "Assets/Cooking/CookedTexture2DFormat.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Limitless::Assets::Cooking
{
    namespace
    {
        static constexpr uint32_t kMagic = 0x3254584Cu; // 'LXT2' (Limitless Texture2D)
        static constexpr uint16_t kVersion = 1;

        struct Header
        {
            uint32_t Magic = kMagic;
            uint16_t Version = kVersion;
            uint16_t Reserved0 = 0;

            uint32_t Width = 0;
            uint32_t Height = 0;
            uint32_t MipCount = 0;

            // TextureSpecification fields (serialized as stable integers).
            uint32_t MinFilter = 0;
            uint32_t MagFilter = 0;
            uint32_t WrapU = 0;
            uint32_t WrapV = 0;
            uint32_t GenerateMipmaps = 0;
        };

        struct MipRecord
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            uint32_t SizeBytes = 0;
        };

        static void WriteBytes(std::vector<uint8_t>& out, const void* data, const size_t size)
        {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(data);
            out.insert(out.end(), b, b + size);
        }

        static Result<void> ReadBytes(const uint8_t* bytes, const size_t byteCount, size_t& cursor, void* out, const size_t outSize)
        {
            if (cursor + outSize > byteCount)
            {
                return Result<void>(ErrorCode::FileCorrupted, "CookedTexture2D: truncated blob");
            }
            std::memcpy(out, bytes + cursor, outSize);
            cursor += outSize;
            return Result<void>();
        }

        static std::vector<uint8_t> DownsampleBox2x2RGBA8(const uint8_t* src, const uint32_t srcW, const uint32_t srcH, uint32_t& outW, uint32_t& outH)
        {
            outW = (std::max)(1u, srcW / 2u);
            outH = (std::max)(1u, srcH / 2u);
            std::vector<uint8_t> dst(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4u);

            for (uint32_t y = 0; y < outH; ++y)
            {
                for (uint32_t x = 0; x < outW; ++x)
                {
                    const uint32_t sx0 = (std::min)(srcW - 1u, x * 2u + 0u);
                    const uint32_t sx1 = (std::min)(srcW - 1u, x * 2u + 1u);
                    const uint32_t sy0 = (std::min)(srcH - 1u, y * 2u + 0u);
                    const uint32_t sy1 = (std::min)(srcH - 1u, y * 2u + 1u);

                    const auto sample = [&](uint32_t sx, uint32_t sy, int c) -> uint32_t {
                        const size_t idx = (static_cast<size_t>(sy) * static_cast<size_t>(srcW) + static_cast<size_t>(sx)) * 4u + static_cast<size_t>(c);
                        return static_cast<uint32_t>(src[idx]);
                    };

                    uint32_t r = sample(sx0, sy0, 0) + sample(sx1, sy0, 0) + sample(sx0, sy1, 0) + sample(sx1, sy1, 0);
                    uint32_t g = sample(sx0, sy0, 1) + sample(sx1, sy0, 1) + sample(sx0, sy1, 1) + sample(sx1, sy1, 1);
                    uint32_t b = sample(sx0, sy0, 2) + sample(sx1, sy0, 2) + sample(sx0, sy1, 2) + sample(sx1, sy1, 2);
                    uint32_t a = sample(sx0, sy0, 3) + sample(sx1, sy0, 3) + sample(sx0, sy1, 3) + sample(sx1, sy1, 3);

                    const size_t di = (static_cast<size_t>(y) * static_cast<size_t>(outW) + static_cast<size_t>(x)) * 4u;
                    dst[di + 0] = static_cast<uint8_t>((r + 2u) / 4u);
                    dst[di + 1] = static_cast<uint8_t>((g + 2u) / 4u);
                    dst[di + 2] = static_cast<uint8_t>((b + 2u) / 4u);
                    dst[di + 3] = static_cast<uint8_t>((a + 2u) / 4u);
                }
            }

            return dst;
        }

        static TextureSpecification DeserializeSpecification(const Header& h)
        {
            TextureSpecification spec;
            spec.MinFilter = static_cast<TextureFilter>(h.MinFilter);
            spec.MagFilter = static_cast<TextureFilter>(h.MagFilter);
            spec.WrapU = static_cast<TextureWrap>(h.WrapU);
            spec.WrapV = static_cast<TextureWrap>(h.WrapV);
            spec.GenerateMipmaps = (h.GenerateMipmaps != 0);
            spec.FlipVerticallyOnLoad = false; // Cooked payloads are already oriented/flipped.
            return spec;
        }
    }

    Result<std::vector<uint8_t>> CookTexture2DFromRGBA8(
        const uint32_t width,
        const uint32_t height,
        const uint8_t* rgbaPixels,
        const TextureSpecification& specification)
    {
        if (width == 0 || height == 0 || rgbaPixels == nullptr)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidArgument, "CookedTexture2D: invalid input image");
        }

        TextureSpecification cookedSpec = specification;
        cookedSpec.FlipVerticallyOnLoad = false;

        std::vector<std::vector<uint8_t>> mipImages;
        mipImages.reserve(16);
        mipImages.emplace_back(rgbaPixels, rgbaPixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

        std::vector<std::pair<uint32_t, uint32_t>> mipSizes;
        mipSizes.reserve(16);
        mipSizes.emplace_back(width, height);

        if (cookedSpec.GenerateMipmaps)
        {
            uint32_t curW = width;
            uint32_t curH = height;
            while (curW > 1 || curH > 1)
            {
                uint32_t nextW = 0;
                uint32_t nextH = 0;
                const std::vector<uint8_t>& src = mipImages.back();
                std::vector<uint8_t> next = DownsampleBox2x2RGBA8(src.data(), curW, curH, nextW, nextH);

                mipImages.emplace_back(std::move(next));
                mipSizes.emplace_back(nextW, nextH);

                curW = nextW;
                curH = nextH;
            }
        }

        Header header;
        header.Width = width;
        header.Height = height;
        header.MipCount = static_cast<uint32_t>(mipImages.size());
        header.MinFilter = static_cast<uint32_t>(cookedSpec.MinFilter);
        header.MagFilter = static_cast<uint32_t>(cookedSpec.MagFilter);
        header.WrapU = static_cast<uint32_t>(cookedSpec.WrapU);
        header.WrapV = static_cast<uint32_t>(cookedSpec.WrapV);
        header.GenerateMipmaps = cookedSpec.GenerateMipmaps ? 1u : 0u;

        std::vector<uint8_t> out;
        out.reserve(sizeof(Header) + mipImages[0].size());

        WriteBytes(out, &header, sizeof(Header));

        // Mip records.
        for (size_t i = 0; i < mipImages.size(); ++i)
        {
            MipRecord rec;
            rec.Width = mipSizes[i].first;
            rec.Height = mipSizes[i].second;
            rec.SizeBytes = static_cast<uint32_t>(mipImages[i].size());
            WriteBytes(out, &rec, sizeof(MipRecord));
        }

        // Mip payloads.
        for (const auto& mip : mipImages)
        {
            WriteBytes(out, mip.data(), mip.size());
        }

        return out;
    }

    Result<CookedTexture2DView> ParseCookedTexture2DView(const uint8_t* bytes, const size_t byteCount)
    {
        if (bytes == nullptr || byteCount < sizeof(Header))
        {
            return Result<CookedTexture2DView>(ErrorCode::FileCorrupted, "CookedTexture2D: blob too small");
        }

        size_t cursor = 0;
        Header header{};
        {
            const auto r = ReadBytes(bytes, byteCount, cursor, &header, sizeof(Header));
            if (r.IsFailure())
            {
                return Result<CookedTexture2DView>(r.GetError());
            }
        }

        if (header.Magic != kMagic || header.Version != kVersion)
        {
            return Result<CookedTexture2DView>(ErrorCode::FileCorrupted, "CookedTexture2D: invalid header");
        }
        if (header.Width == 0 || header.Height == 0 || header.MipCount == 0)
        {
            return Result<CookedTexture2DView>(ErrorCode::FileCorrupted, "CookedTexture2D: invalid dimensions/mips");
        }

        const size_t recordsBytes = static_cast<size_t>(header.MipCount) * sizeof(MipRecord);
        if (byteCount < sizeof(Header) + recordsBytes)
        {
            return Result<CookedTexture2DView>(ErrorCode::FileCorrupted, "CookedTexture2D: truncated mip records");
        }

        std::vector<MipRecord> records;
        records.resize(header.MipCount);
        {
            const auto r = ReadBytes(bytes, byteCount, cursor, records.data(), recordsBytes);
            if (r.IsFailure())
            {
                return Result<CookedTexture2DView>(r.GetError());
            }
        }

        CookedTexture2DView view;
        view.Width = header.Width;
        view.Height = header.Height;
        view.Specification = DeserializeSpecification(header);
        view.MipLevels.clear();
        view.MipLevels.reserve(header.MipCount);

        for (uint32_t i = 0; i < header.MipCount; ++i)
        {
            const MipRecord& rec = records[i];
            if (rec.Width == 0 || rec.Height == 0 || rec.SizeBytes == 0)
            {
                return Result<CookedTexture2DView>(ErrorCode::FileCorrupted, "CookedTexture2D: invalid mip record");
            }
            if (cursor + rec.SizeBytes > byteCount)
            {
                return Result<CookedTexture2DView>(ErrorCode::FileCorrupted, "CookedTexture2D: truncated mip payloads");
            }

            CookedTexture2DMipLevelView mip;
            mip.Width = rec.Width;
            mip.Height = rec.Height;
            mip.SizeBytes = rec.SizeBytes;
            mip.PixelsRGBA8 = bytes + cursor;
            view.MipLevels.emplace_back(mip);

            cursor += rec.SizeBytes;
        }

        return view;
    }
}

