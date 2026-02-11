#include "Core/Hash/XxHash64.h"

#include <algorithm>
#include <cstring>

namespace Limitless::Hash
{
    namespace
    {
        static constexpr uint64_t PRIME64_1 = 11400714785074694791ULL;
        static constexpr uint64_t PRIME64_2 = 14029467366897019727ULL;
        static constexpr uint64_t PRIME64_3 = 1609587929392839161ULL;
        static constexpr uint64_t PRIME64_4 = 9650029242287828579ULL;
        static constexpr uint64_t PRIME64_5 = 2870177450012600261ULL;

        static inline uint64_t Rotl64(const uint64_t x, const int r)
        {
            return (x << r) | (x >> (64 - r));
        }

        static inline uint64_t ReadU64(const void* ptr)
        {
            uint64_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return v;
        }

        static inline uint32_t ReadU32(const void* ptr)
        {
            uint32_t v;
            std::memcpy(&v, ptr, sizeof(v));
            return v;
        }

        static inline uint64_t Round(uint64_t acc, const uint64_t input)
        {
            acc += input * PRIME64_2;
            acc = Rotl64(acc, 31);
            acc *= PRIME64_1;
            return acc;
        }

        static inline uint64_t MergeRound(uint64_t acc, const uint64_t val)
        {
            acc ^= Round(0, val);
            acc *= PRIME64_1;
            acc += PRIME64_4;
            return acc;
        }

        static inline uint64_t Avalanche(uint64_t h)
        {
            h ^= h >> 33;
            h *= PRIME64_2;
            h ^= h >> 29;
            h *= PRIME64_3;
            h ^= h >> 32;
            return h;
        }
    }

    XxHash64::State::State(const uint64_t seed)
    {
        Reset(seed);
    }

    void XxHash64::State::Reset(const uint64_t seed)
    {
        m_Seed = seed;
        m_TotalLength = 0;
        m_BufferSize = 0;

        m_V1 = seed + PRIME64_1 + PRIME64_2;
        m_V2 = seed + PRIME64_2;
        m_V3 = seed + 0;
        m_V4 = seed - PRIME64_1;
    }

    void XxHash64::State::Update(const void* data, const size_t size)
    {
        if (data == nullptr || size == 0)
        {
            return;
        }
        UpdateImpl(reinterpret_cast<const uint8_t*>(data), size);
    }

    void XxHash64::State::UpdateImpl(const uint8_t* input, size_t length)
    {
        m_TotalLength += length;

        // Fill buffer if we have leftovers.
        if (m_BufferSize > 0)
        {
            const uint32_t toCopy = static_cast<uint32_t>((std::min)(static_cast<size_t>(32 - m_BufferSize), length));
            std::memcpy(m_Buffer + m_BufferSize, input, toCopy);
            m_BufferSize += toCopy;
            input += toCopy;
            length -= toCopy;

            if (m_BufferSize < 32)
            {
                return;
            }

            const uint8_t* p = m_Buffer;
            m_V1 = Round(m_V1, ReadU64(p + 0));
            m_V2 = Round(m_V2, ReadU64(p + 8));
            m_V3 = Round(m_V3, ReadU64(p + 16));
            m_V4 = Round(m_V4, ReadU64(p + 24));
            m_BufferSize = 0;
        }

        // Bulk process 32-byte stripes.
        const uint8_t* const limit = input + (length & ~static_cast<size_t>(31));
        while (input < limit)
        {
            m_V1 = Round(m_V1, ReadU64(input + 0));
            m_V2 = Round(m_V2, ReadU64(input + 8));
            m_V3 = Round(m_V3, ReadU64(input + 16));
            m_V4 = Round(m_V4, ReadU64(input + 24));
            input += 32;
        }

        // Buffer remaining bytes.
        const size_t remaining = length & static_cast<size_t>(31);
        if (remaining > 0)
        {
            std::memcpy(m_Buffer, input, remaining);
            m_BufferSize = static_cast<uint32_t>(remaining);
        }
    }

    uint64_t XxHash64::State::Digest() const
    {
        uint64_t h64;
        if (m_TotalLength >= 32)
        {
            h64 = Rotl64(m_V1, 1) + Rotl64(m_V2, 7) + Rotl64(m_V3, 12) + Rotl64(m_V4, 18);
            h64 = MergeRound(h64, m_V1);
            h64 = MergeRound(h64, m_V2);
            h64 = MergeRound(h64, m_V3);
            h64 = MergeRound(h64, m_V4);
        }
        else
        {
            h64 = m_Seed + PRIME64_5;
        }

        h64 += m_TotalLength;

        const uint8_t* p = m_Buffer;
        const uint8_t* const end = p + m_BufferSize;

        while (p + 8 <= end)
        {
            const uint64_t k1 = Round(0, ReadU64(p));
            h64 ^= k1;
            h64 = Rotl64(h64, 27) * PRIME64_1 + PRIME64_4;
            p += 8;
        }

        if (p + 4 <= end)
        {
            h64 ^= static_cast<uint64_t>(ReadU32(p)) * PRIME64_1;
            h64 = Rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
            p += 4;
        }

        while (p < end)
        {
            h64 ^= (*p) * PRIME64_5;
            h64 = Rotl64(h64, 11) * PRIME64_1;
            ++p;
        }

        return Avalanche(h64);
    }

    uint64_t XxHash64::Compute(const void* data, const size_t size, const uint64_t seed)
    {
        State state(seed);
        state.Update(data, size);
        return state.Digest();
    }
}

