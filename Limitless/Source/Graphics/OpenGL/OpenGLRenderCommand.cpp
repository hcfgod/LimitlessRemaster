#include "Graphics/RenderCommand.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/Shader.h"
#include "Graphics/VertexArray.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"

#define LT_USE_GLAD

// OpenGL includes
#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

namespace {
    // Check for OpenGL errors
    void CheckOpenGLError(const char* operation) {
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            LT_CORE_ERROR("OpenGL error in {}: 0x{:x}", operation, error);
        }
    }
}

namespace Limitless
{
    // ClearCommand Execute implementation
    void ClearCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        GLbitfield clearMask = 0;
        
        if (m_Flags.color)
        {
            glClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
            clearMask |= GL_COLOR_BUFFER_BIT;
            CheckOpenGLError("glClearColor");
        }
        
        if (m_Flags.depth)
        {
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }
        
        if (m_Flags.stencil)
        {
            clearMask |= GL_STENCIL_BUFFER_BIT;
        }
        
        if (clearMask != 0)
        {
            glClear(clearMask);
            CheckOpenGLError("glClear");
        }
    }

    // SetViewportCommand Execute implementation
    void SetViewportCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glViewport(m_X, m_Y, m_Width, m_Height);
        CheckOpenGLError("glViewport");
    }

    // SetScissorCommand Execute implementation
    void SetScissorCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Enable)
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(m_X, m_Y, m_Width, m_Height);
            CheckOpenGLError("glScissor");
        }
        else
        {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    // BindShaderCommand Execute implementation
    void BindShaderCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Shader)
        {
            m_Shader->Bind();
        }
        else
        {
            LT_CORE_WARN("BindShaderCommand: shader was null (no-op)");
        }
    }

    // BindVertexArrayCommand Execute implementation
    void BindVertexArrayCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_VertexArray)
        {
            m_VertexArray->Bind();
        }
        else
        {
            LT_CORE_WARN("BindVertexArrayCommand: vertex array was null (no-op)");
        }
    }

    // BindIndexBufferCommand Execute implementation
    void BindIndexBufferCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_IndexBuffer)
        {
            m_IndexBuffer->Bind();
        }
        else
        {
            LT_CORE_WARN("BindIndexBufferCommand: index buffer was null (no-op)");
        }
    }

    // BindVertexBufferCommand Execute implementation
    void BindVertexBufferCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_VertexBuffer)
        {
            m_VertexBuffer->Bind();
        }
        else
        {
            LT_CORE_WARN("BindVertexBufferCommand: vertex buffer was null (no-op)");
        }
    }

    // BindTextureCommand Execute implementation
    void BindTextureCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Texture)
        {
            m_Texture->Bind(m_Slot);
        }
        else
        {
            glActiveTexture(GL_TEXTURE0 + m_Slot);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // BindFramebufferCommand Execute implementation
    void BindFramebufferCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Framebuffer)
        {
            // This should be implemented by the specific render API implementation
            LT_CORE_DEBUG("Binding framebuffer: {}", m_Framebuffer ? "valid" : "null");
        }
        else
        {
            // Bind default framebuffer
            LT_CORE_DEBUG("Binding default framebuffer");
        }
    }

    // DrawArraysCommand Execute implementation
    void DrawArraysCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glDrawArrays(static_cast<GLenum>(m_Mode), m_First, static_cast<GLsizei>(m_Count));
        CheckOpenGLError("glDrawArrays");
    }

    // DrawIndexedCommand Execute implementation
    void DrawIndexedCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_BaseVertex != 0)
        {
            glDrawElementsBaseVertex(static_cast<GLenum>(m_Mode), static_cast<GLsizei>(m_Count),
                                     static_cast<GLenum>(m_IndexType), m_Indices, m_BaseVertex);
            CheckOpenGLError("glDrawElementsBaseVertex");
        }
        else
        {
            glDrawElements(static_cast<GLenum>(m_Mode), static_cast<GLsizei>(m_Count),
                           static_cast<GLenum>(m_IndexType), m_Indices);
            CheckOpenGLError("glDrawElements");
        }
    }

    // DrawInstancedCommand Execute implementation
    void DrawInstancedCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        // TODO: Implement actual OpenGL draw instanced command
    }

    // DrawIndexedInstancedCommand Execute implementation
    void DrawIndexedInstancedCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        // TODO: Implement actual OpenGL draw indexed instanced command
    }

    // SetBlendModeCommand Execute implementation
    void SetBlendModeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("SetBlendMode: enable={}, src={}, dst={}", m_Enable ? "true" : "false", static_cast<uint32_t>(m_SrcFactor), static_cast<uint32_t>(m_DstFactor));
    }

    // SetDepthTestCommand Execute implementation
    void SetDepthTestCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("SetDepthTest: enable={}, func={}", m_Enable ? "true" : "false", static_cast<uint32_t>(m_Func));
    }

    // SetCullFaceCommand Execute implementation
    void SetCullFaceCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("SetCullFace: enable={}, face={}", m_Enable ? "true" : "false", static_cast<uint32_t>(m_Face));
    }

    // SetPolygonModeCommand Execute implementation
    void SetPolygonModeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("SetPolygonMode: face={}, mode={}", static_cast<uint32_t>(m_Face), static_cast<uint32_t>(m_Mode));
    }

    // SetLineWidthCommand Execute implementation
    void SetLineWidthCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("SetLineWidth: {}", m_Width);
    }

    // SetPointSizeCommand Execute implementation
    void SetPointSizeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("SetPointSize: {}", m_Size);
    }

    // PushDebugGroupCommand Execute implementation
    void PushDebugGroupCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("PushDebugGroup: {}", m_GroupName);
    }

    // PopDebugGroupCommand Execute implementation
    void PopDebugGroupCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("PopDebugGroup");
    }

    // InsertDebugMarkerCommand Execute implementation
    void InsertDebugMarkerCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // This should be implemented by the specific render API implementation
        LT_CORE_DEBUG("InsertDebugMarker: {}", m_MarkerName);
    }

    // CustomCommand Execute implementation
    void CustomCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Function)
        {
            m_Function(context);
        }
        
        LT_CORE_DEBUG("CustomCommand: {}", m_Name);
    }

} // namespace Limitless 