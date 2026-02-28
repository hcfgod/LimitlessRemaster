#include "Assets/Cooking/CookedShaderStagesFormat.h"

#include <cstring>
#include <vector>

namespace Limitless::Assets::Cooking
{
    namespace
    {
        static constexpr uint32_t kMagic = 0x3253484Cu; // 'LSH2'
        static constexpr uint16_t kVersion = 1;

        struct Header
        {
            uint32_t Magic = kMagic;
            uint16_t Version = kVersion;
            uint16_t Reserved0 = 0;

            uint32_t NameSize = 0;
            uint32_t VertexSize = 0;
            uint32_t FragmentSize = 0;
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
                return Result<void>(ErrorCode::FileCorrupted, "CookedShaderStages: truncated blob");
            }
            std::memcpy(out, bytes + cursor, outSize);
            cursor += outSize;
            return Result<void>();
        }
    }

    Result<std::vector<uint8_t>> CookShaderStagesToBytes(const CookedShaderStages& stages)
    {
        if (stages.Vertex.empty() || stages.Fragment.empty())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidArgument, "CookedShaderStages: vertex/fragment cannot be empty");
        }

        Header header;
        header.NameSize = static_cast<uint32_t>(stages.Name.size());
        header.VertexSize = static_cast<uint32_t>(stages.Vertex.size());
        header.FragmentSize = static_cast<uint32_t>(stages.Fragment.size());

        std::vector<uint8_t> out;
        out.reserve(sizeof(Header) + header.NameSize + header.VertexSize + header.FragmentSize);

        WriteBytes(out, &header, sizeof(Header));
        WriteBytes(out, stages.Name.data(), stages.Name.size());
        WriteBytes(out, stages.Vertex.data(), stages.Vertex.size());
        WriteBytes(out, stages.Fragment.data(), stages.Fragment.size());

        return out;
    }

    Result<CookedShaderStages> ParseCookedShaderStages(const uint8_t* bytes, const size_t byteCount)
    {
        if (bytes == nullptr || byteCount < sizeof(Header))
        {
            return Result<CookedShaderStages>(ErrorCode::FileCorrupted, "CookedShaderStages: blob too small");
        }

        size_t cursor = 0;
        Header header{};
        {
            const auto r = ReadBytes(bytes, byteCount, cursor, &header, sizeof(Header));
            if (r.IsFailure())
            {
                return Result<CookedShaderStages>(r.GetError());
            }
        }

        if (header.Magic != kMagic || header.Version != kVersion)
        {
            return Result<CookedShaderStages>(ErrorCode::FileCorrupted, "CookedShaderStages: invalid header");
        }

        const size_t totalStrings =
            static_cast<size_t>(header.NameSize) +
            static_cast<size_t>(header.VertexSize) +
            static_cast<size_t>(header.FragmentSize);

        if (cursor + totalStrings > byteCount)
        {
            return Result<CookedShaderStages>(ErrorCode::FileCorrupted, "CookedShaderStages: truncated payload");
        }

        CookedShaderStages out;
        out.Name.assign(reinterpret_cast<const char*>(bytes + cursor), header.NameSize);
        cursor += header.NameSize;
        out.Vertex.assign(reinterpret_cast<const char*>(bytes + cursor), header.VertexSize);
        cursor += header.VertexSize;
        out.Fragment.assign(reinterpret_cast<const char*>(bytes + cursor), header.FragmentSize);

        if (out.Vertex.empty() || out.Fragment.empty())
        {
            return Result<CookedShaderStages>(ErrorCode::FileCorrupted, "CookedShaderStages: empty stage source");
        }

        return out;
    }
}

