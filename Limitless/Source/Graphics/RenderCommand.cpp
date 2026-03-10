#include "RenderCommand.h"
#include "GraphicsContext.h"
#include "Graphics/GraphicsEnums.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"

#include <cstring>

namespace Limitless
{
    // ClearCommand implementation
    ClearCommand::ClearCommand(ClearFlags flags, float r, float g, float b, float a)
        : m_Flags(flags)
    {
        m_ClearColor[0] = r;
        m_ClearColor[1] = g;
        m_ClearColor[2] = b;
        m_ClearColor[3] = a;
    }

    SetDrawColorAttachmentsCommand::SetDrawColorAttachmentsCommand(std::vector<uint32_t> attachments)
        : m_Attachments(std::move(attachments))
    {
    }

    ClearColorAttachmentCommand::ClearColorAttachmentCommand(uint32_t attachmentIndex, const glm::vec4& clearValue)
        : m_AttachmentIndex(attachmentIndex)
        , m_ClearValue(clearValue)
    {
    }

    // BindRenderPipelineCommand implementation
    BindRenderPipelineCommand::BindRenderPipelineCommand(std::shared_ptr<RenderPipeline> pipeline)
        : m_Pipeline(std::move(pipeline))
    {
    }

    SetVertexBufferDataCommand::SetVertexBufferDataCommand(std::shared_ptr<VertexBuffer> vertexBuffer, const void* data, uint32_t sizeBytes)
        : m_VertexBuffer(std::move(vertexBuffer))
    {
        if (sizeBytes == 0 || data == nullptr)
        {
            return;
        }

        m_OwnedData.resize(sizeBytes);
        std::memcpy(m_OwnedData.data(), data, sizeBytes);
        m_DataPtr = m_OwnedData.data();
        m_SizeBytes = sizeBytes;
    }

    SetVertexBufferDataCommand::SetVertexBufferDataCommand(ExternalDataTag, std::shared_ptr<VertexBuffer> vertexBuffer, const void* data, uint32_t sizeBytes)
        : m_VertexBuffer(std::move(vertexBuffer))
    {
        if (sizeBytes == 0 || data == nullptr)
        {
            return;
        }

        m_DataPtr = static_cast<const uint8_t*>(data);
        m_SizeBytes = sizeBytes;
    }

    Renderer2DFlushCommand::Renderer2DFlushCommand(
        KeepAlive&& keepAlive,
        const void* vertexBytes,
        uint32_t vertexByteCount,
        const glm::mat4& viewProjection,
        uint32_t indexCount,
        IndexType indexType,
        uint32_t textureCount)
        : m_KeepAlive(std::move(keepAlive))
        , m_VertexBytes(static_cast<const uint8_t*>(vertexBytes))
        , m_VertexByteCount(vertexByteCount)
        , m_ViewProjection(viewProjection)
        , m_IndexCount(indexCount)
        , m_IndexType(indexType)
        , m_TextureCount(textureCount)
    {
        // Cache texture renderer IDs for faster binds in the hot execution path.
        for (uint32_t i = 0; i < kMaxTextureSlots; ++i)
        {
            if (m_KeepAlive.TextureHandles[i])
            {
                m_TextureRendererIds[i] = static_cast<uint32_t>(m_KeepAlive.TextureHandles[i]->GetNativeHandle());
            }
            else
            {
                m_TextureRendererIds[i] = 0;
            }
        }
    }

    SetTextureSpecificationCommand::SetTextureSpecificationCommand(std::shared_ptr<Texture> texture, const TextureSpecification& specification)
        : m_Texture(std::move(texture))
        , m_Specification(specification)
    {
    }

    // BindFramebufferCommand implementation
    BindFramebufferCommand::BindFramebufferCommand(std::shared_ptr<Framebuffer> framebuffer)
        : m_Framebuffer(std::move(framebuffer))
    {
    }

    BeginRenderPassCommand::BeginRenderPassCommand(RenderPassDescriptor descriptor)
        : m_Descriptor(std::move(descriptor))
    {
    }

    EndRenderPassCommand::EndRenderPassCommand(RenderPassDescriptor descriptor)
        : m_Descriptor(std::move(descriptor))
    {
    }

    ApplyRenderBindingsCommand::ApplyRenderBindingsCommand(std::shared_ptr<Shader> shader,
                                                           std::shared_ptr<VertexArray> vertexArray,
                                                           RenderBindingSet bindings)
        : m_Shader(std::move(shader))
        , m_VertexArray(std::move(vertexArray))
        , m_Bindings(std::move(bindings))
    {
    }

    // DrawArraysCommand implementation
    DrawArraysCommand::DrawArraysCommand(DrawMode mode, int first, uint32_t count)
        : m_Mode(mode), m_First(first), m_Count(count)
    {
    }

    // DrawIndexedCommand implementation
    DrawIndexedCommand::DrawIndexedCommand(DrawMode mode, uint32_t count, IndexType indexType, void* indices, int baseVertex)
        : m_Mode(mode), m_Count(count), m_IndexType(indexType), m_Indices(indices), m_BaseVertex(baseVertex)
    {
    }

    // DrawInstancedCommand implementation
    DrawInstancedCommand::DrawInstancedCommand(DrawMode mode, int first, uint32_t count, uint32_t instanceCount)
        : m_Mode(mode), m_First(first), m_Count(count), m_InstanceCount(instanceCount)
    {
    }

    // DrawIndexedInstancedCommand implementation
    DrawIndexedInstancedCommand::DrawIndexedInstancedCommand(DrawMode mode, uint32_t count, IndexType indexType, 
                                                             void* indices, uint32_t instanceCount, int baseVertex)
        : m_Mode(mode), m_Count(count), m_IndexType(indexType), m_Indices(indices), 
          m_InstanceCount(instanceCount), m_BaseVertex(baseVertex)
    {
    }

    // SetBlendModeCommand implementation
    SetBlendModeCommand::SetBlendModeCommand(BlendFactor srcFactor, BlendFactor dstFactor, bool enable)
        : m_SrcFactor(srcFactor), m_DstFactor(dstFactor), m_Enable(enable)
    {
    }

    // SetDepthTestCommand implementation
    SetDepthTestCommand::SetDepthTestCommand(bool enable, DepthTestFunc func)
        : m_Enable(enable), m_Func(func)
    {
    }

    // SetCullFaceCommand implementation
    SetCullFaceCommand::SetCullFaceCommand(bool enable, CullFace face)
        : m_Enable(enable), m_Face(face)
    {
    }

    // SetPolygonModeCommand implementation
    SetPolygonModeCommand::SetPolygonModeCommand(PolygonFace face, PolygonMode mode)
        : m_Face(face), m_Mode(mode)
    {
    }

    // SetLineWidthCommand implementation
    SetLineWidthCommand::SetLineWidthCommand(float width)
        : m_Width(width)
    {
    }

    // SetPointSizeCommand implementation
    SetPointSizeCommand::SetPointSizeCommand(float size)
        : m_Size(size)
    {
    }

    // PushDebugGroupCommand implementation
    PushDebugGroupCommand::PushDebugGroupCommand(const std::string& name)
        : m_GroupName(name)
    {
    }

    // InsertDebugMarkerCommand implementation
    InsertDebugMarkerCommand::InsertDebugMarkerCommand(const std::string& name)
        : m_MarkerName(name)
    {
    }

    // CustomCommand implementation
    CustomCommand::CustomCommand(CustomFunction func, const std::string& name)
        : m_Function(std::move(func)), m_Name(name)
    {
        if (!m_Function)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Custom function cannot be null");
        }
    }

} // namespace Limitless 