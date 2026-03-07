#pragma once

#include "Graphics/Buffer.h"
#include "Graphics/GraphicsEnums.h"
#include "Graphics/RenderTypes.h"
#include "Graphics/Shader.h"

#include <memory>
#include <string>

namespace Limitless
{
    struct RenderBlendAttachmentState
    {
        bool Enabled = true;
        BlendFactor SourceColorFactor = BlendFactor::SrcAlpha;
        BlendFactor DestinationColorFactor = BlendFactor::OneMinusSrcAlpha;
    };

    struct RenderDepthStencilState
    {
        bool DepthTestEnabled = true;
        bool DepthWriteEnabled = true;
        DepthTestFunc DepthCompare = DepthTestFunc::LessEqual;
    };

    struct RenderRasterState
    {
        bool CullEnabled = true;
        CullFace CullMode = CullFace::Back;
        PolygonMode FillMode = PolygonMode::Fill;
    };

    struct RenderPipelineDescriptor
    {
        std::string DebugName;
        std::shared_ptr<Shader> ShaderProgram;
        BufferLayout VertexLayout{};
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
        RenderBlendAttachmentState BlendState{};
        RenderDepthStencilState DepthStencilState{};
        RenderRasterState RasterState{};
    };

    class RenderPipeline
    {
    public:
        virtual ~RenderPipeline() = default;

        virtual const RenderPipelineDescriptor& GetDescriptor() const = 0;

        static std::shared_ptr<RenderPipeline> Create(const RenderPipelineDescriptor& descriptor);
    };
}
