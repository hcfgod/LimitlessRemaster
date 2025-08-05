#include "RenderCommand.h"
#include "GraphicsContext.h"
#include "Graphics/GraphicsEnums.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"

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

    // SetViewportCommand implementation
    SetViewportCommand::SetViewportCommand(int x, int y, int width, int height)
        : m_X(x), m_Y(y), m_Width(width), m_Height(height)
    {
    }

    // SetScissorCommand implementation
    SetScissorCommand::SetScissorCommand(int x, int y, int width, int height, bool enable)
        : m_X(x), m_Y(y), m_Width(width), m_Height(height), m_Enable(enable)
    {
    }

    // BindShaderCommand implementation
    BindShaderCommand::BindShaderCommand(std::shared_ptr<Shader> shader)
        : m_Shader(std::move(shader))
    {
    }

    // BindVertexArrayCommand implementation
    BindVertexArrayCommand::BindVertexArrayCommand(std::shared_ptr<VertexArray> vertexArray)
        : m_VertexArray(std::move(vertexArray))
    {
    }

    // BindIndexBufferCommand implementation
    BindIndexBufferCommand::BindIndexBufferCommand(std::shared_ptr<IndexBuffer> indexBuffer)
        : m_IndexBuffer(std::move(indexBuffer))
    {
    }

    // BindVertexBufferCommand implementation
    BindVertexBufferCommand::BindVertexBufferCommand(std::shared_ptr<VertexBuffer> vertexBuffer)
        : m_VertexBuffer(std::move(vertexBuffer))
    {
    }

    // BindTextureCommand implementation
    BindTextureCommand::BindTextureCommand(std::shared_ptr<Texture> texture, uint32_t slot)
        : m_Texture(std::move(texture)), m_Slot(slot)
    {
    }

    // BindFramebufferCommand implementation
    BindFramebufferCommand::BindFramebufferCommand(std::shared_ptr<Framebuffer> framebuffer)
        : m_Framebuffer(std::move(framebuffer))
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