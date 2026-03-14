#pragma once

#include <cmath>
#include <glm/gtc/constants.hpp>

namespace Limitless
{
    /// Returns fallbackValue if value is NaN or Inf.
    inline float SanitizeFinite(float value, float fallbackValue)
    {
        if (!std::isfinite(value))
            return fallbackValue;
        return value;
    }

    /// Returns fallbackValue if value is NaN, Inf, or negative.
    inline float SanitizeFiniteNonNegative(float value, float fallbackValue)
    {
        if (!std::isfinite(value) || value < 0.0f)
            return fallbackValue;
        return value;
    }

    /// Wraps an angle in radians to the range [-pi, pi].
    inline float WrapAngleRadians(float angleRadians)
    {
        while (angleRadians > glm::pi<float>())
            angleRadians -= glm::two_pi<float>();
        while (angleRadians < -glm::pi<float>())
            angleRadians += glm::two_pi<float>();
        return angleRadians;
    }
}
