#pragma once

#include <cstdint>

namespace Limitless
{
    class Random final
    {
    public:
        Random() = delete;

        static void SetSeed(uint32_t seed);

        // Unity-style semantics:
        // - Range(int, int): min inclusive, max exclusive.
        // - Range(float, float): min inclusive, max inclusive.
        static int32_t Range(int32_t minInclusive, int32_t maxExclusive);
        static float Range(float minInclusive, float maxInclusive);

        static float Value();
    };
}
