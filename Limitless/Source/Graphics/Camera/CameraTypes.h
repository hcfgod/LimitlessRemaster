#pragma once

#include <cstdint>
#include <string>

namespace Limitless
{
    enum class CameraType : uint8_t
    {
        Orthographic2D = 0,
        Perspective3D  = 1
    };

    enum class CameraUsage : uint8_t
    {
        Gameplay = 0,
        Editor   = 1
    };

    struct CameraId final
    {
        uint64_t Value = 0;

        constexpr bool IsValid() const { return Value != 0; }
        constexpr explicit operator bool() const { return IsValid(); }

        friend constexpr bool operator==(CameraId a, CameraId b) { return a.Value == b.Value; }
        friend constexpr bool operator!=(CameraId a, CameraId b) { return a.Value != b.Value; }
    };
}

