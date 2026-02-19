#include "Scripting/Random.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <random>

namespace Limitless
{
    namespace
    {
        std::mt19937& GetRandomGenerator()
        {
            static std::mt19937 generator(std::random_device{}());
            return generator;
        }

        std::mutex& GetRandomGeneratorMutex()
        {
            static std::mutex mutex;
            return mutex;
        }
    }

    void Random::SetSeed(uint32_t seed)
    {
        std::lock_guard<std::mutex> lock(GetRandomGeneratorMutex());
        GetRandomGenerator().seed(seed);
    }

    int32_t Random::Range(int32_t minInclusive, int32_t maxExclusive)
    {
        if (minInclusive == maxExclusive)
            return minInclusive;

        if (minInclusive > maxExclusive)
            std::swap(minInclusive, maxExclusive);

        const int64_t maxInclusive = static_cast<int64_t>(maxExclusive) - 1;
        if (maxInclusive < static_cast<int64_t>(minInclusive))
            return minInclusive;

        std::uniform_int_distribution<int32_t> distribution(
            minInclusive,
            static_cast<int32_t>(maxInclusive));

        std::lock_guard<std::mutex> lock(GetRandomGeneratorMutex());
        return distribution(GetRandomGenerator());
    }

    float Random::Range(float minInclusive, float maxInclusive)
    {
        if (!std::isfinite(minInclusive) || !std::isfinite(maxInclusive))
            return 0.0f;

        if (minInclusive > maxInclusive)
            std::swap(minInclusive, maxInclusive);

        float safeUpperBound = std::nextafter(maxInclusive, std::numeric_limits<float>::max());
        if (!std::isfinite(safeUpperBound))
            safeUpperBound = maxInclusive;
        if (safeUpperBound < minInclusive)
            safeUpperBound = minInclusive;
        std::uniform_real_distribution<float> distribution(minInclusive, safeUpperBound);

        std::lock_guard<std::mutex> lock(GetRandomGeneratorMutex());
        return distribution(GetRandomGenerator());
    }

    float Random::Value()
    {
        return Range(0.0f, 1.0f);
    }
}
