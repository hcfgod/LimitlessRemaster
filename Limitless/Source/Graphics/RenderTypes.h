#pragma once

#include <cstdint>

namespace Limitless
{
    enum class ResourceUsage
    {
        Immutable = 0,
        Default,
        Dynamic,
        Streaming,
        Staging,
        Transient
    };

    enum class MemoryUsage
    {
        Auto = 0,
        GpuOnly,
        CpuToGpu,
        GpuToCpu
    };

    enum class TextureUsage : uint32_t
    {
        None = 0,
        Sampled = 1u << 0u,
        RenderTarget = 1u << 1u,
        DepthStencil = 1u << 2u,
        Storage = 1u << 3u,
        TransferSource = 1u << 4u,
        TransferDestination = 1u << 5u
    };

    inline constexpr TextureUsage operator|(TextureUsage left, TextureUsage right)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
    }

    inline constexpr TextureUsage operator&(TextureUsage left, TextureUsage right)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(left) & static_cast<uint32_t>(right));
    }

    inline constexpr TextureUsage& operator|=(TextureUsage& left, TextureUsage right)
    {
        left = left | right;
        return left;
    }

    inline constexpr bool HasTextureUsage(TextureUsage value, TextureUsage flag)
    {
        return (static_cast<uint32_t>(value & flag) != 0u);
    }

    enum class TextureFormat
    {
        Unknown = 0,
        R8UNorm,
        RG8UNorm,
        RGB8UNorm,
        RGBA8UNorm,
        BGRA8UNorm,
        RGBA16Float,
        D24UNormS8UInt,
        D32Float
    };

    enum class RenderLoadAction
    {
        DontCare = 0,
        Load,
        Clear
    };

    enum class RenderStoreAction
    {
        DontCare = 0,
        Store
    };

    enum class PrimitiveTopology
    {
        Points = 0,
        Lines,
        LineStrip,
        Triangles,
        TriangleStrip,
        TriangleFan
    };

    struct RenderViewport
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    struct RenderScissorRect
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };
}
