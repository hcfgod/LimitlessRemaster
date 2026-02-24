#include "Graphics/RenderCommand.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/Shader.h"
#include "Graphics/VertexArray.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/OpenGL/OpenGLBuffer.h"
#include "Graphics/OpenGL/OpenGLShader.h"
#include "Graphics/OpenGL/OpenGLVertexArray.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"

#define LT_USE_GLAD

// OpenGL includes
#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

#include <SDL3/SDL.h>
#include <cstring>

namespace {
    // Check for OpenGL errors
    void CheckOpenGLError(const char* operation) {
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            LT_CORE_ERROR("OpenGL error in {}: 0x{:x}", operation, error);
        }
    }

    // GL_KHR_debug / GL 4.3 debug group functions. Loaded at runtime when available.
    using PFNGLPUSHDEBUGGROUPPROC = void (*)(GLenum source, GLuint id, GLsizei length, const GLchar* message);
    using PFNGLPOPDEBUGGROUPPROC = void (*)();
    using PFNGLINSERTDEBUGMARKERPROC = void (*)(GLenum source, GLuint id, GLsizei length, const GLchar* message);

    struct OpenGLDebugFunctionState
    {
        PFNGLPUSHDEBUGGROUPPROC PushDebugGroup = nullptr;
        PFNGLPOPDEBUGGROUPPROC PopDebugGroup = nullptr;
        PFNGLINSERTDEBUGMARKERPROC InsertDebugMarker = nullptr;
        bool DebugFunctionsLoaded = false;
    };

    OpenGLDebugFunctionState& GetOpenGLDebugFunctionState()
    {
        static OpenGLDebugFunctionState state{};
        return state;
    }

    // GL_KHR_debug constants (may not be in GLAD 3.3 headers)
    static constexpr GLenum kGLDebugSourceApplication = 0x824A;

    static void LoadDebugFunctions()
    {
        auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.DebugFunctionsLoaded)
            return;
        debugState.DebugFunctionsLoaded = true;

        debugState.PushDebugGroup = reinterpret_cast<PFNGLPUSHDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPushDebugGroup"));
        if (!debugState.PushDebugGroup)
            debugState.PushDebugGroup = reinterpret_cast<PFNGLPUSHDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPushDebugGroupKHR"));
        debugState.PopDebugGroup = reinterpret_cast<PFNGLPOPDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPopDebugGroup"));
        if (!debugState.PopDebugGroup)
            debugState.PopDebugGroup = reinterpret_cast<PFNGLPOPDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPopDebugGroupKHR"));
        debugState.InsertDebugMarker = reinterpret_cast<PFNGLINSERTDEBUGMARKERPROC>(SDL_GL_GetProcAddress("glInsertDebugMarker"));
        if (!debugState.InsertDebugMarker)
            debugState.InsertDebugMarker = reinterpret_cast<PFNGLINSERTDEBUGMARKERPROC>(SDL_GL_GetProcAddress("glInsertDebugMarkerKHR"));
    }
}

namespace Limitless
{
    namespace
    {
        // Render-thread OpenGL state tracker.
        // Commands that mutate tracked state MUST update this cache.
        // CustomCommand MUST invalidate it (unknown GL calls).
        struct OpenGLStateCache
        {
            GLuint Program = 0;
            GLuint VertexArray = 0;

            uint32_t ActiveTextureUnit = 0;
            std::array<GLuint, 32> BoundTexture2D{};

            void InvalidateAll()
            {
                Program = 0;
                VertexArray = 0;
                ActiveTextureUnit = 0;
                BoundTexture2D.fill(0);
            }

            /// Invalidates texture binding cache for slots [0, count).
            /// Use before binding when external code (e.g. ImGui) may have changed GL state.
            /// Prevents stale cache from skipping glBindTexture, which causes wrong-texture sampling
            /// (e.g. glyph artifacts when color-only quads should use white texture).
            void InvalidateTextureSlots(uint32_t count)
            {
                constexpr GLuint kInvalidId = static_cast<GLuint>(-1);
                for (uint32_t i = 0; i < count && i < BoundTexture2D.size(); ++i)
                    BoundTexture2D[i] = kInvalidId;
            }

            void UseProgram(GLuint program)
            {
                if (Program != program)
                {
                    glUseProgram(program);
                    Program = program;
                }
            }

            void BindVertexArray(GLuint vao)
            {
                if (VertexArray != vao)
                {
                    glBindVertexArray(vao);
                    VertexArray = vao;
                }
            }

            void SetActiveTextureUnit(uint32_t unit)
            {
                if (ActiveTextureUnit != unit)
                {
                    glActiveTexture(GL_TEXTURE0 + unit);
                    ActiveTextureUnit = unit;
                }
            }

            void BindTexture2D(uint32_t unit, GLuint textureId)
            {
                SetActiveTextureUnit(unit);
                if (unit < BoundTexture2D.size())
                {
                    if (BoundTexture2D[unit] != textureId)
                    {
                        glBindTexture(GL_TEXTURE_2D, textureId);
                        BoundTexture2D[unit] = textureId;
                    }
                    return;
                }

                // Fallback: no caching for high slots.
                glBindTexture(GL_TEXTURE_2D, textureId);
            }
        };

        struct Renderer2DUniformCache
        {
            GLuint Program = 0;
            GLint ViewProjectionLocation = -2; // -2 = unknown, -1 = not found
            GLint ModelLocation = -2;          // -2 = unknown, -1 = not found

            glm::mat4 LastViewProjection{1.0f};
            bool HasViewProjection = false;

            void OnProgramBound(GLuint program)
            {
                if (Program != program)
                {
                    Program = program;
                    ViewProjectionLocation = -2;
                    ModelLocation = -2;
                    HasViewProjection = false;
                }
            }
        };

        struct OpenGLRenderCommandRuntimeState
        {
            OpenGLStateCache GLState;
            Renderer2DUniformCache Renderer2DUniforms;
        };

        OpenGLRenderCommandRuntimeState& GetOpenGLRenderCommandRuntimeState()
        {
            static OpenGLRenderCommandRuntimeState state{};
            return state;
        }
    }

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
            if (auto* glShader = dynamic_cast<OpenGLShader*>(m_Shader.get()))
            {
                GetOpenGLRenderCommandRuntimeState().GLState.UseProgram(glShader->GetRendererID());
            }
            else
            {
                m_Shader->Bind();
                GetOpenGLRenderCommandRuntimeState().GLState.Program = 0;
            }
        }
        else
        {
            LT_CORE_WARN("BindShaderCommand: shader was null (no-op)");
        }
    }

    void SetShaderMat4Command::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (!m_Shader)
        {
            LT_CORE_WARN("SetShaderMat4Command: shader was null (no-op)");
            return;
        }

        if (auto* glShader = dynamic_cast<OpenGLShader*>(m_Shader.get()))
        {
            const GLuint program = glShader->GetRendererID();
            GetOpenGLRenderCommandRuntimeState().GLState.UseProgram(program);

            const GLint loc = glGetUniformLocation(program, m_UniformName.c_str());
            if (loc != -1)
            {
                glUniformMatrix4fv(loc, 1, GL_FALSE, &m_Value[0][0]);
            }
            return;
        }

        m_Shader->SetMat4(m_UniformName, m_Value);
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
            if (auto* glVAO = dynamic_cast<OpenGLVertexArray*>(m_VertexArray.get()))
            {
                GetOpenGLRenderCommandRuntimeState().GLState.BindVertexArray(glVAO->GetRendererID());
            }
            else
            {
                m_VertexArray->Bind();
                GetOpenGLRenderCommandRuntimeState().GLState.VertexArray = 0;
            }

            // Defensive: ensure the VAO's index buffer is bound for indexed draws.
            // In OpenGL core profile the element array buffer is part of VAO state, but making this
            // explicit prevents subtle ordering/state issues and avoids driver crashes if the VAO
            // was created without an element binding.
            const auto& indexBuffer = m_VertexArray->GetIndexBuffer();
            if (indexBuffer)
            {
                indexBuffer->Bind();
            }
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

    void SetVertexBufferDataCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (!m_VertexBuffer)
        {
            LT_CORE_WARN("SetVertexBufferDataCommand: vertex buffer was null (no-op)");
            return;
        }

        if (m_DataPtr == nullptr || m_SizeBytes == 0)
        {
            // Allow no-op uploads (useful for defensive code paths).
            return;
        }

        m_VertexBuffer->SetData(m_DataPtr, m_SizeBytes);
    }

    void Renderer2DFlushCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (!m_KeepAlive.VertexArrayHandle || !m_KeepAlive.VertexBufferHandle || !m_KeepAlive.ShaderProgramHandle)
        {
            LT_CORE_WARN("Renderer2DFlushCommand: missing resources (VAO/VBO/Shader), skipping");
            return;
        }

        if (m_VertexBytes == nullptr || m_VertexByteCount == 0 || m_IndexCount == 0)
        {
            // Allow no-op flushes (defensive).
            return;
        }

        // Upload vertices (streaming).
        m_KeepAlive.VertexBufferHandle->SetData(m_VertexBytes, m_VertexByteCount);

        // Bind shader + uniforms (OpenGL fast path when possible).
        if (auto* glShader = dynamic_cast<OpenGLShader*>(m_KeepAlive.ShaderProgramHandle.get()))
        {
            const GLuint program = glShader->GetRendererID();
            GetOpenGLRenderCommandRuntimeState().GLState.UseProgram(program);
            auto& renderer2DUniforms = GetOpenGLRenderCommandRuntimeState().Renderer2DUniforms;
            renderer2DUniforms.OnProgramBound(program);

            if (renderer2DUniforms.ViewProjectionLocation == -2)
            {
                renderer2DUniforms.ViewProjectionLocation = glGetUniformLocation(program, "u_ViewProjection");
            }

            if (renderer2DUniforms.ViewProjectionLocation != -1)
            {
                if (!renderer2DUniforms.HasViewProjection ||
                    std::memcmp(&renderer2DUniforms.LastViewProjection[0][0], &m_ViewProjection[0][0], sizeof(glm::mat4)) != 0)
                {
                    glUniformMatrix4fv(renderer2DUniforms.ViewProjectionLocation, 1, GL_FALSE, &m_ViewProjection[0][0]);
                    renderer2DUniforms.LastViewProjection = m_ViewProjection;
                    renderer2DUniforms.HasViewProjection = true;
                }
            }

            if (renderer2DUniforms.ModelLocation == -2)
            {
                renderer2DUniforms.ModelLocation = glGetUniformLocation(program, "u_Model");
            }

            if (renderer2DUniforms.ModelLocation != -1)
            {
                static constexpr glm::mat4 kIdentity(1.0f);
                glUniformMatrix4fv(renderer2DUniforms.ModelLocation, 1, GL_FALSE, &kIdentity[0][0]);
            }
        }
        else
        {
            m_KeepAlive.ShaderProgramHandle->Bind();
            m_KeepAlive.ShaderProgramHandle->SetMat4("u_ViewProjection", m_ViewProjection);
            m_KeepAlive.ShaderProgramHandle->SetMat4("u_Model", glm::mat4(1.0f));
            GetOpenGLRenderCommandRuntimeState().GLState.Program = 0;
            GetOpenGLRenderCommandRuntimeState().Renderer2DUniforms.OnProgramBound(0);
        }

        // Bind textures (multi-texture batching) using cached renderer IDs.
        // Invalidate texture cache first: ImGui (or other external code) may have bound its font
        // to texture unit 0, leaving our cache stale. Skipping the bind would cause color-only
        // quads to sample the wrong texture (glyph artifacts).
        const uint32_t count = (m_TextureCount > kMaxTextureSlots) ? kMaxTextureSlots : m_TextureCount;
        GetOpenGLRenderCommandRuntimeState().GLState.InvalidateTextureSlots(count);
        for (uint32_t slot = 0; slot < count; ++slot)
        {
            GetOpenGLRenderCommandRuntimeState().GLState.BindTexture2D(slot, static_cast<GLuint>(m_TextureRendererIds[slot]));
        }

        // Bind geometry and issue draw.
        if (auto* glVAO = dynamic_cast<OpenGLVertexArray*>(m_KeepAlive.VertexArrayHandle.get()))
        {
            const GLuint vao = glVAO->GetRendererID();
            GetOpenGLRenderCommandRuntimeState().GLState.BindVertexArray(vao);
        }
        else
        {
            m_KeepAlive.VertexArrayHandle->Bind();
            GetOpenGLRenderCommandRuntimeState().GLState.VertexArray = 0;
        }

        // Safety: in core profile, indexed drawing requires VAO + EBO.
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
        GLint boundEBO = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundEBO);

        if (boundVAO == 0 || boundEBO == 0)
        {
            LT_CORE_ERROR("Renderer2DFlushCommand: invalid VAO/EBO binding (VAO={}, EBO={}), skipping draw", boundVAO, boundEBO);
            return;
        }

        glDrawElements(static_cast<GLenum>(DrawMode::Triangles), static_cast<GLsizei>(m_IndexCount),
                       static_cast<GLenum>(m_IndexType), nullptr);
        CheckOpenGLError("Renderer2DFlushCommand glDrawElements");
    }

    // BindTextureCommand Execute implementation
    void BindTextureCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        const GLuint id = m_Texture ? static_cast<GLuint>(m_Texture->GetRendererID()) : 0;
        GetOpenGLRenderCommandRuntimeState().GLState.BindTexture2D(m_Slot, id);
    }

    void SetTextureSpecificationCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Texture)
        {
            m_Texture->ApplySpecification(m_Specification);
        }
        else
        {
            LT_CORE_WARN("SetTextureSpecificationCommand: texture was null (no-op)");
        }
    }

    // BindFramebufferCommand Execute implementation
    void BindFramebufferCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // Clear any stale errors from previous commands so we only report errors from this bind.
        while (glGetError() != GL_NO_ERROR)
            ;

        if (m_Framebuffer)
        {
            m_Framebuffer->Bind();
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        CheckOpenGLError("BindFramebufferCommand::Execute");
    }

    // DrawArraysCommand Execute implementation
    void DrawArraysCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // Safety: OpenGL core profile requires a VAO to be bound for vertex specification.
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawArraysCommand: no VAO bound (skipping draw)");
            return;
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

        // Safety: In core profile, indexed drawing requires a VAO and an element array buffer
        // unless the caller provides a valid client pointer (which we do not support here).
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);

        GLint boundEBO = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundEBO);

        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawIndexedCommand: no VAO bound (skipping draw)");
            return;
        }

        // If m_Indices is null, we expect an index buffer to be bound via the VAO state.
        if (m_Indices == nullptr && boundEBO == 0)
        {
            LT_CORE_ERROR("DrawIndexedCommand: no index buffer bound and indices pointer is null (skipping draw)");
            return;
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

        // Safety: OpenGL core profile requires a VAO to be bound for vertex specification.
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawInstancedCommand: no VAO bound (skipping draw)");
            return;
        }

        glDrawArraysInstanced(static_cast<GLenum>(m_Mode), m_First, static_cast<GLsizei>(m_Count),
                              static_cast<GLsizei>(m_InstanceCount));
        CheckOpenGLError("glDrawArraysInstanced");
    }

    // DrawIndexedInstancedCommand Execute implementation
    void DrawIndexedInstancedCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // Safety: In core profile, indexed drawing requires a VAO and an element array buffer
        // unless the caller provides a valid client pointer (which we do not support here).
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);

        GLint boundEBO = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundEBO);

        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawIndexedInstancedCommand: no VAO bound (skipping draw)");
            return;
        }

        // If m_Indices is null, we expect an index buffer to be bound via the VAO state.
        if (m_Indices == nullptr && boundEBO == 0)
        {
            LT_CORE_ERROR("DrawIndexedInstancedCommand: no index buffer bound and indices pointer is null (skipping draw)");
            return;
        }

        if (m_BaseVertex != 0)
        {
            glDrawElementsInstancedBaseVertex(static_cast<GLenum>(m_Mode), static_cast<GLsizei>(m_Count),
                                              static_cast<GLenum>(m_IndexType), m_Indices,
                                              static_cast<GLsizei>(m_InstanceCount), m_BaseVertex);
            CheckOpenGLError("glDrawElementsInstancedBaseVertex");
        }
        else
        {
            glDrawElementsInstanced(static_cast<GLenum>(m_Mode), static_cast<GLsizei>(m_Count),
                                    static_cast<GLenum>(m_IndexType), m_Indices, static_cast<GLsizei>(m_InstanceCount));
            CheckOpenGLError("glDrawElementsInstanced");
        }
    }

    // SetBlendModeCommand Execute implementation
    void SetBlendModeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Enable)
        {
            glEnable(GL_BLEND);
            glBlendFunc(static_cast<GLenum>(m_SrcFactor), static_cast<GLenum>(m_DstFactor));
            CheckOpenGLError("glBlendFunc");
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    // SetDepthTestCommand Execute implementation
    void SetDepthTestCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Enable)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(static_cast<GLenum>(m_Func));
            CheckOpenGLError("glDepthFunc");
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
    }

    // SetCullFaceCommand Execute implementation
    void SetCullFaceCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Enable)
        {
            glEnable(GL_CULL_FACE);
            glCullFace(static_cast<GLenum>(m_Face));
            CheckOpenGLError("glCullFace");
        }
        else
        {
            glDisable(GL_CULL_FACE);
        }
    }

    // SetPolygonModeCommand Execute implementation
    void SetPolygonModeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glPolygonMode(static_cast<GLenum>(m_Face), static_cast<GLenum>(m_Mode));
        CheckOpenGLError("glPolygonMode");
    }

    // SetLineWidthCommand Execute implementation
    void SetLineWidthCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glLineWidth(m_Width);
        CheckOpenGLError("glLineWidth");
    }

    // SetPointSizeCommand Execute implementation
    void SetPointSizeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glPointSize(m_Size);
        CheckOpenGLError("glPointSize");
    }

    // PushDebugGroupCommand Execute implementation
    void PushDebugGroupCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        LoadDebugFunctions();
        const auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.PushDebugGroup)
        {
            debugState.PushDebugGroup(kGLDebugSourceApplication, 0, static_cast<GLsizei>(m_GroupName.size()), m_GroupName.c_str());
        }
    }

    // PopDebugGroupCommand Execute implementation
    void PopDebugGroupCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        LoadDebugFunctions();
        const auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.PopDebugGroup)
        {
            debugState.PopDebugGroup();
        }
    }

    // InsertDebugMarkerCommand Execute implementation
    void InsertDebugMarkerCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        LoadDebugFunctions();
        const auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.InsertDebugMarker)
        {
            debugState.InsertDebugMarker(kGLDebugSourceApplication, 0, static_cast<GLsizei>(m_MarkerName.size()), m_MarkerName.c_str());
        }
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

        // Custom code can mutate any OpenGL state. Invalidate cached state for correctness.
        GetOpenGLRenderCommandRuntimeState().GLState.InvalidateAll();
        GetOpenGLRenderCommandRuntimeState().Renderer2DUniforms.OnProgramBound(0);
        
        LT_CORE_DEBUG("CustomCommand: {}", m_Name);
    }

} // namespace Limitless 