#pragma once

#include "Graphics/RenderPipeline.h"

namespace Limitless
{
    class OpenGLRenderPipeline final : public RenderPipeline
    {
    public:
        explicit OpenGLRenderPipeline(const RenderPipelineDescriptor& descriptor)
            : m_Descriptor(descriptor)
        {
        }

        const RenderPipelineDescriptor& GetDescriptor() const override { return m_Descriptor; }

    private:
        RenderPipelineDescriptor m_Descriptor;
    };
}
