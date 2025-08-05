#pragma once

namespace Limitless
{
    // Blend mode factors
    enum class BlendFactor
    {
        Zero = 0,
        One = 1,
        SrcColor = 0x0300,
        OneMinusSrcColor = 0x0301,
        SrcAlpha = 0x0302,
        OneMinusSrcAlpha = 0x0303,
        DstAlpha = 0x0304,
        OneMinusDstAlpha = 0x0305,
        DstColor = 0x0306,
        OneMinusDstColor = 0x0307,
        SrcAlphaSaturate = 0x0308
    };

    // Depth test functions
    enum class DepthTestFunc
    {
        Never = 0x0200,
        Less = 0x0201,
        Equal = 0x0202,
        LessEqual = 0x0203,
        Greater = 0x0204,
        NotEqual = 0x0205,
        GreaterEqual = 0x0206,
        Always = 0x0207
    };

    // Cull face modes
    enum class CullFace
    {
        Front = 0x0404,
        Back = 0x0405,
        FrontAndBack = 0x0408
    };

    // Polygon modes
    enum class PolygonMode
    {
        Point = 0x1B00,
        Line = 0x1B01,
        Fill = 0x1B02
    };

    // Draw modes
    enum class DrawMode
    {
        Points = 0x0000,
        Lines = 0x0001,
        LineLoop = 0x0002,
        LineStrip = 0x0003,
        Triangles = 0x0004,
        TriangleStrip = 0x0005,
        TriangleFan = 0x0006
    };

    // Index types
    enum class IndexType
    {
        UnsignedByte = 0x1401,
        UnsignedShort = 0x1403,
        UnsignedInt = 0x1405
    };

    // Polygon faces
    enum class PolygonFace
    {
        Front = 0x0404,
        Back = 0x0405,
        FrontAndBack = 0x0408
    };

} // namespace Limitless 