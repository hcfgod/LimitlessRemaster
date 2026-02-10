#include "Assets/TextureAsset.h"
#include "Assets/AssetBundle.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetLoadCoordinator.h"

#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"

#include "stb/stb_image/stb_image.h"

#include <vector>
#include <cstring>
#include <mutex>
#include <fstream>
#include <cctype>
#include <future>
#include <sstream>

namespace Limitless::Assets
{
    struct DecodedImageRGBA8
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::vector<uint8_t> Pixels; // RGBA8
    };

    // -----------------------------------------------------------------------------
    // JPEG EXIF orientation handling (minimum viable, professional behavior)
    //
    // Many JPEGs captured/edited on phones are stored "sideways" and rely on an EXIF
    // orientation tag to tell viewers how to rotate/mirror the image. stb_image does
    // not apply EXIF orientation for you, so we fix it at decode time.
    //
    // Supported orientations: 1..8 (including mirror/transpose variants).
    // If parsing fails, we default to 1 (no transform).
    // -----------------------------------------------------------------------------
    static bool EndsWithCaseInsensitive(const std::string& s, const char* suffix)
    {
        if (!suffix)
        {
            return false;
        }

        const size_t suffixLen = std::strlen(suffix);
        if (s.size() < suffixLen)
        {
            return false;
        }

        const size_t start = s.size() - suffixLen;
        for (size_t i = 0; i < suffixLen; ++i)
        {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[start + i])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
            if (a != b)
            {
                return false;
            }
        }
        return true;
    }

    static bool IsJpegPath(const std::string& path)
    {
        return EndsWithCaseInsensitive(path, ".jpg") || EndsWithCaseInsensitive(path, ".jpeg");
    }

    static bool LooksLikeJpeg(const uint8_t* bytes, size_t byteCount)
    {
        return bytes && byteCount >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
    }

    static uint16_t ReadBE16(const uint8_t* p)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
    }

    static uint16_t ReadLE16(const uint8_t* p)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[1]) << 8) | static_cast<uint16_t>(p[0]));
    }

    static uint32_t ReadBE32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |
               (static_cast<uint32_t>(p[3]) << 0);
    }

    static uint32_t ReadLE32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[3]) << 24) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[1]) << 8)  |
               (static_cast<uint32_t>(p[0]) << 0);
    }

    static int ReadExifOrientationFromJpegBytes(const uint8_t* bytes, size_t byteCount)
    {
        if (!LooksLikeJpeg(bytes, byteCount))
        {
            return 1;
        }

        // Walk JPEG segments: SOI (FFD8), then [marker][len][payload]...
        size_t pos = 2;
        while (pos + 4 <= byteCount)
        {
            if (bytes[pos] != 0xFF)
            {
                break;
            }

            const uint8_t marker = bytes[pos + 1];
            pos += 2;

            // Start of Scan / End of Image: segment parsing stops here.
            if (marker == 0xDA || marker == 0xD9)
            {
                break;
            }

            if (pos + 2 > byteCount)
            {
                break;
            }

            const uint16_t segLen = ReadBE16(bytes + pos);
            pos += 2;
            if (segLen < 2)
            {
                break;
            }

            const size_t segPayloadStart = pos;
            const size_t segPayloadEnd = pos + (static_cast<size_t>(segLen) - 2u);
            if (segPayloadEnd > byteCount)
            {
                break;
            }

            // APP1 = EXIF
            if (marker == 0xE1 && (segPayloadEnd - segPayloadStart) >= 6)
            {
                const uint8_t* payload = bytes + segPayloadStart;
                const size_t payloadSize = segPayloadEnd - segPayloadStart;

                if (payloadSize >= 6 &&
                    payload[0] == 'E' && payload[1] == 'x' && payload[2] == 'i' && payload[3] == 'f' &&
                    payload[4] == 0x00 && payload[5] == 0x00)
                {
                    // TIFF header starts immediately after "Exif\0\0"
                    const uint8_t* tiff = payload + 6;
                    const size_t tiffSize = payloadSize - 6;
                    if (tiffSize < 8)
                    {
                        return 1;
                    }

                    const bool littleEndian = (tiff[0] == 'I' && tiff[1] == 'I');
                    const bool bigEndian = (tiff[0] == 'M' && tiff[1] == 'M');
                    if (!littleEndian && !bigEndian)
                    {
                        return 1;
                    }

                    const auto read16 = littleEndian ? ReadLE16 : ReadBE16;
                    const auto read32 = littleEndian ? ReadLE32 : ReadBE32;

                    const uint16_t magic = read16(tiff + 2);
                    if (magic != 42)
                    {
                        return 1;
                    }

                    const uint32_t ifd0Offset = read32(tiff + 4);
                    if (ifd0Offset >= tiffSize || (tiffSize - ifd0Offset) < 2)
                    {
                        return 1;
                    }

                    const uint8_t* ifd0 = tiff + ifd0Offset;
                    const uint16_t entryCount = read16(ifd0);

                    const size_t entriesBytes = static_cast<size_t>(entryCount) * 12u;
                    if ((tiffSize - ifd0Offset) < (2u + entriesBytes))
                    {
                        return 1;
                    }

                    for (uint16_t i = 0; i < entryCount; ++i)
                    {
                        const uint8_t* entry = ifd0 + 2u + static_cast<size_t>(i) * 12u;
                        const uint16_t tag = read16(entry + 0);
                        const uint16_t type = read16(entry + 2);
                        const uint32_t count = read32(entry + 4);

                        // Orientation: tag 0x0112, type SHORT (3), count 1.
                        if (tag == 0x0112 && type == 3 && count == 1)
                        {
                            const uint16_t value = read16(entry + 8);
                            if (value >= 1 && value <= 8)
                            {
                                return static_cast<int>(value);
                            }
                            return 1;
                        }
                    }

                    return 1;
                }
            }

            pos = segPayloadEnd;
        }

        return 1;
    }

    static void ApplyExifOrientationRGBA8(DecodedImageRGBA8& img, int orientation)
    {
        if (orientation <= 1 || orientation > 8)
        {
            return;
        }
        if (img.Width == 0 || img.Height == 0 || img.Pixels.empty())
        {
            return;
        }

        const uint32_t srcW = img.Width;
        const uint32_t srcH = img.Height;
        const uint8_t* src = img.Pixels.data();

        const bool swapWH = (orientation == 5 || orientation == 6 || orientation == 7 || orientation == 8);
        const uint32_t dstW = swapWH ? srcH : srcW;
        const uint32_t dstH = swapWH ? srcW : srcH;

        std::vector<uint8_t> dstPixels(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4u);

        auto srcIndex = [srcW](uint32_t x, uint32_t y) -> size_t {
            return (static_cast<size_t>(y) * static_cast<size_t>(srcW) + static_cast<size_t>(x)) * 4u;
        };
        auto dstIndex = [dstW](uint32_t x, uint32_t y) -> size_t {
            return (static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)) * 4u;
        };

        for (uint32_t y = 0; y < dstH; ++y)
        {
            for (uint32_t x = 0; x < dstW; ++x)
            {
                uint32_t sx = 0;
                uint32_t sy = 0;

                switch (orientation)
                {
                    case 2: // Mirror horizontal
                        sx = (srcW - 1u) - x; sy = y;
                        break;
                    case 3: // Rotate 180
                        sx = (srcW - 1u) - x; sy = (srcH - 1u) - y;
                        break;
                    case 4: // Mirror vertical
                        sx = x; sy = (srcH - 1u) - y;
                        break;
                    case 5: // Transpose
                        sx = y; sy = x;
                        break;
                    case 6: // Rotate 90 CW
                        sx = y; sy = (srcH - 1u) - x;
                        break;
                    case 7: // Transverse
                        sx = (srcW - 1u) - y; sy = (srcH - 1u) - x;
                        break;
                    case 8: // Rotate 90 CCW
                        sx = (srcW - 1u) - y; sy = x;
                        break;
                    default:
                        sx = x; sy = y;
                        break;
                }

                const size_t si = srcIndex(sx, sy);
                const size_t di = dstIndex(x, y);
                dstPixels[di + 0] = src[si + 0];
                dstPixels[di + 1] = src[si + 1];
                dstPixels[di + 2] = src[si + 2];
                dstPixels[di + 3] = src[si + 3];
            }
        }

        img.Width = dstW;
        img.Height = dstH;
        img.Pixels = std::move(dstPixels);
    }

    static void FlipVerticalRGBA8(DecodedImageRGBA8& img)
    {
        if (img.Width == 0 || img.Height == 0 || img.Pixels.empty())
        {
            return;
        }

        const uint32_t w = img.Width;
        const uint32_t h = img.Height;
        const size_t rowBytes = static_cast<size_t>(w) * 4u;

        std::vector<uint8_t> tempRow(rowBytes);
        for (uint32_t y = 0; y < h / 2u; ++y)
        {
            uint8_t* rowA = img.Pixels.data() + static_cast<size_t>(y) * rowBytes;
            uint8_t* rowB = img.Pixels.data() + static_cast<size_t>(h - 1u - y) * rowBytes;

            std::memcpy(tempRow.data(), rowA, rowBytes);
            std::memcpy(rowA, rowB, rowBytes);
            std::memcpy(rowB, tempRow.data(), rowBytes);
        }
    }

    static Result<DecodedImageRGBA8> DecodeToRGBA8(const std::string& path, bool flipVertically)
    {
        // stb_image uses global state for vertical flip, so we serialize decode calls.
        static std::mutex s_StbMutex;
        std::lock_guard<std::mutex> lock(s_StbMutex);

        // Decode unflipped first; we apply EXIF orientation + flip ourselves on the CPU so
        // behavior is deterministic and does not depend on stb's global flip state.
        stbi_set_flip_vertically_on_load(0);

        int exifOrientation = 1;
        if (IsJpegPath(path))
        {
            // Parse only a small prefix; EXIF lives near the start.
            std::ifstream in(path, std::ios::in | std::ios::binary);
            if (in.is_open())
            {
                std::vector<uint8_t> header;
                header.resize(64u * 1024u);
                in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
                const std::streamsize got = in.gcount();
                if (got > 0)
                {
                    exifOrientation = ReadExifOrientationFromJpegBytes(header.data(), static_cast<size_t>(got));
                }
            }
        }

        int w = 0, h = 0, channels = 0;
        // Force 4 channels so GPU upload path is consistent.
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data || w <= 0 || h <= 0)
        {
            if (data) stbi_image_free(data);
            const char* reason = stbi_failure_reason();
            std::string message = "Failed to decode image: " + path;
            if (reason && reason[0] != '\0')
            {
                message += " (";
                message += reason;
                message += ")";
            }
            return Result<DecodedImageRGBA8>(ErrorCode::ResourceNotFound, message);
        }

        DecodedImageRGBA8 img;
        img.Width = static_cast<uint32_t>(w);
        img.Height = static_cast<uint32_t>(h);
        img.Pixels.resize(static_cast<size_t>(img.Width) * static_cast<size_t>(img.Height) * 4u);
        std::memcpy(img.Pixels.data(), data, img.Pixels.size());

        stbi_image_free(data);

        ApplyExifOrientationRGBA8(img, exifOrientation);
        if (flipVertically)
        {
            FlipVerticalRGBA8(img);
        }
        return img;
    }

    static Result<DecodedImageRGBA8> DecodeToRGBA8FromMemory(const uint8_t* bytes, size_t byteCount, const std::string& debugName, bool flipVertically)
    {
        if (!bytes || byteCount == 0)
        {
            return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "DecodeToRGBA8FromMemory: empty input: " + debugName);
        }

        // stb_image uses global state for vertical flip, so we serialize decode calls.
        static std::mutex s_StbMutex;
        std::lock_guard<std::mutex> lock(s_StbMutex);

        stbi_set_flip_vertically_on_load(0);

        const int exifOrientation = LooksLikeJpeg(bytes, byteCount)
            ? ReadExifOrientationFromJpegBytes(bytes, byteCount)
            : 1;

        int w = 0, h = 0, channels = 0;
        stbi_uc* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes), static_cast<int>(byteCount), &w, &h, &channels, 4);
        if (!data || w <= 0 || h <= 0)
        {
            if (data) stbi_image_free(data);
            const char* reason = stbi_failure_reason();
            std::string message = "Failed to decode image from AssetBundle: " + debugName;
            if (reason && reason[0] != '\0')
            {
                message += " (";
                message += reason;
                message += ")";
            }
            return Result<DecodedImageRGBA8>(ErrorCode::ResourceNotFound, message);
        }

        DecodedImageRGBA8 img;
        img.Width = static_cast<uint32_t>(w);
        img.Height = static_cast<uint32_t>(h);
        img.Pixels.resize(static_cast<size_t>(img.Width) * static_cast<size_t>(img.Height) * 4u);
        std::memcpy(img.Pixels.data(), data, img.Pixels.size());

        stbi_image_free(data);

        ApplyExifOrientationRGBA8(img, exifOrientation);
        if (flipVertically)
        {
            FlipVerticalRGBA8(img);
        }
        return img;
    }

    struct PpmTokenReader
    {
        explicit PpmTokenReader(std::istream& in)
            : m_In(in)
        {
        }

        bool ReadToken(std::string& out)
        {
            out.clear();

            while (true)
            {
                int c = m_In.get();
                if (!m_In)
                {
                    return false;
                }

                // Skip whitespace.
                if (std::isspace(static_cast<unsigned char>(c)) != 0)
                {
                    continue;
                }

                // Skip comments.
                if (c == '#')
                {
                    std::string dummy;
                    std::getline(m_In, dummy);
                    continue;
                }

                // Start token.
                out.push_back(static_cast<char>(c));
                break;
            }

            while (true)
            {
                const int c = m_In.peek();
                if (!m_In)
                {
                    break;
                }

                if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == '#')
                {
                    break;
                }

                out.push_back(static_cast<char>(m_In.get()));
            }

            return true;
        }

        std::istream& m_In;
    };

    // Minimal ASCII PPM (P3) loader for small dev/test assets.
    // Produces RGBA8 output.
    static Result<DecodedImageRGBA8> TryDecodePpmP3ToRGBA8(const std::string& path)
    {
        std::ifstream in(path, std::ios::in);
        if (!in.is_open())
        {
            return Result<DecodedImageRGBA8>(ErrorCode::ResourceNotFound, "Failed to open PPM file: " + path);
        }

        PpmTokenReader reader(in);

        std::string token;
        if (!reader.ReadToken(token))
        {
            return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (empty file): " + path);
        }
        if (token != "P3")
        {
            return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "PPM is not ASCII P3: " + path);
        }

        if (!reader.ReadToken(token))
        {
            return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (missing width): " + path);
        }
        const int width = std::stoi(token);

        if (!reader.ReadToken(token))
        {
            return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (missing height): " + path);
        }
        const int height = std::stoi(token);

        if (!reader.ReadToken(token))
        {
            return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (missing max value): " + path);
        }
        const int maxValue = std::stoi(token);

        if (width <= 0 || height <= 0)
        {
            return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "PPM invalid dimensions: " + path);
        }
        if (maxValue <= 0 || maxValue > 255)
        {
            return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "PPM maxValue must be 1..255: " + path);
        }

        DecodedImageRGBA8 img;
        img.Width = static_cast<uint32_t>(width);
        img.Height = static_cast<uint32_t>(height);
        img.Pixels.resize(static_cast<size_t>(img.Width) * static_cast<size_t>(img.Height) * 4u);

        const auto scale = [maxValue](int v) -> uint8_t {
            if (v < 0) v = 0;
            if (v > maxValue) v = maxValue;
            const float f = static_cast<float>(v) / static_cast<float>(maxValue);
            const int out = static_cast<int>(f * 255.0f + 0.5f);
            return static_cast<uint8_t>(out < 0 ? 0 : (out > 255 ? 255 : out));
        };

        for (uint32_t i = 0; i < img.Width * img.Height; ++i)
        {
            std::string rTok, gTok, bTok;
            if (!reader.ReadToken(rTok) || !reader.ReadToken(gTok) || !reader.ReadToken(bTok))
            {
                return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (not enough pixel data): " + path);
            }

            const int r = std::stoi(rTok);
            const int g = std::stoi(gTok);
            const int b = std::stoi(bTok);

            const size_t base = static_cast<size_t>(i) * 4u;
            img.Pixels[base + 0] = scale(r);
            img.Pixels[base + 1] = scale(g);
            img.Pixels[base + 2] = scale(b);
            img.Pixels[base + 3] = 255;
        }

        return img;
    }

    static Result<DecodedImageRGBA8> TryDecodePpmP3ToRGBA8FromMemory(const uint8_t* bytes, size_t byteCount, const std::string& debugName)
    {
        if (!bytes || byteCount == 0)
        {
            return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "PPM parse failed (empty input): " + debugName);
        }

        std::string text(reinterpret_cast<const char*>(bytes), byteCount);
        std::istringstream in(text);

        PpmTokenReader reader(in);

        std::string token;
        if (!reader.ReadToken(token))
        {
            return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (empty stream): " + debugName);
        }
        if (token != "P3")
        {
            return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "PPM is not ASCII P3: " + debugName);
        }

        if (!reader.ReadToken(token)) return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (missing width): " + debugName);
        const int width = std::stoi(token);
        if (!reader.ReadToken(token)) return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (missing height): " + debugName);
        const int height = std::stoi(token);
        if (!reader.ReadToken(token)) return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (missing max value): " + debugName);
        const int maxValue = std::stoi(token);

        if (width <= 0 || height <= 0) return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "PPM invalid dimensions: " + debugName);
        if (maxValue <= 0 || maxValue > 255) return Result<DecodedImageRGBA8>(ErrorCode::InvalidArgument, "PPM maxValue must be 1..255: " + debugName);

        DecodedImageRGBA8 img;
        img.Width = static_cast<uint32_t>(width);
        img.Height = static_cast<uint32_t>(height);
        img.Pixels.resize(static_cast<size_t>(img.Width) * static_cast<size_t>(img.Height) * 4u);

        const auto scale = [maxValue](int v) -> uint8_t {
            if (v < 0) v = 0;
            if (v > maxValue) v = maxValue;
            const float f = static_cast<float>(v) / static_cast<float>(maxValue);
            const int out = static_cast<int>(f * 255.0f + 0.5f);
            return static_cast<uint8_t>(out < 0 ? 0 : (out > 255 ? 255 : out));
        };

        for (uint32_t i = 0; i < img.Width * img.Height; ++i)
        {
            std::string rTok, gTok, bTok;
            if (!reader.ReadToken(rTok) || !reader.ReadToken(gTok) || !reader.ReadToken(bTok))
            {
                return Result<DecodedImageRGBA8>(ErrorCode::FileAccessDenied, "PPM parse failed (not enough pixel data): " + debugName);
            }

            const int r = std::stoi(rTok);
            const int g = std::stoi(gTok);
            const int b = std::stoi(bTok);

            const size_t base = static_cast<size_t>(i) * 4u;
            img.Pixels[base + 0] = scale(r);
            img.Pixels[base + 1] = scale(g);
            img.Pixels[base + 2] = scale(b);
            img.Pixels[base + 3] = 255;
        }

        return img;
    }

    Async::Task<TextureAsset::Ptr> TextureAsset::LoadAsync(const std::string& assetPath, const TextureSpecification& specification)
    {
        return LoadAsync(assetPath, specification, AssetLoadCoordinator::GetGeneration());
    }

    Async::Task<TextureAsset::Ptr> TextureAsset::LoadAsync(const std::string& assetPath, const TextureSpecification& specification, uint64_t generation)
    {
        // Two-stage async:
        // - CPU stage (AsyncIO): resolve path + ensure meta GUID + decode to RGBA8
        // - GPU stage (render thread): create Texture2D + fulfill promise
        std::promise<Ptr> promise;
        std::shared_future<Ptr> shared = promise.get_future().share();

        Async::GetAsyncIO().RunAsync([assetPath, specification, generation, promise = std::move(promise)]() mutable -> void {
            try
            {
                if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
                {
                    promise.set_value(nullptr);
                    return;
                }

                // Bundle-first loading: allows shipped builds without `Assets/` source files.
                bool fromBundle = false;
                std::vector<uint8_t> bundleBytes;
                std::string guid;
                std::string resolvedPath;
                std::string debugName = assetPath;

                auto& bundle = AssetBundle::GetInstance();
                if (bundle.IsEnabled() && bundle.IsLoaded())
                {
                    const auto entry = bundle.FindEntryByKey(assetPath);
                    if (entry.has_value())
                    {
                        const auto bytesResult = bundle.ReadAllBytesByKey(assetPath);
                        if (bytesResult.IsSuccess())
                        {
                            fromBundle = true;
                            bundleBytes = bytesResult.GetValue();
                            guid = entry->Guid;
                        }
                    }
                }

                if (!fromBundle)
                {
                    const auto resolvedPathResult = ResolveAssetKeyToPath(assetPath);
                    if (resolvedPathResult.IsFailure())
                    {
                        LT_CORE_ERROR("TextureAsset::LoadAsync: failed to resolve key '{}': {}",
                                      assetPath, resolvedPathResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    resolvedPath = resolvedPathResult.GetValue().string();
                    debugName = resolvedPath;

                    // Unity-style: `.meta` lives next to the real asset on disk.
                    auto guidResult = LoadOrCreateGuid(resolvedPath);
                    if (guidResult.IsFailure())
                    {
                        LT_CORE_ERROR("TextureAsset::LoadAsync: failed GUID/meta for '{}': {}",
                                      resolvedPath, guidResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    guid = guidResult.GetValue();
                }

                // CPU decode on this AsyncIO worker thread.
                auto decodedResult = fromBundle
                    ? DecodeToRGBA8FromMemory(bundleBytes.data(), bundleBytes.size(), debugName, specification.FlipVerticallyOnLoad)
                    : DecodeToRGBA8(resolvedPath, specification.FlipVerticallyOnLoad);
                if (decodedResult.IsFailure())
                {
                    // stb_image can't decode ASCII PPM (P3) reliably across builds.
                    // For our dev/test checkerboard, fall back to a tiny P3 parser.
                    auto ppmFallback = fromBundle
                        ? TryDecodePpmP3ToRGBA8FromMemory(bundleBytes.data(), bundleBytes.size(), debugName)
                        : TryDecodePpmP3ToRGBA8(resolvedPath);
                    if (ppmFallback.IsSuccess())
                    {
                        if (specification.FlipVerticallyOnLoad)
                        {
                            auto img = ppmFallback.GetValue();
                            FlipVerticalRGBA8(img);
                            ppmFallback = img;
                        }
                        decodedResult = ppmFallback;
                    }
                }
                if (decodedResult.IsFailure())
                {
                    LT_CORE_ERROR("TextureAsset::LoadAsync: decode failed for '{}': {}",
                                  debugName, decodedResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                DecodedImageRGBA8 decoded = decodedResult.GetValue();

                // GPU stage: create texture + asset on render thread without blocking this AsyncIO worker.
                auto& renderer = Renderer::GetInstance();
                if (!renderer.IsRenderThreadEnabled())
                {
                    LT_CORE_ERROR("TextureAsset::LoadAsync: render thread must be enabled for GPU texture creation");
                    promise.set_value(nullptr);
                    return;
                }

                struct SharedState
                {
                    std::promise<Ptr> promise;
                    std::string key;
                    std::string guid;
                    TextureSpecification spec;
                    DecodedImageRGBA8 decoded;
                    uint64_t generation = 0;
                };

                auto state = std::make_shared<SharedState>();
                state->promise = std::move(promise);
                state->key = assetPath;
                state->guid = guid;
                state->spec = specification;
                state->decoded = std::move(decoded);
                state->generation = generation;

                class Command final : public RenderResourceCommandQueue::Command
                {
                public:
                    explicit Command(std::shared_ptr<SharedState> s)
                        : m_State(std::move(s))
                    {
                    }

                    void Execute(GraphicsContext*) override
                    {
                        try
                        {
                            if (!AssetLoadCoordinator::IsGenerationCurrent(m_State->generation))
                            {
                                m_State->promise.set_value(nullptr);
                                return;
                            }

                            // NOTE: Texture2D factory will run inline when called from the render thread.
                            auto texture = Texture2D::CreateFromRGBA8(
                                m_State->decoded.Width,
                                m_State->decoded.Height,
                                m_State->decoded.Pixels.data(),
                                m_State->spec);

                            if (!texture)
                            {
                                m_State->promise.set_value(nullptr);
                                return;
                            }

                            // Ensure cache/dedup through AssetManager.
                            auto asset = AssetManager::GetOrLoad<TextureAsset>(m_State->key, [&]() -> Ptr {
                                return Ptr(new TextureAsset(m_State->key, m_State->guid, std::move(texture), m_State->spec));
                            });

                            m_State->promise.set_value(std::move(asset));
                        }
                        catch (...)
                        {
                            try { m_State->promise.set_value(nullptr); } catch (...) {}
                        }
                    }

                private:
                    std::shared_ptr<SharedState> m_State;
                };

                if (!renderer.SubmitResource(std::make_unique<Command>(state)))
                {
                    LT_CORE_ERROR("TextureAsset::LoadAsync: RenderResourceCommandQueue full while creating '{}'", debugName);
                    state->promise.set_value(nullptr);
                    return;
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("TextureAsset::LoadAsync: exception while loading '{}': {}", assetPath, e.what());
                try { promise.set_value(nullptr); } catch (...) {}
            }
            catch (...)
            {
                LT_CORE_ERROR("TextureAsset::LoadAsync: unknown exception while loading '{}'", assetPath);
                try { promise.set_value(nullptr); } catch (...) {}
            }
        });

        return Async::Task<Ptr>(std::move(shared));
    }

    TextureAsset::Ptr TextureAsset::LoadBlocking(const std::string& assetPath, const TextureSpecification& specification)
    {
        auto task = LoadAsync(assetPath, specification);
        task.Wait();
        return task.Get();
    }

    bool TextureAsset::Reload()
    {
        // IMPORTANT:
        // Reload must rebuild in-place. Calling LoadBlocking() would go through the cache and
        // typically return this same instance without rebuilding GPU resources.
        const std::string key = GetKey();

        bool fromBundle = false;
        std::vector<uint8_t> bundleBytes;
        std::string resolvedPath;
        std::string debugName = key;

        auto& bundle = AssetBundle::GetInstance();
        if (bundle.IsEnabled() && bundle.IsLoaded())
        {
            const auto entry = bundle.FindEntryByKey(key);
            if (entry.has_value())
            {
                const auto bytesResult = bundle.ReadAllBytesByKey(key);
                if (bytesResult.IsSuccess())
                {
                    fromBundle = true;
                    bundleBytes = bytesResult.GetValue();
                }
            }
        }

        if (!fromBundle)
        {
            const auto resolvedPathResult = ResolveAssetKeyToPath(key);
            if (resolvedPathResult.IsFailure())
            {
                LT_CORE_ERROR("TextureAsset::Reload: failed to resolve key '{}': {}", key, resolvedPathResult.GetError().GetErrorMessage());
                return false;
            }
            resolvedPath = resolvedPathResult.GetValue().string();
            debugName = resolvedPath;
        }

        auto decodedResult = fromBundle
            ? DecodeToRGBA8FromMemory(bundleBytes.data(), bundleBytes.size(), debugName, m_Specification.FlipVerticallyOnLoad)
            : DecodeToRGBA8(resolvedPath, m_Specification.FlipVerticallyOnLoad);
        if (decodedResult.IsFailure())
        {
            auto ppmFallback = fromBundle
                ? TryDecodePpmP3ToRGBA8FromMemory(bundleBytes.data(), bundleBytes.size(), debugName)
                : TryDecodePpmP3ToRGBA8(resolvedPath);
            if (ppmFallback.IsSuccess())
            {
                if (m_Specification.FlipVerticallyOnLoad)
                {
                    auto img = ppmFallback.GetValue();
                    FlipVerticalRGBA8(img);
                    ppmFallback = img;
                }
                decodedResult = ppmFallback;
            }
        }

        if (decodedResult.IsFailure())
        {
            LT_CORE_ERROR("TextureAsset::Reload: decode failed for '{}': {}", debugName, decodedResult.GetError().GetErrorMessage());
            return false;
        }

        const DecodedImageRGBA8 decoded = decodedResult.GetValue();

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsRenderThreadEnabled())
        {
            LT_CORE_ERROR("TextureAsset::Reload: render thread must be enabled for GPU texture creation");
            return false;
        }

        std::shared_ptr<Texture2D> created;
        try
        {
            created = renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<Texture2D> {
                return Texture2D::CreateFromRGBA8(decoded.Width, decoded.Height, decoded.Pixels.data(), m_Specification);
            });
        }
        catch (const std::exception& e)
        {
            LT_CORE_ERROR("TextureAsset::Reload: GPU upload threw for '{}': {}", debugName, e.what());
            return false;
        }
        catch (...)
        {
            LT_CORE_ERROR("TextureAsset::Reload: GPU upload threw (unknown) for '{}'", debugName);
            return false;
        }

        if (!created)
        {
            LT_CORE_ERROR("TextureAsset::Reload: GPU upload failed for '{}'", debugName);
            return false;
        }

        m_Texture = std::move(created);
        return true;
    }
}

