#pragma once

#include "Core/Error.h"

#include <cstdint>
#include <vector>

namespace Limitless::Compression
{
    class ZstdCompression final
    {
    public:
        // Compresses `srcSize` bytes into a Zstd frame.
        // Returns compressed bytes on success.
        static Result<std::vector<uint8_t>> Compress(const void* srcData, size_t srcSize, int compressionLevel = 3);

        // Decompresses a Zstd frame into exactly `uncompressedSize` bytes.
        // Returns uncompressed bytes on success.
        static Result<std::vector<uint8_t>> Decompress(const void* compressedData, size_t compressedSize, size_t uncompressedSize);
    };
}

