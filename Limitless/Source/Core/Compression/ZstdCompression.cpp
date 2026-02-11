#include "Core/Compression/ZstdCompression.h"

#include "Core/Debug/Log.h"

#include <cstring>
#include <string>

#if defined(LT_ENABLE_ZSTD)
    #include <zstd.h>
#endif

namespace Limitless::Compression
{
    Result<std::vector<uint8_t>> ZstdCompression::Compress(const void* srcData, const size_t srcSize, const int compressionLevel)
    {
        if (srcData == nullptr || srcSize == 0)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidArgument, "ZstdCompression: invalid input buffer");
        }

#if !defined(LT_ENABLE_ZSTD)
        (void)compressionLevel;
        return Result<std::vector<uint8_t>>(ErrorCode::NotSupported, "ZstdCompression: Zstd is not enabled (missing Vendor/Zstd libs)");
#else
        const size_t bound = ZSTD_compressBound(srcSize);
        std::vector<uint8_t> out;
        out.resize(bound);

        const size_t written = ZSTD_compress(out.data(), out.size(), srcData, srcSize, compressionLevel);
        if (ZSTD_isError(written))
        {
            const char* msg = ZSTD_getErrorName(written);
            return Result<std::vector<uint8_t>>(ErrorCode::ResourceCompressionError, std::string("ZstdCompression: compress failed: ") + (msg ? msg : "Unknown"));
        }

        out.resize(written);
        return out;
#endif
    }

    Result<std::vector<uint8_t>> ZstdCompression::Decompress(const void* compressedData, const size_t compressedSize, const size_t uncompressedSize)
    {
        if (compressedData == nullptr || compressedSize == 0)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidArgument, "ZstdCompression: invalid compressed buffer");
        }
        if (uncompressedSize == 0)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidArgument, "ZstdCompression: uncompressedSize is zero");
        }

#if !defined(LT_ENABLE_ZSTD)
        (void)uncompressedSize;
        return Result<std::vector<uint8_t>>(ErrorCode::NotSupported, "ZstdCompression: Zstd is not enabled (missing Vendor/Zstd libs)");
#else
        std::vector<uint8_t> out;
        out.resize(uncompressedSize);

        const size_t written = ZSTD_decompress(out.data(), out.size(), compressedData, compressedSize);
        if (ZSTD_isError(written))
        {
            const char* msg = ZSTD_getErrorName(written);
            return Result<std::vector<uint8_t>>(ErrorCode::ResourceCompressionError, std::string("ZstdCompression: decompress failed: ") + (msg ? msg : "Unknown"));
        }

        if (written != uncompressedSize)
        {
            // Some pipelines may not store the exact uncompressed size; we treat mismatch as corruption
            // to keep runtime deterministic and avoid passing truncated buffers onward.
            return Result<std::vector<uint8_t>>(ErrorCode::FileCorrupted, "ZstdCompression: decompressed size mismatch");
        }

        return out;
#endif
    }
}

