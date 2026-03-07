#pragma once

namespace Limitless
{
    // Blend mode factors
    enum class BlendFactor
    {
        Zero = 0,
        One,
        SrcColor,
        OneMinusSrcColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        DstColor,
        OneMinusDstColor,
        SrcAlphaSaturate
    };

    // Depth test functions
    enum class DepthTestFunc
    {
        Never = 0,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    // Cull face modes
    enum class CullFace
    {
        Front = 0,
        Back,
        FrontAndBack
    };

    // Polygon modes
    enum class PolygonMode
    {
        Point = 0,
        Line,
        Fill
    };

    // Draw modes
    enum class DrawMode
    {
        Points = 0,
        Lines,
        LineLoop,
        LineStrip,
        Triangles,
        TriangleStrip,
        TriangleFan
    };

    // Index types
    enum class IndexType
    {
        UnsignedByte = 0,
        UnsignedShort,
        UnsignedInt
    };

    // Polygon faces
    enum class PolygonFace
    {
        Front = 0,
        Back,
        FrontAndBack
    };

} // namespace Limitless